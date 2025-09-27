#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Test suite for PSP capable drivers."""

import errno
import fcntl
import socket
import struct
import termios
import time

from lib.py import defer
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_true, ksft_eq, ksft_ne, ksft_raises
from lib.py import KsftSkipEx
from lib.py import NetDrvEpEnv, PSPFamily, NlError
from lib.py import bkg, rand_port, wait_port_listen


def _get_outq(s):
    one = b'\0' * 4
    outq = fcntl.ioctl(s.fileno(), termios.TIOCOUTQ, one)
    return struct.unpack("I", outq)[0]


def _send_with_ack(cfg, msg):
    cfg.comm_sock.send(msg)
    response = cfg.comm_sock.recv(4)
    if response != b'ack\0':
        raise RuntimeError("Unexpected server response", response)


def _remote_read_len(cfg):
    cfg.comm_sock.send(b'read len\0')
    return int(cfg.comm_sock.recv(1024)[:-1].decode('utf-8'))


def _make_psp_conn(cfg, version=0, ipver=None):
    _send_with_ack(cfg, b'conn psp\0' + struct.pack('BB', version, version))
    remote_addr = cfg.remote_addr_v[ipver] if ipver else cfg.remote_addr
    s = socket.create_connection((remote_addr, cfg.comm_port), )
    return s


def _close_conn(cfg, s):
    _send_with_ack(cfg, b'data close\0')
    s.close()


def _close_psp_conn(cfg, s):
    _close_conn(cfg, s)


def _spi_xchg(s, rx):
    s.send(struct.pack('I', rx['spi']) + rx['key'])
    tx = s.recv(4 + len(rx['key']))
    return {
        'spi': struct.unpack('I', tx[:4])[0],
        'key': tx[4:]
    }


def _send_careful(cfg, s, rounds):
    data = b'0123456789' * 200
    for i in range(rounds):
        n = 0
        for _ in range(10): # allow 10 retries
            try:
                n += s.send(data[n:], socket.MSG_DONTWAIT)
                if n == len(data):
                    break
            except BlockingIOError:
                time.sleep(0.05)
        else:
            rlen = _remote_read_len(cfg)
            outq = _get_outq(s)
            report = f'sent: {i * len(data) + n} remote len: {rlen} outq: {outq}'
            raise RuntimeError(report)

    return len(data) * rounds


def _check_data_rx(cfg, exp_len):
    read_len = -1
    for _ in range(30):
        cfg.comm_sock.send(b'read len\0')
        read_len = int(cfg.comm_sock.recv(1024)[:-1].decode('utf-8'))
        if read_len == exp_len:
            break
        time.sleep(0.01)
    ksft_eq(read_len, exp_len)

#
# Test case boiler plate
#

def _init_psp_dev(cfg):
    if not hasattr(cfg, 'psp_dev_id'):
        # Figure out which local device we are testing against
        for dev in cfg.pspnl.dev_get({}, dump=True):
            if dev['ifindex'] == cfg.ifindex:
                cfg.psp_info = dev
                cfg.psp_dev_id = cfg.psp_info['id']
                break
        else:
            raise KsftSkipEx("No PSP devices found")

    # Enable PSP if necessary
    cap = cfg.psp_info['psp-versions-cap']
    ena = cfg.psp_info['psp-versions-ena']
    if cap != ena:
        cfg.pspnl.dev_set({'id': cfg.psp_dev_id, 'psp-versions-ena': cap})
        defer(cfg.pspnl.dev_set, {'id': cfg.psp_dev_id,
                                  'psp-versions-ena': ena })

#
# Test cases
#

def dev_list_devices(cfg):
    """ Dump all devices """
    _init_psp_dev(cfg)

    devices = cfg.pspnl.dev_get({}, dump=True)

    found = False
    for dev in devices:
        found |= dev['id'] == cfg.psp_dev_id
    ksft_true(found)


def dev_get_device(cfg):
    """ Get the device we intend to use """
    _init_psp_dev(cfg)

    dev = cfg.pspnl.dev_get({'id': cfg.psp_dev_id})
    ksft_eq(dev['id'], cfg.psp_dev_id)


