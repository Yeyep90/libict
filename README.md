# libict - ICT Bill Acceptor Library

A C library for interfacing with ICT-compatible bill acceptors via serial communication (RS-232). This library provides a simple, event-driven API with automatic reconnection and background processing.

## Features

- Asynchronous operation with background thread
- Event-based callback system
- Automatic device reconnection
- Configurable bill denomination mapping
- Thread-safe API
- Comprehensive error handling
- Zero external dependencies (POSIX only)
- Clean, well-documented code

## Protocol Support

Implements the ICT bill acceptor serial protocol:
- **Baud rate**: 9600
- **Data format**: 8 bits, even parity, 1 stop bit (8E1)
- **Protocol**: ICT standard command set

## Requirements

- Linux or POSIX-compatible system
- GCC or compatible C compiler
- pthread library
- Serial port device (e.g., `/dev/ttyUSB0`)

## Installation

### Building from source

```bash
# Clone the repository
git clone https://github.com/yetimdasturchi/libict.git
cd libict

# Build the library
make

# Install system-wide (optional)
sudo make install
```

### Building the example

```bash
make examples
```

## Quick Start

### Basic Usage

```c
#include <stdio.h>
#include <unistd.h>
#include "ict.h"

void event_handler(const IctEvent *ev, void *user) {
    switch (ev->type) {
    case ICT_EVENT_READY:
        printf("Device ready\n");
        break;
    case ICT_EVENT_ESCROW:
        printf("Bill: %d\n", ev->bill.amount);
        ict_escrow_accept();  // Accept the bill
        break;
    case ICT_EVENT_STACKED:
        printf("Bill stacked!\n");
        break;
    default:
        break;
    }
}

int main(void) {
    // Initialize
    if (ict_init("/dev/ttyUSB0") != 0) {
        perror("init failed");
        return 1;
    }

    // Register callback
    ict_add_listener(event_handler, NULL);

    // Enable acceptor
    ict_status(1);

    // Wait for events...
    sleep(60);

    // Cleanup
    ict_shutdown();
    return 0;
}
```

### Running the Demo

```bash
# Run with default device (/dev/ttyUSB0)
./build/demo

# Or specify a device
./build/demo /dev/ttyUSB1
```

## API Reference

### Initialization

```c
int ict_init(const char *device_path);
```
Initialize the library and open the serial device. Pass `NULL` to use default `/dev/ttyUSB0`.
Returns 0 on success, -1 on error.

```c
void ict_shutdown(void);
```
Shut down the library, stop background thread, and close the device.

### Control Functions

```c
int ict_status(int enable);
```
Enable (non-zero) or inhibit (zero) the bill acceptor.

```c
int ict_escrow_accept(void);
```
Accept the bill currently in escrow (call after receiving `ICT_EVENT_ESCROW`).

```c
int ict_escrow_reject(void);
```
Reject the bill currently in escrow.

```c
int ict_escrow_hold(void);
```
Hold the bill in escrow without stacking or returning.

### Configuration

```c
void ict_set_bill_value(uint8_t bill_type, int amount);
```
Override the currency value for a specific bill type.

Default mappings (suitable for Uzbek Sum):
- `0x40` → 1,000
- `0x41` → 2,000
- `0x42` → 5,000
- `0x43` → 10,000
- `0x44` → 50,000

Example for US Dollars:
```c
ict_set_bill_value(0x40, 100);    // $1.00
ict_set_bill_value(0x41, 500);    // $5.00
ict_set_bill_value(0x42, 1000);   // $10.00
ict_set_bill_value(0x43, 2000);   // $20.00
ict_set_bill_value(0x44, 10000);  // $100.00
```

### Event Handling

```c
void ict_add_listener(IctListenerCb cb, void *user);
void ict_remove_listener(IctListenerCb cb, void *user);
```

Register/unregister event callbacks. Callbacks are invoked from the background thread.

