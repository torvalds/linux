#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# pylint: disable=too-many-lines

import errno
import glob

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_true, ksft_raises, KsftSkipEx
from lib.py import EthtoolFamily, NetshaperFamily
from lib.py import NetDrvEnv
from lib.py import NlError
from lib.py import cmd, defer

def _delete_shaper(cfg, nl_shaper, handle) -> None:
    """ Delete the shaper identified by handle, ignoring a missing-shaper error. """
    try:
        nl_shaper.delete({'ifindex': cfg.ifindex,
                          'handle': handle})
    except NlError as e:
        if e.error != errno.ENOENT:
            raise

def _require_queues(cfg, count):
    """ Return the netdev TX queue count, skipping the test if fewer than count exist. """
    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/tx-*"))
    if qcnt < count:
        raise KsftSkipEx(f"netdev has {qcnt} queues, {count} required")
    return qcnt

def _cap_get(cfg, nl_shaper, scope):
    """ Return the shaper capabilities for the given scope, caching them on cfg. """
    if not hasattr(cfg, 'cap_cache'):
        cfg.cap_cache = {}
    if scope not in cfg.cap_cache:
        cfg.cap_cache[scope] = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                                  'scope': scope})

    return cfg.cap_cache[scope]

def _require_caps(cfg, nl_shaper, scope, caps, msg) -> None:
    """ Skip the test unless the given scope advertises all the required caps. """
    try:
        supported = _cap_get(cfg, nl_shaper, scope)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx(f"{scope} scope shapers not supported by the device")
        raise

    if not set(caps).issubset(supported):
        raise KsftSkipEx(msg)

def get_shapers(cfg, nl_shaper) -> None:
    try:
        shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise

    # Default configuration: no shapers configured.
    ksft_eq(len(shapers), 0)

def get_caps(cfg, nl_shaper) -> None:
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex}, dump=True)
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise

    # Each device implementing shaper support must support some
    # features in at least a scope.
    ksft_true(len(caps)> 0)

def set_qshapers(cfg, nl_shaper) -> None:
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                 'scope':'queue'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
        raise KsftSkipEx("device does not support queue scope shapers with bw_max and metric bps")

    _require_queues(cfg, 3)
    cfg.queues = True

    nl_shaper.set({'ifindex': cfg.ifindex,
                   'handle': {'scope': 'queue', 'id': 1},
                   'metric': 'bps',
                   'bw-max': 10000})
    nl_shaper.set({'ifindex': cfg.ifindex,
                   'handle': {'scope': 'queue', 'id': 2},
                   'metric': 'bps',
                   'bw-max': 20000})

    # Querying a specific shaper not yet configured must fail.
    raised = False
    try:
        shaper_q0 = nl_shaper.get({'ifindex': cfg.ifindex,
                                   'handle': {'scope': 'queue', 'id': 0}})
    except (NlError):
        raised = True
    ksft_eq(raised, True)

    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex,
                              'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper_q1, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'netdev'},
                        'handle': {'scope': 'queue', 'id': 1},
                        'metric': 'bps',
                        'bw-max': 10000})

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 1},
                       'metric': 'bps',
                       'bw-max': 10000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 2},
                       'metric': 'bps',
                       'bw-max': 20000}])

def del_qshapers(cfg, nl_shaper) -> None:
    if not cfg.queues:
        raise KsftSkipEx("queue shapers not supported by device, skipping delete")

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 2}})
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 1}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def set_nshapers(cfg, nl_shaper) -> None:
    # Check required features.
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                  'scope':'netdev'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
        raise KsftSkipEx("device does not support nested netdev scope shapers with weight")

    cfg.netdev = True;
    nl_shaper.set({'ifindex': cfg.ifindex,
                   'handle': {'scope': 'netdev', 'id': 0},
                   'bw-max': 100000})

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'handle': {'scope': 'netdev'},
                       'metric': 'bps',
                       'bw-max': 100000}])

