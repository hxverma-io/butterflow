# Butterflow: Apple Universal Control for Windows

A reverse-engineered, clean-room native Windows implementation of Apple's **Universal Control** feature.

Butterflow allows two or more Windows PCs to share a single mouse and keyboard seamlessly across the same local network (Ethernet, Wi-Fi LAN, or Mobile Hotspot) with **zero cloud dependencies** and the exact visual fidelity and physics of macOS Universal Control.

---

## Key Features

1. **Authentic Apple Visual Affordance & Physics**:
   * **Frosted-Glass Edge Bubble**: Smooth translucent pill indicator rendered via per-pixel alpha blended GDI+ layered windows (`WS_EX_LAYERED | WS_EX_TRANSPARENT`).
   * **Boundary Spring Resistance**: Implements spring dampening physics ($F = -k \cdot \Delta x$) requiring a deliberate push (~35px displacement) before cursor handoff occurs.
   * **Transition Burst**: An animated visual shockwave ripples out as the cursor breaks through the boundary onto the adjacent PC.

2. **Sub-Millisecond Real-Time Input Streaming**:
   * Direct Win32 Low-Level Hooks (`WH_MOUSE_LL`, `WH_KEYBOARD_LL`).
   * Zero-delay UDP socket transport (`FIONBIO`, non-blocking, zero buffer bloat).
   * Hardware input synthesis on the target PC using Win32 `SendInput` (mouse moves, left/right/middle/extra clicks, high-definition vertical/horizontal wheel scrolling, and keyboard scan codes).

3. **Subnet Auto-Discovery (Zero Configuration)**:
   * Periodically broadcasts discovery beacons on port `54823`.
   * Automatically pairs with any nearby Windows PC running Butterflow on the same Wi-Fi network, wired Ethernet, or Mobile Hotspot.
   * Also supports direct IP connection.

4. **Safety & Failsafe**:
   * **Emergency Breakout**: Press `Ctrl + Alt + Shift + Esc` at any moment to immediately release cursor lock and regain 100% control on the host PC.

---

## How to Run

### Setup: Two Windows PCs on the Same Network

Copy `butterflow.exe` to both Windows PCs (PC-A and PC-B). Both PCs must be on the same local network (connected to the same Wi-Fi router, Ethernet switch, or one connected to the other's Mobile Hotspot).

#### Machine 1 (Left PC):
```cmd
butterflow.exe --edge right
```
*(Tells Machine 1 that the other PC is located physically to its right)*

#### Machine 2 (Right PC):
```cmd
butterflow.exe --edge left
```
*(Tells Machine 2 that Machine 1 is located physically to its left)*

---

### Command Line Options

| Parameter | Example | Description |
| :--- | :--- | :--- |
| `--edge <left\|right\|top\|bottom>` | `--edge right` | Specifies which screen edge transitions to the remote PC. Default is `right`. |
| `--connect <IP>` | `--connect 192.168.1.105` | Directly specifies the remote PC's IP address (bypasses auto-discovery). |
| `--port <PORT>` | `--port 54823` | Sets custom UDP port (default: `54823`). |

---

## Building from Source

Requirements:
* MinGW-w64 G++ (supporting C++17)

Run the included build script:
```cmd
build.bat
```
This produces a fully statically linked `butterflow.exe` with no external runtime or DLL dependencies.
