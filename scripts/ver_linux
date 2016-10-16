#!/bin/awk -f
# Before running this script please ensure that your PATH is
# typical as you use for compilation/installation. I use
# /bin /sbin /usr/bin /usr/sbin /usr/local/bin, but it may
# differ on your system.

BEGIN {
	usage = "If some fields are empty or look unusual you may have an old version.\n"
	usage = usage "Compare to the current minimal requirements in Documentation/Changes.\n"
	print usage

	system("uname -a")
	printf("\n")

	printversion("GNU C", version("gcc -dumpversion 2>&1"))
	printversion("GNU Make", version("make --version 2>&1"))
	printversion("Binutils", version("ld -v 2>&1"))
	printversion("Util-linux", version("mount --version 2>&1"))
	printversion("Mount", version("mount --version 2>&1"))
	printversion("Module-init-tools", version("depmod -V  2>&1"))
	printversion("E2fsprogs", version("tune2fs 2>&1"))
	printversion("Jfsutils", version("fsck.jfs -V 2>&1"))
	printversion("Reiserfsprogs", version("reiserfsck -V 2>&1"))
	printversion("Reiser4fsprogs", version("fsck.reiser4 -V 2>&1"))
	printversion("Xfsprogs", version("xfs_db -V 2>&1"))
	printversion("Pcmciautils", version("pccardctl -V 2>&1"))
	printversion("Pcmcia-cs", version("cardmgr -V 2>&1"))
	printversion("Quota-tools", version("quota -V 2>&1"))
	printversion("PPP", version("pppd --version 2>&1"))
	printversion("Isdn4k-utils", version("isdnctrl 2>&1"))
	printversion("Nfs-utils", version("showmount --version 2>&1"))

	if (system("test -r /proc/self/maps") == 0) {
		while (getline <"/proc/self/maps" > 0) {
			n = split($0, procmaps, "/")
			if (/libc.*so$/ && match(procmaps[n], /[0-9]+([.]?[0-9]+)+/)) {
				ver = substr(procmaps[n], RSTART, RLENGTH)
				printversion("Linux C Library", ver)
				break
			}
		}
	}

	printversion("Dynamic linker (ldd)", version("ldd --version 2>&1"))

	while ("ldconfig -p 2>/dev/null" | getline > 0) {
		if (/(libg|stdc)[+]+\.so/) {
			libcpp = $NF
			break
		}
	}
	if (system("test -r " libcpp) == 0)
		printversion("Linux C++ Library", version("readlink " libcpp))

	printversion("Procps", version("ps --version 2>&1"))
	printversion("Net-tools", version("ifconfig --version 2>&1"))
	printversion("Kbd", version("loadkeys -V 2>&1"))
	printversion("Console-tools", version("loadkeys -V 2>&1"))
	printversion("Oprofile", version("oprofiled --version 2>&1"))
	printversion("Sh-utils", version("expr --v 2>&1"))
	printversion("Udev", version("udevadm --version 2>&1"))
	printversion("Wireless-tools", version("iwconfig --version 2>&1"))

	if (system("test -r /proc/modules") == 0) {
		while ("sort /proc/modules" | getline > 0) {
			mods = mods sep $1
			sep = " "
		}
		printversion("Modules Loaded", mods)
	}
}

function version(cmd,    ver) {
	while (cmd | getline > 0) {
		if (!/ver_linux/ && match($0, /[0-9]+([.]?[0-9]+)+/)) {
			ver = substr($0, RSTART, RLENGTH)
			break
		}
	}
	close(cmd)
	return ver
}

function printversion(name, value,  ofmt) {
	if (value != "") {
		ofmt = "%-20s\t%s\n"
		printf(ofmt, name, value)
	}
}
