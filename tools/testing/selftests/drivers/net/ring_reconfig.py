#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Test channel and ring size configuration via ethtool (-L / -G).
"""

import socket
import struct
import time

from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_eq
from lib.py import KsftSkipEx, KsftXfailEx
from lib.py import NetDrvEpEnv, EthtoolFamily, GenerateTraffic
from lib.py import cmd, defer, rand_port, tc, NlError

# Added in Python 3.13; fallback to 61 for x86/ARM/MIPS
SO_TXTIME = getattr(socket, "SO_TXTIME", 61)

# Not always exported by the socket module; asm-generic value (x86/ARM/MIPS).
SO_SNDBUFFORCE = getattr(socket, "SO_SNDBUFFORCE", 32)

# TX ring size the test shrinks to so the ring fills quickly.
MIN_TX_RING = 32
MAX_TX_RING = 1024


def channels(cfg) -> None:
    """
    Twiddle channel counts in various combinations of parameters.
    We're only looking for driver adhering to the requested config
    if the config is accepted and crashes.
    """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    chans = cfg.eth.channels_get(ehdr)

    all_keys = ["rx", "tx", "combined"]
    mixes = [{"combined"}, {"rx", "tx"}, {"rx", "combined"}, {"tx", "combined"},
             {"rx", "tx", "combined"},]

    # Get the set of keys that device actually supports
    restore = {}
    supported = set()
    for key in all_keys:
        if key + "-max" in chans:
            supported.add(key)
            restore |= {key + "-count": chans[key + "-count"]}

    defer(cfg.eth.channels_set, ehdr | restore)

    def test_config(config):
        try:
            cfg.eth.channels_set(ehdr | config)
            get = cfg.eth.channels_get(ehdr)
            for k, v in config.items():
                ksft_eq(get.get(k, 0), v)
        except NlError as e:
            failed.append(mix)
            ksft_pr("Can't set", config, e)
        else:
            ksft_pr("Okay", config)

    failed = []
    for mix in mixes:
        if not mix.issubset(supported):
            continue

        # Set all the values in the mix to 1, other supported to 0
        config = {}
        for key in all_keys:
            config[key + "-count"] = 1 if key in mix else 0
        test_config(config)

    for mix in mixes:
        if not mix.issubset(supported):
            continue
        if mix in failed:
            continue

        # Set all the values in the mix to max, other supported to 0
        config = {}
        for key in all_keys:
            config[key + "-count"] = chans[key + '-max'] if key in mix else 0
        test_config(config)


def _configure_min_ring_cnt(cfg) -> None:
    """ Try to configure a single Rx/Tx ring. """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    chans = cfg.eth.channels_get(ehdr)

    all_keys = ["rx-count", "tx-count", "combined-count"]
    restore = {}
    config = {}
    for key in all_keys:
        if key in chans:
            restore[key] = chans[key]
            config[key] = 0

    if chans.get('combined-count', 0) > 1:
        config['combined-count'] = 1
    elif chans.get('rx-count', 0) > 1 and chans.get('tx-count', 0) > 1:
        config['tx-count'] = 1
        config['rx-count'] = 1
    else:
        # looks like we're already on 1 channel
        return

    cfg.eth.channels_set(ehdr | config)
    defer(cfg.eth.channels_set, ehdr | restore)


def ringparam(cfg) -> None:
    """
    Tweak the ringparam configuration. Try to run some traffic over min
    ring size to make sure it actually functions.
    """
    ehdr = {'header':{'dev-index': cfg.ifindex}}
    rings = cfg.eth.rings_get(ehdr)

    restore = {}
    maxes = {}
    params = set()
    for key in rings.keys():
        if 'max' in key:
            param = key[:-4]
            maxes[param] = rings[key]
            params.add(param)
            restore[param] = rings[param]

    defer(cfg.eth.rings_set, ehdr | restore)

    # Speed up the reconfig by configuring just one ring
    _configure_min_ring_cnt(cfg)

    # Try to reach min on all settings
    for param in params:
        val = rings[param]
        while True:
            try:
                cfg.eth.rings_set({'header':{'dev-index': cfg.ifindex},
                                   param: val // 2})
                if val == 0:
                    break
                val //= 2
            except NlError:
                break

        get = cfg.eth.rings_get(ehdr)
        ksft_eq(get[param], val)

        ksft_pr(f"Reached min for '{param}' at {val} (max {rings[param]})")

    GenerateTraffic(cfg).wait_pkts_and_stop(10000)

    # Try max across all params, if the driver supports large rings
    # this may OOM so we ignore errors
    try:
        ksft_pr("Applying max settings")
        config = {p: maxes[p] for p in params}
        cfg.eth.rings_set(ehdr | config)
    except NlError as e:
        ksft_pr("Can't set max params", config, e)
    else:
        GenerateTraffic(cfg).wait_pkts_and_stop(10000)


def _write_file(path, val):
    """Write val to a file."""
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(str(val))


def _write_sysfs(path, val):
    """Write val to a sysfs file, restoring the original value on exit."""
    with open(path, "r", encoding="utf-8") as fp:
        orig_val = fp.read().strip()
    if str(val) == orig_val:
        return
    _write_file(path, val)
    defer(_write_file, path, orig_val)


def _get_qdisc_backlog(cfg, mq_handle, queue):
    """Return the qdisc backlog (bytes) for the given TX queue's leaf."""
    target_parent = f"{mq_handle}{queue + 1:x}"
    for q in tc(f"-s qdisc show dev {cfg.ifname}", json=True):
        if q.get("parent", "") == target_parent:
            return q.get("backlog") or 0
    return 0


