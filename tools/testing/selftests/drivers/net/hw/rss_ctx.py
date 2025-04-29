#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import datetime
import random
import re
from lib.py import ksft_run, ksft_pr, ksft_exit
from lib.py import ksft_eq, ksft_ne, ksft_ge, ksft_in, ksft_lt, ksft_true, ksft_raises
from lib.py import NetDrvEpEnv
from lib.py import EthtoolFamily, NetdevFamily
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import rand_port
from lib.py import ethtool, ip, defer, GenerateTraffic, CmdExitFailure


def _rss_key_str(key):
    return ":".join(["{:02x}".format(x) for x in key])


def _rss_key_rand(length):
    return [random.randint(0, 255) for _ in range(length)]


def _rss_key_check(cfg, data=None, context=0):
    if data is None:
        data = get_rss(cfg, context=context)
    if 'rss-hash-key' not in data:
        return
    non_zero = [x for x in data['rss-hash-key'] if x != 0]
    ksft_eq(bool(non_zero), True, comment=f"RSS key is all zero {data['rss-hash-key']}")


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


def require_context_cnt(cfg, need_cnt):
    # There's no good API to get the context count, so the tests
    # which try to add a lot opportunisitically set the count they
    # discovered. Careful with test ordering!
    if need_cnt and cfg.context_cnt and cfg.context_cnt < need_cnt:
        raise KsftSkipEx(f"Test requires at least {need_cnt} contexts, but device only has {cfg.context_cnt}")


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
                f"traffic on other queues ({name})':" + str(cnts))
    if params.get('empty'):
        ksft_eq(sum(cnts[i] for i in params['empty']), 0,
                f"traffic on inactive queues ({name}): " + str(cnts))


def _ntuple_rule_check(cfg, rule_id, ctx_id):
    """Check that ntuple rule references RSS context ID"""
    text = ethtool(f"-n {cfg.ifname} rule {rule_id}").stdout
    pattern = f"RSS Context (ID: )?{ctx_id}"
    ksft_true(re.search(pattern, text), "RSS context not referenced in ntuple rule")


def test_rss_key_indir(cfg):
    """Test basics like updating the main RSS key and indirection table."""

    qcnt = len(_get_rx_cnts(cfg))
    if qcnt < 3:
        KsftSkipEx("Device has fewer than 3 queues (or doesn't support queue stats)")

    data = get_rss(cfg)
    want_keys = ['rss-hash-key', 'rss-hash-function', 'rss-indirection-table']
    for k in want_keys:
        if k not in data:
            raise KsftFailEx("ethtool results missing key: " + k)
        if not data[k]:
            raise KsftFailEx(f"ethtool results empty for '{k}': {data[k]}")

    _rss_key_check(cfg, data=data)
    key_len = len(data['rss-hash-key'])

    # Set the key
    key = _rss_key_rand(key_len)
    ethtool(f"-X {cfg.ifname} hkey " + _rss_key_str(key))

    data = get_rss(cfg)
    ksft_eq(key, data['rss-hash-key'])

    # Set the indirection table and the key together
    key = _rss_key_rand(key_len)
    ethtool(f"-X {cfg.ifname} equal 3 hkey " + _rss_key_str(key))
    reset_indir = defer(ethtool, f"-X {cfg.ifname} default")

    data = get_rss(cfg)
    _rss_key_check(cfg, data=data)
    ksft_eq(0, min(data['rss-indirection-table']))
    ksft_eq(2, max(data['rss-indirection-table']))

    # Reset indirection table and set the key
    key = _rss_key_rand(key_len)
    ethtool(f"-X {cfg.ifname} default hkey " + _rss_key_str(key))
    data = get_rss(cfg)
    _rss_key_check(cfg, data=data)
    ksft_eq(0, min(data['rss-indirection-table']))
    ksft_eq(qcnt - 1, max(data['rss-indirection-table']))

    # Set the indirection table
    ethtool(f"-X {cfg.ifname} equal 2")
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


