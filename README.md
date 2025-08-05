# JESD204 Eye Scan Visualization Utility

A GTK3-based Linux application for visualizing and analyzing eye scan data from JESD204 high-speed serial interfaces on Analog Devices hardware platforms.

## Overview

This project provides two complementary utilities for monitoring and analyzing JESD204 links:

- **`jesd_eye_scan`** - GTK3-based GUI application for eye scan visualization with color-coded BER (Bit Error Rate) display
- **`jesd_status`** - NCurses-based terminal utility for real-time JESD204 link status monitoring

Both applications support two access methods for JESD204 hardware:
- **Direct sysfs access** - Traditional Linux sysfs filesystem interface
- **libiio support** - Cross-platform Industrial I/O library for local and remote access

Both JESD204B (8B/10B encoding) and JESD204C (64B/66B encoding) standards are fully supported.

## Features

### jesd_eye_scan (GUI Application)
- **Eye Diagram Visualization**: Real-time eye scan data display with color-coded BER levels
- **Multi-Lane Support**: Monitor up to 32 lanes simultaneously  
- **Export Capabilities**: Save eye scan data as PNG images and CSV files
- **Clock Validation**: Real-time validation of link, device, and lane rate clocks with accuracy indicators
- **Device Selection**: Auto-discovery and selection of available JESD204 devices
- **Prescale Configuration**: Configurable prescale settings for eye scan measurements
- **Remote Access**: Connect to JESD204 hardware over network via libiio daemon

### jesd_status (Terminal Application)
- **Real-Time Monitoring**: Continuous display of JESD204 link status information
- **Color-Coded Status**: Visual indicators for link health and clock accuracy
- **Lane Information**: Detailed per-lane configuration and error reporting
- **Multiple Device Support**: Cycle through available JESD204 devices
- **Compact Display**: Optimized for terminal/SSH usage
- **Remote Monitoring**: Monitor JESD204 links over network via libiio

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
- **libiio Development Libraries** *(optional)*: `libiio-dev` (for remote access support)

### Runtime Dependencies
- **GTK3**: Version 3.0 or later
- **NCurses**: Version 5 or later
- **Linux Kernel**: With JESD204 and axi-adxcvr driver support
- **libiio** *(optional)*: Version 0.16 or later (for remote access support)

### Installation (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install libgtk-3-dev libncurses5-dev pkg-config build-essential

# Optional: For libiio support (remote access)
sudo apt-get install libiio-dev
```

### Installation (RHEL/CentOS/Fedora)
```bash
sudo yum install gtk3-devel ncurses-devel pkgconfig gcc make
# or for newer versions:
sudo dnf install gtk3-devel ncurses-devel pkgconfig gcc make

# Optional: For libiio support (remote access)
sudo dnf install libiio-devel  # Fedora/newer RHEL
sudo yum install libiio-devel  # older CentOS/RHEL
```

## Building

The project supports two build systems for flexibility:

### Option 1: Traditional Makefile
```bash
# Basic build (sysfs support only)
make                    # Build both applications
make jesd_status        # Build terminal application only
make jesd_eye_scan      # Build GUI application only
make clean              # Clean build artifacts

# With libiio support (enables remote access)
USE_LIBIIO=1 make       # Build with libiio support
```

### Option 2: CMake Build System
```bash
# Basic build (sysfs support only)
cmake .                 # Configure build (overwrites Makefile)
make                    # Build with CMake-generated Makefile

# With libiio support (enables remote access)
cmake -DUSE_LIBIIO=ON . # Configure with libiio support
make                    # Build with libiio support
make clean              # Clean build artifacts
```

Both build systems produce identical executables with the same compiler optimizations and dependencies.

### Build Configuration Options
- **Default**: Sysfs-only support (no additional dependencies)
- **USE_LIBIIO=1** or **-DUSE_LIBIIO=ON**: Enable libiio support for remote access

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

**Local Usage (sysfs):**
```bash
./jesd_eye_scan                    # Auto-detect devices
./jesd_eye_scan -p /custom/path    # Specify custom sysfs path
```

**Remote Usage (libiio):**
```bash
# Connect to remote target via IP
./jesd_eye_scan -u ip:192.168.1.100

# Connect to remote target via hostname  
./jesd_eye_scan -u ip:myboard.local

# Connect to local iiod daemon
./jesd_eye_scan -u local:

# Auto-detect (prefers libiio if available)
./jesd_eye_scan
```

**Features:**
- Select JESD204 core from dropdown menu
- Choose transceiver core for eye scan
- Configure prescale settings (1-31)  
- Enable/disable individual lanes
- Real-time status monitoring with color-coded clock validation
- Export eye diagrams as PNG images
- Save measurement data as CSV files

**Alternative Remote Access (SSHFS):**
```bash
# Mount remote filesystem and run locally (fallback method)
sudo sshfs -o allow_other -o sync_read root@target:/ /mnt/remote
./jesd_eye_scan -p /mnt/remote
```

### jesd_status (Terminal Application)

**Local Usage (sysfs):**
```bash
./jesd_status                      # Auto-detect devices  
./jesd_status -p /custom/path      # Specify custom sysfs path
```

**Remote Usage (libiio):**
```bash
# Connect to remote target via IP
./jesd_status -u ip:192.168.1.100

# Connect to remote target via hostname
./jesd_status -u ip:myboard.local

# Connect to local iiod daemon
./jesd_status -u local:

# Auto-detect (prefers libiio if available)
./jesd_status
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

**Alternative Remote Access (SSHFS):**
```bash
# Mount remote filesystem and run locally (fallback method)
sudo sshfs -o allow_other -o sync_read root@target:/ /mnt/remote
./jesd_status -p /mnt/remote
```

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

**Remote Access Issues (libiio)**
- Ensure iiod daemon is running on target: `systemctl start iiod`
- Check network connectivity: `ping target_ip`  
- Verify firewall allows port 30431: `sudo ufw allow 30431`
- For USB connections, use: `./jesd_status -u usb:`

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
- **jesd_common.[ch]**: Shared interface and data structures
  - Unified API supporting both sysfs (local) and libiio (local/remote) access
  - Automatic backend selection based on availability
  - Multiple device discovery for JESD204 cores and transceivers
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
- **libiio Library**: https://github.com/analogdevicesinc/libiio
- **IIO Documentation**: https://wiki.analog.com/resources/tools-software/linux-software/libiio

## Contributing

1. Follow Linux kernel coding style
2. Test changes with both build systems (Makefile and CMake)
4. Ensure no compiler warnings with `-Wall`
5. Update documentation for new features
6. Verify compatibility with both JESD204B and JESD204C modes
7. Test both sysfs and libiio backends when applicable
