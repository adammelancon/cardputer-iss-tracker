<p align="center">
  <img src="images/logo.png" width="150" alt="ISS Tracker Logo">
</p>

# Cardputer Adv ISS Tracker

I was looking for a unique project for my Cardputer Adv, and as a ham radio operator I wanted a dedicated tool to track the ISS (and soon maybe even satellites) without needing my phone. So, I wrote this ISS Tracker.

It calculates the live ISS position and future passes in real-time.

Features:

- Live Telemetry: Shows Azimuth/Elevation and Lat/Lon in real-time.

- Radar Skyplot: A visual polar plot showing the satellite's path across the sky relative to your position.

- Pass Prediction: Calculates the next visible pass (AOS/LOS) up to 24 hours in advance.

- Elevation Filter: You can set a minimum elevation (e.g., 10 deg) so it ignores low passes.

- Offline Capable: Once it grabs the TLE data via Wi-Fi, it works completely offline.

Setup: You just need to enter your Wi-Fi, Lat/Long, and Timezone in the Options menu. It saves everything to memory so you only have to do it once.

## Screenshots

| Home Screen | Live Telemetry |
| :---: | :---: |
| <img src="images/Home.jpg" width="300" alt="Home Screen"> | <img src="images/Live.jpg" width="300" alt="Live Telemetry"> |

| Polar/Radar View | Next Pass Prediction |
| :---: | :---: |
| <img src="images/Polar.jpg" width="300" alt="Polar View"> | <img src="images/Predict.jpg" width="300" alt="Pass Prediction"> |

| Options Menu | |
| :---: | :---: |
| <img src="images/Options.jpg" width="300" alt="Options"> | |

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

1. Download the latest bin from the **Releases** page.
2. Flash it using **esptool.py**, **M5Burner**, **your favorite flasher**, or even **load it through M5 Launcher**.
3. Reboot the Cardputer.

--- 
Logo created at [PixilArt.com](https://www.pixilart.com/)
