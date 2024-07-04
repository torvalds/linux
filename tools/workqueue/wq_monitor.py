#!/usr/bin/env drgn
#
# Copyright (C) 2023 Tejun Heo <tj@kernel.org>
# Copyright (C) 2023 Meta Platforms, Inc. and affiliates.

desc = """
This is a drgn script to monitor workqueues. For more info on drgn, visit
https://github.com/osandov/drgn.

  total    Total number of work items executed by the workqueue.

  infl     The number of currently in-flight work items.

  CPUtime  Total CPU time consumed by the workqueue in seconds. This is
           sampled from scheduler ticks and only provides ballpark
           measurement. "nohz_full=" CPUs are excluded from measurement.

  CPUitsv  The number of times a concurrency-managed work item hogged CPU
           longer than the threshold (workqueue.cpu_intensive_thresh_us)
           and got excluded from concurrency management to avoid stalling
           other work items.

  CMW/RPR  For per-cpu workqueues, the number of concurrency-management
           wake-ups while executing a work item of the workqueue. For
           unbound workqueues, the number of times a worker was repatriated
           to its affinity scope after being migrated to an off-scope CPU by
           the scheduler.

  mayday   The number of times the rescuer was requested while waiting for
           new worker creation.

  rescued  The number of work items executed by the rescuer.
"""

import signal
import re
import time
import json

import drgn
from drgn.helpers.linux.list import list_for_each_entry

import argparse
parser = argparse.ArgumentParser(description=desc,
                                 formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument('workqueue', metavar='REGEX', nargs='*',
                    help='Target workqueue name patterns (all if empty)')
parser.add_argument('-i', '--interval', metavar='SECS', type=float, default=1,
                    help='Monitoring interval (0 to print once and exit)')
parser.add_argument('-j', '--json', action='store_true',
                    help='Output in json')
args = parser.parse_args()

workqueues              = prog['workqueues']

WQ_UNBOUND              = prog['WQ_UNBOUND']
WQ_MEM_RECLAIM          = prog['WQ_MEM_RECLAIM']

PWQ_STAT_STARTED        = prog['PWQ_STAT_STARTED']      # work items started execution
PWQ_STAT_COMPLETED      = prog['PWQ_STAT_COMPLETED']	# work items completed execution
PWQ_STAT_CPU_TIME       = prog['PWQ_STAT_CPU_TIME']     # total CPU time consumed
PWQ_STAT_CPU_INTENSIVE  = prog['PWQ_STAT_CPU_INTENSIVE'] # wq_cpu_intensive_thresh_us violations
PWQ_STAT_CM_WAKEUP      = prog['PWQ_STAT_CM_WAKEUP']    # concurrency-management worker wakeups
PWQ_STAT_REPATRIATED    = prog['PWQ_STAT_REPATRIATED']  # unbound workers brought back into scope
PWQ_STAT_MAYDAY         = prog['PWQ_STAT_MAYDAY']	# maydays to rescuer
PWQ_STAT_RESCUED        = prog['PWQ_STAT_RESCUED']	# linked work items executed by rescuer
PWQ_NR_STATS            = prog['PWQ_NR_STATS']

class WqStats:
    def __init__(self, wq):
        self.name = wq.name.string_().decode()
        self.unbound = wq.flags & WQ_UNBOUND != 0
        self.mem_reclaim = wq.flags & WQ_MEM_RECLAIM != 0
        self.stats = [0] * PWQ_NR_STATS
        for pwq in list_for_each_entry('struct pool_workqueue', wq.pwqs.address_of_(), 'pwqs_node'):
            for i in range(PWQ_NR_STATS):
                self.stats[i] += int(pwq.stats[i])

    def dict(self, now):
        return { 'timestamp'            : now,
                 'name'                 : self.name,
                 'unbound'              : self.unbound,
                 'mem_reclaim'          : self.mem_reclaim,
                 'started'              : self.stats[PWQ_STAT_STARTED],
                 'completed'            : self.stats[PWQ_STAT_COMPLETED],
                 'cpu_time'             : self.stats[PWQ_STAT_CPU_TIME],
                 'cpu_intensive'        : self.stats[PWQ_STAT_CPU_INTENSIVE],
                 'cm_wakeup'            : self.stats[PWQ_STAT_CM_WAKEUP],
                 'repatriated'          : self.stats[PWQ_STAT_REPATRIATED],
                 'mayday'               : self.stats[PWQ_STAT_MAYDAY],
                 'rescued'              : self.stats[PWQ_STAT_RESCUED], }

    def table_header_str():
        return f'{"":>24} {"total":>8} {"infl":>5} {"CPUtime":>8} '\
            f'{"CPUitsv":>7} {"CMW/RPR":>7} {"mayday":>7} {"rescued":>7}'

    def table_row_str(self):
        cpu_intensive = '-'
        cmw_rpr = '-'
        mayday = '-'
        rescued = '-'

        if self.unbound:
            cmw_rpr = str(self.stats[PWQ_STAT_REPATRIATED]);
        else:
            cpu_intensive = str(self.stats[PWQ_STAT_CPU_INTENSIVE])
            cmw_rpr = str(self.stats[PWQ_STAT_CM_WAKEUP])

        if self.mem_reclaim:
            mayday = str(self.stats[PWQ_STAT_MAYDAY])
            rescued = str(self.stats[PWQ_STAT_RESCUED])

        out = f'{self.name[-24:]:24} ' \
              f'{self.stats[PWQ_STAT_STARTED]:8} ' \
              f'{max(self.stats[PWQ_STAT_STARTED] - self.stats[PWQ_STAT_COMPLETED], 0):5} ' \
              f'{self.stats[PWQ_STAT_CPU_TIME] / 1000000:8.1f} ' \
              f'{cpu_intensive:>7} ' \
              f'{cmw_rpr:>7} ' \
              f'{mayday:>7} ' \
              f'{rescued:>7} '
        return out.rstrip(':')

exit_req = False

def sigint_handler(signr, frame):
    global exit_req
    exit_req = True

def main():
    # handle args
    table_fmt = not args.json
    interval = args.interval

    re_str = None
    if args.workqueue:
        for r in args.workqueue:
            if re_str is None:
                re_str = r
            else:
                re_str += '|' + r

    filter_re = re.compile(re_str) if re_str else None

    # monitoring loop
    signal.signal(signal.SIGINT, sigint_handler)

    while not exit_req:
        now = time.time()

        if table_fmt:
            print()
            print(WqStats.table_header_str())

        for wq in list_for_each_entry('struct workqueue_struct', workqueues.address_of_(), 'list'):
            stats = WqStats(wq)
            if filter_re and not filter_re.search(stats.name):
                continue
            if table_fmt:
                print(stats.table_row_str())
            else:
                print(stats.dict(now))

        if interval == 0:
            break
        time.sleep(interval)

if __name__ == "__main__":
    main()