def test_rss_queue_reconfigure(cfg, main_ctx=True):
    """Make sure queue changes can't override requested RSS config.

    By default main RSS table should change to include all queues.
    When user sets a specific RSS config the driver should preserve it,
    even when queue count changes. Driver should refuse to deactivate
    queues used in the user-set RSS config.
    """

    if not main_ctx:
        require_ntuple(cfg)

    # Start with 4 queues, an arbitrary known number.
    try:
        qcnt = len(_get_rx_cnts(cfg))
        ethtool(f"-L {cfg.ifname} combined 4")
        defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")
    except:
        raise KsftSkipEx("Not enough queues for the test or qstat not supported")

    if main_ctx:
        ctx_id = 0
        ctx_ref = ""
    else:
        ctx_id = ethtool_create(cfg, "-X", "context new")
        ctx_ref = f"context {ctx_id}"
        defer(ethtool, f"-X {cfg.ifname} {ctx_ref} delete")

    # Indirection table should be distributing to all queues.
    data = get_rss(cfg, context=ctx_id)
    ksft_eq(0, min(data['rss-indirection-table']))
    ksft_eq(3, max(data['rss-indirection-table']))

    # Increase queues, indirection table should be distributing to all queues.
    # It's unclear whether tables of additional contexts should be reset, too.
    if main_ctx:
        ethtool(f"-L {cfg.ifname} combined 5")
        data = get_rss(cfg)
        ksft_eq(0, min(data['rss-indirection-table']))
        ksft_eq(4, max(data['rss-indirection-table']))
        ethtool(f"-L {cfg.ifname} combined 4")

    # Configure the table explicitly
    port = rand_port()
    ethtool(f"-X {cfg.ifname} {ctx_ref} weight 1 0 0 1")
    if main_ctx:
        other_key = 'empty'
        defer(ethtool, f"-X {cfg.ifname} default")
    else:
        other_key = 'noise'
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {ctx_id}"
        ntuple = ethtool_create(cfg, "-N", flow)
        defer(ethtool, f"-N {cfg.ifname} delete {ntuple}")

    _send_traffic_check(cfg, port, ctx_ref, { 'target': (0, 3),
                                              other_key: (1, 2) })

    # We should be able to increase queues, but table should be left untouched
    ethtool(f"-L {cfg.ifname} combined 5")
    data = get_rss(cfg, context=ctx_id)
    ksft_eq({0, 3}, set(data['rss-indirection-table']))

    _send_traffic_check(cfg, port, ctx_ref, { 'target': (0, 3),
                                              other_key: (1, 2, 4) })

    # Setting queue count to 3 should fail, queue 3 is used
    try:
        ethtool(f"-L {cfg.ifname} combined 3")
    except CmdExitFailure:
        pass
    else:
        raise Exception(f"Driver didn't prevent us from deactivating a used queue (context {ctx_id})")

    if not main_ctx:
        ethtool(f"-L {cfg.ifname} combined 4")
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {ctx_id} action 1"
        try:
            # this targets queue 4, which doesn't exist
            ntuple2 = ethtool_create(cfg, "-N", flow)
            defer(ethtool, f"-N {cfg.ifname} delete {ntuple2}")
        except CmdExitFailure:
            pass
        else:
            raise Exception(f"Driver didn't prevent us from targeting a nonexistent queue (context {ctx_id})")
        # change the table to target queues 0 and 2
        ethtool(f"-X {cfg.ifname} {ctx_ref} weight 1 0 1 0")
        # ntuple rule therefore targets queues 1 and 3
        try:
            ntuple2 = ethtool_create(cfg, "-N", flow)
        except CmdExitFailure:
            ksft_pr("Driver does not support rss + queue offset")
            return

        defer(ethtool, f"-N {cfg.ifname} delete {ntuple2}")
        # should replace existing filter
        ksft_eq(ntuple, ntuple2)
        _send_traffic_check(cfg, port, ctx_ref, { 'target': (1, 3),
                                                  'noise' : (0, 2) })
        # Setting queue count to 3 should fail, queue 3 is used
        try:
            ethtool(f"-L {cfg.ifname} combined 3")
        except CmdExitFailure:
            pass
        else:
            raise Exception(f"Driver didn't prevent us from deactivating a used queue (context {ctx_id})")


def test_rss_resize(cfg):
    """Test resizing of the RSS table.

    Some devices dynamically increase and decrease the size of the RSS
    indirection table based on the number of enabled queues.
    When that happens driver must maintain the balance of entries
    (preferably duplicating the smaller table).
    """

    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels['combined-max']
    qcnt = channels['combined-count']

    if ch_max < 2:
        raise KsftSkipEx(f"Not enough queues for the test: {ch_max}")

    ethtool(f"-L {cfg.ifname} combined 2")
    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    ethtool(f"-X {cfg.ifname} weight 1 7")
    defer(ethtool, f"-X {cfg.ifname} default")

    ethtool(f"-L {cfg.ifname} combined {ch_max}")
    data = get_rss(cfg)
    ksft_eq(0, min(data['rss-indirection-table']))
    ksft_eq(1, max(data['rss-indirection-table']))

    ksft_eq(7,
            data['rss-indirection-table'].count(1) /
            data['rss-indirection-table'].count(0),
            f"Table imbalance after resize: {data['rss-indirection-table']}")


