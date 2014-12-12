#!/bin/sh

if [ -d /sys/bus/platform/devices/*axi-jesd204b-rx* ]
then
  sudo /usr/local/bin/jesd_eye_scan&
fi