def _setup_fq_qdisc(cfg, port, target_queue, other_queue, flow_limit):
    """Put an fq qdisc on target_queue's leaf and return the mq handle in use.

    We must not disturb the device's existing TX/RX qdisc policy. On a real
    NIC the root mq already has an addressable handle, so we leave the root
    and every other queue alone and only swap this one leaf, restoring its
    original qdisc afterwards.

    @flow_limit raises fq's per-flow packet limit (default 100) so a single
    flow can back up more packets than the Tx ring holds and thus overflow it.
    """
    qdiscs = tc(f"qdisc show dev {cfg.ifname}", json=True)
    root = next((q for q in qdiscs if q.get("root")), None)

    if root and root["kind"] == "mq" and root["handle"] != "0:":
        # Addressable mq (previously-configured): touch only the target queue's
        # leaf and restore its original qdisc afterwards.
        mq_handle = root["handle"]
        parent = f"{mq_handle}{target_queue + 1:x}"
        orig = next((q for q in qdiscs if q.get("parent") == parent), None)
        orig_kind = orig["kind"] if orig else \
            cmd("sysctl -n net.core.default_qdisc").stdout.strip()
        defer(tc, f"qdisc replace dev {cfg.ifname} parent {parent} {orig_kind}")
    elif root is None or root["kind"] in ("mq", "noqueue"):
        # The auto-attached root mq has handle 0: on any device (real or sim),
        # which the kernel rejects as a qdisc parent. A 0: handle means the mq
        # is the untouched kernel default - no custom child qdiscs can hang off
        # an unaddressable parent - so installing a real handle and restoring
        # the default mq on exit preserves the device's effective policy.
        mq_handle = "1:"
        tc(f"qdisc replace dev {cfg.ifname} root handle {mq_handle} mq")
        defer(tc, f"qdisc replace dev {cfg.ifname} root mq")
        parent = f"{mq_handle}{target_queue + 1:x}"
    else:
        raise KsftSkipEx(f"root qdisc '{root['kind']}' is not mq; "
                         "refusing to disturb existing qdisc policy")

    try:
        tc(f"qdisc replace dev {cfg.ifname} parent {parent} fq "
           f"flow_limit {flow_limit} limit {flow_limit * 2}")
    except Exception as exc:
        raise KsftSkipEx(
            f"fq not available (CONFIG_NET_SCH_FQ): {exc}") from exc

    qdisc_j = tc(f"qdisc show dev {cfg.ifname}", json=True)
    has_clsact = any(q['kind'] == 'clsact' for q in qdisc_j)
    if not has_clsact:
        tc(f"qdisc add dev {cfg.ifname} clsact")
        defer(tc, f"qdisc del dev {cfg.ifname} clsact")

    proto = "ipv6" if int(cfg.addr_ipver) == 6 else "ip"
    try:
        tc(f"filter add dev {cfg.ifname} egress protocol {proto} "
           f"pref 1 flower ip_proto udp dst_port {port} "
           f"action skbedit queue_mapping {target_queue}")
    except Exception as exc:
        raise KsftSkipEx("tc flower/act_skbedit not available") from exc
    defer(tc, f"filter del dev {cfg.ifname} egress pref 1")

    tc(f"filter add dev {cfg.ifname} egress pref 101 "
       f"matchall action skbedit queue_mapping {other_queue}")
    defer(tc, f"filter del dev {cfg.ifname} egress pref 101")

    return mq_handle


def _create_sotxtime_socket(cfg, sndbuf):
    """Create a UDP socket with SO_TXTIME enabled, bound to the test device."""
    sock = socket.socket(socket.AF_INET6 if cfg.addr_ipver == "6"
                         else socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, SO_TXTIME, struct.pack("Ii", 1, 0))
    except OSError as exc:
        sock.close()
        raise KsftSkipEx("SO_TXTIME not supported") from exc
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE,
                    cfg.ifname.encode())
    # Deferred completions keep every in-flight skb charged to the socket, so
    # size the send buffer to hold the whole burst. SO_SNDBUFFORCE bypasses
    # net.core.wmem_max (the test runs as root).
    try:
        sock.setsockopt(socket.SOL_SOCKET, SO_SNDBUFFORCE, sndbuf)
    except OSError:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, sndbuf)
    return sock


