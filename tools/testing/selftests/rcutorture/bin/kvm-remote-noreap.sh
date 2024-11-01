#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Periodically scan a directory tree to prevent files from being reaped
# by systemd and friends on long runs.
#
# Usage: kvm-remote-noreap.sh pathname
#
# Copyright (C) 2021 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

pathname="$1"
if test "$pathname" = ""
then
	echo Usage: kvm-remote-noreap.sh pathname
	exit 1
fi
if ! test -d "$pathname"
then
	echo  Usage: kvm-remote-noreap.sh pathname
	echo "       pathname must be a directory."
	exit 2
fi

while test -d "$pathname"
do
	find "$pathname" -type f -exec touch -c {} \; > /dev/null 2>&1
	sleep 30
done
