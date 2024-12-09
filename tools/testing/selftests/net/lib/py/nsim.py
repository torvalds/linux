# SPDX-License-Identifier: GPL-2.0

import errno
import json
import os
import random
import re
import time
from .utils import cmd, ip


class NetdevSim:
    """
    Class for netdevsim netdevice and its attributes.
    """

    def __init__(self, nsimdev, port_index, ifname, ns=None):
        # In case udev renamed the netdev to according to new schema,
        # check if the name matches the port_index.
        nsimnamere = re.compile(r"eni\d+np(\d+)")
        match = nsimnamere.match(ifname)
        if match and int(match.groups()[0]) != port_index + 1:
            raise Exception("netdevice name mismatches the expected one")

        self.ifname = ifname
        self.nsimdev = nsimdev
        self.port_index = port_index
        self.ns = ns
        self.dfs_dir = "%s/ports/%u/" % (nsimdev.dfs_dir, port_index)
        ret = ip("-j link show dev %s" % ifname, ns=ns)
        self.dev = json.loads(ret.stdout)[0]
        self.ifindex = self.dev["ifindex"]

    def dfs_write(self, path, val):
        self.nsimdev.dfs_write(f'ports/{self.port_index}/' + path, val)


class NetdevSimDev:
    """
    Class for netdevsim bus device and its attributes.
    """
    @staticmethod
    def ctrl_write(path, val):
        fullpath = os.path.join("/sys/bus/netdevsim/", path)
        with open(fullpath, "w") as f:
            f.write(val)

    def dfs_write(self, path, val):
        fullpath = os.path.join(f"/sys/kernel/debug/netdevsim/netdevsim{self.addr}/", path)
        with open(fullpath, "w") as f:
            f.write(val)

    def __init__(self, port_count=1, queue_count=1, ns=None):
        # nsim will spawn in init_net, we'll set to actual ns once we switch it there
        self.ns = None

        if not os.path.exists("/sys/bus/netdevsim"):
            cmd("modprobe netdevsim")

        addr = random.randrange(1 << 15)
        while True:
            try:
                self.ctrl_write("new_device", "%u %u %u" % (addr, port_count, queue_count))
            except OSError as e:
                if e.errno == errno.ENOSPC:
                    addr = random.randrange(1 << 15)
                    continue
                raise e
            break
        self.addr = addr

        # As probe of netdevsim device might happen from a workqueue,
        # so wait here until all netdevs appear.
        self.wait_for_netdevs(port_count)

        if ns:
            cmd(f"devlink dev reload netdevsim/netdevsim{addr} netns {ns.name}")
            self.ns = ns

        cmd("udevadm settle", ns=self.ns)
        ifnames = self.get_ifnames()

        self.dfs_dir = "/sys/kernel/debug/netdevsim/netdevsim%u/" % addr

        self.nsims = []
        for port_index in range(port_count):
            self.nsims.append(self._make_port(port_index, ifnames[port_index]))

        self.removed = False

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        """
        __exit__ gets called at the end of a "with" block.
        """
        self.remove()

    def _make_port(self, port_index, ifname):
        return NetdevSim(self, port_index, ifname, self.ns)

    def get_ifnames(self):
        ifnames = []
        listdir = cmd(f"ls /sys/bus/netdevsim/devices/netdevsim{self.addr}/net/",
                      ns=self.ns).stdout.split()
        for ifname in listdir:
            ifnames.append(ifname)
        ifnames.sort()
        return ifnames

    def wait_for_netdevs(self, port_count):
        timeout = 5
        timeout_start = time.time()

        while True:
            try:
                ifnames = self.get_ifnames()
            except FileNotFoundError as e:
                ifnames = []
            if len(ifnames) == port_count:
                break
            if time.time() < timeout_start + timeout:
                continue
            raise Exception("netdevices did not appear within timeout")

    def remove(self):
        if not self.removed:
            self.ctrl_write("del_device", "%u" % (self.addr, ))
            self.removed = True

    def remove_nsim(self, nsim):
        self.nsims.remove(nsim)
        self.ctrl_write("devices/netdevsim%u/del_port" % (self.addr, ),
                        "%u" % (nsim.port_index, ))
