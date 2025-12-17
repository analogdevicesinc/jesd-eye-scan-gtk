# jesd204_topology

A command-line tool for viewing JESD204 device topology information from the Linux kernel's JESD204 sysfs interface.

## Overview

`jesd204_topology` parses `/sys/bus/jesd204/devices` to display comprehensive information about JESD204 devices, links, and connections. It supports multiple output formats including text summary, ASCII graph visualization, and DOT file generation for graphviz.

## Features

- Display detailed topology summary with all devices, links, and connections
- ASCII graph visualization showing device hierarchy
- DOT file generation for graphviz visualization
- Link parameter display with JESD204 parameters (L, M, N, N', F, K, S, E)
- Calculated rates: lane rate, LMFC/LEMC rate, and device clock
- Support for JESD204A/B/C with 8B/10B, 64B/66B, and 64B/80B encoding
- Command line options to set/clear `fsm_ignore_errors` flag

## Usage

```
jesd204_topology [OPTIONS]
```

### Options

| Option | Long Option | Description |
|--------|-------------|-------------|
| `-h` | `--help` | Show help message |
| `-v` | `--verbose` | Enable verbose output |
| `-g` | `--graph` | Display ASCII topology graph |
| `-d <file>` | `--dot <file>` | Generate DOT file for graphviz visualization |
| `-p <path>` | `--path <path>` | Override sysfs path (default: `/sys/bus/jesd204/devices`) |
| `-i [link]` | `--ignore-errors [link]` | Set `fsm_ignore_errors` for link (or all if no link specified) |
| `-I [link]` | `--no-ignore-errors [link]` | Clear `fsm_ignore_errors` for link (or all if no link specified) |

## Examples

### Display topology summary

```bash
jesd204_topology
```

### Display ASCII topology graph

```bash
jesd204_topology -g
```

Example output:
```
================================================================================
                         JESD204 Topology Graph
================================================================================

                          +--------------------------+
                          | ad9081@0                 |
                          | [TOP]                    |
                          +--------------------------+
                                        |
                                        v

           +--------------------------+  +--------------------------+
           | axi-ad9081-rx-hpc@8      |  | axi-ad9081-tx-hpc@8      |
           |                          |  |                          |
           +--------------------------+  +--------------------------+
                         |                             |
                         v                             v

           +--------------------------+  +--------------------------+
           | axi-jesd204-rx@8         |  | axi-jesd204-tx@8         |
           |                          |  |                          |
           +--------------------------+  +--------------------------+
                         |                             |
                         v                             v

           +--------------------------+  +--------------------------+
           | axi-adxcvr-rx@8          |  | axi-adxcvr-tx@8          |
           |                          |  |                          |
           +--------------------------+  +--------------------------+
                         |                             |
                         v                             v

                          +--------------------------+
                          | hmc7044@0                |
                          | [CLK]                    |
                          +--------------------------+

Legend: [TOP] = Top device (ADC/DAC)  [CLK] = Clock/SYSREF source

--------------------------------------------------------------------------------
Link 2 - RX (JESD204B)  State: opt_post_running_stage
--------------------------------------------------------------------------------
  JESD Parameters:  L=8  M=4  N=16  N'=16  F=1  K=32  S=1
  Encoder: 8B/10B    Subclass: 1  Scrambling: Yes  HD: No
  Sample Rate:  1.500000000000 GHz
  Lane Rate:    15.000000000000 GHz
  LMFC Rate:   46.875000000 MHz
  Device Clock: 375.000000000 MHz

--------------------------------------------------------------------------------
Link 0 - TX (JESD204B)  State: opt_post_running_stage
--------------------------------------------------------------------------------
  JESD Parameters:  L=8  M=4  N=16  N'=16  F=1  K=32  S=1
  Encoder: 8B/10B    Subclass: 1  Scrambling: Yes  HD: No
  Sample Rate:  1.500000000000 GHz
  Lane Rate:    15.000000000000 GHz
  LMFC Rate:   46.875000000 MHz
  Device Clock: 375.000000000 MHz

```

### Generate DOT file for graphviz

```bash
jesd204_topology -d topology.dot
```

Then visualize with graphviz:

```bash
dot -Tpng topology.dot -o topology.png
dot -Tsvg topology.dot -o topology.svg
```

### Set fsm_ignore_errors on all links

```bash
jesd204_topology -i
```

### Set fsm_ignore_errors on link 0 only

```bash
jesd204_topology -i 0
```

### Clear fsm_ignore_errors on all links

```bash
jesd204_topology -I
```

### Verbose output with DOT generation

```bash
jesd204_topology -v -d output.dot
```

## JESD204 Parameters

The tool displays standard JESD204 link parameters:

| Parameter | Description |
|-----------|-------------|
| L | Number of lanes |
| M | Number of converters |
| N | Converter resolution (bits) |
| N' | Total bits per sample (including control bits) |
| F | Octets per frame |
| K | Frames per multiframe |
| S | Samples per converter per frame |
| E | Multiblocks per extended multiblock (JESD204C only) |

## Calculated Rates

The tool calculates and displays the following rates:

- **Lane Rate**: Serial data rate per lane
- **LMFC Rate**: Local Multi-Frame Clock (JESD204A/B)
- **LEMC Rate**: Local Extended Multiblock Clock (JESD204C with 64B/66B or 64B/80B)
- **Device Clock**: Clock rate at the device interface

## Requirements

- Linux kernel with JESD204 subsystem enabled
- JESD204 devices configured and registered in sysfs

## Building

The tool is built as part of the jesd-eye-scan-gtk project:

```bash
mkdir build && cd build
cmake ..
make jesd204_topology
```

Or to build only jesd204_topology without GTK3 dependencies:

```bash
mkdir build && cd build
cmake -DUSE_JESD_EYE_SCAN=OFF -DUSE_JESD_STATUS=OFF ..
make
```

## License

Copyright 2024-2025(c) Analog Devices, Inc. All rights reserved.

Licensed under the ADI BSD license. See LICENSE.txt for details.
