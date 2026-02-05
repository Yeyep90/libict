/**
 * @file ict.c
 * @brief ICT Bill Acceptor Library Implementation
 */

#define _DEFAULT_SOURCE

#include "ict.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>

/* ========================================================================== */
/* Configuration Constants                                                    */
/* ========================================================================== */

#define ICT_BAUD_RATE          B9600
#define ICT_MAX_LISTENERS      16
#define ICT_POLL_INTERVAL_SEC  0.5
#define ICT_INIT_CHECK_SEC     2.0
#define ICT_RECONNECT_DELAY    1.0
#define ICT_THREAD_SLEEP_MS    10
#define ICT_TIMEOUT_MS         1000

/* ========================================================================== */
/* Protocol Command Bytes                                                     */
/* ========================================================================== */

#define CMD_POWER_UP        0x80
#define CMD_INITIALIZE      0x8F
#define CMD_ESCROW          0x81
#define CMD_STACKED         0x10
#define CMD_REJECTED        0x11
#define CMD_ENABLE          0x3E
#define CMD_INHIBIT         0x5E
#define CMD_RESET           0x30
#define CMD_STATUS_POLL     0x0C
#define CMD_ACK             0x02
#define CMD_ESCROW_ACCEPT   0x02
#define CMD_ESCROW_REJECT   0x0F
#define CMD_ESCROW_HOLD     0x18

/* ========================================================================== */
/* Logging Macros                                                             */
/* ========================================================================== */

