# SPDX-License-Identifier: GPL-2.0

import ipaddress
import os
import time
import json
from pathlib import Path
from lib.py import KsftSkipEx, KsftXfailEx
from lib.py import ksft_setup, wait_file
from lib.py import cmd, ethtool, ip, CmdExitFailure
from lib.py import NetNS, NetdevSimDev
from .remote import Remote
from . import bpftool, RtnlFamily, Netlink


class NetDrvEnvBase:
    """
    Base class for a NIC / host environments

    Attributes:
      test_dir: Path to the source directory of the test
      net_lib_dir: Path to the net/lib directory
    """
    def __init__(self, src_path):
        self.src_path = Path(src_path)
        self.test_dir = self.src_path.parent.resolve()
        self.net_lib_dir = (Path(__file__).parent / "../../../../net/lib").resolve()

        self.env = self._load_env_file()

        # Following attrs must be set be inheriting classes
        self.dev = None

    def _load_env_file(self):
        env = os.environ.copy()

        src_dir = Path(self.src_path).parent.resolve()
        if not (src_dir / "net.config").exists():
            return ksft_setup(env)

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
        return ksft_setup(env)

    def __del__(self):
        pass

    def __enter__(self):
        ip(f"link set dev {self.dev['ifname']} up")
        wait_file(f"/sys/class/net/{self.dev['ifname']}/carrier",
                  lambda x: x.strip() == "1")

        return self

    def __exit__(self, ex_type, ex_value, ex_tb):
        """
        __exit__ gets called at the end of a "with" block.
        """
        self.__del__()


class NetDrvEnv(NetDrvEnvBase):
    """
    Class for a single NIC / host env, with no remote end
    """
    def __init__(self, src_path, nsim_test=None, **kwargs):
        super().__init__(src_path)

        self._ns = None

        if 'NETIF' in self.env:
            if nsim_test is True:
                raise KsftXfailEx("Test only works on netdevsim")

            self.dev = ip("-d link show dev " + self.env['NETIF'], json=True)[0]
        else:
            if nsim_test is False:
                raise KsftXfailEx("Test does not work on netdevsim")

            self._ns = NetdevSimDev(**kwargs)
            self.dev = self._ns.nsims[0].dev
        self.ifname = self.dev['ifname']
        self.ifindex = self.dev['ifindex']

    def __del__(self):
        if self._ns:
            self._ns.remove()
            self._ns = None


