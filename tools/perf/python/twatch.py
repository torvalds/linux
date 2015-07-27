#! /usr/bin/python
# -*- python -*-
# -*- coding: utf-8 -*-
#   twatch - Experimental use of the perf python interface
#   Copyright (C) 2011 Arnaldo Carvalho de Melo <acme@redhat.com>
#
#   This application is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License
#   as published by the Free Software Foundation; version 2.
#
#   This application is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.

import perf

def main():
	cpus = perf.cpu_map()
	threads = perf.thread_map()
	evsel = perf.evsel(type	  = perf.TYPE_SOFTWARE,
			   config = perf.COUNT_SW_DUMMY,
			   task = 1, comm = 1, mmap = 0, freq = 0,
			   wakeup_events = 1, watermark = 1,
			   sample_id_all = 1,
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
			print "cpu: %2d, pid: %4d, tid: %4d" % (event.sample_cpu,
								event.sample_pid,
								event.sample_tid),
			print event

if __name__ == '__main__':
    main()
