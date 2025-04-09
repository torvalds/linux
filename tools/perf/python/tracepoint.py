#! /usr/bin/env python
# SPDX-License-Identifier: GPL-2.0
# -*- python -*-
# -*- coding: utf-8 -*-

import perf

def change_proctitle():
    try:
        import setproctitle
        setproctitle.setproctitle("tracepoint.py")
    except:
        print("Install the setproctitle python package to help with top and friends")

def main():
    change_proctitle()
    cpus    = perf.cpu_map()
    threads = perf.thread_map(-1)
    evlist = perf.parse_events("sched:sched_switch", cpus, threads)
    # Disable tracking of mmaps and similar that are unnecessary.
    for ev in evlist:
        ev.tracking = False
    # Configure evsels with default record options.
    evlist.config()
    # Simplify the sample_type and read_format of evsels
    for ev in evlist:
        ev.sample_type = ev.sample_type & ~perf.SAMPLE_IP
        ev.read_format = 0

    evlist.open()
    evlist.mmap()
    evlist.enable();

    while True:
        evlist.poll(timeout = -1)
        for cpu in cpus:
            event = evlist.read_on_cpu(cpu)
            if not event:
                continue

            if not isinstance(event, perf.sample_event):
                continue

            print("time %u prev_comm=%s prev_pid=%d prev_prio=%d prev_state=0x%x ==> next_comm=%s next_pid=%d next_prio=%d" % (
                   event.sample_time,
                   event.prev_comm,
                   event.prev_pid,
                   event.prev_prio,
                   event.prev_state,
                   event.next_comm,
                   event.next_pid,
                   event.next_prio))

if __name__ == '__main__':
    main()
