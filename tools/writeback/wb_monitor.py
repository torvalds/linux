#!/usr/bin/env drgn
#
# Copyright (C) 2024 Kemeng Shi <shikemeng@huaweicloud.com>
# Copyright (C) 2024 Huawei Inc

desc = """
This is a drgn script based on wq_monitor.py to monitor writeback info on
backing dev. For more info on drgn, visit https://github.com/osandov/drgn.

  writeback(kB)     Amount of dirty pages are currently being written back to
                    disk.

  reclaimable(kB)   Amount of pages are currently reclaimable.

  dirtied(kB)       Amount of pages have been dirtied.

  wrttien(kB)       Amount of dirty pages have been written back to disk.

  avg_wb(kBps)      Smoothly estimated write bandwidth of writing dirty pages
                    back to disk.
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
parser.add_argument('bdi', metavar='REGEX', nargs='*',
                    help='Target backing device name patterns (all if empty)')
parser.add_argument('-i', '--interval', metavar='SECS', type=float, default=1,
                    help='Monitoring interval (0 to print once and exit)')
parser.add_argument('-j', '--json', action='store_true',
                    help='Output in json')
parser.add_argument('-c', '--cgroup', action='store_true',
                    help='show writeback of bdi in cgroup')
args = parser.parse_args()

bdi_list                = prog['bdi_list']

WB_RECLAIMABLE          = prog['WB_RECLAIMABLE']
WB_WRITEBACK            = prog['WB_WRITEBACK']
WB_DIRTIED              = prog['WB_DIRTIED']
WB_WRITTEN              = prog['WB_WRITTEN']
NR_WB_STAT_ITEMS        = prog['NR_WB_STAT_ITEMS']

PAGE_SHIFT              = prog['PAGE_SHIFT']

def K(x):
    return x << (PAGE_SHIFT - 10)

class Stats:
    def dict(self, now):
        return { 'timestamp'            : now,
                 'name'                 : self.name,
                 'writeback'            : self.stats[WB_WRITEBACK],
                 'reclaimable'          : self.stats[WB_RECLAIMABLE],
                 'dirtied'              : self.stats[WB_DIRTIED],
                 'written'              : self.stats[WB_WRITTEN],
                 'avg_wb'               : self.avg_bw, }

    def table_header_str():
        return f'{"":>16} {"writeback":>10} {"reclaimable":>12} ' \
                f'{"dirtied":>9} {"written":>9} {"avg_bw":>9}'

    def table_row_str(self):
        out = f'{self.name[-16:]:16} ' \
              f'{self.stats[WB_WRITEBACK]:10} ' \
              f'{self.stats[WB_RECLAIMABLE]:12} ' \
              f'{self.stats[WB_DIRTIED]:9} ' \
              f'{self.stats[WB_WRITTEN]:9} ' \
              f'{self.avg_bw:9} '
        return out

    def show_header():
        if Stats.table_fmt:
            print()
            print(Stats.table_header_str())

    def show_stats(self):
        if Stats.table_fmt:
            print(self.table_row_str())
        else:
            print(self.dict(Stats.now))

class WbStats(Stats):
    def __init__(self, wb):
        bdi_name = wb.bdi.dev_name.string_().decode()
        # avoid to use bdi.wb.memcg_css which is only defined when
        # CONFIG_CGROUP_WRITEBACK is enabled
        if wb == wb.bdi.wb.address_of_():
            ino = "1"
        else:
            ino = str(wb.memcg_css.cgroup.kn.id.value_())
        self.name = bdi_name + '_' + ino

        self.stats = [0] * NR_WB_STAT_ITEMS
        for i in range(NR_WB_STAT_ITEMS):
            if wb.stat[i].count >= 0:
                self.stats[i] = int(K(wb.stat[i].count))
            else:
                self.stats[i] = 0

        self.avg_bw = int(K(wb.avg_write_bandwidth))

class BdiStats(Stats):
    def __init__(self, bdi):
        self.name = bdi.dev_name.string_().decode()
        self.stats = [0] * NR_WB_STAT_ITEMS
        self.avg_bw = 0

    def collectStats(self, wb_stats):
        for i in range(NR_WB_STAT_ITEMS):
            self.stats[i] += wb_stats.stats[i]

        self.avg_bw += wb_stats.avg_bw

exit_req = False

def sigint_handler(signr, frame):
    global exit_req
    exit_req = True

def main():
    # handle args
    Stats.table_fmt = not args.json
    interval = args.interval
    cgroup = args.cgroup

    re_str = None
    if args.bdi:
        for r in args.bdi:
            if re_str is None:
                re_str = r
            else:
                re_str += '|' + r

    filter_re = re.compile(re_str) if re_str else None

    # monitoring loop
    signal.signal(signal.SIGINT, sigint_handler)

    while not exit_req:
        Stats.now = time.time()

        Stats.show_header()
        for bdi in list_for_each_entry('struct backing_dev_info', bdi_list.address_of_(), 'bdi_list'):
            bdi_stats = BdiStats(bdi)
            if filter_re and not filter_re.search(bdi_stats.name):
                continue

            for wb in list_for_each_entry('struct bdi_writeback', bdi.wb_list.address_of_(), 'bdi_node'):
                wb_stats = WbStats(wb)
                bdi_stats.collectStats(wb_stats)
                if cgroup:
                    wb_stats.show_stats()

            bdi_stats.show_stats()
            if cgroup and Stats.table_fmt:
                print()

        if interval == 0:
            break
        time.sleep(interval)

if __name__ == "__main__":
    main()
