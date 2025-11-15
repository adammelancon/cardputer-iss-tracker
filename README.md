NOTE: Still working on this — the Lat/Long is hardcoded, and the Wi-Fi credentials are currently compiled into the build.  
An Options menu will be added soon so you can set Wi-Fi and location directly on the Cardputer.  
This is just my first upload.

# Cardputer ISS Tracker

This is a simple ISS (International Space Station) tracker for the M5Stack Cardputer ADV.  
It connects to Wi-Fi, pulls ISS position / pass data, and shows information on the Cardputer screen.

## Hardware

- M5Stack Cardputer ADV  
- USB-C cable

## Firmware

You can either:

1. **Flash the prebuilt firmware** (easiest), or  
2. **Build from source with PlatformIO** (for hacking / tweaking).

---

## 1. Flash the prebuilt firmware

If you just want to run it:

1. Download the latest `firmware.bin` from the **Releases** page.
2. Flash it using **esptool.py**, **M5Burner**, **your favorite flasher**, or even **load it through M5 Launcher**.
3. Reboot the Cardputer.