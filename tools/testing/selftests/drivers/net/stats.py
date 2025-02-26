#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import errno
import subprocess
import time
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_ge, ksft_eq, ksft_is, ksft_in, ksft_lt, ksft_true, ksft_raises
from lib.py import KsftSkipEx, KsftXfailEx
from lib.py import ksft_disruptive
from lib.py import EthtoolFamily, NetdevFamily, RtnlFamily, NlError
from lib.py import NetDrvEnv
from lib.py import cmd, ip, defer

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

    # Sanity check the dumps
    queues = NetdevFamily(recv_size=4096).qstats_get({"scope": "queue"}, dump=True)
    # Reformat the output into {ifindex: {rx: [id, id, ...], tx: [id, id, ...]}}
    parsed = {}
    for entry in queues:
        ifindex = entry["ifindex"]
        if ifindex not in parsed:
            parsed[ifindex] = {"rx":[], "tx": []}
        parsed[ifindex][entry["queue-type"]].append(entry['queue-id'])
    # Now, validate
    for ifindex, queues in parsed.items():
        for qtype in ['rx', 'tx']:
            ksft_eq(len(queues[qtype]), len(set(queues[qtype])),
                    comment="repeated queue keys")
            ksft_eq(len(queues[qtype]), max(queues[qtype]) + 1,
                    comment="missing queue keys")

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


def __run_inf_loop(body):
    body = body.strip()
    if body[-1] != ';':
        body += ';'

    return subprocess.Popen(f"while true; do {body} done", shell=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def __stats_increase_sanely(old, new) -> None:
    for k in old.keys():
        ksft_ge(new[k], old[k])
        ksft_lt(new[k] - old[k], 1 << 31, comment="likely wrapping error")


def procfs_hammer(cfg) -> None:
    """
    Reading stats via procfs only holds the RCU lock, which is not an exclusive
    lock, make sure drivers can handle parallel reads of stats.
    """
    one = __run_inf_loop("cat /proc/net/dev")
    defer(one.kill)
    two = __run_inf_loop("cat /proc/net/dev")
    defer(two.kill)

    time.sleep(1)
    # Make sure the processes are running
    ksft_is(one.poll(), None)
    ksft_is(two.poll(), None)

    rtstat1 = rtnl.getlink({"ifi-index": cfg.ifindex})['stats64']
    time.sleep(2)
    rtstat2 = rtnl.getlink({"ifi-index": cfg.ifindex})['stats64']
    __stats_increase_sanely(rtstat1, rtstat2)
    # defers will kill the loops


@ksft_disruptive
def procfs_downup_hammer(cfg) -> None:
    """
    Reading stats via procfs only holds the RCU lock, drivers often try
    to sleep when reading the stats, or don't protect against races.
    """
    # Max out the queues, we'll flip between max and 1
    channels = ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    if channels['combined-count'] == 0:
        rx_type = 'rx'
    else:
        rx_type = 'combined'
    cur_queue_cnt = channels[f'{rx_type}-count']
    max_queue_cnt = channels[f'{rx_type}-max']

    cmd(f"ethtool -L {cfg.ifname} {rx_type} {max_queue_cnt}")
    defer(cmd, f"ethtool -L {cfg.ifname} {rx_type} {cur_queue_cnt}")

    # Real test stats
    stats = __run_inf_loop("cat /proc/net/dev")
    defer(stats.kill)

    ipset = f"ip link set dev {cfg.ifname}"
    defer(ip, f"link set dev {cfg.ifname} up")
    # The "echo -n 1" lets us count iterations below
    updown = f"{ipset} down; sleep 0.05; {ipset} up; sleep 0.05; " + \
             f"ethtool -L {cfg.ifname} {rx_type} 1; " + \
             f"ethtool -L {cfg.ifname} {rx_type} {max_queue_cnt}; " + \
              "echo -n 1"
    updown = __run_inf_loop(updown)
    kill_updown = defer(updown.kill)

    time.sleep(1)
    # Make sure the processes are running
    ksft_is(stats.poll(), None)
    ksft_is(updown.poll(), None)

    rtstat1 = rtnl.getlink({"ifi-index": cfg.ifindex})['stats64']
    # We're looking for crashes, give it extra time
    time.sleep(9)
    rtstat2 = rtnl.getlink({"ifi-index": cfg.ifindex})['stats64']
    __stats_increase_sanely(rtstat1, rtstat2)

    kill_updown.exec()
    stdout, _ = updown.communicate(timeout=5)
    ksft_pr("completed up/down cycles:", len(stdout.decode('utf-8')))


def main() -> None:
    with NetDrvEnv(__file__, queue_count=100) as cfg:
        ksft_run([check_pause, check_fec, pkt_byte_sum, qstat_by_ifindex,
                  check_down, procfs_hammer, procfs_downup_hammer],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
