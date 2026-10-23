#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Check the quality of the host RSS key (/proc/sys/net/core/netdev_rss_key)
and that the RSS key a device actually uses spreads flows over all of the
entries of its indirection table.

The Toeplitz hash is linear over GF(2): the hash is the XOR of the 32 bit key
windows selected by the set bits of the input, and hardware indexes the
indirection table with the low order bits of the hash. The windows belonging
to the q lowest bits of a header field therefore form a Toeplitz matrix, and
when that matrix is singular the flows of a burst differing only in those
bits, consecutive ephemeral ports typically, can not reach all of the 2 ** q
entries of the table. A key drawn uniformly at random is singular for a given
field and a given q with probability 1/2.

netdev_rss_key_fill() generates keys that are non singular for every field of
the hash input and every q up to RSS_KEY_QMAX.
"""

import errno
import random

from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_eq, ksft_ge
from lib.py import KsftSkipEx
from lib.py import NetDrvEnv, EthtoolFamily, NlError

KEY_PATH = "/proc/sys/net/core/netdev_rss_key"

# Shortest key able to hash an IPv6 4-tuple.
MIN_KEY_LEN = 40

# Matches NETDEV_RSS_KEY_QMAX, that is up to 256 entries of the table.
RSS_KEY_QMAX = 8

# "define" for the ID of the Toeplitz hash function
ETH_RSS_HASH_TOP = 1

FLOW_TYPES = ("tcp4", "udp4", "tcp6", "udp6")

# Map ethtool netlink rxfh-fields flag names to rss_key_layout() codes.
FIELD_NAMES = {
    "ip-src": "s",
    "ip-dst": "d",
    "l3-proto": "t",
    "l4-b-0-1": "f",
    "l4-b-2-3": "n",
    "ip6-fl": "l",
}


def rss_key_bit(buf, bit):
    """Bit @bit of @buf, counting from the most significant bit of byte 0."""
    return (buf[bit // 8] >> (7 - bit % 8)) & 1


def rss_key_assign_bit(buf, bit, value):
    mask = 0x80 >> (bit % 8)

    if value:
        buf[bit // 8] |= mask
    else:
        buf[bit // 8] &= ~mask


def rss_key_window(key, bit):
    """The 32 key bits starting at @bit, what input bit @bit contributes."""
    value = 0

    for i in range(32):
        value = (value << 1) | rss_key_bit(key, bit + i)

    return value


def rss_key_toeplitz(key, inp, nbits):
    """The Toeplitz hash of the @nbits long input @inp under @key."""
    value = 0

    for i in range(nbits):
        if rss_key_bit(inp, i):
            value ^= rss_key_window(key, i)

    return value


def rss_key_full_rank(key, lsb, q):
    """Do the q low order bits of the field at @lsb reach all 2 ** q entries?

    Gaussian elimination over GF(2) on the q windows involved, reduced to
    their q low order bits, which are the ones indexing the table.
    """
    basis = {}

    for j in range(q):
        vector = rss_key_window(key, lsb - j) & ((1 << q) - 1)

        while vector:
            low = vector & -vector
            if low not in basis:
                basis[low] = vector
                break
            vector ^= basis[low]

        if not vector:
            return False

    return True


def rss_key_layout(fields, ipv6):
    """Describe the hash input built from @fields.

    @fields is the flow hash configuration, "sdfn" for a 4-tuple or "sd" for
    a 2-tuple. Returns the list of (name, position of the least significant
    bit) and the length of the input in bits, or None if the layout involves
    something this does not know how to place.
    """
    addr_bits = 128 if ipv6 else 32
    known = (("s", "saddr", addr_bits),
             ("d", "daddr", addr_bits),
             ("f", "sport", 16),
             ("n", "dport", 16))

    if set(fields) - {flag for flag, _, _ in known}:
        return None, 0

    layout = []
    nbits = 0

    for flag, name, width in known:
        if flag not in fields:
            continue
        nbits += width
        layout.append((name, nbits - 1))

    return layout, nbits


def _read_host_key():
    """Return the host RSS key, skipping if it has not been generated."""
    try:
        with open(KEY_PATH, "r", encoding="ascii") as fp:
            text = fp.read().strip()
    except FileNotFoundError as exc:
        raise KsftSkipEx(f"{KEY_PATH} is not available") from exc

    key = bytes(int(byte, 16) for byte in text.split(":")) if text else b""

    if not any(key):
        raise KsftSkipEx("the host RSS key has not been generated yet, "
                         "no driver has called netdev_rss_key_fill()")

    return key


def _get_rss(cfg):
    """The key, indirection table, and flow-hash config of @cfg's device."""
    try:
        rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    except NlError as exc:
        if exc.error == errno.EOPNOTSUPP:
            raise KsftSkipEx(f"{cfg.ifname} does not support RSS") from exc
        raise

    hkey = rss.get("hkey")
    if not hkey or not any(hkey):
        raise KsftSkipEx(f"{cfg.ifname} does not report an RSS key")

    if rss.get("hfunc") != ETH_RSS_HASH_TOP:
        raise KsftSkipEx(f"{cfg.ifname} does not use the Toeplitz hash")

    if rss.get("input-xfrm"):
        raise KsftSkipEx(f"{cfg.ifname} transforms the hash input")

    indir = rss.get("indir")
    if not indir:
        raise KsftSkipEx(f"{cfg.ifname} does not report an indirection table")

    if len(indir) & (len(indir) - 1):
        raise KsftSkipEx(f"{cfg.ifname} has {len(indir)} indirection table "
                         "entries, which is not a power of two")

    return bytes(hkey), indir, rss.get("flow-hash", {})


