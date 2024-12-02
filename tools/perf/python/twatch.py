#! /usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only
# -*- python -*-
# -*- coding: utf-8 -*-
#   twatch - Experimental use of the perf python interface
#   Copyright (C) 2011 Arnaldo Carvalho de Melo <acme@redhat.com>
#

import perf

def main(context_switch = 0, thread = -1):
	cpus = perf.cpu_map()
	threads = perf.thread_map(thread)
	evsel = perf.evsel(type	  = perf.TYPE_SOFTWARE,
			   config = perf.COUNT_SW_DUMMY,
			   task = 1, comm = 1, mmap = 0, freq = 0,
			   wakeup_events = 1, watermark = 1,
			   sample_id_all = 1, context_switch = context_switch,
			   sample_type = perf.SAMPLE_PERIOD | perf.SAMPLE_TID | perf.SAMPLE_CPU)

	"""What we want are just the PERF_RECORD_ lifetime events for threads,
	 using the default, PERF_TYPE_HARDWARE + PERF_COUNT_HW_CYCLES & freq=1
	 (the default), makes perf reenable irq_vectors:local_timer_entry, when
	 disabling nohz, not good for some use cases where all we want is to get
	 threads comes and goes... So use (perf.TYPE_SOFTWARE, perf_COUNT_SW_DUMMY,
	 freq=0) instead."""

	evsel.open(cpus = cpus, threads = threads);
	evlist = perf.evlist(cpus, threads)
	evlist.add(evsel)
	evlist.mmap()
	while True:
		evlist.poll(timeout = -1)
		for cpu in cpus:
			event = evlist.read_on_cpu(cpu)
			if not event:
				continue
			print("cpu: {0}, pid: {1}, tid: {2} {3}".format(event.sample_cpu,
                                                                        event.sample_pid,
                                                                        event.sample_tid,
                                                                        event))

if __name__ == '__main__':
    """
	To test the PERF_RECORD_SWITCH record, pick a pid and replace
	in the following line.

	Example output:

cpu: 3, pid: 31463, tid: 31593 { type: context_switch, next_prev_pid: 31463, next_prev_tid: 31593, switch_out: 1 }
cpu: 1, pid: 31463, tid: 31489 { type: context_switch, next_prev_pid: 31463, next_prev_tid: 31489, switch_out: 1 }
cpu: 2, pid: 31463, tid: 31496 { type: context_switch, next_prev_pid: 31463, next_prev_tid: 31496, switch_out: 1 }
cpu: 3, pid: 31463, tid: 31491 { type: context_switch, next_prev_pid: 31463, next_prev_tid: 31491, switch_out: 0 }

	It is possible as well to use event.misc & perf.PERF_RECORD_MISC_SWITCH_OUT
	to figure out if this is a context switch in or out of the monitored threads.

	If bored, please add command line option parsing support for these options :-)
    """
    # main(context_switch = 1, thread = 31463)
    main()
