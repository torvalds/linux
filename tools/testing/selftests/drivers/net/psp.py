#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Test suite for PSP capable drivers."""

import errno
import fcntl
import os
import socket
import struct
import termios
import time

from lib.py import defer
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_true, ksft_eq, ksft_ne, ksft_gt, ksft_raises
from lib.py import ksft_not_none
from lib.py import ksft_variants, KsftNamedVariant
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import NetDrvEpEnv, NetDrvContEnv
from lib.py import Netlink, NlError, PSPFamily, RtnlFamily
from lib.py import NetNSEnter
from lib.py import bkg, rand_port, wait_port_listen
from lib.py import ip


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


def _make_clr_conn(cfg, ipver=None):
    _send_with_ack(cfg, b'conn clr\0')
    remote_addr = cfg.remote_addr_v[ipver] if ipver else cfg.remote_addr
    s = socket.create_connection((remote_addr, cfg.comm_port), )
    return s


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


def _check_data_outq(s, exp_len, force_wait=False):
    outq = 0
    for _ in range(10):
        outq = _get_outq(s)
        if not force_wait and outq == exp_len:
            break
        time.sleep(0.01)
    ksft_eq(outq, exp_len)


def _get_stat(cfg, key):
    return cfg.pspnl.get_stats({'dev-id': cfg.psp_dev_id})[key]

#
# Test case boiler plate
#

def _init_psp_dev(cfg, use_psp_ifindex=False):
    if not hasattr(cfg, 'psp_dev_id'):
        # Figure out which local device we are testing against
        # For NetDrvContEnv: use psp_ifindex instead of ifindex
        target_ifindex = cfg.psp_ifindex if use_psp_ifindex else cfg.ifindex
        for dev in cfg.pspnl.dev_get({}, dump=True):
            if dev['ifindex'] == target_ifindex:
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

    prev_rotations = _get_stat(cfg, 'key-rotations')

    rot = cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
    ksft_eq(rot['id'], cfg.psp_dev_id)
    rot = cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
    ksft_eq(rot['id'], cfg.psp_dev_id)

    cur_rotations = _get_stat(cfg, 'key-rotations')
    ksft_eq(cur_rotations, prev_rotations + 2)


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


def assoc_basic(cfg):
    """ Test creating associations """
    _init_psp_dev(cfg)

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        assoc = cfg.pspnl.rx_assoc({"version": 0,
                                  "dev-id": cfg.psp_dev_id,
                                  "sock-fd": s.fileno()})
        ksft_eq(assoc['dev-id'], cfg.psp_dev_id)
        ksft_gt(assoc['rx-key']['spi'], 0)
        ksft_eq(len(assoc['rx-key']['key']), 16)

        assoc = cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                                  "version": 0,
                                  "tx-key": assoc['rx-key'],
                                  "sock-fd": s.fileno()})
        ksft_eq(len(assoc), 0)
        s.close()


def assoc_bad_dev(cfg):
    """ Test creating associations with bad device ID """
    _init_psp_dev(cfg)

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        with ksft_raises(NlError) as cm:
            cfg.pspnl.rx_assoc({"version": 0,
                              "dev-id": cfg.psp_dev_id + 1234567,
                              "sock-fd": s.fileno()})
        ksft_eq(cm.exception.nl_msg.error, -errno.ENODEV)


def assoc_sk_only_conn(cfg):
    """ Test creating associations based on socket """
    _init_psp_dev(cfg)

    with _make_clr_conn(cfg) as s:
        assoc = cfg.pspnl.rx_assoc({"version": 0,
                                  "sock-fd": s.fileno()})
        ksft_eq(assoc['dev-id'], cfg.psp_dev_id)
        cfg.pspnl.tx_assoc({"version": 0,
                          "tx-key": assoc['rx-key'],
                          "sock-fd": s.fileno()})
        _close_conn(cfg, s)


def assoc_sk_only_mismatch(cfg):
    """ Test creating associations based on socket (dev mismatch) """
    _init_psp_dev(cfg)

    with _make_clr_conn(cfg) as s:
        with ksft_raises(NlError) as cm:
            cfg.pspnl.rx_assoc({"version": 0,
                              "dev-id": cfg.psp_dev_id + 1234567,
                              "sock-fd": s.fileno()})
        the_exception = cm.exception
        ksft_eq(the_exception.nl_msg.extack['bad-attr'], ".dev-id")
        ksft_eq(the_exception.nl_msg.error, -errno.EINVAL)
        _close_conn(cfg, s)