def del_nshapers(cfg, nl_shaper) -> None:
    if not cfg.netdev:
        raise KsftSkipEx("netdev shaper not supported by device, skipping delete")

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'netdev'}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def set_all_supported_attrs(cfg, nl_shaper) -> None:
    """ Set every queue-scope attribute the device advertises and verify the read-back. """
    _require_queues(cfg, 1)

    _require_caps(cfg, nl_shaper, 'queue', [],
                  "queue scope shapers not supported by the device")
    caps = _cap_get(cfg, nl_shaper, 'queue')

    attrs = {'ifindex': cfg.ifindex,
             'handle': {'scope': 'queue', 'id': 0}}
    expected = {'ifindex': cfg.ifindex,
                'parent': {'scope': 'netdev'},
                'handle': {'scope': 'queue', 'id': 0}}

    rate_attrs = {'support-bw-min': ('bw-min', 10000, 100),
                  'support-bw-max': ('bw-max', 20000, 200),
                  'support-burst': ('burst', 3000, 30)}
    rate_attr_supported = any(cap in caps for cap in rate_attrs)
    bps_supported = 'support-metric-bps' in caps
    pps_supported = 'support-metric-pps' in caps

    def add_rate_attrs(metric, value_idx) -> None:
        attrs['metric'] = metric
        expected['metric'] = metric
        for cap, (attr, bps_value, pps_value) in rate_attrs.items():
            if cap not in caps:
                continue

            value = bps_value if value_idx == 0 else pps_value
            attrs[attr] = value
            expected[attr] = value

    if rate_attr_supported:
        if bps_supported:
            add_rate_attrs('bps', 0)
        elif pps_supported:
            add_rate_attrs('pps', 1)

    if 'support-priority' in caps:
        attrs['priority'] = 1
        expected['priority'] = 1
    if 'support-weight' in caps:
        attrs['weight'] = 2
        expected['weight'] = 2

    if len(attrs) == 2:
        raise KsftSkipEx("device does not advertise any supported queue shaper attributes")

    nl_shaper.set(attrs)
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 0})

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper, expected)

    if rate_attr_supported and bps_supported and pps_supported:
        add_rate_attrs('pps', 1)
        nl_shaper.set(attrs)

        shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                                'handle': {'scope': 'queue', 'id': 0}})
        ksft_eq(shaper, expected)

    _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': 0})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def invalid_set_preserves_state(cfg, nl_shaper) -> None:
    """ Verify a rejected .set leaves the existing shaper configuration unchanged. """
    nq = _require_queues(cfg, 1)
    _require_caps(cfg, nl_shaper, 'queue',
                  ['support-bw-max', 'support-metric-bps'],
                  "device does not support queue scope bw_max with bps metric")

    initial = {'ifindex': cfg.ifindex,
               'parent': {'scope': 'netdev'},
               'handle': {'scope': 'queue', 'id': 0},
               'metric': 'bps',
               'bw-max': 10000}
    nl_shaper.set({'ifindex': cfg.ifindex,
                   'handle': {'scope': 'queue', 'id': 0},
                   'metric': 'bps',
                   'bw-max': 10000})
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 0})

    with ksft_raises(NlError):
        nl_shaper.set({'ifindex': cfg.ifindex,
                       'handle': {'scope': 'node', 'id': 0},
                       'metric': 'bps',
                       'bw-max': 20000})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper, initial)

    with ksft_raises(NlError):
        nl_shaper.set({'ifindex': cfg.ifindex,
                       'handle': {'scope': 'queue', 'id': nq},
                       'metric': 'bps',
                       'bw-max': 20000})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper, initial)

    _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': 0})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def mixed_parent_group_requires_parent(cfg, nl_shaper) -> None:
    r"""Grouping leaves from different nodes requires an explicit parent.

        netdev             netdev
        /    \             parent=netdev
       N1     N2  group        N
       |      |   {Q0,Q1}     / \
       Q0     Q1  ------->   Q0  Q1

    Without an explicit parent the group is rejected; parent=netdev
    collapses the leaves into one new node.
    """
    _require_queues(cfg, 2)
    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps'],
                  "device does not support node scope shapers with bw_max and metric bps")
    _require_caps(cfg, nl_shaper, 'queue',
                  ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    n1_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    n1_id = n1_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 0})

    n2_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 2}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 20000})
    n2_id = n2_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 1})

    with ksft_raises(NlError):
        nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 3},
                             {'handle': {'scope': 'queue', 'id': 1},
                              'weight': 4}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 30000})

    shaper_q0 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper_q0, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n1_id},
                        'handle': {'scope': 'queue', 'id': 0},
                        'weight': 1})
    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper_q1, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n2_id},
                        'handle': {'scope': 'queue', 'id': 1},
                        'weight': 2})

    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 3},
                             {'handle': {'scope': 'queue', 'id': 1},
                              'weight': 4}],
                   'handle': {'scope':'node'},
                   'parent': {'scope': 'netdev'},
                   'metric': 'bps',
                   'bw-max': 30000})
    node_id = node_handle['handle']['id']

    for old_id in (n1_id, n2_id):
        with ksft_raises(NlError):
            nl_shaper.get({'ifindex': cfg.ifindex,
                           'handle': {'scope': 'node', 'id': old_id}})

    shaper_q0 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper_q0, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': node_id},
                        'handle': {'scope': 'queue', 'id': 0},
                        'weight': 3})
    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper_q1, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': node_id},
                        'handle': {'scope': 'queue', 'id': 1},
                        'weight': 4})

    for i in range(2):
        _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': i})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def recursive_empty_node_cleanup(cfg, nl_shaper) -> None:
    r"""Deleting the last leaf recursively removes the emptied ancestors.

        netdev             netdev
          |       del Q0
         N1       ------>   (N1 and N2 removed too)
          |
         N2
          |
         Q0
    """
    _require_queues(cfg, 1)
    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support nested node scope shapers")
    _require_caps(cfg, nl_shaper, 'queue',
                  ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    n1_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    n1_id = n1_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 0})

    n2_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'parent': {'scope': 'node', 'id': n1_id},
                   'metric': 'bps',
                   'bw-max': 5000})
    n2_id = n2_handle['handle']['id']

    shaper_q0 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 0}})
    ksft_eq(shaper_q0, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n2_id},
                        'handle': {'scope': 'queue', 'id': 0},
                        'weight': 1})

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 0}})

    for handle in ({'scope': 'queue', 'id': 0},
                   {'scope': 'node', 'id': n2_id},
                   {'scope': 'node', 'id': n1_id}):
        with ksft_raises(NlError):
            nl_shaper.get({'ifindex': cfg.ifindex, 'handle': handle})

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def _group_under_netdev(cfg, nl_shaper, bw_max=None):
    r"""Group queues under a netdev-scope node; caller owns node teardown.

        netdev               netdev
         /  \      del Q1,Q2
        Q1  Q2     ------->   (netdev node persists)
    """
    group_args = {
        'ifindex': cfg.ifindex,
        'leaves': [{'handle': {'scope': 'queue', 'id': 1},
                    'weight': 1},
                   {'handle': {'scope': 'queue', 'id': 2},
                    'weight': 2}],
        'handle': {'scope': 'netdev'}}
    if bw_max:
        group_args['metric'] = 'bps'
        group_args['bw-max'] = bw_max

    node_handle = nl_shaper.group(group_args)
    ksft_eq(node_handle, {'ifindex': cfg.ifindex,
                          'handle': {'scope': 'netdev'}})

    del_node = defer(_delete_shaper, cfg, nl_shaper, {'scope': 'netdev'})
    del_queues = [defer(_delete_shaper, cfg, nl_shaper,
                        {'scope': 'queue', 'id': qid})
                  for qid in (1, 2)]

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'netdev'},
                     'handle': {'scope': 'queue', 'id': 1},
                     'weight': 1})
    for dq in del_queues:
        dq.exec()

    # Caller owns the node teardown so it can verify the netdev-scope node
    # survives leaf deletion before removing it.
    return del_node

