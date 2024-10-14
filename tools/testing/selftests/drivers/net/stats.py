#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import errno
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_ge, ksft_eq, ksft_in, ksft_true, ksft_raises, KsftSkipEx, KsftXfailEx
from lib.py import ksft_disruptive
from lib.py import EthtoolFamily, NetdevFamily, RtnlFamily, NlError
from lib.py import NetDrvEnv
from lib.py import ip, defer

ethnl = EthtoolFamily()
netfam = NetdevFamily()
rtnl = RtnlFamily()


def check_pause(cfg) -> None:
    global ethnl

    try:
        ethnl.pause_get({"header": {"dev-index": cfg.ifindex}})
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftXfailEx("pause not supported by the device")
        raise

    data = ethnl.pause_get({"header": {"dev-index": cfg.ifindex,
                                       "flags": {'stats'}}})
    ksft_true(data['stats'], "driver does not report stats")


def check_fec(cfg) -> None:
    global ethnl

    try:
        ethnl.fec_get({"header": {"dev-index": cfg.ifindex}})
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftXfailEx("FEC not supported by the device")
        raise

    data = ethnl.fec_get({"header": {"dev-index": cfg.ifindex,
                                     "flags": {'stats'}}})
    ksft_true(data['stats'], "driver does not report stats")


def pkt_byte_sum(cfg) -> None:
    global netfam, rtnl

    def get_qstat(test):
        global netfam
        stats = netfam.qstats_get({}, dump=True)
        if stats:
            for qs in stats:
                if qs["ifindex"]== test.ifindex:
                    return qs

    qstat = get_qstat(cfg)
    if qstat is None:
        raise KsftSkipEx("qstats not supported by the device")

    for key in ['tx-packets', 'tx-bytes', 'rx-packets', 'rx-bytes']:
        ksft_in(key, qstat, "Drivers should always report basic keys")

    # Compare stats, rtnl stats and qstats must match,
    # but the interface may be up, so do a series of dumps
    # each time the more "recent" stats must be higher or same.
    def stat_cmp(rstat, qstat):
        for key in ['tx-packets', 'tx-bytes', 'rx-packets', 'rx-bytes']:
            if rstat[key] != qstat[key]:
                return rstat[key] - qstat[key]
        return 0

    for _ in range(10):
        rtstat = rtnl.getlink({"ifi-index": cfg.ifindex})['stats64']
        if stat_cmp(rtstat, qstat) < 0:
            raise Exception("RTNL stats are lower, fetched later")
        qstat = get_qstat(cfg)
        if stat_cmp(rtstat, qstat) > 0:
            raise Exception("Qstats are lower, fetched later")


def qstat_by_ifindex(cfg) -> None:
    global netfam
    global rtnl

    # Construct a map ifindex -> [dump, by-index, dump]
    ifindexes = {}
    stats = netfam.qstats_get({}, dump=True)
    for entry in stats:
        ifindexes[entry['ifindex']] = [entry, None, None]

    for ifindex in ifindexes.keys():
        entry = netfam.qstats_get({"ifindex": ifindex}, dump=True)
        ksft_eq(len(entry), 1)
        ifindexes[entry[0]['ifindex']][1] = entry[0]

    stats = netfam.qstats_get({}, dump=True)
    for entry in stats:
        ifindexes[entry['ifindex']][2] = entry

    if len(ifindexes) == 0:
        raise KsftSkipEx("No ifindex supports qstats")

    # Now make sure the stats match/make sense
    for ifindex, triple in ifindexes.items():
        all_keys = triple[0].keys() | triple[1].keys() | triple[2].keys()

        for key in all_keys:
            ksft_ge(triple[1][key], triple[0][key], comment="bad key: " + key)
            ksft_ge(triple[2][key], triple[1][key], comment="bad key: " + key)

    # Test invalid dumps
    # 0 is invalid
    with ksft_raises(NlError) as cm:
        netfam.qstats_get({"ifindex": 0}, dump=True)
    ksft_eq(cm.exception.nl_msg.error, -34)
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.ifindex')

    # loopback has no stats
    with ksft_raises(NlError) as cm:
        netfam.qstats_get({"ifindex": 1}, dump=True)
    ksft_eq(cm.exception.nl_msg.error, -errno.EOPNOTSUPP)
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.ifindex')

    # Try to get stats for lowest unused ifindex but not 0
    devs = rtnl.getlink({}, dump=True)
    all_ifindexes = set([dev["ifi-index"] for dev in devs])
    lowest = 2
    while lowest in all_ifindexes:
        lowest += 1

    with ksft_raises(NlError) as cm:
        netfam.qstats_get({"ifindex": lowest}, dump=True)
    ksft_eq(cm.exception.nl_msg.error, -19)
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.ifindex')


@ksft_disruptive
def check_down(cfg) -> None:
    try:
        qstat = netfam.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("qstats not supported by the device")
        raise

    ip(f"link set dev {cfg.dev['ifname']} down")
    defer(ip, f"link set dev {cfg.dev['ifname']} up")

    qstat2 = netfam.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]
    for k, v in qstat.items():
        ksft_ge(qstat2[k], qstat[k], comment=f"{k} went backwards on device down")

    # exercise per-queue API to make sure that "device down" state
    # is handled correctly and doesn't crash
    netfam.qstats_get({"ifindex": cfg.ifindex, "scope": "queue"}, dump=True)


def main() -> None:
    with NetDrvEnv(__file__) as cfg:
        ksft_run([check_pause, check_fec, pkt_byte_sum, qstat_by_ifindex,
                  check_down],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
