#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_true, KsftSkipEx
from lib.py import EthtoolFamily, NetshaperFamily
from lib.py import NetDrvEnv
from lib.py import NlError
from lib.py import cmd

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

    cfg.queues = True;
    netnl = EthtoolFamily()
    channels = netnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    if channels['combined-count'] == 0:
        cfg.rx_type = 'rx'
        cfg.nr_queues = channels['rx-count']
    else:
        cfg.rx_type = 'combined'
        cfg.nr_queues = channels['combined-count']
    if cfg.nr_queues < 3:
        raise KsftSkipEx(f"device does not support enough queues min 3 found {cfg.nr_queues}")

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

def basic_groups(cfg, nl_shaper) -> None:
    if not cfg.netdev:
        raise KsftSkipEx("netdev shaper not supported by the device")
    if cfg.nr_queues < 3:
        raise KsftSkipEx(f"netdev does not have enough queues min 3 reported {cfg.nr_queues}")

    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                  'scope':'queue'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-weight' in caps:
        raise KsftSkipEx("device does not support queue scope shapers with weight")

    node_handle = nl_shaper.group({
                        'ifindex': cfg.ifindex,
                        'leaves':[{'handle': {'scope': 'queue', 'id': 1},
                                   'weight': 1},
                                  {'handle': {'scope': 'queue', 'id': 2},
                                   'weight': 2}],
                         'handle': {'scope':'netdev'},
                         'metric': 'bps',
                         'bw-max': 10000})
    ksft_eq(node_handle, {'ifindex': cfg.ifindex,
                          'handle': {'scope': 'netdev'}})

    shaper = nl_shaper.get({'ifindex': cfg.ifindex,
                            'handle': {'scope': 'queue', 'id': 1}})
    ksft_eq(shaper, {'ifindex': cfg.ifindex,
                     'parent': {'scope': 'netdev'},
                     'handle': {'scope': 'queue', 'id': 1},
                     'weight': 1 })

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 2}})
    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'queue', 'id': 1}})

    # Deleting all the leaves shaper does not affect the node one
    # when the latter has 'netdev' scope.
    shapers = nl_shaper.get({'ifindex': cfg.ifindex}, dump=True)
    ksft_eq(len(shapers), 1)

    nl_shaper.delete({'ifindex': cfg.ifindex,
                      'handle': {'scope': 'netdev'}})

def qgroups(cfg, nl_shaper) -> None:
    if cfg.nr_queues < 4:
        raise KsftSkipEx(f"netdev does not have enough queues min 4 reported {cfg.nr_queues}")
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                  'scope':'node'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-bw-max' in caps or not 'support-metric-bps' in caps:
        raise KsftSkipEx("device does not support node scope shapers with bw_max and metric bps")
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                  'scope':'queue'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("shapers not supported by the device")
        raise
    if not 'support-nesting' in caps or not 'support-weight' in caps or not 'support-metric-bps' in caps:
            raise KsftSkipEx("device does not support nested queue scope shapers with weight")

    cfg.groups = True;
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

def delegation(cfg, nl_shaper) -> None:
    if not cfg.groups:
        raise KsftSkipEx("device does not support node scope")
    try:
        caps = nl_shaper.cap_get({'ifindex': cfg.ifindex,
                                  'scope':'node'})
    except NlError as e:
        if e.error == 95:
            raise KsftSkipEx("node scope shapers not supported by the device")
        raise
    if not 'support-nesting' in caps:
        raise KsftSkipEx("device does not support node scope shapers nesting")

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

def queue_update(cfg, nl_shaper) -> None:
    if cfg.nr_queues < 4:
        raise KsftSkipEx(f"netdev does not have enough queues min 4 reported {cfg.nr_queues}")
    if not cfg.queues:
        raise KsftSkipEx("device does not support queue scope")

    for i in range(3):
        nl_shaper.set({'ifindex': cfg.ifindex,
                       'handle': {'scope': 'queue', 'id': i},
                       'metric': 'bps',
                       'bw-max': (i + 1) * 1000})
    # Delete a channel, with no shapers configured on top of the related
    # queue: no changes expected
    cmd(f"ethtool -L {cfg.dev['ifname']} {cfg.rx_type} 3", timeout=10)
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
    cmd(f"ethtool -L {cfg.dev['ifname']} {cfg.rx_type} 2", timeout=10)

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
    cmd(f"ethtool -L {cfg.dev['ifname']} {cfg.rx_type} {cfg.nr_queues}", timeout=10)
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

def main() -> None:
    with NetDrvEnv(__file__, queue_count=4) as cfg:
        cfg.queues = False
        cfg.netdev = False
        cfg.groups = False
        cfg.nr_queues = 0
        ksft_run([get_shapers,
                  get_caps,
                  set_qshapers,
                  del_qshapers,
                  set_nshapers,
                  del_nshapers,
                  basic_groups,
                  qgroups,
                  delegation,
                  queue_update], args=(cfg, NetshaperFamily()))
    ksft_exit()


if __name__ == "__main__":
    main()