def basic_groups(cfg, nl_shaper) -> None:
    r"""Group queues under a netdev-scope node, then tear it down.

        netdev
         /  \
        Q1  Q2
    """
    _require_queues(cfg, 3)

    _require_caps(cfg, nl_shaper, 'netdev', [], "netdev scope not supported by the device")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "queue scope not supported with nesting and weight")

    del_node = _group_under_netdev(cfg, nl_shaper)

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'handle': {'scope': 'netdev'}}])

    del_node.exec()
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def basic_groups_with_rate(cfg, nl_shaper) -> None:
    r"""Rate-limited netdev-scope node outlives deletion of its leaves.

        netdev[10kbps]          netdev[10kbps]
          /  \       del Q1,Q2
        Q1    Q2     ------->    (node persists)
    """
    bw_max = 10000

    _require_queues(cfg, 3)

    _require_caps(cfg, nl_shaper, 'netdev', ['support-bw-max', 'support-metric-bps'],
                  "device does not support netdev scope rate limiting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support queue scope shapers with nesting and weight")

    del_node = _group_under_netdev(cfg, nl_shaper, bw_max=bw_max)

    # Deleting all the leaves shaper does not affect the node one
    # when the latter has 'netdev' scope.
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'handle': {'scope': 'netdev'},
                       'metric': 'bps',
                       'bw-max': bw_max}])

    del_node.exec()