```c
typedef void (*IctListenerCb)(const IctEvent *ev, void *user);
```

### Event Types

```c
typedef enum {
    ICT_EVENT_READY,     // Device initialized and ready
    ICT_EVENT_ESCROW,    // Bill in escrow, awaiting decision
    ICT_EVENT_STACKED,   // Bill accepted and stacked
    ICT_EVENT_REJECTED,  // Bill rejected
    ICT_EVENT_ERROR      // Device error occurred
} IctEventType;
```

### Error Codes

```c
typedef enum {
    ICT_ERR_NONE = 0x00,
    ICT_ERR_MOTOR = 0x20,
    ICT_ERR_CHECKSUM = 0x21,
    ICT_ERR_BILL_JAM = 0x22,
    ICT_ERR_BILL_REMOVE = 0x23,
    ICT_ERR_STACKER_OPEN = 0x24,
    ICT_ERR_SENSOR = 0x25,
    ICT_ERR_BILL_FISH = 0x27,
    ICT_ERR_STACKER = 0x28,
    // ...
} IctError;
```

## Architecture

```
┌─────────────────┐
│  Application    │
│  (Your Code)    │
└────────┬────────┘
         │ API calls
         │ Event callbacks
┌────────▼────────┐
│    libict       │
│  ┌───────────┐  │
│  │Background │  │
│  │  Thread   │  │
│  └─────┬─────┘  │
│        │        │
└────────┼────────┘
         │ Serial I/O
┌────────▼────────┐
│   /dev/ttyUSB0  │
│  (Bill Acceptor)│
└─────────────────┘
```

- **Main thread**: Your application code
- **Background thread**: Handles serial I/O, protocol state machine, automatic reconnection
- **Thread-safe**: All public API functions are safe to call from any thread

## Troubleshooting

### Permission Denied

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Or set permissions
sudo chmod 666 /dev/ttyUSB0
```

### Device Not Found

The library will automatically retry connection in the background. Check:
- Device is plugged in
- Device appears in `ls -l /dev/ttyUSB*`
- Driver is loaded: `lsmod | grep usb`

### No Events Received

- Ensure device is powered
- Check baud rate (should be 9600 8E1)
- Enable debug logging (see below)

### Debug Logging

All library messages are printed to stderr. Redirect or filter as needed:

```bash
./demo 2>&1 | grep libict
```

## License

MIT License - See [LICENSE](LICENSE) file for details

## Contributing

Got an idea or found a bug? Jump in:

1. Fork this repository
2. Create a feature branch
3. Make your changes with clear commit messages
4. Add or update tests if needed
5. Open a pull request

## Authors

- **Asilbek Askarov** – Project Lead, protocol whisperer and overall “does-this-even-work?” guy.
- **Manuchehr Usmonov** – Core Library Developer, responsible for making the ICT talk like a civilized device.
- **Yusufjon Yunusov** – Testing & QA, the one who happily breaks things so users don’t have to.
- **Umidjon Nurmatov** – Project Sponsor, keeping the project alive with real hardware, real bills and real support.

## Support

- **Issues**: https://github.com/yetimdasturchi/libict/issues
- **Email**: yetimdasturchi@gmail.com
- **Telegram**: https://t.me/yetimdasturchi

## Donations

If this library saved you some time (or a few hours of fighting with the ICT manual :|) and you’d like to support the project:

- Visa: `4140 8400 0153 1195`
- Uzcard/Humo: `8600 4929 5502 3508`
- Local support: [tirikchilik.uz/yetimdasturchi](http://tirikchilik.uz/yetimdasturchi)
- Buy me a coffee: [buymeacoffee.com/yetimdasturchi](https://buymeacoffee.com/yetimdasturchi)

Donations are completely optional, but highly appreciated and help keep the hardware, tests and development going.

## Changelog

### Version 1.0.0 (2025-02-05)
- Initial release
- Core protocol implementation
- Event-based API
- Automatic reconnection
- Example application