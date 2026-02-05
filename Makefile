# libict Makefile

# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS := -pthread

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
EXAMPLE_DIR := examples

# Library
LIB_NAME := libict.a
LIB_SHARED := libict.so
LIB_SRC := $(SRC_DIR)/ict.c
LIB_OBJ := $(BUILD_DIR)/ict.o

# Examples
EXAMPLE_SRCS := $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS := $(patsubst $(EXAMPLE_DIR)/%.c,$(BUILD_DIR)/%,$(EXAMPLE_SRCS))

# Installation paths
PREFIX ?= /usr/local
INSTALL_LIB_DIR := $(PREFIX)/lib
INSTALL_INC_DIR := $(PREFIX)/include
INSTALL_BIN_DIR := $(PREFIX)/bin

# Colors for output
COLOR_RESET := \033[0m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m

# Default target
.PHONY: all
all: library

# Create build directory
$(BUILD_DIR):
	@echo "$(COLOR_BLUE)Creating build directory...$(COLOR_RESET)"
	@mkdir -p $(BUILD_DIR)

# Build static library
.PHONY: library
library: $(BUILD_DIR) $(BUILD_DIR)/$(LIB_NAME)

$(BUILD_DIR)/$(LIB_NAME): $(LIB_OBJ)
	@echo "$(COLOR_GREEN)Creating static library: $@$(COLOR_RESET)"
	@ar rcs $@ $^

$(LIB_OBJ): $(LIB_SRC) $(INC_DIR)/ict.h
	@echo "$(COLOR_YELLOW)Compiling: $<$(COLOR_RESET)"
	@$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Build shared library
.PHONY: shared
shared: $(BUILD_DIR) $(BUILD_DIR)/$(LIB_SHARED)

$(BUILD_DIR)/$(LIB_SHARED): $(LIB_SRC) $(INC_DIR)/ict.h
	@echo "$(COLOR_GREEN)Creating shared library: $@$(COLOR_RESET)"
	@$(CC) $(CFLAGS) -fPIC -shared -I$(INC_DIR) $< -o $@ $(LDFLAGS)

# Build examples
.PHONY: examples
examples: library $(EXAMPLE_BINS)

$(BUILD_DIR)/%: $(EXAMPLE_DIR)/%.c $(BUILD_DIR)/$(LIB_NAME)
	@echo "$(COLOR_YELLOW)Building example: $@$(COLOR_RESET)"
	@$(CC) $(CFLAGS) -I$(INC_DIR) $< -L$(BUILD_DIR) -lict $(LDFLAGS) -o $@

# Clean build artifacts
.PHONY: clean
clean:
	@echo "$(COLOR_YELLOW)Cleaning build artifacts...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR)

# Install library
.PHONY: install
install: library
	@echo "$(COLOR_GREEN)Installing library to $(PREFIX)...$(COLOR_RESET)"
	@install -d $(INSTALL_LIB_DIR)
	@install -d $(INSTALL_INC_DIR)
	@install -m 644 $(BUILD_DIR)/$(LIB_NAME) $(INSTALL_LIB_DIR)/
	@install -m 644 $(INC_DIR)/ict.h $(INSTALL_INC_DIR)/
	@echo "$(COLOR_GREEN)Installation complete!$(COLOR_RESET)"
	@echo "To use: #include <ict.h> and link with -lict -pthread"

# Install shared library
.PHONY: install-shared
install-shared: shared
	@echo "$(COLOR_GREEN)Installing shared library to $(PREFIX)...$(COLOR_RESET)"
	@install -d $(INSTALL_LIB_DIR)
	@install -d $(INSTALL_INC_DIR)
	@install -m 755 $(BUILD_DIR)/$(LIB_SHARED) $(INSTALL_LIB_DIR)/
	@install -m 644 $(INC_DIR)/ict.h $(INSTALL_INC_DIR)/
	@ldconfig
	@echo "$(COLOR_GREEN)Installation complete!$(COLOR_RESET)"

# Uninstall
.PHONY: uninstall
uninstall:
	@echo "$(COLOR_YELLOW)Uninstalling library from $(PREFIX)...$(COLOR_RESET)"
	@rm -f $(INSTALL_LIB_DIR)/$(LIB_NAME)
	@rm -f $(INSTALL_LIB_DIR)/$(LIB_SHARED)
	@rm -f $(INSTALL_INC_DIR)/ict.h
	@echo "$(COLOR_GREEN)Uninstallation complete!$(COLOR_RESET)"

# Run tests (if implemented)
.PHONY: test
test:
	@echo "$(COLOR_YELLOW)No tests implemented yet$(COLOR_RESET)"

# Format code
.PHONY: format
format:
	@echo "$(COLOR_YELLOW)Formatting code...$(COLOR_RESET)"
	@command -v clang-format >/dev/null 2>&1 && \
		find $(SRC_DIR) $(INC_DIR) $(EXAMPLE_DIR) -name '*.[ch]' -exec clang-format -i {} \; || \
		echo "clang-format not found, skipping"

# Show help
.PHONY: help
help:
	@echo "$(COLOR_BLUE)libict - ICT Bill Acceptor Library$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_GREEN)Available targets:$(COLOR_RESET)"
	@echo "  all             - Build static library (default)"
	@echo "  library         - Build static library"
	@echo "  shared          - Build shared library"
	@echo "  examples        - Build example programs"
	@echo "  clean           - Remove build artifacts"
	@echo "  install         - Install static library system-wide"
	@echo "  install-shared  - Install shared library system-wide"
	@echo "  uninstall       - Remove installed files"
	@echo "  format          - Format code with clang-format"
	@echo "  help            - Show this help message"
	@echo ""
	@echo "$(COLOR_GREEN)Examples:$(COLOR_RESET)"
	@echo "  make                    # Build library"
	@echo "  make examples           # Build examples"
	@echo "  make clean all          # Clean rebuild"
	@echo "  sudo make install       # Install system-wide"
	@echo "  sudo make PREFIX=/opt/local install  # Custom prefix"

.PHONY: info
info:
	@echo "$(COLOR_BLUE)Build Information:$(COLOR_RESET)"
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  PREFIX: $(PREFIX)"
	@echo "  Build directory: $(BUILD_DIR)"