def assoc_sk_only_mismatch_tx(cfg):
    """ Test creating associations based on socket (dev mismatch) """
    _init_psp_dev(cfg)

    with _make_clr_conn(cfg) as s:
        with ksft_raises(NlError) as cm:
            assoc = cfg.pspnl.rx_assoc({"version": 0,
                                      "sock-fd": s.fileno()})
            cfg.pspnl.tx_assoc({"version": 0,
                              "tx-key": assoc['rx-key'],
                              "dev-id": cfg.psp_dev_id + 1234567,
                              "sock-fd": s.fileno()})
        the_exception = cm.exception
        ksft_eq(the_exception.nl_msg.extack['bad-attr'], ".dev-id")
        ksft_eq(the_exception.nl_msg.error, -errno.EINVAL)
        _close_conn(cfg, s)


def assoc_sk_only_unconn(cfg):
    """ Test creating associations based on socket (unconnected, should fail) """
    _init_psp_dev(cfg)

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        with ksft_raises(NlError) as cm:
            cfg.pspnl.rx_assoc({"version": 0,
                              "sock-fd": s.fileno()})
        the_exception = cm.exception
        ksft_eq(the_exception.nl_msg.extack['miss-type'], "dev-id")
        ksft_eq(the_exception.nl_msg.error, -errno.EINVAL)


def assoc_version_mismatch(cfg):
    """ Test creating associations where Rx and Tx PSP versions do not match """
    _init_psp_dev(cfg)

    versions = list(cfg.psp_info['psp-versions-cap'])
    if len(versions) < 2:
        raise KsftSkipEx("Not enough PSP versions supported by the device for the test")

    # Translate versions to integers
    versions = [cfg.pspnl.consts["version"].entries[v].value for v in versions]

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        rx = cfg.pspnl.rx_assoc({"version": versions[0],
                                 "dev-id": cfg.psp_dev_id,
                                 "sock-fd": s.fileno()})

        for version in versions[1:]:
            with ksft_raises(NlError) as cm:
                cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                                    "version": version,
                                    "tx-key": rx['rx-key'],
                                    "sock-fd": s.fileno()})
            the_exception = cm.exception
            ksft_eq(the_exception.nl_msg.error, -errno.EINVAL)


def assoc_twice(cfg):
    """ Test reusing Tx assoc for two sockets """
    _init_psp_dev(cfg)

    def rx_assoc_check(s):
        assoc = cfg.pspnl.rx_assoc({"version": 0,
                                  "dev-id": cfg.psp_dev_id,
                                  "sock-fd": s.fileno()})
        ksft_eq(assoc['dev-id'], cfg.psp_dev_id)
        ksft_gt(assoc['rx-key']['spi'], 0)
        ksft_eq(len(assoc['rx-key']['key']), 16)

        return assoc

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
        assoc = rx_assoc_check(s)
        tx = cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                               "version": 0,
                               "tx-key": assoc['rx-key'],
                               "sock-fd": s.fileno()})
        ksft_eq(len(tx), 0)

        # Use the same Tx assoc second time
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s2:
            rx_assoc_check(s2)
            tx = cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                                   "version": 0,
                                   "tx-key": assoc['rx-key'],
                                   "sock-fd": s2.fileno()})
            ksft_eq(len(tx), 0)

        s.close()


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


def __bad_xfer_do(cfg, s, tx, version='hdr0-aes-gcm-128'):
    # Make sure we accept the ACK for the SPI before we seal with the bad assoc
    _check_data_outq(s, 0)

    cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                        "version": version,
                        "tx-key": tx,
                        "sock-fd": s.fileno()})

    data_len = _send_careful(cfg, s, 20)
    _check_data_outq(s, data_len, force_wait=True)
    _check_data_rx(cfg, 0)
    _close_psp_conn(cfg, s)


