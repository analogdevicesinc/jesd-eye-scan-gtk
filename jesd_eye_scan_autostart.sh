#!/bin/sh

if ls /sys/bus/platform/devices/*axi-jesd204* 1> /dev/null 2>&1;
then
	sudo /usr/local/bin/jesd_eye_scan&
fi
