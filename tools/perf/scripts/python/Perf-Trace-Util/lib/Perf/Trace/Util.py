# Util.py - Python extension for perf script, miscellaneous utility code
#
# Copyright (C) 2010 by Tom Zanussi <tzanussi@gmail.com>
#
# This software may be distributed under the terms of the GNU General
# Public License ("GPL") version 2 as published by the Free Software
# Foundation.
from __future__ import print_function

import errno, os

FUTEX_WAIT = 0
FUTEX_WAKE = 1
FUTEX_PRIVATE_FLAG = 128
FUTEX_CLOCK_REALTIME = 256
FUTEX_CMD_MASK = ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

NSECS_PER_SEC    = 1000000000

def avg(total, n):
    return total / n

def nsecs(secs, nsecs):
    return secs * NSECS_PER_SEC + nsecs

def nsecs_secs(nsecs):
    return nsecs / NSECS_PER_SEC

def nsecs_nsecs(nsecs):
    return nsecs % NSECS_PER_SEC

def nsecs_str(nsecs):
    str = "%5u.%09u" % (nsecs_secs(nsecs), nsecs_nsecs(nsecs)),
    return str

def add_stats(dict, key, value):
	if key not in dict:
		dict[key] = (value, value, value, 1)
	else:
		min, max, avg, count = dict[key]
		if value < min:
			min = value
		if value > max:
			max = value
		avg = (avg + value) / 2
		dict[key] = (min, max, avg, count + 1)

def clear_term():
    print("\x1b[H\x1b[2J")

audit_package_warned = False

try:
	import audit
	machine_to_id = {
		'x86_64': audit.MACH_86_64,
		'aarch64': audit.MACH_AARCH64,
		'alpha'	: audit.MACH_ALPHA,
		'ia64'	: audit.MACH_IA64,
		'ppc'	: audit.MACH_PPC,
		'ppc64'	: audit.MACH_PPC64,
		'ppc64le' : audit.MACH_PPC64LE,
		's390'	: audit.MACH_S390,
		's390x'	: audit.MACH_S390X,
		'i386'	: audit.MACH_X86,
		'i586'	: audit.MACH_X86,
		'i686'	: audit.MACH_X86,
	}
	try:
		machine_to_id['armeb'] = audit.MACH_ARMEB
	except:
		pass
	machine_id = machine_to_id[os.uname()[4]]
except:
	if not audit_package_warned:
		audit_package_warned = True
		print("Install the python-audit package to get syscall names.\n"
                    "For example:\n  # apt-get install python3-audit (Ubuntu)"
                    "\n  # yum install python3-audit (Fedora)"
                    "\n  etc.\n")

def syscall_name(id):
	try:
		return audit.audit_syscall_to_name(id, machine_id)
	except:
		return str(id)

def strerror(nr):
	try:
		return errno.errorcode[abs(nr)]
	except:
		return "Unknown %d errno" % nr