def qgroups(cfg, nl_shaper) -> None:
    _require_queues(cfg, 4)
    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps'],
                  "device does not support node scope shapers with bw_max and metric bps")
    _require_caps(cfg, nl_shaper, 'queue',
                  ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 3},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 2}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    node_id = node_handle['handle']['id']

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node_id},
                     'handle': {'scope': 'queue', 'id': 1},
                     'weight': 3})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': 10000})

    # Grouping to a specified, not existing node scope shaper must fail
    raised = False
    try:
        nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 3}],
                   'handle': {'scope':'node', 'id': node_id + 1},
                   'metric': 'bps',
                   'bw-max': 10000})

    except (NlError):
        raised = True
    ksft_eq(raised, True)

    # Add to an existing node
    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 4}],
                   'handle': {'scope':'node', 'id': node_id}})
    ksft_eq(node_handle, {'ifindex': cfg.ifindex,
                          'handle': {'scope': 'node', 'id': node_id}})

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 3}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node_id},
                     'handle': {'scope': 'queue', 'id': 3},
                     'weight': 4})

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 2}})
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 1}})

    # Deleting a non empty node will move the leaves downstream.
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'node', 'id': node_id}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 3},
                       'weight': 4}])

    # Finish and verify the complete cleanup.
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 3}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def set_node_shaper(cfg, nl_shaper) -> None:
    """ Verify a node-scope shaper rate can be updated via .set. """
    _require_queues(cfg, 2)
    _require_caps(cfg, nl_shaper, 'node', ['support-bw-max', 'support-metric-bps'],
                  "device does not support node scope shapers with bw_max and metric bps")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    node_id = node_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 1})

    # Update the node's rate via .set
    nl_shaper.set({'ifindex': cfg.ifindex,
                   'handle': {'scope': 'node', 'id': node_id},
                   'metric': 'bps',
                   'bw-max': 20000})

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': 20000})

    # Cleanup
    _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': 1})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def group_update_rate(cfg, nl_shaper) -> None:
    """ Verify re-grouping a node updates its rate while leaving the leaves untouched. """
    _require_queues(cfg, 3)
    _require_caps(cfg, nl_shaper, 'node', ['support-bw-max', 'support-metric-bps'],
                  "device does not support node scope shapers with bw_max and metric bps")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    # Create node with Q1, Q2 at bw_max=10000
    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    node_id = node_handle['handle']['id']
    for i in range(1, 3):
        defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': i})

    # Update rate via .group on the same node
    nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 1}],
                   'handle': {'scope':'node', 'id': node_id},
                   'metric': 'bps',
                   'bw-max': 50000})

    # Verify rate updated
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': 50000})

    # Verify leaves unchanged
    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper_q1, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': node_id},
                        'handle': {'scope': 'queue', 'id': 1},
                        'weight': 1})
    shaper_q2 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 2}})
    ksft_eq(shaper_q2, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': node_id},
                        'handle': {'scope': 'queue', 'id': 2},
                        'weight': 1})

    # Make sure we only have 3 shapers including 2 queues and the node
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 3)

    # Cleanup
    for i in range(1, 3):
        _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': i})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def delegation(cfg, nl_shaper) -> None:
    _require_queues(cfg, 4)
    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support node scope shapers with bw_max, metric bps and nesting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 3},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 2},
                             {'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})
    node_id = node_handle['handle']['id']

    # Create the nested node and validate the hierarchy
    nested_node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 3},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 2}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 5000})
    nested_node_id = nested_node_handle['handle']['id']
    ksft_true(nested_node_id != node_id)
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': nested_node_id},
                       'handle': {'scope': 'queue', 'id': 1},
                       'weight': 3},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': nested_node_id},
                       'handle': {'scope': 'queue', 'id': 2},
                       'weight': 2},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': node_id},
                       'handle': {'scope': 'queue', 'id': 3},
                       'weight': 1},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'node', 'id': node_id},
                       'metric': 'bps',
                       'bw-max': 10000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': node_id},
                       'handle': {'scope': 'node', 'id': nested_node_id},
                       'metric': 'bps',
                       'bw-max': 5000}])

    # Deleting a non empty node will move the leaves downstream.
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'node', 'id': nested_node_id}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': node_id},
                       'handle': {'scope': 'queue', 'id': 1},
                       'weight': 3},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': node_id},
                       'handle': {'scope': 'queue', 'id': 2},
                       'weight': 2},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'node', 'id': node_id},
                       'handle': {'scope': 'queue', 'id': 3},
                       'weight': 1},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'node', 'id': node_id},
                       'metric': 'bps',
                       'bw-max': 10000}])

    # Final cleanup.
    for i in range(1, 4):
        nl_shaper.delete({'ifindex': cfg.ifindex,
                          'handle': {'scope': 'queue', 'id': i}})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def nested_depth_limit(cfg, nl_shaper) -> None:
    r"""Nest nodes as deep as the device allows to find the max depth.

        netdev
          |
         N1 -- Q1
          |
         N2 -- Q2
          |
         N3 -- Q3
          :       (deepen until the driver rejects)
    """
    bw_max = 10000

    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support node scope shapers with bw_max, metric bps and nesting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    nq = _require_queues(cfg, 3)

    node_ids = []
    cleanups = []
    queue_id = 1
    max_depth = 0
    limit_err = None

    # Create initial node with a queue leaf
    node_id = nl_shaper.group({
        'ifindex': cfg.ifindex,
        'leaves': [{'handle': {'scope': 'queue', 'id': queue_id},
                     'weight': 1}],
        'handle': {'scope': 'node'},
        'metric': 'bps',
        'bw-max': bw_max})['handle']['id']
    node_ids.append(node_id)
    cleanups.append(defer(_delete_shaper, cfg, nl_shaper,
                          {'scope': 'node', 'id': node_id}))
    cleanups.append(defer(_delete_shaper, cfg, nl_shaper,
                          {'scope': 'queue', 'id': queue_id}))
    max_depth = 1
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': bw_max})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': queue_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node_id},
                     'handle': {'scope': 'queue', 'id': queue_id},
                     'weight': 1})
    queue_id += 1

    # Keep nesting deeper until the driver rejects or queues run out.
    while queue_id < nq:
        parent_id = node_ids[-1]
        try:
            node_id = nl_shaper.group({
                'ifindex': cfg.ifindex,
                'leaves': [{'handle': {'scope': 'queue',
                                       'id': queue_id},
                             'weight': 1}],
                'handle': {'scope': 'node'},
                'parent': {'scope': 'node',
                           'id': parent_id},
                'metric': 'bps',
                'bw-max': bw_max})['handle']['id']
        except NlError as e:
            # Only treat "cannot nest deeper" errors as the depth limit;
            # drivers report it differently (EOPNOTSUPP/ENOSPC/E2BIG/EINVAL).
            # Anything else (ENOMEM, EIO, EPERM, driver bug) is a real failure.
            if e.error not in (errno.EOPNOTSUPP, errno.ENOSPC,
                               errno.E2BIG, errno.EINVAL):
                raise
            limit_err = e
            break

        node_ids.append(node_id)
        cleanups.append(defer(_delete_shaper, cfg, nl_shaper,
                              {'scope': 'node', 'id': node_id}))
        cleanups.append(defer(_delete_shaper, cfg, nl_shaper,
                              {'scope': 'queue', 'id': queue_id}))
        max_depth += 1
        shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                                'handle': {'scope': 'node', 'id': node_id}})
        ksft_eq(shaper, {'ifindex': cfg.ifindex,
                         'handle': {'scope': 'node', 'id': node_id},
                         'parent': {'scope': 'node', 'id': parent_id},
                         'metric': 'bps',
                         'bw-max': bw_max})
        shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                                'handle': {'scope': 'queue',
                                           'id': queue_id}})
        ksft_eq(shaper, {'ifindex': cfg.ifindex,
                         'parent': {'scope': 'node', 'id': node_id},
                         'handle': {'scope': 'queue', 'id': queue_id},
                         'weight': 1})
        queue_id += 1

    if limit_err:
        print(f"# max nesting depth supported: {max_depth} (errno {limit_err.error})")
    else:
        print(f"# max nesting depth tested: {max_depth}")
    ksft_true(max_depth >= 2,
              f"max nesting depth: {max_depth}")

    # Cleanup: exec the deferred deletes in reverse creation order, so each
    # queue leaf and deeper node is removed before its parent node.
    for cleanup in reversed(cleanups):
        cleanup.exec()
    ksft_eq(len(nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)), 0)

