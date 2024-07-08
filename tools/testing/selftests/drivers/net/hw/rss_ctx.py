#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import datetime
import random
from lib.py import ksft_run, ksft_pr, ksft_exit, ksft_eq, ksft_ge, ksft_lt
from lib.py import NetDrvEpEnv
from lib.py import NetdevFamily
from lib.py import KsftSkipEx
from lib.py import rand_port
from lib.py import ethtool, ip, defer, GenerateTraffic, CmdExitFailure


def _rss_key_str(key):
    return ":".join(["{:02x}".format(x) for x in key])


def _rss_key_rand(length):
    return [random.randint(0, 255) for _ in range(length)]


def get_rss(cfg, context=0):
    return ethtool(f"-x {cfg.ifname} context {context}", json=True)[0]


def get_drop_err_sum(cfg):
    stats = ip("-s -s link show dev " + cfg.ifname, json=True)[0]
    cnt = 0
    for key in ['errors', 'dropped', 'over_errors', 'fifo_errors',
                'length_errors', 'crc_errors', 'missed_errors',
                'frame_errors']:
        cnt += stats["stats64"]["rx"][key]
    return cnt, stats["stats64"]["tx"]["carrier_changes"]


def ethtool_create(cfg, act, opts):
    output = ethtool(f"{act} {cfg.ifname} {opts}").stdout
    # Output will be something like: "New RSS context is 1" or
    # "Added rule with ID 7", we want the integer from the end
    return int(output.split()[-1])


def require_ntuple(cfg):
    features = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not features["ntuple-filters"]["active"]:
        # ntuple is more of a capability than a config knob, don't bother
        # trying to enable it (until some driver actually needs it).
        raise KsftSkipEx("Ntuple filters not enabled on the device: " + str(features["ntuple-filters"]))


# Get Rx packet counts for all queues, as a simple list of integers
# if @prev is specified the prev counts will be subtracted
def _get_rx_cnts(cfg, prev=None):
    cfg.wait_hw_stats_settle()
    data = cfg.netdevnl.qstats_get({"ifindex": cfg.ifindex, "scope": ["queue"]}, dump=True)
    data = [x for x in data if x['queue-type'] == "rx"]
    max_q = max([x["queue-id"] for x in data])
    queue_stats = [0] * (max_q + 1)
    for q in data:
        queue_stats[q["queue-id"]] = q["rx-packets"]
        if prev and q["queue-id"] < len(prev):
            queue_stats[q["queue-id"]] -= prev[q["queue-id"]]
    return queue_stats


def _send_traffic_check(cfg, port, name, params):
    # params is a dict with 3 possible keys:
    #  - "target": required, which queues we expect to get iperf traffic
    #  - "empty": optional, which queues should see no traffic at all
    #  - "noise": optional, which queues we expect to see low traffic;
    #             used for queues of the main context, since some background
    #             OS activity may use those queues while we're testing
    # the value for each is a list, or some other iterable containing queue ids.

    cnts = _get_rx_cnts(cfg)
    GenerateTraffic(cfg, port=port).wait_pkts_and_stop(20000)
    cnts = _get_rx_cnts(cfg, prev=cnts)

    directed = sum(cnts[i] for i in params['target'])

    ksft_ge(directed, 20000, f"traffic on {name}: " + str(cnts))
    if params.get('noise'):
        ksft_lt(sum(cnts[i] for i in params['noise']), directed / 2,
                "traffic on other queues:" + str(cnts))
    if params.get('empty'):
        ksft_eq(sum(cnts[i] for i in params['empty']), 0,
                "traffic on inactive queues: " + str(cnts))


def test_rss_key_indir(cfg):
    """Test basics like updating the main RSS key and indirection table."""

    if len(_get_rx_cnts(cfg)) < 2:
        KsftSkipEx("Device has only one queue (or doesn't support queue stats)")

    data = get_rss(cfg)
    want_keys = ['rss-hash-key', 'rss-hash-function', 'rss-indirection-table']
    for k in want_keys:
        if k not in data:
            raise KsftFailEx("ethtool results missing key: " + k)
        if not data[k]:
            raise KsftFailEx(f"ethtool results empty for '{k}': {data[k]}")

    key_len = len(data['rss-hash-key'])

    # Set the key
    key = _rss_key_rand(key_len)
    ethtool(f"-X {cfg.ifname} hkey " + _rss_key_str(key))

    data = get_rss(cfg)
    ksft_eq(key, data['rss-hash-key'])

    # Set the indirection table
    ethtool(f"-X {cfg.ifname} equal 2")
    reset_indir = defer(ethtool, f"-X {cfg.ifname} default")
    data = get_rss(cfg)
    ksft_eq(0, min(data['rss-indirection-table']))
    ksft_eq(1, max(data['rss-indirection-table']))

    # Check we only get traffic on the first 2 queues
    cnts = _get_rx_cnts(cfg)
    GenerateTraffic(cfg).wait_pkts_and_stop(20000)
    cnts = _get_rx_cnts(cfg, prev=cnts)
    # 2 queues, 20k packets, must be at least 5k per queue
    ksft_ge(cnts[0], 5000, "traffic on main context (1/2): " + str(cnts))
    ksft_ge(cnts[1], 5000, "traffic on main context (2/2): " + str(cnts))
    # The other queues should be unused
    ksft_eq(sum(cnts[2:]), 0, "traffic on unused queues: " + str(cnts))

    # Restore, and check traffic gets spread again
    reset_indir.exec()

    cnts = _get_rx_cnts(cfg)
    GenerateTraffic(cfg).wait_pkts_and_stop(20000)
    cnts = _get_rx_cnts(cfg, prev=cnts)
    # First two queues get less traffic than all the rest
    ksft_lt(sum(cnts[:2]), sum(cnts[2:]), "traffic distributed: " + str(cnts))


