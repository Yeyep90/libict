/**
 * @file main.c
 * @brief Simple demonstration of libict usage
 * 
 * This example shows basic usage of the ICT bill acceptor library.
 * It initializes the device, registers event handlers, and runs
 * until interrupted with Ctrl+C.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ict.h"

/* Global flag for clean shutdown */
static volatile sig_atomic_t g_running = 1;

/**
 * @brief Signal handler for graceful shutdown
 */
static void handle_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

/**
 * @brief Event callback handler
 * 
 * This function is called from the library's background thread
 * whenever a bill acceptor event occurs.
 */
static void ict_event_handler(const IctEvent *ev, void *user)
{
    (void)user;  /* Unused in this example */

    switch (ev->type) {
    case ICT_EVENT_READY:
        printf("[ICT] Device is ready and initialized\n");
        break;

    case ICT_EVENT_ESCROW:
        printf("[ICT] Bill in escrow:\n");
        printf("      Type: 0x%02X\n", ev->bill.bill_type);
        printf("      Amount: %d\n", ev->bill.amount);

        /* Decide whether to accept or reject the bill */
        if (ev->bill.amount > 0) {
            printf("      Action: Accepting bill\n");
            if (ict_escrow_accept() != 0) {
                perror("      Failed to accept bill");
            }
        } else {
            printf("      Action: Rejecting (unknown denomination)\n");
            if (ict_escrow_reject() != 0) {
                perror("      Failed to reject bill");
            }
        }
        break;

    case ICT_EVENT_STACKED:
        printf("[ICT] Bill successfully stacked:\n");
        printf("      Type: 0x%02X\n", ev->bill.bill_type);
        printf("      Amount: %d\n", ev->bill.amount);
        break;

    case ICT_EVENT_REJECTED:
        printf("[ICT] Bill was rejected:\n");
        printf("      Type: 0x%02X\n", ev->bill.bill_type);
        printf("      Amount: %d\n", ev->bill.amount);
        break;

    case ICT_EVENT_ERROR:
        printf("[ICT] Device error occurred:\n");
        printf("      Error code: 0x%02X\n", (unsigned)ev->error);
        
        /* Print human-readable error description */
        switch (ev->error) {
        case ICT_ERR_MOTOR:
            printf("      Description: Motor failure\n");
            break;
        case ICT_ERR_CHECKSUM:
            printf("      Description: Checksum error\n");
            break;
        case ICT_ERR_BILL_JAM:
            printf("      Description: Bill jammed\n");
            break;
        case ICT_ERR_BILL_REMOVE:
            printf("      Description: Bill removal detected\n");
            break;
        case ICT_ERR_STACKER_OPEN:
            printf("      Description: Stacker is open\n");
            break;
        case ICT_ERR_SENSOR:
            printf("      Description: Sensor failure\n");
            break;
        case ICT_ERR_BILL_FISH:
            printf("      Description: Bill fishing attempt\n");
            break;
        case ICT_ERR_STACKER:
            printf("      Description: Stacker failure\n");
            break;
        default:
            printf("      Description: Unknown error\n");
            break;
        }
        break;

    default:
        printf("[ICT] Unknown event type: %d\n", ev->type);
        break;
    }

    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *device = (argc > 1) ? argv[1] : "/dev/ttyUSB0";

    printf("=================================================\n");
    printf("  libict - ICT Bill Acceptor Demo\n");
    printf("=================================================\n");
    printf("Device: %s\n", device);
    printf("Press Ctrl+C to exit\n\n");

    /* Optional: Override default bill values
     * Uncomment and modify as needed for your currency
     */
    /*
    ict_set_bill_value(0x40, 100);     // 1 unit
    ict_set_bill_value(0x41, 500);     // 5 units
    ict_set_bill_value(0x42, 1000);    // 10 units
    ict_set_bill_value(0x43, 5000);    // 50 units
    ict_set_bill_value(0x44, 10000);   // 100 units
    */

    /* Initialize the library */
    if (ict_init(device) != 0) {
        perror("Failed to initialize ICT library");
        return EXIT_FAILURE;
    }

    /* Register event callback */
    ict_add_listener(ict_event_handler, NULL);

    /* Enable the bill acceptor */
    printf("Enabling bill acceptor...\n");
    if (ict_status(1) != 0) {
        perror("Failed to enable bill acceptor");
        ict_shutdown();
        return EXIT_FAILURE;
    }

    printf("Bill acceptor enabled. Waiting for bills...\n\n");

    /* Set up signal handler for clean exit */
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* Main loop - just wait for events */
    while (g_running) {
        sleep(1);
    }

    /* Clean shutdown */
    printf("\n\nShutting down...\n");
    ict_shutdown();
    printf("Goodbye!\n");

    return EXIT_SUCCESS;
}