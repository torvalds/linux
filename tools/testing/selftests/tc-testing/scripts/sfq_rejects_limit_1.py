#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Script that checks that SFQ rejects a limit of 1 at the kernel
# level. We can't use iproute2's tc because it does not accept a limit
# of 1.

import sys
import os

from pyroute2 import IPRoute
from pyroute2.netlink.exceptions import NetlinkError

ip = IPRoute()
ifidx = ip.link_lookup(ifname=sys.argv[1])

try:
    ip.tc('add', 'sfq', ifidx, limit=1)
    sys.exit(1)
except NetlinkError:
    sys.exit(0)