def test_hitless_key_update(cfg):
    """Test that flows may be rehashed without impacting traffic.

    Some workloads may want to rehash the flows in response to an imbalance.
    Most effective way to do that is changing the RSS key. Check that changing
    the key does not cause link flaps or traffic disruption.

    Disrupting traffic for key update is not a bug, but makes the key
    update unusable for rehashing under load.
    """
    data = get_rss(cfg)
    key_len = len(data['rss-hash-key'])

    key = _rss_key_rand(key_len)

    tgen = GenerateTraffic(cfg)
    try:
        errors0, carrier0 = get_drop_err_sum(cfg)
        t0 = datetime.datetime.now()
        ethtool(f"-X {cfg.ifname} hkey " + _rss_key_str(key))
        t1 = datetime.datetime.now()
        errors1, carrier1 = get_drop_err_sum(cfg)
    finally:
        tgen.wait_pkts_and_stop(5000)

    ksft_lt((t1 - t0).total_seconds(), 0.2)
    ksft_eq(errors1 - errors1, 0)
    ksft_eq(carrier1 - carrier0, 0)


def test_rss_context_dump(cfg):
    """
    Test dumping RSS contexts. This tests mostly exercises the kernel APIs.
    """

    # Get a random key of the right size
    data = get_rss(cfg)
    if 'rss-hash-key' in data:
        key_data = _rss_key_rand(len(data['rss-hash-key']))
        key = _rss_key_str(key_data)
    else:
        key_data = []
        key = "ba:ad"

    ids = []
    try:
        ids.append(ethtool_create(cfg, "-X", f"context new"))
        defer(ethtool, f"-X {cfg.ifname} context {ids[-1]} delete")

        ids.append(ethtool_create(cfg, "-X", f"context new weight 1 1"))
        defer(ethtool, f"-X {cfg.ifname} context {ids[-1]} delete")

        ids.append(ethtool_create(cfg, "-X", f"context new hkey {key}"))
        defer(ethtool, f"-X {cfg.ifname} context {ids[-1]} delete")
    except CmdExitFailure:
        if not ids:
            raise KsftSkipEx("Unable to add any contexts")
        ksft_pr(f"Added only {len(ids)} out of 3 contexts")

    expect_tuples = set([(cfg.ifname, -1)] + [(cfg.ifname, ctx_id) for ctx_id in ids])

    # Dump all
    ctxs = cfg.ethnl.rss_get({}, dump=True)
    tuples = [(c['header']['dev-name'], c.get('context', -1)) for c in ctxs]
    ksft_eq(len(tuples), len(set(tuples)), "duplicates in context dump")
    ctx_tuples = set([ctx for ctx in tuples if ctx[0] == cfg.ifname])
    ksft_eq(expect_tuples, ctx_tuples)

    # Sanity-check the results
    for data in ctxs:
        ksft_ne(set(data.get('indir', [1])), {0}, "indir table is all zero")
        ksft_ne(set(data.get('hkey', [1])), {0}, "key is all zero")

        # More specific checks
        if len(ids) > 1 and data.get('context') == ids[1]:
            ksft_eq(set(data['indir']), {0, 1},
                    "ctx1 - indir table mismatch")
        if len(ids) > 2 and data.get('context') == ids[2]:
            ksft_eq(data['hkey'], bytes(key_data), "ctx2 - key mismatch")

    # Ifindex filter
    ctxs = cfg.ethnl.rss_get({'header': {'dev-name': cfg.ifname}}, dump=True)
    tuples = [(c['header']['dev-name'], c.get('context', -1)) for c in ctxs]
    ctx_tuples = set(tuples)
    ksft_eq(len(tuples), len(ctx_tuples), "duplicates in context dump")
    ksft_eq(expect_tuples, ctx_tuples)

    # Skip ctx 0
    expect_tuples.remove((cfg.ifname, -1))

    ctxs = cfg.ethnl.rss_get({'start-context': 1}, dump=True)
    tuples = [(c['header']['dev-name'], c.get('context', -1)) for c in ctxs]
    ksft_eq(len(tuples), len(set(tuples)), "duplicates in context dump")
    ctx_tuples = set([ctx for ctx in tuples if ctx[0] == cfg.ifname])
    ksft_eq(expect_tuples, ctx_tuples)

    # And finally both with ifindex and skip main
    ctxs = cfg.ethnl.rss_get({'header': {'dev-name': cfg.ifname}, 'start-context': 1}, dump=True)
    ctx_tuples = set([(c['header']['dev-name'], c.get('context', -1)) for c in ctxs])
    ksft_eq(expect_tuples, ctx_tuples)


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
            if cfg.context_cnt is None:
                cfg.context_cnt = ctx_cnt
            break

        _rss_key_check(cfg, context=ctx_id)

        if not create_with_cfg:
            ethtool(f"-X {cfg.ifname} context {ctx_id} {want_cfg}")
            _rss_key_check(cfg, context=ctx_id)

        # Sanity check the context we just created
        data = get_rss(cfg, ctx_id)
        ksft_eq(min(data['rss-indirection-table']), 2 + i * 2, "Unexpected context cfg: " + str(data))
        ksft_eq(max(data['rss-indirection-table']), 2 + i * 2 + 1, "Unexpected context cfg: " + str(data))

        ports.append(rand_port())
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {ports[i]} context {ctx_id}"
        ntuple = ethtool_create(cfg, "-N", flow)
        defer(ethtool, f"-N {cfg.ifname} delete {ntuple}")

        _ntuple_rule_check(cfg, ntuple, ctx_id)

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