def _get_layouts(flow_hash):
    """The hash input layouts in use, mapped to the flow types sharing them."""
    layouts = {}

    for fl_type in FLOW_TYPES:
        nl_fields = flow_hash.get(fl_type)
        if not nl_fields:
            continue

        fields = "".join(FIELD_NAMES.get(name, "?") for name in nl_fields)
        layout, nbits = rss_key_layout(fields, fl_type.endswith("6"))
        if layout is None:
            ksft_pr(f"{fl_type}: not checked, hashes fields we can not place "
                    f"({nl_fields})")
            continue

        layouts.setdefault((tuple(layout), nbits), []).append(fl_type)

    if not layouts:
        raise KsftSkipEx("no flow type with a hash input we can describe")

    return layouts


def test_host_rss_key_length(cfg) -> None:
    key = _read_host_key()

    ksft_pr(f"host RSS key is {len(key)} bytes")
    ksft_ge(len(key), MIN_KEY_LEN, "key too short to hash an IPv6 4-tuple")


def test_host_rss_key_spread(cfg) -> None:
    key = _read_host_key()
    degenerate = []

    for ipv6 in (False, True):
        layout, _ = rss_key_layout("sdfn", ipv6)
        family = "IPv6" if ipv6 else "IPv4"

        for name, lsb in layout:
            if lsb + 32 > len(key) * 8:
                continue

            for q in range(1, RSS_KEY_QMAX + 1):
                if not rss_key_full_rank(key, lsb, q):
                    degenerate.append(f"{family} {name} over {1 << q} queues")

    for bad in degenerate:
        ksft_pr(f"degenerate: {bad}")

    ksft_eq(len(degenerate), 0,
            "the host RSS key does not spread flows over all the queues")


def test_host_rss_key_grid(cfg) -> None:
    """Sweep the whole key, not only the fields of the usual layouts."""
    key = _read_host_key()
    bits = len(key) * 8
    positions = 0
    degenerate = []

    for lsb in range(15, bits - 31, 16):
        positions += 1

        for q in range(1, RSS_KEY_QMAX + 1):
            if not rss_key_full_rank(key, lsb, q):
                degenerate.append(f"field ending at bit {lsb} "
                                  f"over {1 << q} queues")

    ksft_pr(f"checked {positions} positions of the {len(key)} byte key")

    for bad in degenerate[:8]:
        ksft_pr(f"degenerate: {bad}")

    ksft_eq(len(degenerate), 0,
            "the host RSS key does not spread flows over all the queues "
            "at every 16-bit aligned position")


def test_dev_rss_key_rank(cfg) -> None:
    """The key has to be non singular for the size of the table."""
    hkey, indir, flow_hash = _get_rss(cfg)
    q = min((len(indir) - 1).bit_length(), RSS_KEY_QMAX)
    degenerate = []

    if not q:
        raise KsftSkipEx("the indirection table has a single entry")

    for (layout, _), fl_types in _get_layouts(flow_hash).items():
        for name, lsb in layout:
            if lsb + 32 > len(hkey) * 8:
                ksft_pr(f"{name}: not checked, the key is {len(hkey)} bytes")
                continue

            if not rss_key_full_rank(hkey, lsb, q):
                degenerate.append(f"{'/'.join(fl_types)} {name}")

    for bad in degenerate:
        ksft_pr(f"degenerate: {bad}")

    ksft_eq(len(degenerate), 0,
            f"the key of {cfg.ifname} does not spread flows over the "
            f"{1 << q} entries of its indirection table")


def test_dev_rss_key_spread(cfg) -> None:
    """Hash bursts differing in one field only, and place them in the table."""
    hkey, indir, flow_hash = _get_rss(cfg)
    q = (len(indir) - 1).bit_length()
    collisions = []

    if q > RSS_KEY_QMAX:
        raise KsftSkipEx(f"{len(indir)} indirection table entries is more "
                         "than the kernel guarantees")
    if not q:
        raise KsftSkipEx("the indirection table has a single entry")

    for (layout, nbits), fl_types in _get_layouts(flow_hash).items():
        if nbits + 31 > len(hkey) * 8:
            ksft_pr(f"{'/'.join(fl_types)}: not checked, the key is "
                    f"{len(hkey)} bytes, input needs {(nbits + 31 + 7) // 8}")
            continue

        for name, lsb in layout:
            inp = bytearray(random.randbytes(nbits // 8))
            entries = set()
            for value in range(1 << q):
                for bit in range(q):
                    rss_key_assign_bit(inp, lsb - bit, value & (1 << bit))
                hash_ = rss_key_toeplitz(hkey, inp, nbits)
                entries.add(hash_ & (len(indir) - 1))

            if len(entries) != 1 << q:
                collisions.append(f"{'/'.join(fl_types)} {name} reaches "
                                  f"{len(entries)} of the {1 << q} entries")

    for bad in collisions:
        ksft_pr(bad)

    ksft_eq(len(collisions), 0,
            f"flows differing in one field only do not fill the "
            f"indirection table of {cfg.ifname}")


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEnv(__file__, queue_count=4) as cfg:
        cfg.ethnl = EthtoolFamily()
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