class NetDrvEpEnv(NetDrvEnvBase):
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
        super().__init__(src_path)

        self._stats_settle_time = None

        # Things we try to destroy
        self.remote = None
        # These are for local testing state
        self._netns = None
        self._ns = None
        self._ns_peer = None

        self.addr_v        = { "4": None, "6": None }
        self.remote_addr_v = { "4": None, "6": None }

        if "NETIF" in self.env:
            if nsim_test is True:
                raise KsftXfailEx("Test only works on netdevsim")
            self._check_env()

            self.dev = ip("-d link show dev " + self.env['NETIF'], json=True)[0]

            self.addr_v["4"] = self.env.get("LOCAL_V4")
            self.addr_v["6"] = self.env.get("LOCAL_V6")
            self.remote_addr_v["4"] = self.env.get("REMOTE_V4")
            self.remote_addr_v["6"] = self.env.get("REMOTE_V6")
            kind = self.env["REMOTE_TYPE"]
            args = self.env["REMOTE_ARGS"]
        else:
            if nsim_test is False:
                raise KsftXfailEx("Test does not work on netdevsim")

            self.create_local()

            self.dev = self._ns.nsims[0].dev

            self.addr_v["4"] = self.nsim_v4_pfx + "1"
            self.addr_v["6"] = self.nsim_v6_pfx + "1"
            self.remote_addr_v["4"] = self.nsim_v4_pfx + "2"
            self.remote_addr_v["6"] = self.nsim_v6_pfx + "2"
            kind = "netns"
            args = self._netns.name

        self.remote = Remote(kind, args, src_path)

        self.addr_ipver = "6" if self.addr_v["6"] else "4"
        self.addr = self.addr_v[self.addr_ipver]
        self.remote_addr = self.remote_addr_v[self.addr_ipver]

        # Bracketed addresses, some commands need IPv6 to be inside []
        self.baddr = f"[{self.addr_v['6']}]" if self.addr_v["6"] else self.addr_v["4"]
        self.remote_baddr = f"[{self.remote_addr_v['6']}]" if self.remote_addr_v["6"] else self.remote_addr_v["4"]

        self.ifname = self.dev['ifname']
        self.ifindex = self.dev['ifindex']

        # resolve remote interface name
        self.remote_ifname = self.resolve_remote_ifc()
        self.remote_dev = ip("-d link show dev " + self.remote_ifname,
                             host=self.remote, json=True)[0]
        self.remote_ifindex = self.remote_dev['ifindex']

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

    def resolve_remote_ifc(self):
        v4 = v6 = None
        if self.remote_addr_v["4"]:
            v4 = ip("addr show to " + self.remote_addr_v["4"], json=True, host=self.remote)
        if self.remote_addr_v["6"]:
            v6 = ip("addr show to " + self.remote_addr_v["6"], json=True, host=self.remote)
        if v4 and v6 and v4[0]["ifname"] != v6[0]["ifname"]:
            raise Exception("Can't resolve remote interface name, v4 and v6 don't match")
        if (v4 and len(v4) > 1) or (v6 and len(v6) > 1):
            raise Exception("Can't resolve remote interface name, multiple interfaces match")
        return v6[0]["ifname"] if v6 else v4[0]["ifname"]

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

    def require_ipver(self, ipver):
        if not self.addr_v[ipver] or not self.remote_addr_v[ipver]:
            raise KsftSkipEx(f"Test requires IPv{ipver} connectivity")

    def require_nsim(self, nsim_test=True):
        """Require or exclude netdevsim for this test"""
        if nsim_test and self._ns is None:
            raise KsftXfailEx("Test only works on netdevsim")
        if nsim_test is False and self._ns is not None:
            raise KsftXfailEx("Test does not work on netdevsim")

    def get_local_nsim_dev(self):
        """Returns the local netdevsim device or None.
           Using this method is discouraged, as it makes tests nsim-specific.
           Standard interfaces available on all HW should ideally be used.
           This method is intended for the few cases where nsim-specific
           assertions need to be verified which cannot be verified otherwise.
        """
        return self._ns

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
            if not self._require_cmd(comm, "remote", host=self.remote):
                raise KsftSkipEx("Test requires (remote) command: " + comm)

    def wait_hw_stats_settle(self):
        """
        Wait for HW stats to become consistent, some devices DMA HW stats
        periodically so events won't be reflected until next sync.
        Good drivers will tell us via ethtool what their sync period is.
        """
        if self._stats_settle_time is None:
            data = {}
            try:
                data = ethtool("-c " + self.ifname, json=True)[0]
            except CmdExitFailure as e:
                if "Operation not supported" not in e.cmd.stderr:
                    raise

            self._stats_settle_time = \
                1.25 * data.get('stats-block-usecs', 20000) / 1000 / 1000

        time.sleep(self._stats_settle_time)


