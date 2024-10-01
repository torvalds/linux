#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Runs tests for the intel_sdsi driver

if ! command -v python3 > /dev/null 2>&1; then
	echo "drivers/sdsi: [SKIP] python3 not installed"
	exit 77
fi

if ! python3 -c "import pytest" > /dev/null 2>&1; then
	echo "drivers/sdsi: [SKIP] pytest module not installed"
	exit 77
fi

if ! /sbin/modprobe -q -r intel_sdsi; then
	echo "drivers/sdsi: [SKIP]"
	exit 77
fi

if /sbin/modprobe -q intel_sdsi && python3 -m pytest sdsi_test.py; then
	echo "drivers/sdsi: [OK]"
else
	echo "drivers/sdsi: [FAIL]"
	exit 1
fi
