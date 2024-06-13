#!/usr/bin/env drgn
#
# Copyright (C) 2019 Tejun Heo <tj@kernel.org>
# Copyright (C) 2019 Facebook

desc = """
This is a drgn script to monitor the blk-iocost cgroup controller.
See the comment at the top of block/blk-iocost.c for more details.
For drgn, visit https://github.com/osandov/drgn.
"""

import sys
import re
import time
import json
import math

import drgn
from drgn import container_of
from drgn.helpers.linux.list import list_for_each_entry,list_empty
from drgn.helpers.linux.radixtree import radix_tree_for_each,radix_tree_lookup

import argparse
parser = argparse.ArgumentParser(description=desc,
                                 formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument('devname', metavar='DEV',
                    help='Target block device name (e.g. sda)')
parser.add_argument('--cgroup', action='append', metavar='REGEX',
                    help='Regex for target cgroups, ')
parser.add_argument('--interval', '-i', metavar='SECONDS', type=float, default=1,
                    help='Monitoring interval in seconds (0 exits immediately '
                    'after checking requirements)')
parser.add_argument('--json', action='store_true',
                    help='Output in json')
args = parser.parse_args()

def err(s):
    print(s, file=sys.stderr, flush=True)
    sys.exit(1)

try:
    blkcg_root = prog['blkcg_root']
    plid = prog['blkcg_policy_iocost'].plid.value_()
except:
    err('The kernel does not have iocost enabled')

IOC_RUNNING     = prog['IOC_RUNNING'].value_()
WEIGHT_ONE      = prog['WEIGHT_ONE'].value_()
VTIME_PER_SEC   = prog['VTIME_PER_SEC'].value_()
VTIME_PER_USEC  = prog['VTIME_PER_USEC'].value_()
AUTOP_SSD_FAST  = prog['AUTOP_SSD_FAST'].value_()
AUTOP_SSD_DFL   = prog['AUTOP_SSD_DFL'].value_()
AUTOP_SSD_QD1   = prog['AUTOP_SSD_QD1'].value_()
AUTOP_HDD       = prog['AUTOP_HDD'].value_()

autop_names = {
    AUTOP_SSD_FAST:        'ssd_fast',
    AUTOP_SSD_DFL:         'ssd_dfl',
    AUTOP_SSD_QD1:         'ssd_qd1',
    AUTOP_HDD:             'hdd',
}

class BlkgIterator:
    def __init__(self, root_blkcg, q_id, include_dying=False):
        self.include_dying = include_dying
        self.blkgs = []
        self.walk(root_blkcg, q_id, '')

    def blkcg_name(blkcg):
        return blkcg.css.cgroup.kn.name.string_().decode('utf-8')

    def walk(self, blkcg, q_id, parent_path):
        if not self.include_dying and \
           not (blkcg.css.flags.value_() & prog['CSS_ONLINE'].value_()):
            return

        name = BlkgIterator.blkcg_name(blkcg)
        path = parent_path + '/' + name if parent_path else name
        blkg = drgn.Object(prog, 'struct blkcg_gq',
                           address=radix_tree_lookup(blkcg.blkg_tree.address_of_(), q_id))
        if not blkg.address_:
            return

        self.blkgs.append((path if path else '/', blkg))

        for c in list_for_each_entry('struct blkcg',
                                     blkcg.css.children.address_of_(), 'css.sibling'):
            self.walk(c, q_id, path)

    def __iter__(self):
        return iter(self.blkgs)

class IocStat:
    def __init__(self, ioc):
        global autop_names

        self.enabled = ioc.enabled.value_()
        self.running = ioc.running.value_() == IOC_RUNNING
        self.period_ms = ioc.period_us.value_() / 1_000
        self.period_at = ioc.period_at.value_() / 1_000_000
        self.vperiod_at = ioc.period_at_vtime.value_() / VTIME_PER_SEC
        self.vrate_pct = ioc.vtime_base_rate.value_() * 100 / VTIME_PER_USEC
        self.busy_level = ioc.busy_level.value_()
        self.autop_idx = ioc.autop_idx.value_()
        self.user_cost_model = ioc.user_cost_model.value_()
        self.user_qos_params = ioc.user_qos_params.value_()

        if self.autop_idx in autop_names:
            self.autop_name = autop_names[self.autop_idx]
        else:
            self.autop_name = '?'

    def dict(self, now):
        return { 'device'               : devname,
                 'timestamp'            : now,
                 'enabled'              : self.enabled,
                 'running'              : self.running,
                 'period_ms'            : self.period_ms,
                 'period_at'            : self.period_at,
                 'period_vtime_at'      : self.vperiod_at,
                 'busy_level'           : self.busy_level,
                 'vrate_pct'            : self.vrate_pct, }

    def table_preamble_str(self):
        state = ('RUN' if self.running else 'IDLE') if self.enabled else 'OFF'
        output = f'{devname} {state:4} ' \
                 f'per={self.period_ms}ms ' \
                 f'cur_per={self.period_at:.3f}:v{self.vperiod_at:.3f} ' \
                 f'busy={self.busy_level:+3} ' \
                 f'vrate={self.vrate_pct:6.2f}% ' \
                 f'params={self.autop_name}'
        if self.user_cost_model or self.user_qos_params:
            output += f'({"C" if self.user_cost_model else ""}{"Q" if self.user_qos_params else ""})'
        return output

    def table_header_str(self):
        return f'{"":25} active {"weight":>9} {"hweight%":>13} {"inflt%":>6} ' \
               f'{"debt":>7} {"delay":>7} {"usage%"}'

class IocgStat:
    def __init__(self, iocg):
        ioc = iocg.ioc
        blkg = iocg.pd.blkg

        self.is_active = not list_empty(iocg.active_list.address_of_())
        self.weight = iocg.weight.value_() / WEIGHT_ONE
        self.active = iocg.active.value_() / WEIGHT_ONE
        self.inuse = iocg.inuse.value_() / WEIGHT_ONE
        self.hwa_pct = iocg.hweight_active.value_() * 100 / WEIGHT_ONE
        self.hwi_pct = iocg.hweight_inuse.value_() * 100 / WEIGHT_ONE
        self.address = iocg.value_()

        vdone = iocg.done_vtime.counter.value_()
        vtime = iocg.vtime.counter.value_()
        vrate = ioc.vtime_rate.counter.value_()
        period_vtime = ioc.period_us.value_() * vrate
        if period_vtime:
            self.inflight_pct = (vtime - vdone) * 100 / period_vtime
        else:
            self.inflight_pct = 0

        self.usage = (100 * iocg.usage_delta_us.value_() /
                      ioc.period_us.value_()) if self.active else 0
        self.debt_ms = iocg.abs_vdebt.value_() / VTIME_PER_USEC / 1000
        if blkg.use_delay.counter.value_() != 0:
            self.delay_ms = blkg.delay_nsec.counter.value_() / 1_000_000
        else:
            self.delay_ms = 0

    def dict(self, now, path):
        out = { 'cgroup'                : path,
                'timestamp'             : now,
                'is_active'             : self.is_active,
                'weight'                : self.weight,
                'weight_active'         : self.active,
                'weight_inuse'          : self.inuse,
                'hweight_active_pct'    : self.hwa_pct,
                'hweight_inuse_pct'     : self.hwi_pct,
                'inflight_pct'          : self.inflight_pct,
                'debt_ms'               : self.debt_ms,
                'delay_ms'              : self.delay_ms,
                'usage_pct'             : self.usage,
                'address'               : self.address }
        return out

    def table_row_str(self, path):
        out = f'{path[-28:]:28} ' \
              f'{"*" if self.is_active else " "} ' \
              f'{round(self.inuse):5}/{round(self.active):5} ' \
              f'{self.hwi_pct:6.2f}/{self.hwa_pct:6.2f} ' \
              f'{self.inflight_pct:6.2f} ' \
              f'{self.debt_ms:7.2f} ' \
              f'{self.delay_ms:7.2f} '\
              f'{min(self.usage, 999):6.2f}'
        out = out.rstrip(':')
        return out

# handle args
table_fmt = not args.json
interval = args.interval
devname = args.devname

if args.json:
    table_fmt = False

re_str = None
if args.cgroup:
    for r in args.cgroup:
        if re_str is None:
            re_str = r
        else:
            re_str += '|' + r

filter_re = re.compile(re_str) if re_str else None

# Locate the roots
q_id = None
root_iocg = None
ioc = None

for i, ptr in radix_tree_for_each(blkcg_root.blkg_tree.address_of_()):
    blkg = drgn.Object(prog, 'struct blkcg_gq', address=ptr)
    try:
        if devname == blkg.q.kobj.parent.name.string_().decode('utf-8'):
            q_id = blkg.q.id.value_()
            if blkg.pd[plid]:
                root_iocg = container_of(blkg.pd[plid], 'struct ioc_gq', 'pd')
                ioc = root_iocg.ioc
            break
    except:
        pass

if ioc is None:
    err(f'Could not find ioc for {devname}');

if interval == 0:
    sys.exit(0)

# Keep printing
while True:
    now = time.time()
    iocstat = IocStat(ioc)
    output = ''

    if table_fmt:
        output += '\n' + iocstat.table_preamble_str()
        output += '\n' + iocstat.table_header_str()
    else:
        output += json.dumps(iocstat.dict(now))

    for path, blkg in BlkgIterator(blkcg_root, q_id):
        if filter_re and not filter_re.match(path):
            continue
        if not blkg.pd[plid]:
            continue

        iocg = container_of(blkg.pd[plid], 'struct ioc_gq', 'pd')
        iocg_stat = IocgStat(iocg)

        if not filter_re and not iocg_stat.is_active:
            continue

        if table_fmt:
            output += '\n' + iocg_stat.table_row_str(path)
        else:
            output += '\n' + json.dumps(iocg_stat.dict(now, path))

    print(output)
    sys.stdout.flush()
    time.sleep(interval)