def data_send_bad_key(cfg):
    """ Test send data with bad key """
    _init_psp_dev(cfg)

    s = _make_psp_conn(cfg)

    rx_assoc = cfg.pspnl.rx_assoc({"version": 0,
                                   "dev-id": cfg.psp_dev_id,
                                   "sock-fd": s.fileno()})
    rx = rx_assoc['rx-key']
    tx = _spi_xchg(s, rx)
    tx['key'] = (tx['key'][0] ^ 0xff).to_bytes(1, 'little') + tx['key'][1:]
    __bad_xfer_do(cfg, s, tx)


def data_send_disconnect(cfg):
    """ Test socket close after sending data """
    _init_psp_dev(cfg)

    with _make_psp_conn(cfg) as s:
        assoc = cfg.pspnl.rx_assoc({"version": 0,
                                  "sock-fd": s.fileno()})
        tx = _spi_xchg(s, assoc['rx-key'])
        cfg.pspnl.tx_assoc({"version": 0,
                          "tx-key": tx,
                          "sock-fd": s.fileno()})

        data_len = _send_careful(cfg, s, 100)
        _check_data_rx(cfg, data_len)

        s.shutdown(socket.SHUT_RDWR)
        s.close()


def _data_mss_adjust(cfg, ipver):
    _init_psp_dev(cfg)

    # First figure out what the MSS would be without any adjustments
    s = _make_clr_conn(cfg, ipver)
    s.send(b"0123456789abcdef" * 1024)
    _check_data_rx(cfg, 16 * 1024)
    mss = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
    _close_conn(cfg, s)

    s = _make_psp_conn(cfg, 0, ipver)
    try:
        rx_assoc = cfg.pspnl.rx_assoc({"version": 0,
                                     "dev-id": cfg.psp_dev_id,
                                     "sock-fd": s.fileno()})
        rx = rx_assoc['rx-key']
        tx = _spi_xchg(s, rx)

        rxmss = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
        ksft_eq(mss, rxmss)

        cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                          "version": 0,
                          "tx-key": tx,
                          "sock-fd": s.fileno()})

        txmss = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
        ksft_eq(mss, txmss + 40)

        data_len = _send_careful(cfg, s, 100)
        _check_data_rx(cfg, data_len)
        _check_data_outq(s, 0)

        txmss = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
        ksft_eq(mss, txmss + 40)
    finally:
        _close_psp_conn(cfg, s)


def data_stale_key(cfg):
    """ Test send on a double-rotated key """
    _init_psp_dev(cfg)

    prev_stale = _get_stat(cfg, 'stale-events')
    s = _make_psp_conn(cfg)
    try:
        rx_assoc = cfg.pspnl.rx_assoc({"version": 0,
                                     "dev-id": cfg.psp_dev_id,
                                     "sock-fd": s.fileno()})
        rx = rx_assoc['rx-key']
        tx = _spi_xchg(s, rx)

        cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                          "version": 0,
                          "tx-key": tx,
                          "sock-fd": s.fileno()})

        data_len = _send_careful(cfg, s, 100)
        _check_data_rx(cfg, data_len)
        _check_data_outq(s, 0)

        cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})
        cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})

        cur_stale = _get_stat(cfg, 'stale-events')
        ksft_gt(cur_stale, prev_stale)

        s.send(b'0123456789' * 200)
        _check_data_outq(s, 2000, force_wait=True)
    finally:
        _close_psp_conn(cfg, s)


def __nsim_psp_rereg(cfg):
    # The PSP dev ID will change, remember what was there before
    before = set([x['id'] for x in cfg.pspnl.dev_get({}, dump=True)])

    cfg._ns.nsims[0].dfs_write('psp_rereg', '1')

    after = set([x['id'] for x in cfg.pspnl.dev_get({}, dump=True)])

    new_devs = list(after - before)
    ksft_eq(len(new_devs), 1)
    cfg.psp_dev_id = list(after - before)[0]


def removal_device_rx(cfg):
    """ Test removing a netdev / PSD with active Rx assoc """

    # We could technically devlink reload real devices, too
    # but that kills the control socket. So test this on
    # netdevsim only for now
    cfg.require_nsim()

    s = _make_clr_conn(cfg)
    try:
        rx_assoc = cfg.pspnl.rx_assoc({"version": 0,
                                       "dev-id": cfg.psp_dev_id,
                                       "sock-fd": s.fileno()})
        ksft_not_none(rx_assoc)

        __nsim_psp_rereg(cfg)
    finally:
        _close_conn(cfg, s)