def delete_child_reparent(cfg, nl_shaper) -> None:
    r"""Deleting a child node reparents its queue leaf to the parent.

        netdev              netdev
          |                   |
          N1      del N2      N1
        / | \     ----->    / | \
      Q1 Q2 N2            Q1 Q2 Q3
             |
            Q3
    """
    n1_bw_max = 10000
    n2_bw_max = 5000

    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support node scope shapers with bw_max, metric bps and nesting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    _require_queues(cfg, 4)

    # Create parent node N1 with Q1, Q2
    n1_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': n1_bw_max})
    n1_id = n1_handle['handle']['id']
    for i in range(1, 3):
        defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': i})

    # Create child node N2 under N1 with Q3
    n2_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'parent': {'scope': 'node', 'id': n1_id},
                   'metric': 'bps',
                   'bw-max': n2_bw_max})
    n2_id = n2_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 3})

    # Delete child N2 - Q3 should reparent to N1
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'node', 'id': n2_id}})

    with ksft_raises(NlError):
        nl_shaper.get({'ifindex': cfg.ifindex,
                       'handle': {'scope': 'node', 'id': n2_id}})

    shaper_n1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'node', 'id': n1_id}})
    ksft_eq(shaper_n1, {'ifindex': cfg.ifindex,
                        'handle': {'scope': 'node', 'id': n1_id},
                        'parent': {'scope': 'netdev'},
                        'metric': 'bps',
                        'bw-max': n1_bw_max})
    shaper_q3 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 3}})
    ksft_eq(shaper_q3, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n1_id},
                        'handle': {'scope': 'queue', 'id': 3},
                        'weight': 1})

    # Cleanup
    for i in range(1, 4):
        _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': i})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def move_queue_between_nodes(cfg, nl_shaper) -> None:
    r"""Move a queue between nodes by re-grouping the destination node.

        netdev                  netdev
        /    \    .group N2      /    \
       N1     N2  {Q1,Q3}       N1     N2
      /  \    |   ------->       |    /  \
     Q1  Q2  Q3                 Q2  Q1   Q3
    """
    n1_bw_max = 10000
    n2_bw_max = 20000

    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support node scope shapers with bw_max, metric bps and nesting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    _require_queues(cfg, 4)

    # Create N1 with Q1, Q2
    n1_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1},
                             {'handle': {'scope': 'queue', 'id': 2},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': n1_bw_max})
    n1_id = n1_handle['handle']['id']
    for i in range(1, 3):
        defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': i})

    # Create N2 with Q3
    n2_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': n2_bw_max})
    n2_id = n2_handle['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 3})

    # Move Q1 from N1 to N2 by re-grouping N2 with Q1, Q3
    nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 2},
                             {'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node', 'id': n2_id},
                   'metric': 'bps',
                   'bw-max': n2_bw_max})

    shaper_n1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'node', 'id': n1_id}})
    ksft_eq(shaper_n1, {'ifindex': cfg.ifindex,
                        'handle': {'scope': 'node', 'id': n1_id},
                        'parent': {'scope': 'netdev'},
                        'metric': 'bps',
                        'bw-max': n1_bw_max})
    shaper_n2 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'node', 'id': n2_id}})
    ksft_eq(shaper_n2, {'ifindex': cfg.ifindex,
                        'handle': {'scope': 'node', 'id': n2_id},
                        'parent': {'scope': 'netdev'},
                        'metric': 'bps',
                        'bw-max': n2_bw_max})

    # Verify Q1 moved to N2
    shaper_q1 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper_q1, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n2_id},
                        'handle': {'scope': 'queue', 'id': 1},
                        'weight': 2})

    # Verify Q2 still under N1
    shaper_q2 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 2}})
    ksft_eq(shaper_q2, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n1_id},
                        'handle': {'scope': 'queue', 'id': 2},
                        'weight': 1})

    # Verify Q3 remained under N2
    shaper_q3 = nl_shaper.get({'ifindex': cfg.ifindex,
                               'handle': {'scope': 'queue', 'id': 3}})
    ksft_eq(shaper_q3, {'ifindex': cfg.ifindex,
                        'parent': {'scope': 'node', 'id': n2_id},
                        'handle': {'scope': 'queue', 'id': 3},
                        'weight': 1})

    # Cleanup
    for i in range(1, 4):
        _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': i})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def reject_reparenting(cfg, nl_shaper) -> None:
    r"""Reject reparenting an existing node; the hierarchy stays intact.

        netdev
        /    \      rejected:  N3 -> netdev
       N1     N2    rejected:  N1 -> N2
      /  \    |     (both EOPNOTSUPP)
     Q1  N3  Q2
         |
         Q3
    """
    node1_bw_max = 10000
    node2_bw_max = 5000
    node3_bw_max = 20000

    _require_caps(cfg, nl_shaper, 'node',
                  ['support-bw-max', 'support-metric-bps', 'support-nesting'],
                  "device does not support node scope shapers with bw_max, metric bps and nesting")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    _require_queues(cfg, 4)

    # Create Node1 under netdev with Q1.
    node1_id = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': node1_bw_max})['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 1})
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'node', 'id': node1_id})

    # Create Node2 under netdev with Q2.
    node2_id = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 2},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': node2_bw_max})['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 2})
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'node', 'id': node2_id})

    # Create Node3 nested under Node1 with Q3.
    node3_id = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': node3_bw_max,
                   'parent': {'scope': 'node', 'id': node1_id}})['handle']['id']
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'queue', 'id': 3})
    defer(_delete_shaper, cfg, nl_shaper, {'scope': 'node', 'id': node3_id})

    # Reparenting a nested node up to netdev must fail.
    with ksft_raises(NlError) as cm:
        nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 3},
                              'weight': 1}],
                   'handle': {'scope':'node', 'id': node3_id},
                   'parent': {'scope': 'netdev'}})
    if cm.exception:
        ksft_eq(cm.exception.error, errno.EOPNOTSUPP)

    # Reparenting a node under another node must fail as well.
    with ksft_raises(NlError) as cm:
        nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 1}],
                   'handle': {'scope':'node', 'id': node1_id},
                   'parent': {'scope': 'node', 'id': node2_id}})
    if cm.exception:
        ksft_eq(cm.exception.error, errno.EOPNOTSUPP)

    # Updating a node with the same parent must succeed.
    nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                              'weight': 5}],
                   'handle': {'scope':'node', 'id': node1_id},
                   'parent': {'scope': 'netdev'}})

    # Updating a node without specifying the parent must succeed.
    nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 2},
                              'weight': 7}],
                   'handle': {'scope':'node', 'id': node2_id}})

    # The rejected reparents must have left the hierarchy intact.
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node1_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node1_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': node1_bw_max})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node2_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node2_id},
                     'parent': {'scope': 'netdev'},
                     'metric': 'bps',
                     'bw-max': node2_bw_max})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'node', 'id': node3_id}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'handle': {'scope': 'node', 'id': node3_id},
                     'parent': {'scope': 'node', 'id': node1_id},
                     'metric': 'bps',
                     'bw-max': node3_bw_max})

    # Verify the leaf weights were updated and parents unchanged.
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node1_id},
                     'handle': {'scope': 'queue', 'id': 1},
                     'weight': 5})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 2}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node2_id},
                     'handle': {'scope': 'queue', 'id': 2},
                     'weight': 7})
    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 3}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'node', 'id': node3_id},
                     'handle': {'scope': 'queue', 'id': 3},
                     'weight': 1})

    # Cleanup. Delete the nodes explicitly instead of relying on the
    # empty-node auto-delete: a kernel that wrongly accepts a reparent may
    # mishandle the leaf accounting and leave a node behind. Removing them
    # by handle keeps a failing run from leaking state into later tests.
    for i in range(1, 4):
        _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': i})
    for nid in (node1_id, node2_id, node3_id):
        _delete_shaper(cfg, nl_shaper, {'scope': 'node', 'id': nid})
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 0)

