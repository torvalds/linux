#!/usr/bin/env python3

import os
import signal
from string import Template
import subprocess
import time
from TdcPlugin import TdcPlugin

from tdc_config import *

try:
    from scapy.all import *
except ImportError:
    print("Unable to import the scapy python module.")
    print("\nIf not already installed, you may do so with:")
    print("\t\tpip3 install scapy==2.4.2")
    exit(1)

class SubPlugin(TdcPlugin):
    def __init__(self):
        self.sub_class = 'scapy/SubPlugin'
        super().__init__()

    def post_execute(self):
        if 'scapy' not in self.args.caseinfo:
            if self.args.verbose:
                print('{}.post_execute: no scapy info in test case'.format(self.sub_class))
            return

        # Check for required fields
        scapyinfo = self.args.caseinfo['scapy']
        scapy_keys = ['iface', 'count', 'packet']
        missing_keys = []
        keyfail = False
        for k in scapy_keys:
            if k not in scapyinfo:
                keyfail = True
                missing_keys.append(k)
        if keyfail:
            print('{}: Scapy block present in the test, but is missing info:'
                .format(self.sub_class))
            print('{}'.format(missing_keys))

        pkt = eval(scapyinfo['packet'])
        if '$' in scapyinfo['iface']:
            tpl = Template(scapyinfo['iface'])
            scapyinfo['iface'] = tpl.safe_substitute(NAMES)
        for count in range(scapyinfo['count']):
            sendp(pkt, iface=scapyinfo['iface'])
