# World Boss Raid - High Concurrency Game System

**World Boss Raid** is a high-concurrency multiplayer online game system developed based on Linux System Programming. This project demonstrates a complete implementation from low-level Socket communication, custom binary protocol, multi-process/multi-thread architecture to IPC (Inter-Process Communication).

The system simulates a scenario where multiple clients simultaneously attack a World Boss, and provides real-time terminal visualization interface through Ncurses.

## Key Features

* **High Performance Server**
    * **Master-Worker Multi-Process Architecture** (Process Pool) that effectively utilizes multi-core CPUs.
    * Uses **Shared Memory** to manage World Boss HP and game state, achieving zero-copy data sharing.
    * Implements **Semaphore** mechanism to ensure data consistency and thread safety under concurrent attacks.
* **High Concurrency Client**
    * **Multi-thread (Pthreads)** architecture where a single client program can simulate 100+ concurrent connections for stress testing.
    * **Ncurses TUI Interface**: Real-time rendering of Boss animations, dynamic health bars, and attack effects without affecting transmission performance.
* **Custom Binary Protocol**
    * Independent of HTTP/JSON, self-designed compact **Binary Protocol**.
    * Supports **Checksum integrity verification** to prevent packet corruption.
    * Packet structure includes: Header (Length, OpCode, SeqNum) + Body (Payload Union).
* **Security Features**
    * **TLS/SSL Encryption**: Secure communication using OpenSSL with certificate verification.
    * **Replay Attack Protection**: Sequence number validation to prevent packet replay attacks.
    * **Multi-level Logging System**: Configurable log levels (DEBUG, INFO, WARN, ERROR, FATAL) with file/console output support.
* **Fault Tolerance & Stability**
    * Implements **Graceful Shutdown**: Captures signals (SIGINT) to ensure proper release of IPC resources when the server shuts down (prevents zombie processes).
    * Includes heartbeat mechanism and disconnection detection.

## Project Structure

```text
WorldBossRaid/
├── CMakeLists.txt          # CMake build configuration
├── README.md               # Project documentation
├── src/
│   ├── common/             # [Common Layer] Protocol & Utilities
│   │   ├── protocol.h     # Packet structure, OpCode, Payload definitions
│   │   ├── tls.h          # TLS/SSL wrapper functions
│   │   ├── tls.c          # TLS/SSL implementation
│   │   ├── log.h          # Multi-level logging system
│   │   ├── log.c          # Logging implementation
│   │   └── log_example.c  # Logging usage examples
│   │
│   ├── server/            # [Server Side]
│   │   ├── server.c       # Entry point, Socket initialization, Master-Worker management
│   │   ├── logic/         # [Business Logic Layer]
│   │   │   ├── client_handler.h
│   │   │   ├── client_handler.c  # Client connection handler
│   │   │   ├── gamestate.h
│   │   │   ├── gamestate.c       # IPC management (Shared Memory creation/destruction)
│   │   │   ├── dice.h
│   │   │   └── dice.c            # Damage calculation & probability logic
│   │   └── security/      # [Security Layer]
│   │       ├── replay_protection.h
│   │       └── replay_protection.c  # Replay attack protection
│   │
│   └── client/            # [Client Side]
│       ├── client.c       # Connection establishment, packet I/O, multi-thread stress testing
│       └── ui/            # [UI Layer]
│           ├── boss.c     # Ncurses rendering for Boss and health bar
│           ├── boss.h
│           ├── player.c  # Player status rendering
│           └── player.h
```

## Build & Run

### Prerequisites

* Linux Environment (Ubuntu/Debian/CentOS)
* GCC Compiler
* CMake (3.10 or higher)
* OpenSSL Development Libraries
* Ncurses Library
* pthread (usually included in glibc)

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libssl-dev libncurses5-dev libncursesw5-dev

# CentOS/RHEL
sudo yum install gcc cmake openssl-devel ncurses-devel

# Fedora
sudo dnf install gcc cmake openssl-devel ncurses-devel
```

### Compilation

The project uses CMake for building. Create a build directory and compile:

```bash
mkdir build
cd build
cmake ..
make
```

After successful compilation, two executables will be generated: `server` and `client`.

### Usage

1. **Start the Server**

```bash
cd build
./server
# Server will start on port 8888 and create a Worker Pool waiting for connections
```

2. **Start the Client**

```bash
cd build
./client
# Launches Ncurses interface, automatically connects and starts attacking
```

3. **Clean Build Files**

```bash
cd build
make clean
# Or remove the entire build directory:
rm -rf build
```

## Protocol Specification

Communication uses a fixed Header length + variable Body design:

| Byte Offset | Field | Type | Description |
|------------|-------|------|-------------|
| 0-3 | Length | uint32_t | Total packet length (Header + Body) |
| 4-5 | OpCode | uint16_t | Operation code (e.g., 0x11 Attack) |
| 6-7 | Checksum | uint16_t | Simple checksum |
| 8-11 | SeqNum | uint32_t | Packet sequence number |
| 12+ | Body | Union | Payload structure determined by OpCode |

### Main OpCodes

* `OP_JOIN (0x10)`: Player join request
* `OP_ATTACK (0x11)`: Attack request (includes damage value)
* `OP_GAME_STATE (0x21)`: Broadcast Boss current HP (Server -> Client)

## Security

* **TLS/SSL Encryption**: All communication is encrypted using TLS 1.2+ with server certificate verification.
* **Replay Attack Protection**: Sequence number validation prevents replaying old packets.
* **Certificate-based Authentication**: Server uses X.509 certificates for identity verification.

## Logging

The project includes a multi-level logging system with the following features:

* **Log Levels**: DEBUG, INFO, WARN, ERROR, FATAL
* **Output Options**: Console (stderr) or file
* **Automatic Timestamps**: Each log entry includes timestamp, file, line, and function name
* **Color Support**: ANSI color codes for terminal output (automatically disabled for file output)

Example usage:
```c
#include "common/log.h"

log_init(LOG_INFO, NULL);  // Initialize with INFO level, output to stderr
LOG_INFO("Server started on port %d", PORT);
LOG_ERROR("Failed to bind socket: %s", strerror(errno));
log_cleanup();
```

## Architecture

### Server Architecture

* **Master Process**: Accepts incoming connections and forks worker processes
* **Worker Processes**: Handle individual client connections using TLS
* **Shared Memory**: Game state (Boss HP, player count) shared across all workers
* **Mutex Protection**: Ensures thread-safe access to shared game state

### Client Architecture

* **Main Thread**: Manages UI rendering (Ncurses)
* **Worker Threads**: Handle network I/O and game logic
* **TLS Connection**: Secure encrypted communication with the server

## License

This project is for educational purposes, demonstrating Linux system programming concepts.
