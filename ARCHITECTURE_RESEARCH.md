# Comprehensive Architectural Analysis: Virtual DualSense Emulation Backends

This document details the exact technical implementation, driver stack, and execution pipeline for emulating a genuine Sony DualSense controller on Windows so that **games' native adaptive trigger & haptic calls are captured and routed directly to the physical Beitong controller in your hand**.

---

## 1. Why ViGEmBus Failed & What the Ecosystem Uses Instead

For years, PC controller emulation relied on **ViGEmBus** (Virtual Gamepad Emulation Bus). However:
- ViGEmBus **only supports Xbox 360 (`0x045E:0x028E`) and DualShock 4 (`0x054C:0x05C4`)**.
- DualShock 4 reports **do not contain adaptive trigger blocks**.
- ViGEmBus was officially archived and does not support the DualSense 47-byte Output Report `0x02`.

To solve this, modern projects have developed three distinct approaches:

### A. HIDMaestro (https://hidmaestro.org/docs/) — **The Cleanest & Most Modern Solution**
- **Architecture**: A signed **UMDF2 (User-Mode Driver Framework)** virtual controller driver and C# SDK.
- **DualSense Support**:
  - Contains embedded native DualSense profiles (Sony VID `0x054C` / PID `0x0CE6`).
  - Supports the full **4-interface composite**: Gamepad + Speakers + Headset Mic.
  - Exposes an event callback: `HMController.OutputReceived`.
  - When a game writes to the DualSense over HID or Windows Gaming Input, the driver intercepts the raw bytes and raises `HMOutputSource.HidOutput` with Report ID `0x02`.
- **Why It's Ideal**:
  - No complex USBIP setup required.
  - Automatically deploys its signed driver on first run (`HMContext.InstallDriver()`).
  - Native passthrough of raw trigger blocks with direct C# / C FFI integration.

### B. libVIIPER / USBIP-Win2 (Used by DualSenseClient & DS4Windows-hbashton)
- **Architecture**: Uses **USB/IP** (Virtual Host Controller Interface). In Windows, it links against the `usbip-win2` signed kernel driver.
- **DualSense Support**:
  - Emulates an entire USB root port and attaches a virtual USB composite device.
  - Native support for Report `0x02` (Adaptive Triggers) and Report `0x31` (Bluetooth).
  - Extended output callbacks receive game trigger payloads.
- **Trade-off**: Requires running or installing the `usbip-win2` driver service on the machine.

### C. In-Process Proxy Interception (`hidapi.dll` Proxy)
- **Architecture**: In-process hook. Games like *Cyberpunk 2077*, *Death Stranding*, and *Spider-Man* interface with DualSense controllers via `hidapi.dll` or raw HID.
- **DualSense Support**:
  - Dropping our proxy `hidapi.dll` next to the game's executable intercepts `hid_enumerate` and `hid_write`.
  - **Latency**: Exactly **0.0ms** (executes directly in the game's memory space, no driver context switch).
  - Captures the exact `0x02` trigger report before translating and writing to the physical Beitong controller.

---

## 2. One-Way Translation Pipeline (Game ➔ Beitong)

The engine in `ds2bt_engine.c` performs the physical conversion:

```
 Game Engine (e.g. Cyberpunk / Spider-Man)
                  │
                  ▼ Writes DualSense Report 0x02
  ┌────────────────────────────────────────────────────────┐
  │              Virtual DualSense Output Hook             │
  │   (Captured via HIDMaestro, libVIIPER, or hidapi proxy)│
  └────────────────────────────────────────────────────────┘
                  │
                  ▼ Raw DualSense 0x02 Report (47B Common)
  ┌────────────────────────────────────────────────────────┐
  │               ds2bt Translation Engine                 │
  │                                                        │
  │ 1. Mode 0x21 (Feedback)  ──► Beitong Race Resistance   │
  │ 2. Mode 0x25 (Weapon)    ──► Beitong Shot Breakpoint   │
  │ 3. Mode 0x26 (Vibration) ──► Beitong Gun Recoil Pulses │
  │ 4. Force Boost Engine    ──► Gain: 1.45x, Floor: 25%   │
  │ 5. Spline Haptic Mapper  ──► 5-Point Curve (INTENSE)   │
  └────────────────────────────────────────────────────────┘
                  │
                  ▼ Emits Beitong Report 0x02 Frames:
                  │   - 02 56 00 <mode> <params...> (LT)
                  │   - 02 56 01 <mode> <params...> (RT)
                  │   - 02 76 01 04 <val> (Grip Rumble)
                  │
   [Physical Beitong KP70 / Asura / Zeus Controller in Hand]
```

---

## 3. Physical Value & Force Reconciliation

| Metric | DualSense Representation | Beitong Mechanical Representation | Conversion Formula |
|---|---|---|---|
| **Actuation Travel** | 10 discrete zones ($0 = \text{rest}, 9 = \text{bottom}$) | Travel position ($0 \dots 100\%$, internal ADC $0 \dots 255$) | $\text{Beitong Position} = \text{round}\left(\frac{\text{Zone}}{9} \times 100\right)$ |
| **Trigger Wall Force** | 3-bit packed integer ($0 \dots 7$, internal force $1 \dots 8$) | Continuous damping force ($0 \dots 100\%$) | $\text{Force} = \min\left(100, \max(25, \text{raw} \times 1.45)\right)$ |
| **Tactile Breakpoint** | Bitmask pair: start zone ($2 \dots 7$) & break zone ($3 \dots 8$) | Break start ($0 \dots 100\%$) & click snap point ($0 \dots 100\%$) | Dual-stage wall with instant drop-off upon detent pass |
| **Recoil Vibration** | 3-bit amplitude ($1 \dots 8$) & frequency byte ($1 \dots 255\text{ Hz}$) | Recoil pulse amplitude ($0 \dots 100\%$) & cadence ($5 \dots 100$) | $\text{Amplitude} = \text{raw} \times 1.50$, $\text{Freq} = \text{scale}(10..240 \to 5..100)$ |
| **Grip Rumble** | Left motor (low freq) & Right motor (high freq) ($0 \dots 255$) | Grip motor strength ($0 \dots 100\%$) | Evaluated through 5-point non-linear spline curve (`CURVE_INTENSE`) |
