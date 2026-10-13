# Complete DualSense <-> Beitong Adaptive Trigger & Haptic Translation System

Designed for using physical Beitong controllers (KP70 / Asura / Zeus) in PC games that natively support the Sony DualSense (e.g. Cyberpunk 2077, Spider-Man Remastered, Returnal, Death Stranding, Ratchet & Clank).

---

## Architecture Overview

```
 [PC Game (Cyberpunk / Spider-Man / etc.)]
                    │
                    ▼  (DualSense Native Report 0x02 / 0x31)
  ┌────────────────────────────────────────────────────────┐
  │         DUALSENSE EMULATION & TRANSLATION LAYER        │
  │                                                        │
  │ 1. HIDHIDE Cloaking Engine                             │
  │    - Hides physical Beitong VID 0x20BC from games      │
  │    - Whitelists bridge process (0 double-input bugs)   │
  │                                                        │
  │ 2. Bidirectional Translation Matrix                    │
  │    - Sony 0x21 (Feedback)  ──► Beitong Race Damping    │
  │    - Sony 0x25 (Weapon)    ──► Beitong Tactile Break   │
  │    - Sony 0x26 (Vibration) ──► Beitong Recoil Pulses   │
  │    - 5-Point Coordinate Spline Haptic Boost (1.40x)    │
  │    - Sticks/Buttons/Axes   ──► DualSense Report 0x01   │
  └────────────────────────────────────────────────────────┘
                    │
                    ▼  (Beitong HID Report 0x02 Frames)
   [Physical Beitong Controller in your hand (USB/2.4G)]
```

---

## Component Breakdown

1. **`bidirectional_bridge.c` / `bidirectional_bridge.h`**:
   - **Game ➔ Controller**: Translates Sony 10-zone packed bitmasks (`0x21 Feedback`, `0x25 Weapon Click`, `0x26 Vibration`) into Beitong's `02 56 <side> <mode> <params...>` packets.
   - **Oomph Tuning**:
     - `trigger_force_gain = 1.40f` (prevents soft/mushy trigger walls).
     - `trigger_vibe_gain = 1.45f` (punchy recoil strikes).
     - `min_force_floor = 25%` (guarantees tactile detents stay authoritative).
   - **Controller ➔ Game**: Translates Beitong LX, LY, RX, RY, LT, RT, D-pad, and button bitmasks into standard Sony DualSense 64-byte Input Reports (`0x01`).

2. **`hidhide_cloak.c`**:
   - Interfaces directly with the Nefarius **HidHide** kernel filter driver (`\\.\HidHide`).
   - Automatically blacklists all Beitong Hardware IDs (`HID\VID_20BC&PID_5126`, `5128`, `518C`, etc.) so games cannot see the physical controller directly.
   - Whitelists the bridge process so it retains full hardware read/write access.

3. **`hidapi_proxy.c` (Zero-Latency In-Process DLL)**:
   - Proxy drop-in replacement for games using `hidapi.dll`.
   - Intercepts `hid_enumerate` and reports a genuine Sony DualSense (`0x054C:0x0CE6`).
   - Intercepts `hid_write` and `hid_read` directly inside the game process (**<0.1ms latency**).

4. **`dualsense_feeder.c` (Standalone Emulation App)**:
   - Standalone background service with 1000Hz (1ms) polling loop for system-wide virtual controller operation.

---

## Windows Deployment Options

### Method A: In-Process Zero-Latency Proxy (Recommended for per-game use)
1. Build `hidapi_proxy.dll` (rename to `hidapi.dll`).
2. Place `hidapi.dll` next to the game executable.
3. Rename the game's original `hidapi.dll` to `hidapi_orig.dll`.
4. Launch the game — it will immediately detect a wired DualSense with full trigger haptics.

### Method B: Standalone Virtual Feeder + HidHide
1. Install [HidHide](https://github.com/nefarius/HidHide/releases).
2. Launch `dualsense_feeder.exe`.
3. The app automatically cloaks the physical Beitong pad, initializes the translation matrix, and feeds the virtual DualSense controller.