def removal_device_bi(cfg):
    """ Test removing a netdev / PSD with active Rx/Tx assoc """

    # We could technically devlink reload real devices, too
    # but that kills the control socket. So test this on
    # netdevsim only for now
    cfg.require_nsim()

    s = _make_clr_conn(cfg)
    try:
        rx_assoc = cfg.pspnl.rx_assoc({"version": 0,
                                       "dev-id": cfg.psp_dev_id,
                                       "sock-fd": s.fileno()})
        cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                            "version": 0,
                            "tx-key": rx_assoc['rx-key'],
                            "sock-fd": s.fileno()})
        __nsim_psp_rereg(cfg)
    finally:
        _close_conn(cfg, s)


def _get_psp_ver_ip_variants():
    for ver in range(4):
        for ipv in ("4", "6"):
            yield KsftNamedVariant(f"v{ver}_ip{ipv}", ver, ipv)


def _get_ip_variants():
    for ipv in ("4", "6"):
        yield KsftNamedVariant(f"ip{ipv}", ipv)


@ksft_variants(_get_psp_ver_ip_variants())
def data_basic_send(cfg, version, ipver):
    """Test basic PSP data send."""
    cfg.require_ipver(ipver)
    _data_basic_send(cfg, version, ipver)


@ksft_variants(_get_ip_variants())
def data_mss_adjust(cfg, ipver):
    """Test MSS adjustment with PSP."""
    cfg.require_ipver(ipver)
    _data_mss_adjust(cfg, ipver)


def _check_assoc_list(cfg, psp_dev_id, ifindex, nsid=None):
    """Verify assoc-list contains device with given ifindex, no duplicates."""
    dev_info = cfg.pspnl.dev_get({'id': psp_dev_id})

    ksft_true('assoc-list' in dev_info,
              "No assoc-list in dev_get() response after association")
    found = False
    for assoc in dev_info['assoc-list']:
        if assoc['ifindex'] != ifindex:
            continue
        if nsid is not None and assoc['nsid'] != nsid:
            continue
        ksft_eq(found, False, "Duplicate assoc entry found")
        found = True
    ksft_eq(found, True,
            "Associated device not found in dev_get() response")


def _data_basic_send_netkit_psp_assoc(cfg, version, ipver):
    """
    Test basic data send with netkit interface associated with PSP dev.
    """
    _assoc_nk_guest(cfg)

    # Enter guest namespace (netns) to run PSP test
    with NetNSEnter(cfg.netns.name):
        cfg.pspnl = PSPFamily()

        sock = _make_psp_conn(cfg, version, ipver)

        rx_assoc = cfg.pspnl.rx_assoc({"version": version,
                                       "dev-id": cfg.psp_dev_id,
                                       "sock-fd": sock.fileno()})
        rx_key = rx_assoc['rx-key']
        tx_key = _spi_xchg(sock, rx_key)

        cfg.pspnl.tx_assoc({"dev-id": cfg.psp_dev_id,
                            "version": version,
                            "tx-key": tx_key,
                            "sock-fd": sock.fileno()})

        data_len = _send_careful(cfg, sock, 100)
        _check_data_rx(cfg, data_len)
        _close_psp_conn(cfg, sock)


def _assoc_check_list(cfg):
    """Test that assoc-list is correctly populated after dev-assoc."""
    _assoc_nk_guest(cfg)
    _check_assoc_list(cfg, cfg.psp_dev_id, cfg.nk_guest_ifindex,
                      cfg.psp_dev_peer_nsid)


def _get_psp_ver_ip6_variants():
    for ver in range(4):
        yield KsftNamedVariant(f"v{ver}_ip6", ver, "6")


@ksft_variants(_get_psp_ver_ip6_variants())
def data_basic_send_netkit_psp_assoc(cfg, version, ipver):
    """Test PSP data send via netkit with dev-assoc."""
    cfg.require_ipver(ipver)
    _data_basic_send_netkit_psp_assoc(cfg, version, ipver)


