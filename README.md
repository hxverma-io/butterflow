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

## macOS Universal Control User Interface

When opening `butterflow.exe`, Butterflow launches an authentic, state-of-the-art **macOS Universal Control** native interface designed with Apple's iconic design language:

* **Dark Frosted Glass & Titanium Aesthetics**: Double-buffered GDI+ hardware-rendered interface with macOS dark mode colors, specular rim borders, and Windows 11 rounded corners (`DwmSetWindowAttribute`).
* **Authentic macOS Traffic Light Controls**: Interactive Close (Red), Minimize (Yellow), and Zoom (Green) controls with authentic glyph hovers and system tray integration.
* **Interactive Displays Arrangement Canvas**:
  - Live hardware monitor mockups for **This PC** and **Remote Companion PC**.
  - Interactive edge positioning: Click or arrange displays to the **Left**, **Right**, **Top**, or **Bottom** to mirror your real-world desk setup.
  - **Dynamic Universal Control Linking Beam**: An animated glowing Apple-cyan gradient wave pulsing along the active display boundary.
* **Cupertino Control Cards**:
  - **Push-Through Handoff**: Smooth animated Apple pill toggle switch.
  - **Apple Spring Resistance**: Interactive Cupertino slider tuning spring dampening ($F = -k \cdot \Delta x$) from 15px to 65px displacement.
  - **Frosted Edge Bubble Demo**: Live preview button that triggers the translucent Apple edge bubble and transition burst shockwave on command.
* **Subnet Auto-Discovery Radar**:
  - Real-time animated radar sweep scanning UDP port `54823`.
  - Discovered peer cards with 1-click **Connect / Disconnect** actions.
  - Direct IP connection box with keyboard typing and auto-connect.
* **Live Telemetry & Diagnostics**:
  - Real-time gauges: **< 0.4 ms** input stream latency, packet rate counters, active control focus states, and timestamped kernel event logs.
* **System Tray Integration**:
  - Minimizes smoothly to the Windows taskbar notification tray.
  - Right-click tray menu for quick edge switching, emergency cursor breakout, and window restore.

---

## How to Run

Simply double-click `butterflow.exe` on each Windows PC. Butterflow will immediately open the macOS Universal Control dashboard, start broadcasting on your local Wi-Fi or Ethernet network, and auto-pair with nearby peers!

Alternatively, you can customize startup via command line:

### Command Line Options

| Parameter | Example | Description |
| :--- | :--- | :--- |
| `--edge <left\|right\|top\|bottom>` | `--edge right` | Specifies which screen edge transitions to the remote PC. Default is `right`. |
| `--connect <IP>` | `--connect 192.168.1.105` | Directly specifies the remote PC's IP address (bypasses auto-discovery). |
| `--port <PORT>` | `--port 54823` | Sets custom UDP port (default: `54823`). |
| `--headless` | `--headless` | Runs engine in headless background mode without opening the GUI. |
| `--console` | `--console` | Attaches stdout/stderr logging to the launching terminal window. |

---

## Building from Source

Requirements:
* MinGW-w64 G++ (supporting C++17)

Run the included build script:
```cmd
build.bat
```
This produces a fully statically linked `butterflow.exe` with no external runtime or DLL dependencies.
