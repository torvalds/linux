#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

check_dependencies()
{
	if [ $EUID -ne 0 ]
	then
		echo "Run as root"
		exit $ksft_skip
	fi
}