def _key_rotation_notify_multi_ns_netkit(cfg):
    """ Test key rotation notifications across multiple namespaces using netkit """
    _assoc_nk_guest(cfg)

    # Create listener in guest namespace; socket stays bound to that ns
    with NetNSEnter(cfg.netns.name):
        peer_pspnl = PSPFamily()
        peer_pspnl.ntf_subscribe('use')

    # Create listener in main namespace
    main_pspnl = PSPFamily()
    main_pspnl.ntf_subscribe('use')

    # Trigger key rotation on the PSP device
    cfg.pspnl.key_rotate({"id": cfg.psp_dev_id})

    # Poll both sockets from main thread
    for pspnl, label in [(main_pspnl, "main"), (peer_pspnl, "guest")]:
        for ntf in pspnl.poll_ntf(duration=10):
            if ntf['msg'].get('id') == cfg.psp_dev_id:
                break
        else:
            raise KsftFailEx(
                f"No key rotation notification received"
                f" in {label} namespace")


def _dev_change_notify_multi_ns_netkit(cfg):
    """ Test dev_change notifications across multiple namespaces using netkit """
    _assoc_nk_guest(cfg)

    # Create listener in guest namespace; socket stays bound to that ns
    with NetNSEnter(cfg.netns.name):
        peer_pspnl = PSPFamily()
        peer_pspnl.ntf_subscribe('mgmt')

    # Create listener in main namespace
    main_pspnl = PSPFamily()
    main_pspnl.ntf_subscribe('mgmt')

    # Trigger dev_change by calling dev_set (notification is always sent)
    cfg.pspnl.dev_set({'id': cfg.psp_dev_id,
                       'psp-versions-ena': cfg.psp_info['psp-versions-cap']})

    # Poll both sockets from main thread
    for pspnl, label in [(main_pspnl, "main"), (peer_pspnl, "guest")]:
        for ntf in pspnl.poll_ntf(duration=10):
            if ntf['msg'].get('id') == cfg.psp_dev_id:
                break
        else:
            raise KsftFailEx(
                f"No dev_change notification received"
                f" in {label} namespace")


def _psp_dev_get_check_netkit_psp_assoc(cfg):
    """ Check psp dev-get output with netkit interface associated with PSP dev """
    _assoc_nk_guest(cfg)

    # Check 1: In default netns, verify dev-get has correct ifindex and assoc-list
    dev_info = cfg.pspnl.dev_get({'id': cfg.psp_dev_id})
    ksft_eq(dev_info['ifindex'], cfg.psp_ifindex)
    _check_assoc_list(cfg, cfg.psp_dev_id, cfg.nk_guest_ifindex,
                      cfg.psp_dev_peer_nsid)

    # Check 2: In guest netns, verify dev-get has assoc-list with nk_guest device
    with NetNSEnter(cfg.netns.name):
        peer_pspnl = PSPFamily()

        # Dump all devices in the guest namespace
        peer_devices = peer_pspnl.dev_get({}, dump=True)

        # Find the device with by-association flag
        peer_dev = None
        for dev in peer_devices:
            if dev.get('by-association'):
                peer_dev = dev
                break

        ksft_not_none(peer_dev, "No PSP device found with by-association flag in guest netns")

        # Verify assoc-list contains the nk_guest device
        ksft_true('assoc-list' in peer_dev and len(peer_dev['assoc-list']) > 0,
                  "Guest device should have assoc-list with local devices")

        # Verify the assoc-list contains nk_guest ifindex with nsid=-1 (same namespace)
        found = False
        for assoc in peer_dev['assoc-list']:
            if assoc['ifindex'] == cfg.nk_guest_ifindex:
                ksft_eq(assoc['nsid'], -1,
                        "nsid should be -1 (NETNSA_NSID_NOT_ASSIGNED) for same-namespace device")
                found = True
                break
        ksft_true(found, "nk_guest ifindex not found in assoc-list")


