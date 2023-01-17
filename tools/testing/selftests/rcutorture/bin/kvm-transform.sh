#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Transform a qemu-cmd file to allow reuse.
#
# Usage: kvm-transform.sh bzImage console.log jitter_dir seconds [ bootargs ] < qemu-cmd-in > qemu-cmd-out
#
#	bzImage: Kernel and initrd from the same prior kvm.sh run.
#	console.log: File into which to place console output.
#	jitter_dir: Jitter directory for TORTURE_JITTER_START and
#		TORTURE_JITTER_STOP environment variables.
#	seconds: Run duaration for *.shutdown_secs module parameter.
#	bootargs: New kernel boot parameters.  Beware of Robert Tables.
#
# The original qemu-cmd file is provided on standard input.
# The transformed qemu-cmd file is on standard output.
# The transformation assumes that the qemu command is confined to a
# single line.  It also assumes no whitespace in filenames.
#
# Copyright (C) 2020 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

T=`mktemp -d /tmp/kvm-transform.sh.XXXXXXXXXX`
trap 'rm -rf $T' 0 2

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
jitter_dir="$3"
if test -z "$jitter_dir" || ! test -d "$jitter_dir"
then
	echo "Need valid jitter directory: '$jitter_dir'"
	exit 1
fi
seconds="$4"
if test -n "$seconds" && echo $seconds | grep -q '[^0-9]'
then
	echo "Invalid duration, should be numeric in seconds: '$seconds'"
	exit 1
fi
bootargs="$5"

# Build awk program.
echo "BEGIN {" > $T/bootarg.awk
echo $bootargs | tr -s ' ' '\012' |
	awk -v dq='"' '/./ { print "\tbootarg[" NR "] = " dq $1 dq ";" }' >> $T/bootarg.awk
echo $bootargs | tr -s ' ' '\012' | sed -e 's/=.*$//' |
	awk -v dq='"' '/./ { print "\tbootpar[" NR "] = " dq $1 dq ";" }' >> $T/bootarg.awk
cat >> $T/bootarg.awk << '___EOF___'
}

/^# seconds=/ {
	if (seconds == "")
		print $0;
	else
		print "# seconds=" seconds;
	next;
}

/^# TORTURE_JITTER_START=/ {
	print "# TORTURE_JITTER_START=\". jitterstart.sh " $4 " " jitter_dir " " $6 " " $7;
	next;
}

/^# TORTURE_JITTER_STOP=/ {
	print "# TORTURE_JITTER_STOP=\". jitterstop.sh " " " jitter_dir " " $5;
	next;
}

/^#/ {
	print $0;
	next;
}

{
	line = "";
	for (i = 1; i <= NF; i++) {
		if (line == "") {
			line = $i;
		} else {
			line = line " " $i;
		}
		if ($i == "-serial") {
			i++;
			line = line " file:" consolelog;
		} else if ($i == "-kernel") {
			i++;
			line = line " " image;
		} else if ($i == "-append") {
			for (i++; i <= NF; i++) {
				arg = $i;
				lq = "";
				rq = "";
				if ("" seconds != "" && $i ~ /\.shutdown_secs=[0-9]*$/)
					sub(/[0-9]*$/, seconds, arg);
				if (arg ~ /^"/) {
					lq = substr(arg, 1, 1);
					arg  = substr(arg, 2);
				}
				if (arg ~ /"$/) {
					rq = substr(arg, length($i), 1);
					arg = substr(arg, 1, length($i) - 1);
				}
				par = arg;
				gsub(/=.*$/, "", par);
				j = 1;
				while (bootpar[j] != "") {
					if (bootpar[j] == par) {
						arg = "";
						break;
					}
					j++;
				}
				if (line == "")
					line = lq arg;
				else
					line = line " " lq arg;
			}
			for (j in bootarg)
				line = line " " bootarg[j];
			line = line rq;
		}
	}
	print line;
}
___EOF___

awk -v image="$image" -v consolelog="$consolelog" -v jitter_dir="$jitter_dir" \
    -v seconds="$seconds" -f $T/bootarg.awk