def test_rss_context(cfg, ctx_cnt=1, create_with_cfg=None):
    """
    Test separating traffic into RSS contexts.
    The queues will be allocated 2 for each context:
     ctx0  ctx1  ctx2  ctx3
    [0 1] [2 3] [4 5] [6 7] ...
    """

    require_ntuple(cfg)

    requested_ctx_cnt = ctx_cnt

    # Try to allocate more queues when necessary
    qcnt = len(_get_rx_cnts(cfg))
    if qcnt < 2 + 2 * ctx_cnt:
        try:
            ksft_pr(f"Increasing queue count {qcnt} -> {2 + 2 * ctx_cnt}")
            ethtool(f"-L {cfg.ifname} combined {2 + 2 * ctx_cnt}")
            defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")
        except:
            raise KsftSkipEx("Not enough queues for the test")

    ports = []

    # Use queues 0 and 1 for normal traffic
    ethtool(f"-X {cfg.ifname} equal 2")
    defer(ethtool, f"-X {cfg.ifname} default")

    for i in range(ctx_cnt):
        want_cfg = f"start {2 + i * 2} equal 2"
        create_cfg = want_cfg if create_with_cfg else ""

        try:
            ctx_id = ethtool_create(cfg, "-X", f"context new {create_cfg}")
            defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")
        except CmdExitFailure:
            # try to carry on and skip at the end
            if i == 0:
                raise
            ksft_pr(f"Failed to create context {i + 1}, trying to test what we got")
            ctx_cnt = i
            break

        if not create_with_cfg:
            ethtool(f"-X {cfg.ifname} context {ctx_id} {want_cfg}")

        # Sanity check the context we just created
        data = get_rss(cfg, ctx_id)
        ksft_eq(min(data['rss-indirection-table']), 2 + i * 2, "Unexpected context cfg: " + str(data))
        ksft_eq(max(data['rss-indirection-table']), 2 + i * 2 + 1, "Unexpected context cfg: " + str(data))

        ports.append(rand_port())
        flow = f"flow-type tcp{cfg.addr_ipver} dst-port {ports[i]} context {ctx_id}"
        ntuple = ethtool_create(cfg, "-N", flow)
        defer(ethtool, f"-N {cfg.ifname} delete {ntuple}")

    for i in range(ctx_cnt):
        _send_traffic_check(cfg, ports[i], f"context {i}",
                            { 'target': (2+i*2, 3+i*2),
                              'noise': (0, 1),
                              'empty': list(range(2, 2+i*2)) + list(range(4+i*2, 2+2*ctx_cnt)) })

    if requested_ctx_cnt != ctx_cnt:
        raise KsftSkipEx(f"Tested only {ctx_cnt} contexts, wanted {requested_ctx_cnt}")


def test_rss_context4(cfg):
    test_rss_context(cfg, 4)


def test_rss_context32(cfg):
    test_rss_context(cfg, 32)


def test_rss_context4_create_with_cfg(cfg):
    test_rss_context(cfg, 4, create_with_cfg=True)


