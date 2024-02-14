# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) NXP 2019

import gdb
import sys

from linux.utils import CachedType, gdb_eval_or_none
from linux.lists import list_for_each_entry

generic_pm_domain_type = CachedType('struct generic_pm_domain')
pm_domain_data_type = CachedType('struct pm_domain_data')
device_link_type = CachedType('struct device_link')


def kobject_get_path(kobj):
    path = kobj['name'].string()
    parent = kobj['parent']
    if parent:
        path = kobject_get_path(parent) + '/' + path
    return path


def rtpm_status_str(dev):
    if dev['power']['runtime_error']:
        return 'error'
    if dev['power']['disable_depth']:
        return 'unsupported'
    _RPM_STATUS_LOOKUP = [
        "active",
        "resuming",
        "suspended",
        "suspending"
    ]
    return _RPM_STATUS_LOOKUP[dev['power']['runtime_status']]


class LxGenPDSummary(gdb.Command):
    '''Print genpd summary

Output is similar to /sys/kernel/debug/pm_genpd/pm_genpd_summary'''

    def __init__(self):
        super(LxGenPDSummary, self).__init__('lx-genpd-summary', gdb.COMMAND_DATA)

    def summary_one(self, genpd):
        if genpd['status'] == 0:
            status_string = 'on'
        else:
            status_string = 'off-{}'.format(genpd['state_idx'])

        child_names = []
        for link in list_for_each_entry(
                genpd['parent_links'],
                device_link_type.get_type().pointer(),
                'parent_node'):
            child_names.append(link['child']['name'])

        gdb.write('%-30s  %-15s %s\n' % (
                genpd['name'].string(),
                status_string,
                ', '.join(child_names)))

        # Print devices in domain
        for pm_data in list_for_each_entry(genpd['dev_list'],
                        pm_domain_data_type.get_type().pointer(),
                        'list_node'):
            dev = pm_data['dev']
            kobj_path = kobject_get_path(dev['kobj'])
            gdb.write('    %-50s  %s\n' % (kobj_path, rtpm_status_str(dev)))

    def invoke(self, arg, from_tty):
        if gdb_eval_or_none("&gpd_list") is None:
            raise gdb.GdbError("No power domain(s) registered")
        gdb.write('domain                          status          children\n');
        gdb.write('    /device                                             runtime status\n');
        gdb.write('----------------------------------------------------------------------\n');
        for genpd in list_for_each_entry(
                gdb.parse_and_eval('&gpd_list'),
                generic_pm_domain_type.get_type().pointer(),
                'gpd_list_node'):
            self.summary_one(genpd)


LxGenPDSummary()
