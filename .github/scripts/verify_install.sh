#!/bin/bash
set -e

EXECUTABLE_LOCATION="/usr/bin"
SHARE_LOCATION="/usr/share"

echo "Verifying installation..."

# Check executables
for exe in jesd_eye_scan jesd_status jesd204_topology; do
    if [ -e "$EXECUTABLE_LOCATION/$exe" ]; then
        echo "[OK] $EXECUTABLE_LOCATION/$exe exists"
    else
        echo "[FAIL] $EXECUTABLE_LOCATION/$exe is missing"
        exit 1
    fi
done

# Check data files
for file in "$SHARE_LOCATION/jesd-eye-scan-gtk/jesd.glade" "$SHARE_LOCATION/jesd-eye-scan-gtk/ADIlogo.png"; do
    if [ -e "$file" ]; then
        echo "[OK] $file exists"
    else
        echo "[FAIL] $file is missing"
        exit 1
    fi
done

# Check desktop file
if [ -e "$SHARE_LOCATION/applications/jesd_eye_scan.desktop" ]; then
    echo "[OK] $SHARE_LOCATION/applications/jesd_eye_scan.desktop exists"
else
    echo "[FAIL] $SHARE_LOCATION/applications/jesd_eye_scan.desktop is missing"
    exit 1
fi

echo "Installation verification successful!"