def dev_get_device_bad(cfg):
    """ Test getting device which doesn't exist """
    raised = False
    try:
        cfg.pspnl.dev_get({'id': 1234567})
    except NlError as e:
        ksft_eq(e.nl_msg.error, -errno.ENODEV)
        raised = True
    ksft_true(raised)


def dev_rotate(cfg):
    """ Test key rotation """
    _init_psp_dev(cfg)

    rot = cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
    ksft_eq(rot['id'], cfg.psp_dev_id)
    rot = cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
    ksft_eq(rot['id'], cfg.psp_dev_id)


def dev_rotate_spi(cfg):
    """ Test key rotation and SPI check """
    _init_psp_dev(cfg)

    top_a = top_b = 0
    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        assoc_a = cfg.pspnl.rx_assoc({"version": 0,
                                     "dev-id": cfg.psp_dev_id,
                                     "sock-fd": s.fileno()})
        top_a = assoc_a['rx-key']['spi'] >> 31
        s.close()
    rot = cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        ksft_eq(rot['id'], cfg.psp_dev_id)
        assoc_b = cfg.pspnl.rx_assoc({"version": 0,
                                    "dev-id": cfg.psp_dev_id,
                                    "sock-fd": s.fileno()})
        top_b = assoc_b['rx-key']['spi'] >> 31
        s.close()
    ksft_ne(top_a, top_b)


def _data_basic_send(cfg, version, ipver):
    """ Test basic data send """
    _init_psp_dev(cfg)

    # Version 0 is required by spec, don't let it skip
    if version:
        name = cfg.pspnl.consts["version"].entries_by_val[version].name
        if name not in cfg.psp_info['psp-versions-cap']:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                with ksft_raises(NlError) as cm:
                    cfg.pspnl.rx_assoc({"version": version,
                                        "dev-id": cfg.psp_dev_id,
                                        "sock-fd": s.fileno()})
                ksft_eq(cm.exception.nl_msg.error, -errno.EOPNOTSUPP)
            raise KsftSkipEx("PSP version not supported", name)

    s = _make_psp_conn(cfg, version, ipver)

    rx_assoc = cfg.pspnl.rx_assoc({"version": version,
                                   "dev-id": cfg.psp_dev_id,
                                   "sock-fd": s.fileno()})
    rx = rx_assoc['rx-key']
    tx = _spi_xchg(s, rx)

    cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                        "version": version,
                        "tx-key": tx,
                        "sock-fd": s.fileno()})

    data_len = _send_careful(cfg, s, 100)
    _check_data_rx(cfg, data_len)
    _close_psp_conn(cfg, s)


def psp_ip_ver_test_builder(name, test_func, psp_ver, ipver):
    """Build test cases for each combo of PSP version and IP version"""
    def test_case(cfg):
        cfg.require_ipver(ipver)
        test_case.__name__ = f"{name}_v{psp_ver}_ip{ipver}"
        test_func(cfg, psp_ver, ipver)
    return test_case


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__) as cfg:
        cfg.pspnl = PSPFamily()

        # Set up responder and communication sock
        responder = cfg.remote.deploy("psp_responder")

        cfg.comm_port = rand_port()
        srv = None
        try:
            with bkg(responder + f" -p {cfg.comm_port}", host=cfg.remote,
                     exit_wait=True) as srv:
                wait_port_listen(cfg.comm_port, host=cfg.remote)

                cfg.comm_sock = socket.create_connection((cfg.remote_addr,
                                                          cfg.comm_port),
                                                         timeout=1)

                cases = [
                    psp_ip_ver_test_builder(
                        "data_basic_send", _data_basic_send, version, ipver
                    )
                    for version in range(0, 4)
                    for ipver in ("4", "6")
                ]

                ksft_run(cases=cases, globs=globals(), case_pfx={"dev_",}, args=(cfg, ))

                cfg.comm_sock.send(b"exit\0")
                cfg.comm_sock.close()
        finally:
            if srv and (srv.stdout or srv.stderr):
                ksft_pr("")
                ksft_pr(f"Responder logs ({srv.ret}):")
            if srv and srv.stdout:
                ksft_pr("STDOUT:\n#  " + srv.stdout.strip().replace("\n", "\n#  "))
            if srv and srv.stderr:
                ksft_pr("STDERR:\n#  " + srv.stderr.strip().replace("\n", "\n#  "))
    ksft_exit()


if __name__ == "__main__":
    main()