class NetDrvContEnv(NetDrvEpEnv):
    """
    Class for an environment with a netkit pair setup for forwarding traffic
    between the physical interface and a network namespace.
      NETIF           = "eth0"
      LOCAL_V6        = "2001:db8:1::1"
      REMOTE_V6       = "2001:db8:1::2"
      LOCAL_PREFIX_V6 = "2001:db8:2::0/64"

              +-----------------------------+        +------------------------------+
      dst     | INIT NS                     |        | TEST NS                      |
      2001:   | +---------------+           |        |                              |
      db8:2::2| | NETIF         |           |  bpf   |                              |
          +---|>| 2001:db8:1::1 |           |redirect| +-------------------------+  |
          |   | |               |-----------|--------|>| Netkit                  |  |
          |   | +---------------+           | _peer  | | nk_guest                |  |
          |   | +-------------+ Netkit pair |        | | fe80::2/64              |  |
          |   | | Netkit      |.............|........|>| 2001:db8:2::2/64        |  |
          |   | | nk_host     |             |        | +-------------------------+  |
          |   | | fe80::1/64  |             |        |                              |
          |   | +-------------+             |        | route:                       |
          |   |                             |        |   default                    |
          |   | route:                      |        |     via fe80::1 dev nk_guest |
          |   |   2001:db8:2::2/128         |        +------------------------------+
          |   |     via fe80::2 dev nk_host |
          |   +-----------------------------+
          |
          |   +---------------+
          |   | REMOTE        |
          +---| 2001:db8:1::2 |
              +---------------+
    """

    def __init__(self, src_path, rxqueues=1, **kwargs):
        self.netns = None
        self._nk_host_ifname = None
        self._nk_guest_ifname = None
        self._tc_clsact_added = False
        self._tc_attached = False
        self._bpf_prog_pref = None
        self._bpf_prog_id = None
        self._init_ns_attached = False
        self._old_fwd = None
        self._old_accept_ra = None

        super().__init__(src_path, **kwargs)

        self.require_ipver("6")
        local_prefix = self.env.get("LOCAL_PREFIX_V6")
        if not local_prefix:
            raise KsftSkipEx("LOCAL_PREFIX_V6 required")

        net = ipaddress.IPv6Network(local_prefix, strict=False)
        self.ipv6_prefix = str(net.network_address)
        self.nk_host_ipv6 = f"{self.ipv6_prefix}2:1"
        self.nk_guest_ipv6 = f"{self.ipv6_prefix}2:2"

        local_v6 = ipaddress.IPv6Address(self.addr_v["6"])
        if local_v6 in net:
            raise KsftSkipEx("LOCAL_V6 must not fall within LOCAL_PREFIX_V6")

        rtnl = RtnlFamily()
        rtnl.newlink(
            {
                "linkinfo": {
                    "kind": "netkit",
                    "data": {
                        "mode": "l2",
                        "policy": "forward",
                        "peer-policy": "forward",
                    },
                },
                "num-rx-queues": rxqueues,
            },
            flags=[Netlink.NLM_F_CREATE, Netlink.NLM_F_EXCL],
        )

        all_links = ip("-d link show", json=True)
        netkit_links = [link for link in all_links
                        if link.get('linkinfo', {}).get('info_kind') == 'netkit'
                        and 'UP' not in link.get('flags', [])]

        if len(netkit_links) != 2:
            raise KsftSkipEx("Failed to create netkit pair")

        netkit_links.sort(key=lambda x: x['ifindex'])
        self._nk_host_ifname = netkit_links[1]['ifname']
        self._nk_guest_ifname = netkit_links[0]['ifname']
        self.nk_host_ifindex = netkit_links[1]['ifindex']
        self.nk_guest_ifindex = netkit_links[0]['ifindex']

        self._setup_ns()
        self._attach_bpf()

    def __del__(self):
        if self._tc_attached:
            cmd(f"tc filter del dev {self.ifname} ingress pref {self._bpf_prog_pref}")
            self._tc_attached = False

        if self._tc_clsact_added:
            cmd(f"tc qdisc del dev {self.ifname} clsact")
            self._tc_clsact_added = False

        if self._nk_host_ifname:
            cmd(f"ip link del dev {self._nk_host_ifname}")
            self._nk_host_ifname = None
            self._nk_guest_ifname = None

        if self._init_ns_attached:
            cmd("ip netns del init", fail=False)
            self._init_ns_attached = False

        if self.netns:
            del self.netns
            self.netns = None

        if self._old_fwd is not None:
            with open("/proc/sys/net/ipv6/conf/all/forwarding", "w",
                      encoding="utf-8") as f:
                f.write(self._old_fwd)
            self._old_fwd = None
        if self._old_accept_ra is not None:
            with open("/proc/sys/net/ipv6/conf/all/accept_ra", "w",
                      encoding="utf-8") as f:
                f.write(self._old_accept_ra)
            self._old_accept_ra = None

        super().__del__()

    def _setup_ns(self):
        fwd_path = "/proc/sys/net/ipv6/conf/all/forwarding"
        ra_path = "/proc/sys/net/ipv6/conf/all/accept_ra"
        with open(fwd_path, encoding="utf-8") as f:
            self._old_fwd = f.read().strip()
        with open(ra_path, encoding="utf-8") as f:
            self._old_accept_ra = f.read().strip()
        with open(fwd_path, "w", encoding="utf-8") as f:
            f.write("1")
        with open(ra_path, "w", encoding="utf-8") as f:
            f.write("2")

        self.netns = NetNS()
        cmd("ip netns attach init 1")
        self._init_ns_attached = True
        ip("netns set init 0", ns=self.netns)
        ip(f"link set dev {self._nk_guest_ifname} netns {self.netns.name}")
        ip(f"link set dev {self._nk_host_ifname} up")
        ip(f"-6 addr add fe80::1/64 dev {self._nk_host_ifname} nodad")
        ip(f"-6 route add {self.nk_guest_ipv6}/128 via fe80::2 dev {self._nk_host_ifname}")

        ip("link set lo up", ns=self.netns)
        ip(f"link set dev {self._nk_guest_ifname} up", ns=self.netns)
        ip(f"-6 addr add fe80::2/64 dev {self._nk_guest_ifname}", ns=self.netns)
        ip(f"-6 addr add {self.nk_guest_ipv6}/64 dev {self._nk_guest_ifname} nodad", ns=self.netns)
        ip(f"-6 route add default via fe80::1 dev {self._nk_guest_ifname}", ns=self.netns)

    def _tc_ensure_clsact(self):
        qdisc = json.loads(cmd(f"tc -j qdisc show dev {self.ifname}").stdout)
        for q in qdisc:
            if q['kind'] == 'clsact':
                return
        cmd(f"tc qdisc add dev {self.ifname} clsact")
        self._tc_clsact_added = True

    def _get_bpf_prog_ids(self):
        filters = json.loads(cmd(f"tc -j filter show dev {self.ifname} ingress").stdout)
        for bpf in filters:
            if 'options' not in bpf:
                continue
            if bpf['options']['bpf_name'].startswith('nk_forward.bpf'):
                return (bpf['pref'], bpf['options']['prog']['id'])
        raise Exception("Failed to get BPF prog ID")

    def _attach_bpf(self):
        bpf_obj = self.test_dir / "nk_forward.bpf.o"
        if not bpf_obj.exists():
            raise KsftSkipEx("BPF prog not found")

        self._tc_ensure_clsact()
        cmd(f"tc filter add dev {self.ifname} ingress bpf obj {bpf_obj}"
            " sec tc/ingress direct-action")
        self._tc_attached = True

        (self._bpf_prog_pref, self._bpf_prog_id) = self._get_bpf_prog_ids()
        prog_info = bpftool(f"prog show id {self._bpf_prog_id}", json=True)
        map_ids = prog_info.get("map_ids", [])

        bss_map_id = None
        for map_id in map_ids:
            map_info = bpftool(f"map show id {map_id}", json=True)
            if map_info.get("name").endswith("bss"):
                bss_map_id = map_id

        if bss_map_id is None:
            raise Exception("Failed to find .bss map")

        ipv6_addr = ipaddress.IPv6Address(self.ipv6_prefix)
        ipv6_bytes = ipv6_addr.packed
        ifindex_bytes = self.nk_host_ifindex.to_bytes(4, byteorder='little')
        value = ipv6_bytes + ifindex_bytes
        value_hex = ' '.join(f'{b:02x}' for b in value)
        bpftool(f"map update id {bss_map_id} key hex 00 00 00 00 value hex {value_hex}")