#define ICT_LOG_DEBUG(fmt, ...) \
    fprintf(stderr, "[libict][DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ICT_LOG_INFO(fmt, ...) \
    fprintf(stderr, "[libict][INFO ] " fmt "\n", ##__VA_ARGS__)
#define ICT_LOG_WARN(fmt, ...) \
    fprintf(stderr, "[libict][WARN ] " fmt "\n", ##__VA_ARGS__)
#define ICT_LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[libict][ERROR] " fmt "\n", ##__VA_ARGS__)

/* ========================================================================== */
/* Internal Types                                                             */
/* ========================================================================== */

/**
 * @brief State machine states
 */
typedef enum {
    ICT_SM_IDLE = 0,
    ICT_SM_WAIT_POWERUP,
    ICT_SM_WAIT_READY,
    ICT_SM_RUNNING,
    ICT_SM_ESCROW_WAIT_TYPE
} IctSmState;

/**
 * @brief Serial port handle
 */
typedef struct {
    int fd;  /**< File descriptor */
} serial_port_t;

/**
 * @brief Event listener entry
 */
typedef struct {
    IctListenerCb cb;    /**< Callback function */
    void         *user;  /**< User data pointer */
} IctListener;

/* ========================================================================== */
/* Global State                                                               */
/* ========================================================================== */

static serial_port_t *g_port           = NULL;
static IctSmState     g_state          = ICT_SM_IDLE;
static IctStatus      g_status         = ICT_STATUS_UNKNOWN;
static IctError       g_last_error     = ICT_ERR_NONE;
static int            g_enabled_cached = 0;
static uint8_t        g_last_bill_type = 0;
static int            g_bill_values[256];
static char           g_dev_path[256]  = {0};

static double g_last_reconnect_try = 0.0;
static double g_last_poll_time     = 0.0;
static double g_last_init_check    = 0.0;

static pthread_t g_thread;
static int       g_thread_running = 0;

static IctListener g_listeners[ICT_MAX_LISTENERS];
static int         g_listener_count = 0;

/* ========================================================================== */
/* Time Utilities                                                             */
/* ========================================================================== */

/**
 * @brief Get current time in seconds
 * @return Current time as double (seconds since epoch)
 */
static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* ========================================================================== */
/* Serial Port Functions                                                      */
/* ========================================================================== */

/**
 * @brief Configure serial port attributes
 * 
 * Sets up 9600 baud, 8 data bits, even parity, 1 stop bit (8E1)
 */
static int serial_set_attribs(int fd, int speed, int parity_even)
{
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  /* 8-bit chars */
    tty.c_iflag &= ~IGNBRK;                      /* Disable break processing */
    tty.c_lflag = 0;                             /* No signaling chars, no echo */
    tty.c_oflag = 0;                             /* No remapping, no delays */
    tty.c_cc[VMIN]  = 0;                         /* Non-blocking read */
    tty.c_cc[VTIME] = 0;                         /* No inter-byte timeout */

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);      /* Disable XON/XOFF flow control */
    tty.c_cflag |= (CLOCAL | CREAD);             /* Ignore modem controls, enable reading */
    tty.c_cflag &= ~(PARENB | PARODD);           /* Clear parity bits */
    
    if (parity_even) {
        tty.c_cflag |= PARENB;                   /* Enable even parity */
    }
    
    tty.c_cflag &= ~CSTOPB;                      /* 1 stop bit */
    
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;                     /* No hardware flow control */
#endif

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Open serial port
 */
static int serial_open(serial_port_t **out, const char *dev_path)
{
    int fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    if (serial_set_attribs(fd, ICT_BAUD_RATE, 1) != 0) {
        close(fd);
        return -1;
    }

    serial_port_t *p = (serial_port_t*)calloc(1, sizeof(*p));
    if (!p) {
        close(fd);
        errno = ENOMEM;
        return -1;
    }
    
    p->fd = fd;
    *out = p;
    
    return 0;
}

/**
 * @brief Close serial port
 */
static void serial_close(serial_port_t *p)
{
    if (!p) {
        return;
    }
    
    if (p->fd >= 0) {
        close(p->fd);
    }
    
    free(p);
}

/**
 * @brief Write data to serial port with timeout
 */
static ssize_t serial_write(serial_port_t *p, const void *buf, size_t len, 
                           int timeout_ms)
{
    if (!p || p->fd < 0) {
        errno = ENODEV;
        return -1;
    }

    const uint8_t *ptr = (const uint8_t*)buf;
    size_t total = 0;

    while (total < len) {
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(p->fd, &wfds);

        int rv = select(p->fd + 1, NULL, &wfds, NULL, &tv);
        if (rv <= 0) {
            if (rv == 0) {
                errno = ETIMEDOUT;
            }
            return -1;
        }

        ssize_t n = write(p->fd, ptr + total, len - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        
        total += (size_t)n;
    }

    return (ssize_t)total;
}

/**
 * @brief Read data from serial port with timeout
 */
static ssize_t serial_read(serial_port_t *p, void *buf, size_t len, 
                          int timeout_ms)
{
    if (!p || p->fd < 0) {
        errno = ENODEV;
        return -1;
    }

    if (timeout_ms < 0) {
        timeout_ms = 0;
    }

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(p->fd, &rfds);

    int rv = select(p->fd + 1, &rfds, NULL, NULL, &tv);
    if (rv < 0) {
        return -1;
    }
    if (rv == 0) {
        return 0;  /* Timeout */
    }

    ssize_t n = read(p->fd, buf, len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    
    return n;
}

/* ========================================================================== */
/* Event Notification                                                         */
/* ========================================================================== */

/**
 * @brief Notify all registered listeners of an event
 */
static void ict_notify(const IctEvent *ev)
{
    for (int i = 0; i < g_listener_count; ++i) {
        if (g_listeners[i].cb) {
            g_listeners[i].cb(ev, g_listeners[i].user);
        }
    }
}

/* ========================================================================== */
/* Bill Value Mapping                                                         */
/* ========================================================================== */

/**
 * @brief Get the currency amount for a bill type
 */
static int ict_calc_amount(uint8_t bill_type)
{
    return g_bill_values[bill_type];
}

/**
 * @brief Load default bill denomination mapping
 * 
 * Maps standard bill types to currency values. These defaults
 * are suitable for Uzbek Sum but can be overridden.
 */
static void ict_load_default_bill_mapping(void)
{
    static const int default_values[5] = {
        1000,    /* 0x40: 1,000 */
        2000,    /* 0x41: 2,000 */
        5000,    /* 0x42: 5,000 */
        10000,   /* 0x43: 10,000 */
        50000    /* 0x44: 50,000 */
    };

    memset(g_bill_values, 0, sizeof(g_bill_values));
    
    for (uint8_t i = 0; i < 5; ++i) {
        uint8_t bill_type = (uint8_t)(0x40 + i);
        g_bill_values[bill_type] = default_values[i];
        ICT_LOG_DEBUG("Default mapping: 0x%02X -> %d", 
                     bill_type, g_bill_values[bill_type]);
    }
}

/* ========================================================================== */
/* Connection Management                                                      */
/* ========================================================================== */

/**
 * @brief Close the serial port
 */
static void ict_close_port(void)
{
    if (g_port) {
        ICT_LOG_INFO("Closing serial port");
        serial_close(g_port);
        g_port = NULL;
    }
}

/**
 * @brief Handle I/O error by closing port
 */
static void ict_io_error(const char *where)
{
    ICT_LOG_ERROR("I/O error in %s (errno=%d: %s)", 
                 where, errno, strerror(errno));
    g_status = ICT_STATUS_ERROR;
    ict_close_port();
}

/**
 * @brief Attempt to reconnect to the device
 */
static void ict_try_reconnect(void)
{
    if (g_port) {
        return;  /* Already connected */
    }

    if (g_dev_path[0] == '\0') {
        return;  /* No device configured */
    }

    double t = now_sec();
    if (t - g_last_reconnect_try < ICT_RECONNECT_DELAY) {
        return;  /* Too soon to retry */
    }
    
    g_last_reconnect_try = t;

    ICT_LOG_INFO("Attempting to reconnect to %s", g_dev_path);

    serial_port_t *port = NULL;
    if (serial_open(&port, g_dev_path) < 0) {
        ICT_LOG_WARN("Reconnection failed: %s", strerror(errno));
        return;
    }

    g_port = port;
    ICT_LOG_INFO("Successfully reconnected to %s", g_dev_path);

    /* Reset state */
    g_state           = ICT_SM_WAIT_POWERUP;
    g_status          = ICT_STATUS_UNKNOWN;
    g_last_error      = ICT_ERR_NONE;
    g_enabled_cached  = 0;
    g_last_bill_type  = 0;
    g_last_poll_time  = now_sec();
    g_last_init_check = now_sec();
}

/* ========================================================================== */
/* Protocol State Machine                                                     */
/* ========================================================================== */

/**
 * @brief Handle a single byte received from the device
 */
static void ict_handle_byte(uint8_t b)
{
    IctEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.error = ICT_ERR_NONE;

    ICT_LOG_DEBUG("RX: 0x%02X", b);

    switch (b) {
    case CMD_POWER_UP:
        /* Device power-up, send ACK */
        if (g_port) {
            uint8_t ack = CMD_ACK;
            if (serial_write(g_port, &ack, 1, ICT_TIMEOUT_MS) != 1) {
                ict_io_error("powerup_ack");
                return;
            }
        }
        g_state  = ICT_SM_WAIT_READY;
        g_status = ICT_STATUS_UNKNOWN;
        ICT_LOG_INFO("Device power-up detected");
        break;

    case CMD_INITIALIZE:
        /* Device initialized and ready */
        g_state  = ICT_SM_RUNNING;
        g_status = ICT_STATUS_READY;
        ICT_LOG_INFO("Device ready");
        
        ev.type = ICT_EVENT_READY;
        ict_notify(&ev);
        break;

    case CMD_ESCROW:
        /* Bill in escrow, waiting for type byte */
        g_state = ICT_SM_ESCROW_WAIT_TYPE;
        ICT_LOG_DEBUG("Escrow state entered");
        break;

    case CMD_STACKED:
        /* Bill successfully stacked */
        ev.type           = ICT_EVENT_STACKED;
        ev.bill.bill_type = g_last_bill_type;
        ev.bill.amount    = ict_calc_amount(g_last_bill_type);
        ICT_LOG_INFO("Bill stacked: type=0x%02X amount=%d", 
                    ev.bill.bill_type, ev.bill.amount);
        ict_notify(&ev);
        break;

    case CMD_REJECTED:
        /* Bill rejected */
        ev.type           = ICT_EVENT_REJECTED;
        ev.bill.bill_type = g_last_bill_type;
        ev.bill.amount    = ict_calc_amount(g_last_bill_type);
        ICT_LOG_INFO("Bill rejected: type=0x%02X amount=%d", 
                    ev.bill.bill_type, ev.bill.amount);
        ict_notify(&ev);
        break;

    case CMD_ENABLE:
        g_status         = ICT_STATUS_ENABLE;
        g_enabled_cached = 1;
        ICT_LOG_DEBUG("Device enabled");
        break;

    case CMD_INHIBIT:
        g_status         = ICT_STATUS_INHIBIT;
        g_enabled_cached = 0;
        ICT_LOG_DEBUG("Device inhibited");
        break;

    case ICT_ERR_MOTOR:
    case ICT_ERR_CHECKSUM:
    case ICT_ERR_BILL_JAM:
    case ICT_ERR_BILL_REMOVE:
    case ICT_ERR_STACKER_OPEN:
    case ICT_ERR_SENSOR:
    case ICT_ERR_BILL_FISH:
    case ICT_ERR_STACKER:
    case ICT_ERR_REJECT:
    case ICT_ERR_INVALID_CMD:
    case ICT_ERR_RESERVED_2E:
    case ICT_ERR_RESERVED_2F:
        g_status     = ICT_STATUS_ERROR;
        g_last_error = (IctError)b;
        ev.type      = ICT_EVENT_ERROR;
        ev.error     = g_last_error;
        ICT_LOG_ERROR("Device error: 0x%02X", (unsigned)ev.error);
        ict_notify(&ev);
        break;

    default:
        /* Check for bill type in escrow state */
        if (g_state == ICT_SM_ESCROW_WAIT_TYPE && b >= 0x40 && b <= 0x44) {
            g_last_bill_type   = b;
            ev.type            = ICT_EVENT_ESCROW;
            ev.bill.bill_type  = b;
            ev.bill.amount     = ict_calc_amount(b);
            ICT_LOG_INFO("Bill in escrow: type=0x%02X amount=%d", 
                        ev.bill.bill_type, ev.bill.amount);
            ict_notify(&ev);
            g_state = ICT_SM_RUNNING;
        } else {
            ICT_LOG_DEBUG("Unhandled byte: 0x%02X (state=%d)", b, g_state);
        }
        break;
    }
}

/* ========================================================================== */
/* Background Thread                                                          */
/* ========================================================================== */

/**
 * @brief Execute one iteration of the protocol handler
 */
static void ict_thread_step(void)
{
    if (!g_port) {
        ict_try_reconnect();
        return;
    }

    double t = now_sec();

    /* Send periodic reset during initialization */
    if (g_state == ICT_SM_WAIT_POWERUP || g_state == ICT_SM_WAIT_READY) {
        if (t - g_last_init_check > ICT_INIT_CHECK_SEC) {
            uint8_t rst = CMD_RESET;
            if (serial_write(g_port, &rst, 1, ICT_TIMEOUT_MS) != 1) {
                ict_io_error("reset");
                return;
            }
            g_last_init_check = t;
            ICT_LOG_DEBUG("Sent reset command");
        }
    }

    /* Send periodic status poll */
    if (t - g_last_poll_time > ICT_POLL_INTERVAL_SEC) {
        uint8_t cmd = CMD_STATUS_POLL;
        if (serial_write(g_port, &cmd, 1, ICT_TIMEOUT_MS) != 1) {
            ict_io_error("status_poll");
            return;
        }
        g_last_poll_time = t;
    }

    /* Read and process incoming bytes */
    for (;;) {
        uint8_t b;
        ssize_t n = serial_read(g_port, &b, 1, 0);
        if (n < 0) {
            ict_io_error("read");
            break;
        }
        if (n == 0) {
            break;  /* No more data available */
        }
        ict_handle_byte(b);
    }
}

/**
 * @brief Background thread main function
 */
static void *ict_thread_func(void *data)
{
    (void)data;
    ICT_LOG_DEBUG("Background thread started");

    while (g_thread_running) {
        ict_thread_step();
        usleep(ICT_THREAD_SLEEP_MS * 1000);
    }

    ICT_LOG_DEBUG("Background thread exiting");
    return NULL;
}

/* ========================================================================== */
/* Public API Implementation                                                  */
/* ========================================================================== */

int ict_init(const char *device_path)
{
    if (g_thread_running) {
        ICT_LOG_WARN("Library already initialized");
        return 0;
    }

    const char *dev = (device_path && *device_path) ? device_path : "/dev/ttyUSB0";
    snprintf(g_dev_path, sizeof(g_dev_path), "%s", dev);
    
    ICT_LOG_INFO("Initializing library with device: %s", g_dev_path);

    ict_load_default_bill_mapping();

    /* Try to open the port */
    serial_port_t *port = NULL;
    if (serial_open(&port, g_dev_path) < 0) {
        ICT_LOG_WARN("Cannot open device '%s' now: %s", 
                    g_dev_path, strerror(errno));
        ICT_LOG_INFO("Will retry connection in background");
        g_port = NULL;
    } else {
        g_port = port;
        ICT_LOG_INFO("Successfully opened device '%s'", g_dev_path);
    }

    /* Initialize state */
    g_state              = g_port ? ICT_SM_WAIT_POWERUP : ICT_SM_IDLE;
    g_status             = ICT_STATUS_UNKNOWN;
    g_last_error         = ICT_ERR_NONE;
    g_enabled_cached     = 0;
    g_last_bill_type     = 0;
    g_last_poll_time     = now_sec();
    g_last_init_check    = now_sec();
    g_last_reconnect_try = now_sec();

    /* Start background thread */
    g_thread_running = 1;
    if (pthread_create(&g_thread, NULL, ict_thread_func, NULL) != 0) {
        ICT_LOG_ERROR("Failed to create background thread: %s", strerror(errno));
        if (g_port) {
            ict_close_port();
        }
        g_thread_running = 0;
        return -1;
    }

    ICT_LOG_INFO("Library initialized successfully");
    return 0;
}

void ict_shutdown(void)
{
    if (!g_thread_running) {
        return;
    }

    ICT_LOG_INFO("Shutting down library");

    /* Stop background thread */
    g_thread_running = 0;
    pthread_join(g_thread, NULL);

    /* Disable acceptor before closing */
    if (g_port) {
        (void)ict_status(0);
    }

    ict_close_port();

    /* Clear listeners */
    memset(g_listeners, 0, sizeof(g_listeners));
    g_listener_count = 0;

    /* Reset state */
    g_state          = ICT_SM_IDLE;
    g_status         = ICT_STATUS_UNKNOWN;
    g_last_error     = ICT_ERR_NONE;
    g_enabled_cached = 0;
    g_last_bill_type = 0;

    ICT_LOG_INFO("Library shut down complete");
}

int ict_status(int enable)
{
    if (!g_port) {
        errno = ENODEV;
        return -1;
    }

    uint8_t cmd = enable ? CMD_ENABLE : CMD_INHIBIT;
    ssize_t w = serial_write(g_port, &cmd, 1, ICT_TIMEOUT_MS);
    if (w != 1) {
        ICT_LOG_ERROR("Failed to send status command: %s", strerror(errno));
        ict_io_error("ict_status");
        return -1;
    }

    g_enabled_cached = enable ? 1 : 0;
    ICT_LOG_INFO("Device %s", enable ? "enabled" : "inhibited");
    
    return 0;
}

int ict_escrow_accept(void)
{
    if (!g_port) {
        errno = ENODEV;
        return -1;
    }

    uint8_t cmd = CMD_ESCROW_ACCEPT;
    ssize_t w = serial_write(g_port, &cmd, 1, ICT_TIMEOUT_MS);
    if (w != 1) {
        ICT_LOG_ERROR("Failed to send accept command: %s", strerror(errno));
        ict_io_error("ict_escrow_accept");
        return -1;
    }

    ICT_LOG_DEBUG("Escrow accept command sent");
    return 0;
}

int ict_escrow_reject(void)
{
    if (!g_port) {
        errno = ENODEV;
        return -1;
    }

    uint8_t cmd = CMD_ESCROW_REJECT;
    ssize_t w = serial_write(g_port, &cmd, 1, ICT_TIMEOUT_MS);
    if (w != 1) {
        ICT_LOG_ERROR("Failed to send reject command: %s", strerror(errno));
        ict_io_error("ict_escrow_reject");
        return -1;
    }

    ICT_LOG_DEBUG("Escrow reject command sent");
    return 0;
}

int ict_escrow_hold(void)
{
    if (!g_port) {
        errno = ENODEV;
        return -1;
    }

    uint8_t cmd = CMD_ESCROW_HOLD;
    ssize_t w = serial_write(g_port, &cmd, 1, ICT_TIMEOUT_MS);
    if (w != 1) {
        ICT_LOG_ERROR("Failed to send hold command: %s", strerror(errno));
        ict_io_error("ict_escrow_hold");
        return -1;
    }

    ICT_LOG_DEBUG("Escrow hold command sent");
    return 0;
}

void ict_set_bill_value(uint8_t bill_type, int amount)
{
    g_bill_values[bill_type] = amount;
    ICT_LOG_INFO("Bill value override: 0x%02X -> %d", bill_type, amount);
}

void ict_add_listener(IctListenerCb cb, void *user)
{
    if (!cb) {
        return;
    }

    /* Check for duplicate */
    for (int i = 0; i < g_listener_count; ++i) {
        if (g_listeners[i].cb == cb && g_listeners[i].user == user) {
            ICT_LOG_DEBUG("Listener already registered");
            return;
        }
    }

    /* Check capacity */
    if (g_listener_count >= ICT_MAX_LISTENERS) {
        ICT_LOG_WARN("Maximum listener count (%d) reached", ICT_MAX_LISTENERS);
        return;
    }

    g_listeners[g_listener_count++] = (IctListener){ cb, user };
    ICT_LOG_DEBUG("Listener registered (total: %d)", g_listener_count);
}

void ict_remove_listener(IctListenerCb cb, void *user)
{
    for (int i = 0; i < g_listener_count; ++i) {
        if (g_listeners[i].cb == cb && g_listeners[i].user == user) {
            /* Replace with last element */
            g_listeners[i] = g_listeners[g_listener_count - 1];
            g_listener_count--;
            ICT_LOG_DEBUG("Listener removed (remaining: %d)", g_listener_count);
            return;
        }
    }
    
    ICT_LOG_DEBUG("Listener not found for removal");
}