def queue_update(cfg, nl_shaper) -> None:
    nq = _require_queues(cfg, 4)
    if not cfg.queues:
        raise KsftSkipEx("device does not support queue scope")

    netnl = EthtoolFamily()
    channels = netnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_type = 'combined' if channels['combined-count'] else 'tx'

    for i in range(3):
        nl_shaper.set({'ifindex': cfg.ifindex,
                       'handle': {'scope': 'queue', 'id': i},
                       'metric': 'bps',
                       'bw-max': (i + 1) * 1000})
    defer(cmd, f"ethtool -L {cfg.dev['ifname']} {ch_type} {nq}")

    # Delete a channel, with no shapers configured on top of the related
    # queue: no changes expected
    cmd(f"ethtool -L {cfg.dev['ifname']} {ch_type} 3")
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 0},
                       'metric': 'bps',
                       'bw-max': 1000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 1},
                       'metric': 'bps',
                       'bw-max': 2000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 2},
                       'metric': 'bps',
                       'bw-max': 3000}])

    # Delete a channel, with a shaper configured on top of the related
    # queue: the shaper must be deleted, too
    cmd(f"ethtool -L {cfg.dev['ifname']} {ch_type} 2")

    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 0},
                       'metric': 'bps',
                       'bw-max': 1000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 1},
                       'metric': 'bps',
                       'bw-max': 2000}])

    # Restore the original channels number, no expected changes
    cmd(f"ethtool -L {cfg.dev['ifname']} {ch_type} {nq}")
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(shapers, [{'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 0},
                       'metric': 'bps',
                       'bw-max': 1000},
                      {'ifindex': cfg.ifindex,
                       'parent': {'scope': 'netdev'},
                       'handle': {'scope': 'queue', 'id': 1},
                       'metric': 'bps',
                       'bw-max': 2000}])

    # Final cleanup.
    for i in range(0, 2):
        nl_shaper.delete({'ifindex': cfg.ifindex,
                          'handle': {'scope': 'queue', 'id': i}})

