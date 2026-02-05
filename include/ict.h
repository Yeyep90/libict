/**
 * @file ict.h
 * @brief ICT Bill Acceptor Library
 * 
 * A C library for interfacing with ICT bill acceptors via serial communication.
 * Supports asynchronous operation with event-based callbacks.
 * 
 * @author Manuchehr Usmonov
 * @version 1.0.0
 * @date 2026
 * 
 * @section DESCRIPTION
 * This library provides a simple API to communicate with ICT-compatible bill
 * acceptors. It handles the low-level serial protocol, state machine, and
 * provides high-level callbacks for bill acceptance events.
 * 
 * @section USAGE
 * 1. Initialize with ict_init()
 * 2. Register event listeners with ict_add_listener()
 * 3. Enable the acceptor with ict_status(1)
 * 4. Handle events in your callback
 * 5. Clean up with ict_shutdown()
 */

#pragma once

#include <stdint.h>

typedef enum {
    ICT_STATUS_UNKNOWN = 0,  /**< Status unknown or initializing */
    ICT_STATUS_READY,        /**< Device ready to accept bills */
    ICT_STATUS_ENABLE,       /**< Device enabled and accepting */
    ICT_STATUS_INHIBIT,      /**< Device inhibited (not accepting) */
    ICT_STATUS_ERROR         /**< Device in error state */
} IctStatus;

/**
 * @brief Error codes reported by the device
 */
typedef enum {
    ICT_ERR_NONE         = 0x00, /**< No error */
    ICT_ERR_MOTOR        = 0x20, /**< Motor failure */
    ICT_ERR_CHECKSUM     = 0x21, /**< Checksum error */
    ICT_ERR_BILL_JAM     = 0x22, /**< Bill jammed */
    ICT_ERR_BILL_REMOVE  = 0x23, /**< Bill removal detected */
    ICT_ERR_STACKER_OPEN = 0x24, /**< Stacker is open */
    ICT_ERR_SENSOR       = 0x25, /**< Sensor failure */
    ICT_ERR_BILL_FISH    = 0x27, /**< Bill fishing attempt */
    ICT_ERR_STACKER      = 0x28, /**< Stacker failure */
    ICT_ERR_REJECT       = 0x29, /**< Bill rejected */
    ICT_ERR_INVALID_CMD  = 0x2A, /**< Invalid command */
    ICT_ERR_RESERVED_2E  = 0x2E, /**< Reserved error code */
    ICT_ERR_RESERVED_2F  = 0x2F  /**< Reserved error code */
} IctError;

/**
 * @brief Information about a bill
 */
typedef struct {
    uint8_t bill_type;  /**< Bill type identifier (e.g., 0x40-0x44) */
    int     amount;     /**< Bill value in smallest currency unit */
} IctBillInfo;

/**
 * @brief Event types
 */
typedef enum {
    ICT_EVENT_READY,     /**< Device is ready */
    ICT_EVENT_ESCROW,    /**< Bill in escrow, awaiting accept/reject */
    ICT_EVENT_STACKED,   /**< Bill successfully stacked */
    ICT_EVENT_REJECTED,  /**< Bill was rejected */
    ICT_EVENT_ERROR      /**< Device error occurred */
} IctEventType;

/**
 * @brief Event structure passed to listener callbacks
 */
typedef struct {
    IctEventType type;   /**< Event type */
    IctBillInfo  bill;   /**< Bill information (for bill events) */
    IctError     error;  /**< Error code (for error events) */
} IctEvent;

/**
 * @brief Callback function type for event notifications
 * 
 * @param ev   Pointer to event structure
 * @param user User data pointer passed during registration
 */
typedef void (*IctListenerCb)(const IctEvent *ev, void *user);

/**
 * @brief Initialize the ICT library and start background thread
 * 
 * Opens the serial device and starts a background thread that handles
 * communication with the bill acceptor. If the device cannot be opened
 * initially, the library will retry in the background.
 * 
 * @param device_path Path to serial device (e.g., "/dev/ttyUSB0").
 *                    If NULL, defaults to "/dev/ttyUSB0"
 * @return 0 on success, -1 on error
 */
int ict_init(const char *device_path);

/**
 * @brief Shut down the library
 * 
 * Stops the background thread, disables the acceptor, and closes
 * the serial port. Should be called before program exit.
 */
void ict_shutdown(void);

/**
 * @brief Enable or disable the bill acceptor
 * 
 * @param enable Non-zero to enable, 0 to inhibit (disable)
 * @return 0 on success, -1 on error
 */
int ict_status(int enable);

/**
 * @brief Accept the bill currently in escrow
 * 
 * Call this after receiving an ICT_EVENT_ESCROW event to accept
 * the bill and move it to the stacker.
 * 
 * @return 0 on success, -1 on error
 */
int ict_escrow_accept(void);

/**
 * @brief Reject the bill currently in escrow
 * 
 * Call this after receiving an ICT_EVENT_ESCROW event to reject
 * the bill and return it to the customer.
 * 
 * @return 0 on success, -1 on error
 */
int ict_escrow_reject(void);

/**
 * @brief Hold the bill in escrow
 * 
 * Keep the bill in escrow without stacking or returning it.
 * 
 * @return 0 on success, -1 on error
 */
int ict_escrow_hold(void);

/**
 * @brief Set or override the value for a specific bill type
 * 
 * By default, bill types are mapped as follows:
 * - 0x40: 1,000 (smallest unit)
 * - 0x41: 5,000
 * - 0x42: 10,000
 * - 0x43: 50,000
 * - 0x44: 100,000
 * 
 * Use this function to override these defaults for your currency.
 * 
 * @param bill_type Bill type identifier (e.g., 0x40-0x44)
 * @param amount    Value in smallest currency unit (e.g., sums, tiyin, cents)
 */
void ict_set_bill_value(uint8_t bill_type, int amount);

/**
 * @brief Register an event listener callback
 * 
 * The callback will be invoked from the background thread whenever
 * an event occurs. Multiple listeners can be registered.
 * 
 * @param cb   Callback function
 * @param user User data pointer to pass to callback
 */
void ict_add_listener(IctListenerCb cb, void *user);

/**
 * @brief Remove a previously registered listener
 * 
 * @param cb   Callback function to remove
 * @param user User data pointer used during registration
 */
void ict_remove_listener(IctListenerCb cb, void *user);