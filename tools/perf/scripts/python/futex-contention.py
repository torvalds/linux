# futex contention
# (c) 2010, Arnaldo Carvalho de Melo <acme@redhat.com>
# Licensed under the terms of the GNU GPL License version 2
#
# Translation of:
#
# http://sourceware.org/systemtap/wiki/WSFutexContention
#
# to perf python scripting.
#
# Measures futex contention

from __future__ import print_function

import os
import sys
sys.path.append(os.environ['PERF_EXEC_PATH'] +
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')
from Util import *

process_names = {}
thread_thislock = {}
thread_blocktime = {}

lock_waits = {}  # long-lived stats on (tid,lock) blockage elapsed time
process_names = {}  # long-lived pid-to-execname mapping


def syscalls__sys_enter_futex(event, ctxt, cpu, s, ns, tid, comm, callchain,
                              nr, uaddr, op, val, utime, uaddr2, val3):
    cmd = op & FUTEX_CMD_MASK
    if cmd != FUTEX_WAIT:
        return  # we don't care about originators of WAKE events

    process_names[tid] = comm
    thread_thislock[tid] = uaddr
    thread_blocktime[tid] = nsecs(s, ns)


def syscalls__sys_exit_futex(event, ctxt, cpu, s, ns, tid, comm, callchain,
                             nr, ret):
    if tid in thread_blocktime:
        elapsed = nsecs(s, ns) - thread_blocktime[tid]
        add_stats(lock_waits, (tid, thread_thislock[tid]), elapsed)
        del thread_blocktime[tid]
        del thread_thislock[tid]


def trace_begin():
    print("Press control+C to stop and show the summary")


def trace_end():
    for (tid, lock) in lock_waits:
        min, max, avg, count = lock_waits[tid, lock]
        print("%s[%d] lock %x contended %d times, %d avg ns [max: %d ns, min %d ns]" %
              (process_names[tid], tid, lock, count, avg, max, min))
