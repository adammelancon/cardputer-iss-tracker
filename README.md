# Cardputer ISS Tracker

This is a simple ISS (International Space Station) tracker for the M5Stack Cardputer ADV.  
It connects to Wi-Fi, pulls ISS position / pass data, and shows information on the Cardputer screen.

## Hardware

- M5Stack Cardputer ADV
- USB-C cable

## Firmware

You can either:

1. **Flash the prebuilt firmware** (easiest), or  
2. **Build from source** (for hacking / tweaking).

### 1. Flash the prebuilt firmware

If you just want to run it:

1. Download the latest `firmware.bin` from the repo (or Releases page).
2. Use `esptool.py`, M5Burner, or your favorite flasher to write it to the Cardputer.
3. Reboot the Cardputer.

### 2. Build from source

1. Clone this repo:

   ```bash
   git clone https://github.com/adammelancon/cardputer-iss-tracker.git
   cd cardputer-iss-tracker
