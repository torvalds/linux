#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Tests for the gtp netlink family."""

import errno

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_in, ksft_not_in, ksft_raises
from lib.py import KsftSkipEx
from lib.py import NetNS, NetNSEnter
from lib.py import GtpFamily, NlError
from lib.py import CmdExitFailure
from lib.py import ip, defer

_NO_GTP_ERRORS = ("Unknown device type", "Operation not supported",
                  "Garbage instead of arguments")


def _setup(ns):
    """Return a gtp family handle, and the ifindex of a new gtp device.

    Create the device first: that loads gtp.ko through its rtnl link
    alias. ynl resolves the family ids once per process, with a
    CTRL_CMD_GETFAMILY dump which does not autoload modules, so the
    family must be registered before the first family handle is created.
    """
    try:
        ip("link add gtp0 type gtp role ggsn", ns=str(ns))
    except CmdExitFailure as e:
        # gtp.ko not available, or iproute2 without gtp support: any other
        # failure to create the device is a real error.
        if not any(msg in e.cmd.stderr for msg in _NO_GTP_ERRORS):
            raise
        raise KsftSkipEx("no gtp support (CONFIG_GTP, iproute2)") from e
    defer(ip, "link del gtp0", ns=str(ns))
    ip("link set gtp0 up", ns=str(ns))
    link = ip("-d link show gtp0", ns=str(ns), json=True)[0]["ifindex"]

    with NetNSEnter(str(ns)):
        gtp = GtpFamily()
    defer(gtp.close)

    return gtp, link


def _contexts(gtp, link):
    return [c for c in gtp.getpdp({}, dump=True) if c.get("link") == link]


def _iteis(contexts):
    return [c.get("i-tei") for c in contexts]


def add_del_pdp(ns) -> None:
    """Add a PDP context, query it, then delete it by TEI.

    The dump must return the context with the link, TEIDs and addresses
    it was created with, and it must be gone after the delete. The delete
    only identifies the context by its TEI, to use the GTPv1 TEI lookup.
    """
    gtp, link = _setup(ns)

    gtp.newpdp({"link": link, "version": 1,
                "i-tei": 0x100, "o-tei": 0x200,
                "ms-address": "10.0.0.1", "peer-address": "192.0.2.1"})

    pdp = [c for c in _contexts(gtp, link) if c.get("i-tei") == 0x100]
    ksft_eq(len(pdp), 1)
    ksft_eq(pdp[0]["o-tei"], 0x200)
    ksft_eq(pdp[0]["ms-address"], "10.0.0.1")
    ksft_eq(pdp[0]["peer-address"], "192.0.2.1")

    gtp.delpdp({"link": link, "version": 1, "i-tei": 0x100})
    ksft_not_in(0x100, _iteis(_contexts(gtp, link)))


def add_two_pdp(ns) -> None:
    """Add two PDP contexts, check both are dumped, delete one by address.

    The delete identifies the context by its MS address, and must leave
    the other context in place.
    """
    gtp, link = _setup(ns)

    for i_tei, o_tei, ms in ((0x111, 0x211, "10.0.1.1"),
                             (0x112, 0x212, "10.0.1.2")):
        gtp.newpdp({"link": link, "version": 1,
                    "i-tei": i_tei, "o-tei": o_tei,
                    "ms-address": ms, "peer-address": "192.0.2.5"})

    iteis = _iteis(_contexts(gtp, link))
    ksft_in(0x111, iteis)
    ksft_in(0x112, iteis)

    gtp.delpdp({"link": link, "version": 1, "ms-address": "10.0.1.1"})
    iteis = _iteis(_contexts(gtp, link))
    ksft_not_in(0x111, iteis)
    ksft_in(0x112, iteis)


def del_unknown_pdp(ns) -> None:
    """Deleting a context that was never added must fail with ENOENT.

    Both lookups are checked: by TEI, and by MS address.
    """
    gtp, link = _setup(ns)

    for attrs in ({"i-tei": 0x999}, {"ms-address": "10.9.9.9"}):
        with ksft_raises(NlError) as cm:
            gtp.delpdp({"link": link, "version": 1, **attrs})
        ksft_eq(cm.exception.error, errno.ENOENT)


def main() -> None:
    """Run the PDP context tests in a temporary netns."""
    with NetNS() as ns:
        ksft_run([add_del_pdp, add_two_pdp, del_unknown_pdp], args=(ns,))
    ksft_exit()


if __name__ == "__main__":
    main()
