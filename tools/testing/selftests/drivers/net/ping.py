#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import os
import random, string, time
from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, KsftSkipEx, KsftFailEx
from lib.py import EthtoolFamily, NetDrvEpEnv
from lib.py import bkg, cmd, wait_port_listen, rand_port
from lib.py import defer, ethtool, ip

remote_ifname=""
no_sleep=False

def _test_v4(cfg) -> None:
    cfg.require_v4()

    cmd(f"ping -c 1 -W0.5 {cfg.remote_v4}")
    cmd(f"ping -c 1 -W0.5 {cfg.v4}", host=cfg.remote)
    cmd(f"ping -s 65000 -c 1 -W0.5 {cfg.remote_v4}")
    cmd(f"ping -s 65000 -c 1 -W0.5 {cfg.v4}", host=cfg.remote)

def _test_v6(cfg) -> None:
    cfg.require_v6()

    cmd(f"ping -c 1 -W5 {cfg.remote_v6}")
    cmd(f"ping -c 1 -W5 {cfg.v6}", host=cfg.remote)
    cmd(f"ping -s 65000 -c 1 -W0.5 {cfg.remote_v6}")
    cmd(f"ping -s 65000 -c 1 -W0.5 {cfg.v6}", host=cfg.remote)

def _test_tcp(cfg) -> None:
    cfg.require_cmd("socat", remote=True)

    port = rand_port()
    listen_cmd = f"socat -{cfg.addr_ipver} -t 2 -u TCP-LISTEN:{port},reuseport STDOUT"

    test_string = ''.join(random.choice(string.ascii_lowercase) for _ in range(65536))
    with bkg(listen_cmd, exit_wait=True) as nc:
        wait_port_listen(port)

        cmd(f"echo {test_string} | socat -t 2 -u STDIN TCP:{cfg.baddr}:{port}",
            shell=True, host=cfg.remote)
    ksft_eq(nc.stdout.strip(), test_string)

    test_string = ''.join(random.choice(string.ascii_lowercase) for _ in range(65536))
    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as nc:
        wait_port_listen(port, host=cfg.remote)

        cmd(f"echo {test_string} | socat -t 2 -u STDIN TCP:{cfg.remote_baddr}:{port}", shell=True)
    ksft_eq(nc.stdout.strip(), test_string)

def _set_offload_checksum(cfg, netnl, on) -> None:
    try:
        ethtool(f" -K {cfg.ifname} rx {on} tx {on} ")
    except:
        return

def _set_xdp_generic_sb_on(cfg) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    prog = test_dir + "/../../net/lib/xdp_dummy.bpf.o"
    cmd(f"ip link set dev {remote_ifname} mtu 1500", shell=True, host=cfg.remote)
    cmd(f"ip link set dev {cfg.ifname} mtu 1500 xdpgeneric obj {prog} sec xdp", shell=True)
    defer(cmd, f"ip link set dev {cfg.ifname} xdpgeneric off")

    if no_sleep != True:
        time.sleep(10)

def _set_xdp_generic_mb_on(cfg) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    prog = test_dir + "/../../net/lib/xdp_dummy.bpf.o"
    cmd(f"ip link set dev {remote_ifname} mtu 9000", shell=True, host=cfg.remote)
    defer(ip, f"link set dev {remote_ifname} mtu 1500", host=cfg.remote)
    ip("link set dev %s mtu 9000 xdpgeneric obj %s sec xdp.frags" % (cfg.ifname, prog))
    defer(ip, f"link set dev {cfg.ifname} mtu 1500 xdpgeneric off")

    if no_sleep != True:
        time.sleep(10)

def _set_xdp_native_sb_on(cfg) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    prog = test_dir + "/../../net/lib/xdp_dummy.bpf.o"
    cmd(f"ip link set dev {remote_ifname} mtu 1500", shell=True, host=cfg.remote)
    cmd(f"ip -j link set dev {cfg.ifname} mtu 1500 xdp obj {prog} sec xdp", shell=True)
    defer(ip, f"link set dev {cfg.ifname} mtu 1500 xdp off")
    xdp_info = ip("-d link show %s" % (cfg.ifname), json=True)[0]
    if xdp_info['xdp']['mode'] != 1:
        """
        If the interface doesn't support native-mode, it falls back to generic mode.
        The mode value 1 is native and 2 is generic.
        So it raises an exception if mode is not 1(native mode).
        """
        raise KsftSkipEx('device does not support native-XDP')

    if no_sleep != True:
        time.sleep(10)