def _dev_assoc_no_nsid(cfg):
    """ Test dev-assoc and dev-disassoc without nsid attribute """
    _init_psp_dev(cfg, True)

    # Associate without nsid - should look up ifindex in caller's netns
    cfg.pspnl.dev_assoc({'id': cfg.psp_dev_id,
                         'ifindex': cfg.nk_host_ifindex})
    defer(_try_disassoc, cfg,
          cfg.psp_dev_id, cfg.nk_host_ifindex)
    defer(delattr, cfg, 'psp_dev_id')
    defer(delattr, cfg, 'psp_info')

    # Verify assoc-list contains the device (match by ifindex only)
    _check_assoc_list(cfg, cfg.psp_dev_id, cfg.nk_host_ifindex)

    # Disassociate without nsid - should also use caller's netns
    cfg.pspnl.dev_disassoc({'id': cfg.psp_dev_id,
                            'ifindex': cfg.nk_host_ifindex})

    # Verify assoc-list no longer contains the device
    dev_info = cfg.pspnl.dev_get({'id': cfg.psp_dev_id})
    found = False
    if 'assoc-list' in dev_info:
        for assoc in dev_info['assoc-list']:
            if assoc['ifindex'] == cfg.nk_host_ifindex:
                found = True
                break
    ksft_true(not found, "Device should not be in assoc-list after disassociation")


def _psp_dev_assoc_cleanup_on_netkit_del(cfg):
    """Test that assoc-list is cleared when associated netkit is deleted.

    Creates a disposable netkit pair for this test to avoid destroying
    the shared environment.
    """
    _init_psp_dev(cfg, True)
    defer(delattr, cfg, 'psp_dev_id')
    defer(delattr, cfg, 'psp_info')

    existing = {cfg.nk_host_ifindex, cfg.nk_guest_ifindex}

    # Create a temporary netkit pair
    tmp_host_name = "tmp_nk_host"
    tmp_guest_name = "tmp_nk_guest"
    rtnl = RtnlFamily()
    rtnl.newlink(
        {
            "ifname": tmp_host_name,
            "linkinfo": {
                "kind": "netkit",
                "data": {
                    "mode": "l2",
                    "policy": "forward",
                    "peer-policy": "forward",
                },
            },
        },
        flags=[Netlink.NLM_F_CREATE, Netlink.NLM_F_EXCL],
    )
    cleanup_netkit = defer(ip, f"link del {tmp_host_name}")

    # Find the peer by diffing against existing netkit ifindexes
    all_links = ip("-d link show", json=True)
    tmp_peer = [link for link in all_links
                if link.get('linkinfo', {}).get('info_kind') == 'netkit'
                and link['ifindex'] not in existing
                and link['ifname'] != tmp_host_name]
    ksft_eq(len(tmp_peer), 1,
            "Failed to find temporary netkit peer")
    guest_name = tmp_peer[0]['ifname']

    # Rename and move guest end into the test namespace
    ip(f"link set dev {guest_name} name {tmp_guest_name}")
    ip(f"link set dev {tmp_guest_name} netns {cfg.netns.name}")
    tmp_guest_dev = ip(f"link show dev {tmp_guest_name}",
                       json=True, ns=cfg.netns)[0]
    tmp_guest_ifindex = tmp_guest_dev['ifindex']
    ip(f"link set dev {tmp_guest_name} up", ns=cfg.netns)

    # Associate PSP device with the temporary guest interface
    cfg.pspnl.dev_assoc({'id': cfg.psp_dev_id,
                         'ifindex': tmp_guest_ifindex,
                         'nsid': cfg.psp_dev_peer_nsid})

    # Verify assoc-list contains the temporary device
    _check_assoc_list(cfg, cfg.psp_dev_id, tmp_guest_ifindex,
                      cfg.psp_dev_peer_nsid)

    # Delete the temporary netkit pair (deleting one end removes both)
    ip(f"link del {tmp_host_name}")
    cleanup_netkit.cancel()

    # Verify assoc-list is cleared after netkit deletion
    dev_info = cfg.pspnl.dev_get({'id': cfg.psp_dev_id})
    ksft_true('assoc-list' not in dev_info
              or len(dev_info['assoc-list']) == 0,
              "assoc-list should be empty after netkit deletion")


def _try_disassoc(cfg, psp_dev_id, ifindex, nsid=None):
    """Best-effort disassociate, ignoring errors if already removed."""
    try:
        params = {'id': psp_dev_id, 'ifindex': ifindex}
        if nsid is not None:
            params['nsid'] = nsid
        cfg.pspnl.dev_disassoc(params)
    except NlError:
        pass


def _assoc_nk_guest(cfg):
    """Associate nk_guest with PSP device and register cleanup via defer()."""
    _init_psp_dev(cfg, True)

    cfg.pspnl.dev_assoc({'id': cfg.psp_dev_id,
                         'ifindex': cfg.nk_guest_ifindex,
                         'nsid': cfg.psp_dev_peer_nsid})
    defer(_disassoc_nk_guest, cfg,
          cfg.psp_dev_id, cfg.nk_guest_ifindex)


