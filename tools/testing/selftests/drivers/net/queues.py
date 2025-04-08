#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_disruptive, ksft_exit, ksft_run
from lib.py import ksft_eq, ksft_not_in, ksft_raises, KsftSkipEx, KsftFailEx
from lib.py import EthtoolFamily, NetdevFamily, NlError
from lib.py import NetDrvEnv
from lib.py import bkg, cmd, defer, ip
import errno
import glob
import os
import socket
import struct

def sys_get_queues(ifname, qtype='rx') -> int:
    folders = glob.glob(f'/sys/class/net/{ifname}/queues/{qtype}-*')
    return len(folders)


def nl_get_queues(cfg, nl, qtype='rx'):
    queues = nl.queue_get({'ifindex': cfg.ifindex}, dump=True)
    if queues:
        return len([q for q in queues if q['type'] == qtype])
    return None


def check_xsk(cfg, nl, xdp_queue_id=0) -> None:
    # Probe for support
    xdp = cmd(f'{cfg.test_dir / "xdp_helper"} - -', fail=False)
    if xdp.ret == 255:
        raise KsftSkipEx('AF_XDP unsupported')
    elif xdp.ret > 0:
        raise KsftFailEx('unable to create AF_XDP socket')

    with bkg(f'{cfg.test_dir / "xdp_helper"} {cfg.ifindex} {xdp_queue_id}',
             ksft_wait=3):

        rx = tx = False

        queues = nl.queue_get({'ifindex': cfg.ifindex}, dump=True)
        if not queues:
            raise KsftSkipEx("Netlink reports no queues")

        for q in queues:
            if q['id'] == 0:
                if q['type'] == 'rx':
                    rx = True
                if q['type'] == 'tx':
                    tx = True

                ksft_eq(q.get('xsk', None), {},
                        comment="xsk attr on queue we configured")
            else:
                ksft_not_in('xsk', q,
                            comment="xsk attr on queue we didn't configure")

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
    rx_type = 'rx'
    if channels.get('combined-count', 0) > 0:
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
        ksft_run([get_queues, addremove_queues, check_down, check_xsk],
                 args=(cfg, NetdevFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
