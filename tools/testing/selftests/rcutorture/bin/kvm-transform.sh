#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Transform a qemu-cmd file to allow reuse.
#
# Usage: kvm-transform.sh bzImage console.log < qemu-cmd-in > qemu-cmd-out
#
#	bzImage: Kernel and initrd from the same prior kvm.sh run.
#	console.log: File into which to place console output.
#
# The original qemu-cmd file is provided on standard input.
# The transformed qemu-cmd file is on standard output.
# The transformation assumes that the qemu command is confined to a
# single line.  It also assumes no whitespace in filenames.
#
# Copyright (C) 2020 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

image="$1"
if test -z "$image"
then
	echo Need kernel image file.
	exit 1
fi
consolelog="$2"
if test -z "$consolelog"
then
	echo "Need console log file name."
	exit 1
fi

awk -v image="$image" -v consolelog="$consolelog" '
{
	line = "";
	for (i = 1; i <= NF; i++) {
		if (line == "")
			line = $i;
		else
			line = line " " $i;
		if ($i == "-serial") {
			i++;
			line = line " file:" consolelog;
		}
		if ($i == "-kernel") {
			i++;
			line = line " " image;
		}
	}
	print line;
}'