def _set_xdp_native_mb_on(cfg) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    prog = test_dir + "/../../net/lib/xdp_dummy.bpf.o"
    cmd(f"ip link set dev {remote_ifname} mtu 9000", shell=True, host=cfg.remote)
    defer(ip, f"link set dev {remote_ifname} mtu 1500", host=cfg.remote)
    try:
        cmd(f"ip link set dev {cfg.ifname} mtu 9000 xdp obj {prog} sec xdp.frags", shell=True)
        defer(ip, f"link set dev {cfg.ifname} mtu 1500 xdp off")
    except Exception as e:
        raise KsftSkipEx('device does not support native-multi-buffer XDP')

    if no_sleep != True:
        time.sleep(10)

def _set_xdp_offload_on(cfg) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    prog = test_dir + "/../../net/lib/xdp_dummy.bpf.o"
    cmd(f"ip link set dev {cfg.ifname} mtu 1500", shell=True)
    try:
        cmd(f"ip link set dev {cfg.ifname} xdpoffload obj {prog} sec xdp", shell=True)
    except Exception as e:
        raise KsftSkipEx('device does not support offloaded XDP')
    defer(ip, f"link set dev {cfg.ifname} xdpoffload off")
    cmd(f"ip link set dev {remote_ifname} mtu 1500", shell=True, host=cfg.remote)

    if no_sleep != True:
        time.sleep(10)

def get_interface_info(cfg) -> None:
    global remote_ifname
    global no_sleep

    remote_info = cmd(f"ip -4 -o addr show to {cfg.remote_v4} | awk '{{print $2}}'", shell=True, host=cfg.remote).stdout
    remote_ifname = remote_info.rstrip('\n')
    if remote_ifname == "":
        raise KsftFailEx('Can not get remote interface')
    local_info = ip("-d link show %s" % (cfg.ifname), json=True)[0]
    if 'parentbus' in local_info and local_info['parentbus'] == "netdevsim":
        no_sleep=True
    if 'linkinfo' in local_info and local_info['linkinfo']['info_kind'] == "veth":
        no_sleep=True

def set_interface_init(cfg) -> None:
    cmd(f"ip link set dev {cfg.ifname} mtu 1500", shell=True)
    cmd(f"ip link set dev {cfg.ifname} xdp off ", shell=True)
    cmd(f"ip link set dev {cfg.ifname} xdpgeneric off ", shell=True)
    cmd(f"ip link set dev {cfg.ifname} xdpoffload off", shell=True)
    cmd(f"ip link set dev {remote_ifname} mtu 1500", shell=True, host=cfg.remote)

def test_default(cfg, netnl) -> None:
    _set_offload_checksum(cfg, netnl, "off")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)
    _set_offload_checksum(cfg, netnl, "on")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def test_xdp_generic_sb(cfg, netnl) -> None:
    _set_xdp_generic_sb_on(cfg)
    _set_offload_checksum(cfg, netnl, "off")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)
    _set_offload_checksum(cfg, netnl, "on")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def test_xdp_generic_mb(cfg, netnl) -> None:
    _set_xdp_generic_mb_on(cfg)
    _set_offload_checksum(cfg, netnl, "off")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)
    _set_offload_checksum(cfg, netnl, "on")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def test_xdp_native_sb(cfg, netnl) -> None:
    _set_xdp_native_sb_on(cfg)
    _set_offload_checksum(cfg, netnl, "off")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)
    _set_offload_checksum(cfg, netnl, "on")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def test_xdp_native_mb(cfg, netnl) -> None:
    _set_xdp_native_mb_on(cfg)
    _set_offload_checksum(cfg, netnl, "off")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)
    _set_offload_checksum(cfg, netnl, "on")
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def test_xdp_offload(cfg, netnl) -> None:
    _set_xdp_offload_on(cfg)
    _test_v4(cfg)
    _test_v6(cfg)
    _test_tcp(cfg)

def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        get_interface_info(cfg)
        set_interface_init(cfg)
        ksft_run([test_default,
                  test_xdp_generic_sb,
                  test_xdp_generic_mb,
                  test_xdp_native_sb,
                  test_xdp_native_mb,
                  test_xdp_offload],
                 args=(cfg, EthtoolFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
