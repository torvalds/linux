#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_disruptive, ksft_exit, ksft_run
from lib.py import ksft_eq, ksft_raises, KsftSkipEx, KsftFailEx
from lib.py import EthtoolFamily, NetdevFamily, NlError
from lib.py import NetDrvEnv
from lib.py import cmd, defer, ip
import errno
import glob
import os
import socket
import struct
import subprocess

def sys_get_queues(ifname, qtype='rx') -> int:
    folders = glob.glob(f'/sys/class/net/{ifname}/queues/{qtype}-*')
    return len(folders)


def nl_get_queues(cfg, nl, qtype='rx'):
    queues = nl.queue_get({'ifindex': cfg.ifindex}, dump=True)
    if queues:
        return len([q for q in queues if q['type'] == qtype])
    return None

def check_xdp(cfg, nl, xdp_queue_id=0) -> None:
    test_dir = os.path.dirname(os.path.realpath(__file__))
    xdp = subprocess.Popen([f"{test_dir}/xdp_helper", f"{cfg.ifindex}", f"{xdp_queue_id}"],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=1,
                           text=True)
    defer(xdp.kill)

    stdout, stderr = xdp.communicate(timeout=10)
    rx = tx = False

    if xdp.returncode == 255:
        raise KsftSkipEx('AF_XDP unsupported')
    elif xdp.returncode > 0:
        raise KsftFailEx('unable to create AF_XDP socket')

    queues = nl.queue_get({'ifindex': cfg.ifindex}, dump=True)
    if not queues:
        raise KsftSkipEx("Netlink reports no queues")

    for q in queues:
        if q['id'] == 0:
            if q['type'] == 'rx':
                rx = True
            if q['type'] == 'tx':
                tx = True

            ksft_eq(q['xsk'], {})
        else:
            if 'xsk' in q:
                _fail("Check failed: xsk attribute set.")

    ksft_eq(rx, True)
    ksft_eq(tx, True)

def get_queues(cfg, nl) -> None:
    snl = NetdevFamily(recv_size=4096)

    for qtype in ['rx', 'tx']:
        queues = nl_get_queues(cfg, snl, qtype)
        if not queues:
            raise KsftSkipEx('queue-get not supported by device')

        expected = sys_get_queues(cfg.dev['ifname'], qtype)
        ksft_eq(queues, expected)


def addremove_queues(cfg, nl) -> None:
    queues = nl_get_queues(cfg, nl)
    if not queues:
        raise KsftSkipEx('queue-get not supported by device')

    curr_queues = sys_get_queues(cfg.dev['ifname'])
    if curr_queues == 1:
        raise KsftSkipEx('cannot decrement queue: already at 1')

    netnl = EthtoolFamily()
    channels = netnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    if channels['combined-count'] == 0:
        rx_type = 'rx'
    else:
        rx_type = 'combined'

    expected = curr_queues - 1
    cmd(f"ethtool -L {cfg.dev['ifname']} {rx_type} {expected}", timeout=10)
    queues = nl_get_queues(cfg, nl)
    ksft_eq(queues, expected)

    expected = curr_queues
    cmd(f"ethtool -L {cfg.dev['ifname']} {rx_type} {expected}", timeout=10)
    queues = nl_get_queues(cfg, nl)
    ksft_eq(queues, expected)


@ksft_disruptive
def check_down(cfg, nl) -> None:
    # Check the NAPI IDs before interface goes down and hides them
    napis = nl.napi_get({'ifindex': cfg.ifindex}, dump=True)

    ip(f"link set dev {cfg.dev['ifname']} down")
    defer(ip, f"link set dev {cfg.dev['ifname']} up")

    with ksft_raises(NlError) as cm:
        nl.queue_get({'ifindex': cfg.ifindex, 'id': 0, 'type': 'rx'})
    ksft_eq(cm.exception.nl_msg.error, -errno.ENOENT)

    if napis:
        with ksft_raises(NlError) as cm:
            nl.napi_get({'id': napis[0]['id']})
        ksft_eq(cm.exception.nl_msg.error, -errno.ENOENT)


def main() -> None:
    with NetDrvEnv(__file__, queue_count=100) as cfg:
        ksft_run([get_queues, addremove_queues, check_down, check_xdp], args=(cfg, NetdevFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
