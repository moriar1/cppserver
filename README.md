# CPPServer

Multi-threaded HTTP server using custom thread pool and BSD sockets.

## Building from Source

**Prerequisites**:

- Unix-like OS: Linux, FreeBSD
- C++ Compiler: GCC or Clang
- CMake
- Ninja (optional, for faster builds)

Installing dependencies for Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build
```

**Release Compilation**:

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build
```

**Debug Compilation** (with Sanitizers, Ninja and Clang):

```sh
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Debug -B build -S . -G Ninja -DUSE_SANITIZER=address,undefined
cmake --build build
```

## Usage

```sh
./build/cppserver
```

**Example output**:

```text
[INFO] main.cpp:60: binding to ::
[INFO] main.cpp:124: waiting connections...
[INFO] main.cpp:111: got connection from ::1
[INFO] httphandler.cpp:116: client ::1 status: 200, closing connection...
[INFO] main.cpp:111: got connection from ::1
[INFO] httphandler.cpp:116: client ::1 status: 404, closing connection...
```

*Use Ctrl+C to stop.*

## Testing

### Using Web Browser

Run `cppserver` then open this link in your web browser: `http://localhost:3490`

### Using curl

```sh
# HEAD request:
curl --head localhost:3490

# GET request:
curl localhost:3490
```
