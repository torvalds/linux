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
	evsel = perf.evsel(task = 1, comm = 1, mmap = 0,
			   wakeup_events = 1, sample_period = 1,
			   sample_id_all = 1,
			   sample_type = perf.SAMPLE_PERIOD | perf.SAMPLE_TID | perf.SAMPLE_CPU | perf.SAMPLE_TID)
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
