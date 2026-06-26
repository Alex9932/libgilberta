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
- Channel‑based messaging with priority levels.
- UDP transport with control flags (SYN/ACK/FIN/RST/PING).
- Reliable delivery option with retransmission and ordering.
- Custom memory allocator and logging hooks.
- Event polling with `glb_pollevent()`.

## Protocol Header

| Field          | Type      | Description |
|----------------|-----------|-------------|
| `magic`        | uint16_t  | Gilberta packet identifier |
| `version`      | uint8_t   | Protocol version |
| `channel_id`   | uint8_t   | Channel ID (0–255) |
| `payload_len`  | uint16_t  | Payload length |
| `reserved`     | uint8_t   | Reserved (must be 0) |
| `ctrlflags`    | uint8_t   | SYN / ACK / PING / FIN / RST |
| `client_gen`   | uint16_t  | Generation counter (default 0) |
| `client_id`    | uint16_t  | Client ID (0xFFFF for initiation) |
| `reserved`     | uint32_t  | Reserved (must be 0) |

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
    glbcfg_t cfg = {
        .ip = NULL,                  // server mode
        .port = 8080,
        .flags = GLB_FLAG_BIND_PORT,
        .channel_count = 1,
        .eventqueue_length = 64,
        .alloc = NULL,               // use malloc/free
        .log = NULL,                 // no logging
        .channels = NULL             // default channel params
    };

    glbctx_t* ctx = glb_create(&cfg);
    if (!ctx) return 1;

    glbevent_t ev;
    while (glb_pollevent(ctx, &ev)) {
        switch (ev.type) {
            case GLB_EVENT_CONNECT:
                puts("New connection");
                break;
            case GLB_EVENT_RECIEVE:
                printf("Received %zu bytes\n", ev.recieve.length);
                break;
            case GLB_EVENT_DISCONNECT:
                puts("Disconnected");
                break;
            default: break;
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
    glbcfg_t cfg = {
        .ip = "127.0.0.1",
        .port = 8080,
        .flags = 0,                  // client mode
        .channel_count = 1,
        .eventqueue_length = 64
    };

    glbctx_t* ctx = glb_create(&cfg);
    if (!ctx) return 1;
    if (glb_connect(ctx) != 0) { glb_destroy(ctx); return 1; }

    const char* msg = "Hello, Gilberta!";
    glbsendinfo_t info = {
        .data = msg,
        .len = strlen(msg) + 1,
        .conn = NULL,
        .channel_id = 0
    };
    

    // poll events...
    glbevent_t ev;
    int connected = 0;
    while (!connected && glb_pollevent(ctx, &ev) == 0) {
        if (ev.type == GLB_EVENT_CONNECT) {
            connected = 1;
            break;
        }
    }

    // Wait for GLB_EVENT_CONNECT before sending
    if (connected) {
        glb_send(ctx, &info);
    }

    glb_destroy(ctx);
    return 0;
}
```

## 📚 API Overview

Full API documentation is in the header file `libgilberta.h` (with Doxygen‑style comments).  
Key functions:

- `glb_create` / `glb_destroy` – context lifecycle.
- `glb_connect` – start listening (server) or connect (client).
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
- Inspired by Enet.