def test_rss_context_out_of_order(cfg, ctx_cnt=4):
    """
    Test separating traffic into RSS contexts.
    Contexts are removed in semi-random order, and steering re-tested
    to make sure removal doesn't break steering to surviving contexts.
    Test requires 3 contexts to work.
    """

    require_ntuple(cfg)

    requested_ctx_cnt = ctx_cnt

    # Try to allocate more queues when necessary
    qcnt = len(_get_rx_cnts(cfg))
    if qcnt < 2 + 2 * ctx_cnt:
        try:
            ksft_pr(f"Increasing queue count {qcnt} -> {2 + 2 * ctx_cnt}")
            ethtool(f"-L {cfg.ifname} combined {2 + 2 * ctx_cnt}")
            defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")
        except:
            raise KsftSkipEx("Not enough queues for the test")

    ntuple = []
    ctx = []
    ports = []

    def remove_ctx(idx):
        ntuple[idx].exec()
        ntuple[idx] = None
        ctx[idx].exec()
        ctx[idx] = None

    def check_traffic():
        for i in range(ctx_cnt):
            if ctx[i]:
                expected = {
                    'target': (2+i*2, 3+i*2),
                    'noise': (0, 1),
                    'empty': list(range(2, 2+i*2)) + list(range(4+i*2, 2+2*ctx_cnt))
                }
            else:
                expected = {
                    'target': (0, 1),
                    'empty':  range(2, 2+2*ctx_cnt)
                }

            _send_traffic_check(cfg, ports[i], f"context {i}", expected)

    # Use queues 0 and 1 for normal traffic
    ethtool(f"-X {cfg.ifname} equal 2")
    defer(ethtool, f"-X {cfg.ifname} default")

    for i in range(ctx_cnt):
        ctx_id = ethtool_create(cfg, "-X", f"context new start {2 + i * 2} equal 2")
        ctx.append(defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete"))

        ports.append(rand_port())
        flow = f"flow-type tcp{cfg.addr_ipver} dst-port {ports[i]} context {ctx_id}"
        ntuple_id = ethtool_create(cfg, "-N", flow)
        ntuple.append(defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}"))

    check_traffic()

    # Remove middle context
    remove_ctx(ctx_cnt // 2)
    check_traffic()

    # Remove first context
    remove_ctx(0)
    check_traffic()

    # Remove last context
    remove_ctx(-1)
    check_traffic()

    if requested_ctx_cnt != ctx_cnt:
        raise KsftSkipEx(f"Tested only {ctx_cnt} contexts, wanted {requested_ctx_cnt}")


def test_rss_context_overlap(cfg, other_ctx=0):
    """
    Test contexts overlapping with each other.
    Use 4 queues for the main context, but only queues 2 and 3 for context 1.
    """

    require_ntuple(cfg)

    queue_cnt = len(_get_rx_cnts(cfg))
    if queue_cnt < 4:
        try:
            ksft_pr(f"Increasing queue count {queue_cnt} -> 4")
            ethtool(f"-L {cfg.ifname} combined 4")
            defer(ethtool, f"-L {cfg.ifname} combined {queue_cnt}")
        except:
            raise KsftSkipEx("Not enough queues for the test")

    if other_ctx == 0:
        ethtool(f"-X {cfg.ifname} equal 4")
        defer(ethtool, f"-X {cfg.ifname} default")
    else:
        other_ctx = ethtool_create(cfg, "-X", "context new")
        ethtool(f"-X {cfg.ifname} context {other_ctx} equal 4")
        defer(ethtool, f"-X {cfg.ifname} context {other_ctx} delete")

    ctx_id = ethtool_create(cfg, "-X", "context new")
    ethtool(f"-X {cfg.ifname} context {ctx_id} start 2 equal 2")
    defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")

    port = rand_port()
    if other_ctx:
        flow = f"flow-type tcp{cfg.addr_ipver} dst-port {port} context {other_ctx}"
        ntuple_id = ethtool_create(cfg, "-N", flow)
        ntuple = defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}")

    # Test the main context
    cnts = _get_rx_cnts(cfg)
    GenerateTraffic(cfg, port=port).wait_pkts_and_stop(20000)
    cnts = _get_rx_cnts(cfg, prev=cnts)

    ksft_ge(sum(cnts[ :4]), 20000, "traffic on main context: " + str(cnts))
    ksft_ge(sum(cnts[ :2]),  7000, "traffic on main context (1/2): " + str(cnts))
    ksft_ge(sum(cnts[2:4]),  7000, "traffic on main context (2/2): " + str(cnts))
    if other_ctx == 0:
        ksft_eq(sum(cnts[4: ]),     0, "traffic on other queues: " + str(cnts))

    # Now create a rule for context 1 and make sure traffic goes to a subset
    if other_ctx:
        ntuple.exec()
    flow = f"flow-type tcp{cfg.addr_ipver} dst-port {port} context {ctx_id}"
    ntuple_id = ethtool_create(cfg, "-N", flow)
    defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}")

    cnts = _get_rx_cnts(cfg)
    GenerateTraffic(cfg, port=port).wait_pkts_and_stop(20000)
    cnts = _get_rx_cnts(cfg, prev=cnts)

    directed = sum(cnts[2:4])
    ksft_lt(sum(cnts[ :2]), directed / 2, "traffic on main context: " + str(cnts))
    ksft_ge(directed, 20000, "traffic on extra context: " + str(cnts))
    if other_ctx == 0:
        ksft_eq(sum(cnts[4: ]),     0, "traffic on other queues: " + str(cnts))


def test_rss_context_overlap2(cfg):
    test_rss_context_overlap(cfg, True)


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.netdevnl = NetdevFamily()

        ksft_run([test_rss_key_indir,
                  test_rss_context, test_rss_context4, test_rss_context32,
                  test_rss_context_overlap, test_rss_context_overlap2,
                  test_rss_context_out_of_order, test_rss_context4_create_with_cfg],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
