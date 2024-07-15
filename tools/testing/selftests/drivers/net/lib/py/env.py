# SPDX-License-Identifier: GPL-2.0

import os
from pathlib import Path
from lib.py import KsftSkipEx, KsftXfailEx
from lib.py import cmd, ip
from lib.py import NetNS, NetdevSimDev
from .remote import Remote


def _load_env_file(src_path):
    env = os.environ.copy()

    src_dir = Path(src_path).parent.resolve()
    if not (src_dir / "net.config").exists():
        return env

    with open((src_dir / "net.config").as_posix(), 'r') as fp:
        for line in fp.readlines():
            full_file = line
            # Strip comments
            pos = line.find("#")
            if pos >= 0:
                line = line[:pos]
            line = line.strip()
            if not line:
                continue
            pair = line.split('=', maxsplit=1)
            if len(pair) != 2:
                raise Exception("Can't parse configuration line:", full_file)
            env[pair[0]] = pair[1]
    return env


class NetDrvEnv:
    """
    Class for a single NIC / host env, with no remote end
    """
    def __init__(self, src_path, **kwargs):
        self._ns = None

        self.env = _load_env_file(src_path)

        if 'NETIF' in self.env:
            self.dev = ip("link show dev " + self.env['NETIF'], json=True)[0]
        else:
            self._ns = NetdevSimDev(**kwargs)
            self.dev = self._ns.nsims[0].dev
        self.ifindex = self.dev['ifindex']

    def __enter__(self):
        ip(f"link set dev {self.dev['ifname']} up")

        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        """
        __exit__ gets called at the end of a "with" block.
        """
        self.__del__()

    def __del__(self):
        if self._ns:
            self._ns.remove()
            self._ns = None


class NetDrvEpEnv:
    """
    Class for an environment with a local device and "remote endpoint"
    which can be used to send traffic in.

    For local testing it creates two network namespaces and a pair
    of netdevsim devices.
    """

    # Network prefixes used for local tests
    nsim_v4_pfx = "192.0.2."
    nsim_v6_pfx = "2001:db8::"

    def __init__(self, src_path, nsim_test=None):

        self.env = _load_env_file(src_path)

        # Things we try to destroy
        self.remote = None
        # These are for local testing state
        self._netns = None
        self._ns = None
        self._ns_peer = None

        if "NETIF" in self.env:
            if nsim_test is True:
                raise KsftXfailEx("Test only works on netdevsim")
            self._check_env()

            self.dev = ip("link show dev " + self.env['NETIF'], json=True)[0]

            self.v4 = self.env.get("LOCAL_V4")
            self.v6 = self.env.get("LOCAL_V6")
            self.remote_v4 = self.env.get("REMOTE_V4")
            self.remote_v6 = self.env.get("REMOTE_V6")
            kind = self.env["REMOTE_TYPE"]
            args = self.env["REMOTE_ARGS"]
        else:
            if nsim_test is False:
                raise KsftXfailEx("Test does not work on netdevsim")

            self.create_local()

            self.dev = self._ns.nsims[0].dev

            self.v4 = self.nsim_v4_pfx + "1"
            self.v6 = self.nsim_v6_pfx + "1"
            self.remote_v4 = self.nsim_v4_pfx + "2"
            self.remote_v6 = self.nsim_v6_pfx + "2"
            kind = "netns"
            args = self._netns.name

        self.remote = Remote(kind, args, src_path)

        self.addr = self.v6 if self.v6 else self.v4
        self.remote_addr = self.remote_v6 if self.remote_v6 else self.remote_v4

        self.addr_ipver = "6" if self.v6 else "4"
        # Bracketed addresses, some commands need IPv6 to be inside []
        self.baddr = f"[{self.v6}]" if self.v6 else self.v4
        self.remote_baddr = f"[{self.remote_v6}]" if self.remote_v6 else self.remote_v4

        self.ifname = self.dev['ifname']
        self.ifindex = self.dev['ifindex']

        self._required_cmd = {}

    def create_local(self):
        self._netns = NetNS()
        self._ns = NetdevSimDev()
        self._ns_peer = NetdevSimDev(ns=self._netns)

        with open("/proc/self/ns/net") as nsfd0, \
             open("/var/run/netns/" + self._netns.name) as nsfd1:
            ifi0 = self._ns.nsims[0].ifindex
            ifi1 = self._ns_peer.nsims[0].ifindex
            NetdevSimDev.ctrl_write('link_device',
                                    f'{nsfd0.fileno()}:{ifi0} {nsfd1.fileno()}:{ifi1}')

        ip(f"   addr add dev {self._ns.nsims[0].ifname} {self.nsim_v4_pfx}1/24")
        ip(f"-6 addr add dev {self._ns.nsims[0].ifname} {self.nsim_v6_pfx}1/64 nodad")
        ip(f"   link set dev {self._ns.nsims[0].ifname} up")

        ip(f"   addr add dev {self._ns_peer.nsims[0].ifname} {self.nsim_v4_pfx}2/24", ns=self._netns)
        ip(f"-6 addr add dev {self._ns_peer.nsims[0].ifname} {self.nsim_v6_pfx}2/64 nodad", ns=self._netns)
        ip(f"   link set dev {self._ns_peer.nsims[0].ifname} up", ns=self._netns)

    def _check_env(self):
        vars_needed = [
            ["LOCAL_V4", "LOCAL_V6"],
            ["REMOTE_V4", "REMOTE_V6"],
            ["REMOTE_TYPE"],
            ["REMOTE_ARGS"]
        ]
        missing = []

        for choice in vars_needed:
            for entry in choice:
                if entry in self.env:
                    break
            else:
                missing.append(choice)
        # Make sure v4 / v6 configs are symmetric
        if ("LOCAL_V6" in self.env) != ("REMOTE_V6" in self.env):
            missing.append(["LOCAL_V6", "REMOTE_V6"])
        if ("LOCAL_V4" in self.env) != ("REMOTE_V4" in self.env):
            missing.append(["LOCAL_V4", "REMOTE_V4"])
        if missing:
            raise Exception("Invalid environment, missing configuration:", missing,
                            "Please see tools/testing/selftests/drivers/net/README.rst")

    def __enter__(self):
        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        """
        __exit__ gets called at the end of a "with" block.
        """
        self.__del__()

    def __del__(self):
        if self._ns:
            self._ns.remove()
            self._ns = None
        if self._ns_peer:
            self._ns_peer.remove()
            self._ns_peer = None
        if self._netns:
            del self._netns
            self._netns = None
        if self.remote:
            del self.remote
            self.remote = None

    def require_v4(self):
        if not self.v4 or not self.remote_v4:
            raise KsftSkipEx("Test requires IPv4 connectivity")

    def require_v6(self):
        if not self.v6 or not self.remote_v6:
            raise KsftSkipEx("Test requires IPv6 connectivity")

    def _require_cmd(self, comm, key, host=None):
        cached = self._required_cmd.get(comm, {})
        if cached.get(key) is None:
            cached[key] = cmd("command -v -- " + comm, fail=False,
                              shell=True, host=host).ret == 0
        self._required_cmd[comm] = cached
        return cached[key]

    def require_cmd(self, comm, local=True, remote=False):
        if local:
            if not self._require_cmd(comm, "local"):
                raise KsftSkipEx("Test requires command: " + comm)
        if remote:
            if not self._require_cmd(comm, "remote"):
                raise KsftSkipEx("Test requires (remote) command: " + comm)