def _send_sotxtime_burst(cfg, sock, port, count, delay_ns, pkt_size):
    """Send count UDP packets scheduled delay_ns ahead using SO_TXTIME."""
    payload = b'\x00' * pkt_size
    txtime_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC) + delay_ns

    ancdata = [(socket.SOL_SOCKET, SO_TXTIME, struct.pack("Q", txtime_ns))]
    if int(cfg.addr_ipver) == 6:
        dest = (cfg.remote_addr, port, 0, 0)
    else:
        dest = (cfg.remote_addr, port)
    for _ in range(count):
        sock.sendmsg([payload], ancdata, 0, dest)


def _set_small_tx_ring(cfg, ehdr):
    """Set the Tx ring to the smallest size the driver accepts.

    Start at 32 so the ring fills quickly, then grow exponentially (64,
    128, 256, ...) up to 1024. Some drivers enforce a minimum well above 32
    (e.g. bnxt needs a large ring for software UDP segmentation), so raise
    the lower bound until the driver accepts it, giving up past 1024.
    """
    size = MIN_TX_RING
    while size <= MAX_TX_RING:
        try:
            cfg.eth.rings_set(ehdr | {'tx': size})
            return size
        except NlError:
            size = size * 2
            continue
    raise KsftSkipEx("driver rejects all tx ring sizes up to 1024")


def reconfig_tx_stall(cfg) -> None:
    """Test that qdisc backlog drains after ring reconfiguration."""
    target_queue = 1
    other_queue = 0

    ehdr = {'header': {'dev-index': cfg.ifindex}}
    chans = cfg.eth.channels_get(ehdr)

    if "combined-max" not in chans:
        raise KsftSkipEx("device does not support combined channels")
    if chans.get("combined-max", 0) < 2:
        raise KsftSkipEx("device does not support 2+ combined channels")
    if chans["combined-count"] < 2:
        defer(cfg.eth.channels_set,
              ehdr | {"combined-count": chans["combined-count"]})
        cfg.eth.channels_set(ehdr | {"combined-count": 2})

    rings = cfg.eth.rings_get(ehdr)
    if 'rx' not in rings or 'tx' not in rings:
        raise KsftSkipEx("device does not expose rx/tx ring params")
    tx_cur = rings['tx']
    if tx_cur <= MIN_TX_RING:
        raise KsftSkipEx("tx ring size already at minimum")
    defer(cfg.eth.rings_set, ehdr | {'tx': tx_cur})

    # Use the smallest Tx ring the driver accepts (32, growing to 1024).
    tx_ring = _set_small_tx_ring(cfg, ehdr)

    # Slow completions so the ring stays full after FQ releases packets
    napi_defer = f"/sys/class/net/{cfg.ifname}/napi_defer_hard_irqs"
    gro_timeout = f"/sys/class/net/{cfg.ifname}/gro_flush_timeout"
    _write_sysfs(napi_defer, 100)
    _write_sysfs(gro_timeout, 1000000000)

    port = rand_port()
    # A single flow must overflow the ring, so send twice the ring depth and
    # let fq hold that many packets for the flow.
    pkt_count = tx_ring * 2
    mq_handle = _setup_fq_qdisc(cfg, port, target_queue, other_queue,
                               tx_ring * 2)

    # Size each packet to one MTU (less L3/L4 headers to avoid fragmentation).
    pkt_size = cfg.dev['mtu'] - (48 if int(cfg.addr_ipver) == 6 else 28)

    # Each queued skb charges the socket its truesize (~2x the payload), so
    # budget the send buffer for the whole in-flight burst.
    sock = _create_sotxtime_socket(cfg, pkt_count * pkt_size * 2)
    defer(sock.close)

    for delay_ms in [100, 200, 500]:
        _send_sotxtime_burst(cfg, sock, port, pkt_count,
                             delay_ms * 1_000_000, pkt_size)
        ksft_pr(f"Sent {pkt_count} SO_TXTIME packets (+{delay_ms}ms)")
        time.sleep(delay_ms / 1000 + 0.3)

        backlog = _get_qdisc_backlog(cfg, mq_handle, target_queue)
        if backlog:
            break
    else:
        # A device that completes Tx synchronously (e.g. a software/virtual
        # driver like netdevsim) never keeps the ring full long enough for a
        # backlog to form, so the wake-vs-start behavior can't be exercised.
        # Treat that as an expected failure rather than a hard failure.
        raise KsftXfailEx("could not build qdisc backlog")

    ksft_pr(f"Backlog before reconfig: {backlog} bytes")

    # Trigger ring reconfig — driver should call wake, not just start.
    # Grow back to the original size so the driver actually switches channels
    # (setting the current size is a no-op the driver short-circuits).
    cfg.eth.rings_set(ehdr | {'tx': tx_cur})

    # Let completions proceed normally
    _write_sysfs(napi_defer, 0)
    _write_sysfs(gro_timeout, 0)

    # Poll for backlog to drain
    for _ in range(100):
        backlog = _get_qdisc_backlog(cfg, mq_handle, target_queue)
        if not backlog:
            break
        time.sleep(0.1)

    ksft_eq(0, backlog,
            comment=f"qdisc backlog stuck on queue {target_queue} "
                    f"after ring reconfig")


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__, queue_count=2) as cfg:
        cfg.eth = EthtoolFamily()

        ksft_run([channels,
                  ringparam,
                  reconfig_tx_stall],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