def _disassoc_nk_guest(cfg, psp_dev_id, nk_guest_ifindex):
    """Disassociate nk_guest and reset cfg PSP state."""
    pspnl = PSPFamily()
    pspnl.dev_disassoc({'id': psp_dev_id, 'ifindex': nk_guest_ifindex,
                        'nsid': cfg.psp_dev_peer_nsid})
    cfg.pspnl = pspnl
    del cfg.psp_dev_id
    del cfg.psp_info


def _get_nsid(ns_name):
    """Get the nsid for a namespace."""
    for entry in ip("netns list-id", json=True):
        if entry.get("name") == str(ns_name):
            return entry["nsid"]
    raise KsftSkipEx(f"nsid not found for namespace {ns_name}")


def _setup_psp_attributes(cfg):
    # pylint: disable=protected-access
    """
    Set up PSP-specific attributes on the environment.

    This sets attributes needed for PSP tests based on whether we're using
    netdevsim or a real NIC.
    """
    if cfg._ns is not None:
        # netdevsim case: PSP device is the local dev (in host namespace)
        cfg.psp_dev = cfg._ns.nsims[0].dev
        cfg.psp_ifname = cfg.psp_dev['ifname']
        cfg.psp_ifindex = cfg.psp_dev['ifindex']

        # PSP peer device is the remote dev (in _netns, where psp_responder runs)
        cfg.psp_dev_peer = cfg._ns_peer.nsims[0].dev
        cfg.psp_dev_peer_ifname = cfg.psp_dev_peer['ifname']
        cfg.psp_dev_peer_ifindex = cfg.psp_dev_peer['ifindex']
    else:
        # Real NIC case: PSP device is the local interface
        cfg.psp_dev = cfg.dev
        cfg.psp_ifname = cfg.ifname
        cfg.psp_ifindex = cfg.ifindex

        # PSP peer device is the remote interface
        cfg.psp_dev_peer = cfg.remote_dev
        cfg.psp_dev_peer_ifname = cfg.remote_ifname
        cfg.psp_dev_peer_ifindex = cfg.remote_ifindex

    # Get nsid for the guest namespace (netns) where nk_guest is
    cfg.psp_dev_peer_nsid = _get_nsid(cfg.netns.name)



def main() -> None:
    """ Ksft boiler plate main """

    # Make sure LOCAL_PREFIX_V6 is set
    if "LOCAL_PREFIX_V6" not in os.environ:
        os.environ["LOCAL_PREFIX_V6"] = "2001:db8:2::"

    try:
        env = NetDrvContEnv(__file__, primary_rx_redirect=True)
        has_cont = True
    except KsftSkipEx:
        env = NetDrvEpEnv(__file__)
        has_cont = False

    with env as cfg:
        cfg.pspnl = PSPFamily()

        if has_cont:
            _setup_psp_attributes(cfg)

        # Set up responder and communication sock
        # psp_responder runs in _netns (remote namespace with psp_dev_peer)
        responder = cfg.remote.deploy("psp_responder")

        cfg.comm_port = rand_port()
        srv = None
        try:
            with bkg(responder + f" -p {cfg.comm_port} -i {cfg.remote_ifindex}",
                     host=cfg.remote, exit_wait=True) as srv:
                wait_port_listen(cfg.comm_port, host=cfg.remote)

                cfg.comm_sock = socket.create_connection((cfg.remote_addr,
                                                          cfg.comm_port),
                                                         timeout=1)

                cases = [data_basic_send, data_mss_adjust]

                if has_cont:
                    cases += [
                        _assoc_check_list,
                        data_basic_send_netkit_psp_assoc,
                        _key_rotation_notify_multi_ns_netkit,
                        _dev_change_notify_multi_ns_netkit,
                        _psp_dev_get_check_netkit_psp_assoc,
                        _dev_assoc_no_nsid,
                        _psp_dev_assoc_cleanup_on_netkit_del,
                    ]

                ksft_run(cases=cases, globs=globals(),
                         case_pfx={"dev_", "data_", "assoc_", "removal_"},
                         args=(cfg, ))

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
