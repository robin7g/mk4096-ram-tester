# MK4096 (4K×1) DRAM Tester 

A small, practical DRAM tester for **Mostek MK4096N-16** (and similar 4K×1 DRAMs) using an **Arduino Uno R4 Minima** and a common **4116-style DRAM tester PCB** (the kind that generates **+12V / +5V / −5V**).

This project is aimed at quickly validating vintage DRAM chips on the bench with clear serial output, visible LED status, and an optional “stress soak” that increases timing pressure as the test progresses.


<img width="2273" height="2852" alt="image" src="https://github.com/user-attachments/assets/2217251b-3735-4ac2-94bc-4824e53f3ce9" />




---

## What this is for

- ✅ Verify that an **MK4096N-16** is functional (read/write + basic pattern coverage)
- ✅ Catch common faults (stuck bits, write failures, address decoding issues, weak cells)
- ✅ “Soak” the chip by repeating tests and **gradually increasing inter-cycle pause**
- ✅ Provide **human-friendly progress + summary** over the serial port

If you’re restoring retro hardware (Apple-1 era parts, arcade boards, old terminals, etc.), this is a quick way to sort good DRAM from bad DRAM before you start debugging a whole system.

---

## How it works (high level)

The MK4096 is a **dynamic RAM** that stores **4096 bits (4K×1)**. Internally it’s organized as:

- **64 rows × 64 columns**
- **6-bit row address + 6-bit column address = 12-bit total address**
- Address is **multiplexed** on the same address pins:
  - Put **row** on A0..A5 → pulse **/RAS**
  - Put **column** on A0..A5 → pulse **/CAS**
- DRAM needs **refresh**, but in practice this tester naturally revisits rows frequently during the test.

### The one critical difference from a 4116
A typical 4116 is 16K×1 and uses **A0..A6** (7 address bits).  
The MK4096 uses **A0..A5** (6 address bits) and repurposes the “A6 pin position”:

- **Socket pin 13 is /CS on MK4096** (chip select, active LOW)
- Many 4116 tester boards drive socket pin 13 as an address bit → that will break MK4096 testing
- This project **holds /CS LOW** and uses only A0..A5 as address lines

---

## Test patterns

Each run performs **4 passes** across the full memory:

1. **Checkerboard** starting at 0 (0101… pattern through the address sweep)
2. **Checkerboard** starting at 1 (1010…)
3. **Solid 0** across all addresses
4. **Solid 1** across all addresses

By default the suite repeats for **10 runs**, and can optionally **ramp a “stress soak pause”** that grows as the test advances, to increase confidence in marginal parts.

---

## Hardware requirements

### Arduino
- **Arduino Uno R4 Minima** (works great; fast enough to run the suite quickly)

### DRAM tester PCB
- A common **4116 RAM tester board** (or equivalent) that provides:
  - **+5V (VCC)**
  - **+12V (VDD)**
  - **−5V (VBB)**
- A DIP-16 socket for the DRAM

> ⚠️ These DRAMs are sensitive to correct power rails and wiring.  
> Double-check polarity, chip orientation, and power rail stability.

---

## Pin mapping (typical 4116 tester board wiring)

This repo assumes the common wiring used by many DIY 4116 tester PCBs, where:

- Arduino drives A0..A5, /RAS, /CAS, /WE, DIN
- Arduino reads DOUT
- **Arduino A2 is connected to socket pin 13**
  - On MK4096 that is **/CS** → we drive it LOW

If your PCB differs, you may need to edit the pin definitions at the top of the sketch.

---

## How to use

1. **Wire / assemble** your tester board and connect it to the Uno R4 Minima.
2. Open the sketch:
   - `MK4096_tester_UnoR4_10runs_soak_ramp.ino` (recommended)
3. In Arduino IDE:
   - Select **Board: Arduino Uno R4 Minima**
   - Select the correct **Port**
4. Upload the sketch.
5. Open **Serial Monitor**:
   - Baud rate: **9600**
6. Insert your DRAM chip:
   - **MK4096N-16**
   - Ensure correct orientation (notch/dot = pin 1)
7. Reset the Arduino (or power-cycle the tester).

### What you’ll see
- Startup banner
- “Priming rows…” message
- Progress for each pass (dots + 25/50/75/100%)
- Per-run timing
- Final summary:
  - Min / Avg / Max runtime
  - Total elapsed time
  - Final soak level
  - PASS/FAIL

### LED behavior (dim + readable)
LEDs are active-low on most 4116 tester PCBs:

- **During test:** dim red “running glow” + brief green pulses for progress
- **PASS:** slow green pulse loop
- **FAIL:** red double-flash loop + serial output describing the failure

---

## Stress soak ramp (keeping total time < ~30s)

The soak adds a per-column pause that **ramps up** over the entire test suite.

```#define MAX_SOAK_US 8000u```

## Credits 

Inspired by the many excellent DIY 4116 tester builds floating around the retrocomputing community — this repo focuses specifically on making them work cleanly for MK4096 and friends.

The arduino shield used is a 4116 RAM Tester that I bought from ebay 
https://ebay.us/m/ABwXwL
The code for testing 4116 RAM is here 
https://github.com/cpyne/4116MemTest


## License
Do whatever you want with this. MIT License


PRs welcome :-) 