def test_rss_context_queue_reconfigure(cfg):
    test_rss_queue_reconfigure(cfg, main_ctx=False)


def test_rss_context_out_of_order(cfg, ctx_cnt=4):
    """
    Test separating traffic into RSS contexts.
    Contexts are removed in semi-random order, and steering re-tested
    to make sure removal doesn't break steering to surviving contexts.
    Test requires 3 contexts to work.
    """

    require_ntuple(cfg)
    require_context_cnt(cfg, 4)

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
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {ports[i]} context {ctx_id}"
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


def test_rss_context_overlap(cfg, other_ctx=0):
    """
    Test contexts overlapping with each other.
    Use 4 queues for the main context, but only queues 2 and 3 for context 1.
    """

    require_ntuple(cfg)
    if other_ctx:
        require_context_cnt(cfg, 2)

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
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {other_ctx}"
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
    flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {ctx_id}"
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


def test_flow_add_context_missing(cfg):
    """
    Test that we are not allowed to add a rule pointing to an RSS context
    which was never created.
    """

    require_ntuple(cfg)

    # Find a context which doesn't exist
    for ctx_id in range(1, 100):
        try:
            get_rss(cfg, context=ctx_id)
        except CmdExitFailure:
            break

    with ksft_raises(CmdExitFailure) as cm:
        flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port 1234 context {ctx_id}"
        ntuple_id = ethtool_create(cfg, "-N", flow)
        ethtool(f"-N {cfg.ifname} delete {ntuple_id}")
    if cm.exception:
        ksft_in('Invalid argument', cm.exception.cmd.stderr)


def test_delete_rss_context_busy(cfg):
    """
    Test that deletion returns -EBUSY when an rss context is being used
    by an ntuple filter.
    """

    require_ntuple(cfg)

    # create additional rss context
    ctx_id = ethtool_create(cfg, "-X", "context new")
    ctx_deleter = defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")

    # utilize context from ntuple filter
    port = rand_port()
    flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {ctx_id}"
    ntuple_id = ethtool_create(cfg, "-N", flow)
    defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}")

    # attempt to delete in-use context
    try:
        ctx_deleter.exec_only()
        ctx_deleter.cancel()
        raise KsftFailEx(f"deleted context {ctx_id} used by rule {ntuple_id}")
    except CmdExitFailure:
        pass


def test_rss_ntuple_addition(cfg):
    """
    Test that the queue offset (ring_cookie) of an ntuple rule is added
    to the queue number read from the indirection table.
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

    # Use queue 0 for normal traffic
    ethtool(f"-X {cfg.ifname} equal 1")
    defer(ethtool, f"-X {cfg.ifname} default")

    # create additional rss context
    ctx_id = ethtool_create(cfg, "-X", "context new equal 2")
    defer(ethtool, f"-X {cfg.ifname} context {ctx_id} delete")

    # utilize context from ntuple filter
    port = rand_port()
    flow = f"flow-type tcp{cfg.addr_ipver} dst-ip {cfg.addr} dst-port {port} context {ctx_id} action 2"
    try:
        ntuple_id = ethtool_create(cfg, "-N", flow)
    except CmdExitFailure:
        raise KsftSkipEx("Ntuple filter with RSS and nonzero action not supported")
    defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}")

    _send_traffic_check(cfg, port, f"context {ctx_id}", { 'target': (2, 3),
                                                          'empty' : (1,),
                                                          'noise' : (0,) })


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.context_cnt = None
        cfg.ethnl = EthtoolFamily()
        cfg.netdevnl = NetdevFamily()

        ksft_run([test_rss_key_indir, test_rss_queue_reconfigure,
                  test_rss_resize, test_hitless_key_update,
                  test_rss_context, test_rss_context4, test_rss_context32,
                  test_rss_context_dump, test_rss_context_queue_reconfigure,
                  test_rss_context_overlap, test_rss_context_overlap2,
                  test_rss_context_out_of_order, test_rss_context4_create_with_cfg,
                  test_flow_add_context_missing,
                  test_delete_rss_context_busy, test_rss_ntuple_addition],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
