![Build](https://github.com/Alex9932/libgilberta/actions/workflows/build.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-yellow)

# 🚀 Gilberta – Real-time Communication Library

**Gilberta** is a lightweight C library for real-time data exchange between applications.  
It provides a simple non‑blocking API over UDP with priority channels, delivery control (reliable/retransmission/ordering), and a clean event‑driven model.

## 🎯 Why "Gilberta"?

In Arknights: Endfield, Gilberta is a Messenger from Rhodes Island assigned to Endfield Industries. She makes sure nothing gets lost in transit. Sounds familiar? That's exactly what this library does — but for UDP packets.
Also, she's a 6★ operator. This library aims to be 6★ too. No promises though.

## 🤔 Why another RUDP?

ENet is good, RakNet is heavy, and writing your own wheel is a tradition. Gilberta is an attempt to create a minimal but full-featured RUDP library with a clear API and no magic. No hidden threads, no callback spaghetti — just an event queue and your game loop.

## ✨ Features

- Simple non‑blocking API (send, poll, connect, close).
- Cross‑platform: Windows (MSVC), Linux, macOS (POSIX).
- Little‑endian architectures only (x86/x86‑64, ARM/ARM64, RISC‑V). No big‑endian support.
- Channel‑based messaging with priority levels (last one is not implemented yet).
- UDP transport with control flags (SYN/ACK/FIN/RST/PING).
- Reliable delivery option with retransmission and ordering.
- Custom memory allocator and logging hooks.
- Event polling with `glb_pollevent()`.

## Protocol Header

| Field          | Type      | Description |
|----------------|-----------|-------------|
| `magic`        | uint16_t  | Gilberta packet identifier |
| `payload_len`  | uint16_t  | Payload length |
| `version`      | uint8_t   | Protocol version |
| `channel_id`   | uint8_t   | Channel ID (0–255) |
| `chan_flags`   | uint8_t   | Reserved (must be 0) |
| `ctrlflags`    | uint8_t   | SYN / ACK / PING / FIN / RST |
| `client_gen`   | uint16_t  | Generation counter (default 0) |
| `client_id`    | uint16_t  | Client ID (0xFFFF for initiation) |
| `checksum`     | uint16_t  | CRC16 Modbus of data (0xFFFF if no data) |
| `wnd`          | uint16_t  | Window size (flow control) |
| `seq`          | uint32_t  | Sequence number |
| `ack`          | uint32_t  | Acknowledgment number |
> ⚠️ **Endianness:** all multi-byte header fields are transmitted in host byte order
> with no conversion. Gilberta currently supports **little-endian hosts only**
> (x86/x86-64, ARM/ARM64, RISC-V). Communication between little-endian and
> big-endian machines (e.g. s390x, classic PowerPC/SPARC) is **not supported**
> and will corrupt packets silently.

## ⚙️ Building

This project uses [Premake5](https://premake.github.io/) to generate build files.

### 1. Install Premake5

- **Windows**: download `premake5.exe` and add it to your `PATH`.
- **Linux / macOS**: use your package manager or download the binary.

### 2. Generate project files

From the project root:

```bash
premake5 gmake2      # Makefile (Linux/macOS)
premake5 vs2026      # Visual Studio 2026 (Windows)
premake5 xcode4      # Xcode (macOS)
```

### 3. Build

- **Makefile**: run `make`
- **Visual Studio**: open the generated `.sln` and build.
- **Xcode**: open the project and build.

The output (static/dynamic library) will be placed in `bin/` or `lib/`.

## 💻 Basic Usage

### Server example

```c
#include <libgilberta.h>
#include <stdio.h>

int main() {
    glbchan_t channel = {
        .priority = 0,                        // highest priority (not implemented yet)
        .flags    = GLB_CHANNEL_FLAG_RELIABLE // reliable delivery
    };

    glbcfg_t cfg = {
        .ip = NULL,                  // server mode
        .port = 8080,                // listen on port 8080
        .flags = GLB_FLAG_BIND_PORT, // bind to port
        .channel_count = 1,          // one channel
        .eventqueue_length = 64,     // event queue size
        .max_connections = 16,       // max clients
        .alloc = NULL,               // use malloc/free
        .log = NULL,                 // default log callback
        .channels = &channel         // channel params
    };

    char buffer[1024] = {0};
    glbrecvinfo_t recvinfo = {
        .buffer = buffer,
        .buflen = 1024
    };

    glbctx_t* ctx = glb_create(&cfg);
    if (!ctx) return 1;
    while(1) {
        glb_tick(ctx);
        glbevent_t ev;
        while (glb_pollevent(ctx, &ev) == GLB_SUCCESS) {
            switch (ev.type) {
                case GLB_EVENT_CONNECT:
                    puts("New connection");
                    break;
                case GLB_EVENT_RECEIVE:
                    recvinfo.con = ev.receive.connection;
                    recvinfo.channel_id = ev.receive.channel;
                    if (glb_popdata(ctx, &recvinfo) == GLB_SUCCESS) {
                        printf("Received %zu bytes\n", recvinfo.datalen);
                    } else {
                        printf("Failed to pop data!");
                    }
                    break;
                case GLB_EVENT_DISCONNECT:
                    puts("Disconnected");
                    break;
                default: break;
            }
        }
    }

    glb_destroy(ctx);
    return 0;
}
```

### Client example

```c
#include <libgilberta.h>
#include <string.h>

int main() {
    glbchan_t channel = {
        .priority = 0,                        // highest priority (not implemented yet)
        .flags    = GLB_CHANNEL_FLAG_RELIABLE // reliable delivery
    };

    glbcfg_t cfg = {
        .ip = "127.0.0.1",           // connect to localhost
        .port = 8080,                // server port
        .flags = 0,                  // client mode
        .channel_count = 1,          // one channel
        .eventqueue_length = 64,     // event queue size
        .max_connections = 16,       // max clients
        .alloc = NULL,               // use malloc/free
        .log = NULL,                 // default log callback
        .channels = &channel         // channel params
    };

    glbctx_t* ctx = glb_create(&cfg);
    if (!ctx) return 1;
    if (glb_connect(ctx) != 0) { glb_destroy(ctx); return 1; }

    const char* msg = "Hello, Gilberta!";
    glbsendinfo_t info = {
        .data = msg,
        .len = strlen(msg) + 1,
        .con = NULL,
        .channel_id = 0
    };
    

    // poll events...
    glbevent_t ev;
    int running = 1;
    
    while(running) {
        glb_tick(ctx);
        while (glb_pollevent(ctx, &ev) == GLB_SUCCESS) {

            // Wait for GLB_EVENT_CONNECT before sending
            if (ev.type == GLB_EVENT_CONNECT) {
                glb_send(ctx, &info);
                break;
            }
            if (ev.type == GLB_EVENT_DISCONNECT) {
                running = 0;
                break;
            }
        }
    }

    glb_destroy(ctx);
    return 0;
}
```

## 🧪 More Examples
Check out the `src/test/` directory for ready-to-run examples:
- **`echo.c`** — minimal echo test to verify the setup
- **`msg.c`** — simple chat-like application

## 📚 API Overview

Full API documentation is in the header file `libgilberta.h` (with Doxygen‑style comments).  
Key functions:

- `glb_create` / `glb_destroy` – context lifecycle.
- `glb_connect` – connect to server (client only).
- `glb_close` – close a specific connection.
- `glb_send` – send data on a channel.
- `glb_pollevent` – non‑blocking event retrieval.

## 📜 License

**MIT** – see the `LICENSE` file.

## 👤 Author

**Alex9932** – 2026

## 🤝 Contributing

Issues and pull requests are welcome on [GitHub](https://github.com/Alex9932/libgilberta).

## 🙏 Acknowledgments

- The developers of *Arknights: Endfield* — for the character who inspired the library's name.
- [Premake](https://premake.github.io/) for build system generation.
- Event system inspired by SDL/Enet.