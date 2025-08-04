# JESD204 Eye Scan Visualization Utility

A GTK3-based Linux application for visualizing and analyzing eye scan data from JESD204 high-speed serial interfaces on Analog Devices hardware platforms.

## Overview

This project provides two complementary utilities for monitoring and analyzing JESD204 links:

- **`jesd_eye_scan`** - GTK3-based GUI application for eye scan visualization with color-coded BER (Bit Error Rate) display
- **`jesd_status`** - NCurses-based terminal utility for real-time JESD204 link status monitoring

Both applications interface with JESD204 hardware through the Linux sysfs filesystem and support both JESD204B (8B/10B encoding) and JESD204C (64B/66B encoding) standards.

## Features

### jesd_eye_scan (GUI Application)
- **Eye Diagram Visualization**: Real-time eye scan data display with color-coded BER levels
- **Multi-Lane Support**: Monitor up to 32 lanes simultaneously  
- **Export Capabilities**: Save eye scan data as PNG images and CSV files
- **Clock Validation**: Real-time validation of link, device, and lane rate clocks with accuracy indicators
- **Device Selection**: Auto-discovery and selection of available JESD204 devices
- **Prescale Configuration**: Configurable prescale settings for eye scan measurements

### jesd_status (Terminal Application)
- **Real-Time Monitoring**: Continuous display of JESD204 link status information
- **Color-Coded Status**: Visual indicators for link health and clock accuracy
- **Lane Information**: Detailed per-lane configuration and error reporting
- **Multiple Device Support**: Cycle through available JESD204 devices
- **Compact Display**: Optimized for terminal/SSH usage

## Hardware Requirements

- Linux system with JESD204 hardware using Analog Devices drivers
- JESD204 transceivers with eye scan capability (axi-adxcvr driver)
- Compatible JESD204 IP cores (axi-jesd204-rx, axi-jesd204-tx)

## Dependencies

### Build Dependencies
- **GTK3 Development Libraries**: `libgtk-3-dev` (for jesd_eye_scan)
- **NCurses Development Libraries**: `libncurses5-dev` (for jesd_status)
- **pkg-config**: For dependency detection
- **GCC**: C compiler with C99 support
- **Make**: Build system
- **CMake** *(optional)*: Alternative build system

### Runtime Dependencies
- **GTK3**: Version 3.0 or later
- **NCurses**: Version 5 or later
- **Linux Kernel**: With JESD204 and axi-adxcvr driver support

### Installation (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install libgtk-3-dev libncurses5-dev pkg-config build-essential
```

### Installation (RHEL/CentOS/Fedora)
```bash
sudo yum install gtk3-devel ncurses-devel pkgconfig gcc make
# or for newer versions:
sudo dnf install gtk3-devel ncurses-devel pkgconfig gcc make
```

## Building

The project supports two build systems for flexibility:

### Option 1: Traditional Makefile
```bash
make                    # Build both applications
make jesd_status        # Build terminal application only
make jesd_eye_scan      # Build GUI application only
make clean              # Clean build artifacts
```

### Option 2: CMake Build System
```bash
cmake .                 # Configure build (overwrites Makefile)
make                    # Build with CMake-generated Makefile
make clean              # Clean build artifacts
```

Both build systems produce identical executables with the same compiler optimizations and dependencies.

## Installation

```bash
sudo make install
```

This installs:
- Executables to `/usr/local/bin/`
- Glade UI file to `/usr/local/share/jesd/`  
- Application icon to `/usr/local/share/jesd/`
- Desktop autostart file to user's `~/.config/autostart/`
- Autostart script to `/usr/local/bin/`

To install to a different location:
```bash
sudo make DESTDIR=/custom/path install
```

## Usage

### jesd_eye_scan (GUI Application)

**Basic Usage:**
```bash
./jesd_eye_scan                    # Auto-detect devices
./jesd_eye_scan -p /custom/path    # Specify custom sysfs path
./jesd_eye_scan -d 0               # Select specific device index
```

**Features:**
- Select JESD204 core from dropdown menu
- Choose transceiver core for eye scan
- Configure prescale settings (1-31)  
- Enable/disable individual lanes
- Real-time status monitoring with color-coded clock validation
- Export eye diagrams as PNG images
- Save measurement data as CSV files

**Remote Usage (via SSH):**
```bash
# Mount remote filesystem and run locally
sudo sshfs -o allow_other -o sync_read root@target:/ /mnt/remote
./jesd_eye_scan -p /mnt/remote
```

### jesd_status (Terminal Application)

**Basic Usage:**
```bash
./jesd_status                      # Auto-detect devices  
./jesd_status -p /custom/path      # Specify custom sysfs path
```

**Interactive Controls:**
- **a or d**: Navigate between devices
- **q/Ctrl+C**: Quit application

**Status Information:**
- Link state and status
- Clock measurements with accuracy validation
- Lane-specific error counts and latency
- CGS (Code Group Synchronization) states
- Frame synchronization status

## System Integration

### Autostart Configuration
The GUI application can be configured to start automatically when JESD204 hardware is detected:

```bash
# The installer places this script in /usr/local/bin/
jesd_eye_scan_autostart.sh
```

### Systemd Service (Optional)
For headless monitoring, create a systemd service:

```ini
[Unit]
Description=JESD204 Status Monitor
After=multi-user.target

[Service]
Type=simple  
ExecStart=/usr/local/bin/jesd_status
Restart=always
User=root

[Install]
WantedBy=multi-user.target
```

## Troubleshooting

### Common Issues

**"Could not open current directory"**
- Ensure JESD204 hardware is present and drivers are loaded
- Check that sysfs path contains JESD204 device entries
- Verify permissions to access `/sys/bus/platform/devices/`

**"Failed to find JESD devices"**  
- Confirm axi-jesd204-rx/tx drivers are loaded: `lsmod | grep jesd`
- Check for device tree entries: `ls /sys/bus/platform/devices/*jesd*`
- Verify hardware compatibility with driver version

**GUI Application Won't Start**
- Ensure X11 forwarding for SSH: `ssh -X user@host`
- Check GTK3 installation: `pkg-config --modversion gtk+-3.0`
- Verify display environment: `echo $DISPLAY`

**Clock Accuracy Warnings**
- Red clock indicators show >200 PPM deviation from expected values
- Check hardware clock sources and PLL configuration
- Verify SYSREF timing and alignment

### Debug Mode
Set environment variable for detailed output:
```bash
export GTK_DEBUG=interactive
./jesd_eye_scan
```

## Development

### Code Style
The project follows Linux kernel coding style.


### Architecture Overview
- **jesd_common.[ch]**: Shared sysfs interface and data structures
- **jesd_status.c**: NCurses-based terminal application  
- **jesd_eye_scan.c**: GTK3-based GUI application
- **jesd.glade**: Glade UI definition file

## License

Copyright 2019-2025 (c) Analog Devices, Inc.

All rights reserved. See [LICENSE.txt](LICENSE.txt) for full license terms.

## Links

- **Documentation**: http://wiki.analog.com/resources/tools-software/linux-software/jesd_eye_scan
- **Hardware Support**: https://wiki.analog.com/resources/fpga/docs/axi_jesd204
- **Driver Documentation**: https://wiki.analog.com/resources/tools-software/linux-drivers/jesd204

## Contributing

1. Follow Linux kernel coding style
2. Test changes with both build systems
3. Ensure no compiler warnings with `-Wall`
4. Update documentation for new features
5. Verify compatibility with both JESD204B and JESD204C modes
