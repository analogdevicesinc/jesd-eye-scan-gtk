#!/bin/sh

if [ -d /sys/bus/platform/devices/*axi-jesd204* ]
then
  sudo /usr/local/bin/jesd_eye_scan&
fi