def dup_leaves(cfg, nl_shaper) -> None:
    """ Ensure that the kernel rejects duplicate leaves. """
    _require_caps(cfg, nl_shaper, 'node', ['support-bw-max', 'support-metric-bps'],
                  "device does not support node scope shapers with bw_max and metric bps")
    _require_caps(cfg, nl_shaper, 'queue', ['support-nesting', 'support-weight'],
                  "device does not support nested queue scope shapers with weight")

    node_handle = None
    with ksft_raises(NlError) as cm:
        node_handle = nl_shaper.group({
                   'ifindex': cfg.ifindex,
                   'leaves':[{'handle': {'scope': 'queue', 'id': 0},
                              'weight': 1},
                             {'handle': {'scope': 'queue', 'id': 0},
                              'weight': 2}],
                   'handle': {'scope':'node'},
                   'metric': 'bps',
                   'bw-max': 10000})

    # Clean up in case the kernel wrongly accepted the request.
    if node_handle:
        _delete_shaper(cfg, nl_shaper, node_handle['handle'])
    _delete_shaper(cfg, nl_shaper, {'scope': 'queue', 'id': 0})

    # ksft_raises() has already recorded the failure if nothing was raised.
    if cm.exception is None:
        return
    ksft_eq(cm.exception.error, errno.EINVAL)

def main() -> None:
    with NetDrvEnv(__file__, queue_count=4) as cfg:
        cfg.queues = False
        cfg.netdev = False
        ksft_run([get_shapers,
                  get_caps,
                  set_qshapers,
                  del_qshapers,
                  set_nshapers,
                  del_nshapers,
                  set_all_supported_attrs,
                  invalid_set_preserves_state,
                  mixed_parent_group_requires_parent,
                  recursive_empty_node_cleanup,
                  basic_groups,
                  basic_groups_with_rate,
                  qgroups,
                  set_node_shaper,
                  group_update_rate,
                  delegation,
                  nested_depth_limit,
                  delete_child_reparent,
                  move_queue_between_nodes,
                  reject_reparenting,
                  dup_leaves,
                  queue_update],
                 args=(cfg, NetshaperFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
