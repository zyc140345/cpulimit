# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

cpulimit is a CPU usage limiting tool that controls the CPU consumption of processes by sending SIGSTOP and SIGCONT signals. It's written in C and supports Linux, macOS, and FreeBSD.

## Build Commands

### Main Build
```bash
make                    # Build cpulimit binary (calls src/Makefile)
make clean             # Clean build artifacts
```

### Platform-specific Build
- Linux/macOS: `make`
- FreeBSD: `gmake`

### Test Build
```bash
cd tests && make       # Build test binaries (busy, process_iterator_test)
cd tests && make clean # Clean test artifacts
```

## Testing

### Run Tests
```bash
./tests/process_iterator_test    # Run process iterator unit tests
```

### Test Utilities
- `tests/busy` - CPU-intensive test program for validating cpulimit functionality
- `tests/process_iterator_test` - Unit tests for process discovery and iteration

## Code Architecture

### Core Components

#### Main Entry Point
- `src/cpulimit.c` - Main program logic, command-line parsing, and control loop

#### Process Management
- `src/process_group.h/.c` - Process group management with hashtable for efficient process tracking
- `src/process_iterator.h/.c` - Cross-platform process discovery and information gathering
- Platform-specific implementations:
  - `src/process_iterator_linux.c` - Linux /proc filesystem implementation
  - `src/process_iterator_freebsd.c` - FreeBSD kvm library implementation  
  - `src/process_iterator_apple.c` - macOS sysctl implementation

#### Data Structures
- `src/list.h/.c` - Generic doubly-linked list implementation used throughout the codebase

### Key Data Structures

#### Process Structure
```c
struct process {
    pid_t pid;              // Process ID
    pid_t ppid;             // Parent process ID
    int starttime;          // Process start time
    int cputime;            // CPU time used (milliseconds)
    double cpu_usage;       // CPU usage estimation (0-1)
    char command[PATH_MAX+1]; // Executable path
};
```

#### Process Group
- Uses hashtable (PIDHASH_SZ = 1024) for O(1) process lookup
- Tracks target process and optionally its children
- Maintains process list and timing information

### Platform Abstraction
The process iterator provides a unified interface across platforms:
- Linux: Reads /proc filesystem directly
- FreeBSD: Uses libkvm for process information
- macOS: Uses sysctl system calls

### Signal-Based CPU Limiting
The tool works by:
1. Monitoring target process CPU usage
2. Sending SIGSTOP to pause process when CPU limit exceeded
3. Sending SIGCONT to resume process when CPU usage drops
4. Dynamically adjusting to system load

## Development Notes

### File Organization
- `src/` - All source code
- `tests/` - Test programs and utilities
- Root Makefile delegates to src/ and tests/ subdirectories

### Build System
- Uses GNU Make with recursive makefiles
- Supports cross-platform compilation with conditional FreeBSD linking (-lkvm)
- Standard C compilation flags: `-Wall -g -D_GNU_SOURCE`

### Memory Management
- Uses custom list implementation for dynamic data structures
- Process group uses hashtable for efficient process tracking
- Proper cleanup functions for all data structures