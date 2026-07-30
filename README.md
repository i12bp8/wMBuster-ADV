# wM-Buster ADV

An advanced, highly optimized Wireless M-Bus (wM-Bus) packet sniffer and decoder firmware for the M5Stack Cardputer. 

Built for performance and reliability, wM-Buster ADV natively supports zero-touch hardware auto-detection for both the M5Stack LoRa Cap (SX1262) and Hydra RF (CC1101) modules. It features a completely autonomous heuristic decoding engine capable of decoding EN 13757-4 C1, T1, and S1 mode packets locally on the device without requiring manual drivers.

## Features

* **Zero-Touch Hardware Auto-Detection**: Seamlessly hot-swap between LoRa Cap (SX1262) and Hydra RF (CC1101). The firmware automatically probes the hardware and configures the SPI and interrupt lines safely, completely eliminating the risk of GPIO conflict brownouts.
* **Universal Heuristic Decoding Engine**: No need for manual patches. The engine uses a 3-pass heuristic search (Exact match -> Type match -> Manufacturer match) to decode payloads even for unlisted or newly deployed meters.
* **Live Configuration**: Change listen modes (C1/T1 vs S1) or rescan for new hardware via the settings menu—applied instantly without rebooting.
* **Optimized RF Drivers**: Custom implementations of RadioLib pipelines to prevent hardware FIFO overflows (e.g. CC1101 64-byte limitation) and ensure maximum capture reliability.
* **Premium UI**: Smooth animations, dual-card layouts, and an intuitive carousel menu.

## Hardware Requirements

* [M5Stack Cardputer](https://m5stack.com/products/m5stack-cardputer)
* **RF Module (Pick One)**:
  * [M5Stack LoRa Cap](https://docs.m5stack.com/en/hat/hat_lora1262) (868 MHz SX1262) - Recommended for maximum range.
  * Hydra RF (CC1101) - Supported via auto-detection. *(Note: Ensure you are using an 868 MHz tuned board. Using a 433 MHz board for 868 MHz wM-Bus will result in heavily attenuated signals and degraded capture rates due to hardware bandpass filters).*

## Installation

You can flash the compiled firmware directly to your Cardputer. 

1. Download `firmware.bin` from the [Releases](https://github.com/pingequalab/cardputer-adv-hydra-rf/releases) page.
2. Use [esptool](https://github.com/espressif/esptool) or the Web Flasher of your choice to flash the `.bin` file to offset `0x0`.

Alternatively, via esptool:
```bash
esptool.py -p /dev/ttyACM0 -b 115200 --before default_reset --after hard_reset --chip esp32s3 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 firmware.bin
```

## Build from Source

This project uses [PlatformIO](https://platformio.org/). 

```bash
# Clone the repository
git clone https://github.com/yourusername/wMBuster-ADV.git
cd wMBuster-ADV

# Build the firmware
pio run -e m5stack-cardputer-adv

# Upload to your Cardputer
pio run -e m5stack-cardputer-adv -t upload
```

## License
GPL-3.0 License.
