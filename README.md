# CPPServer

[![CPPServer CI](https://github.com/moriar1/cppserver/actions/workflows/test.yml/badge.svg)](https://github.com/moriar1/cppserver/actions/workflows/test.yml)
[![CPPServer CD](https://github.com/moriar1/cppserver/actions/workflows/release.yml/badge.svg)](https://github.com/moriar1/cppserver/actions/workflows/release.yml)

Multi-threaded HTTP server using custom thread pool and BSD sockets.

## Building from Source

**Prerequisites**:

- Unix-like OS: Linux, FreeBSD
- C++ Compiler: GCC or Clang
- CMake
- Ninja (optional)

Installing dependencies for Ubuntu:

```sh
sudo apt install -y build-essential cmake ninja-build
```

**Release Compilation:**

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build
```

**Debug Compilation** (with Sanitizers, Ninja and Clang):

```sh
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Debug -B build -S . -G Ninja -DUSE_SANITIZER=address,undefined
cmake --build build
```

### Build Options

- `-DENABLE_LOGS=OFF` - disable server logging to `std::cerr` (enabled by default).
- `-DUSE_SANITIZER=<options>` - enable sanitizers (`address,undefined` are used by default in Debug build).

## Usage

**Basic (default parameters):**

```sh
./build/cppserver
```

**Using custom parameters:**

```sh
./build/cppserver --port 3490 --workers 4 --timeout 5 --backlog 10
```

*For available options see `--help`.*

**Example output:**

```text
[10:58:46.293] [INFO] binding to ::
[10:58:46.293] [INFO] waiting connections on port 3490...
[10:58:54.248] [INFO] ::1 got connection
[10:58:54.248] [INFO] ::1 status: 200
[10:58:54.312] [INFO] ::1 got connection
[10:58:54.312] [INFO] ::1 status: 404
```

*Use Ctrl+C to stop.*

## Testing

### Automated Tests

Run C++ unit tests and Python integration tests (requires `requests` library):

```sh
cmake --build build && ctest --test-dir build
python tests/integration_test.py
```

### Manual Testing

#### Using Web Browser

Run `cppserver` then open link: `http://localhost:3490`

#### Using curl and nc

```sh
# GET request
curl -i localhost:3490

# HEAD request
curl -i --head localhost:3490

# Invalid path => 404
curl -i http://localhost:3490/file_not_exists.txt

# Unsupported HTTP methods => 405
curl -i -X POST localhost:3490

# Bad request => 400
echo -ne "\r\n\r\n" | nc localhost 3490

# Timeout: waits end of headers `\r\n\r\n` for 10 seconds
echo -ne "anything" | nc localhost 3490
```

## Project structure

```text
.
├── .github/workflows     -- Github workflow
├── include
│   ├── config.hpp        -- CLI parser and server configuration structure
│   ├── customlogger.hpp  -- thread-safe logger
│   ├── httphandler.hpp   -- connection and HTTP handler
│   ├── httputils.hpp     -- hex_to_char(), url_decode(), etc.
│   ├── socket.hpp        -- RAII sockets wrapper (`bind`, `listen`, etc.)
│   ├── threadpool.hpp    -- thread pool for client handling
│   └── uniquefd.hpp      -- RAII wrapper for POSIX file descriptors
├── src
│   ├── main.cpp          -- server loop
│   ├── config.cpp
│   ├── httphandler.cpp
│   ├── httputils.cpp
│   └── threadpool.cpp
└── tests
    ├── httphandler_test.cpp -- unit tests
    ├── threadpool_test.cpp  -- unit tests
    └── integration_test.py  -- python integration tests
```
