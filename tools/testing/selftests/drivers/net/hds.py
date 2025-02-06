#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import errno
from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_raises, KsftSkipEx
from lib.py import EthtoolFamily, NlError
from lib.py import NetDrvEnv

def get_hds(cfg, netnl) -> None:
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'tcp-data-split' not in rings:
        raise KsftSkipEx('tcp-data-split not supported by device')

def get_hds_thresh(cfg, netnl) -> None:
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'hds-thresh' not in rings:
        raise KsftSkipEx('hds-thresh not supported by device')

def set_hds_enable(cfg, netnl) -> None:
    try:
        netnl.rings_set({'header': {'dev-index': cfg.ifindex}, 'tcp-data-split': 'enabled'})
    except NlError as e:
        if e.error == errno.EINVAL:
            raise KsftSkipEx("disabling of HDS not supported by the device")
        elif e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("ring-set not supported by the device")
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'tcp-data-split' not in rings:
        raise KsftSkipEx('tcp-data-split not supported by device')

    ksft_eq('enabled', rings['tcp-data-split'])

def set_hds_disable(cfg, netnl) -> None:
    try:
        netnl.rings_set({'header': {'dev-index': cfg.ifindex}, 'tcp-data-split': 'disabled'})
    except NlError as e:
        if e.error == errno.EINVAL:
            raise KsftSkipEx("disabling of HDS not supported by the device")
        elif e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("ring-set not supported by the device")
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'tcp-data-split' not in rings:
        raise KsftSkipEx('tcp-data-split not supported by device')

    ksft_eq('disabled', rings['tcp-data-split'])

def set_hds_thresh_zero(cfg, netnl) -> None:
    try:
        netnl.rings_set({'header': {'dev-index': cfg.ifindex}, 'hds-thresh': 0})
    except NlError as e:
        if e.error == errno.EINVAL:
            raise KsftSkipEx("hds-thresh-set not supported by the device")
        elif e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("ring-set not supported by the device")
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'hds-thresh' not in rings:
        raise KsftSkipEx('hds-thresh not supported by device')

    ksft_eq(0, rings['hds-thresh'])

def set_hds_thresh_max(cfg, netnl) -> None:
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'hds-thresh' not in rings:
        raise KsftSkipEx('hds-thresh not supported by device')
    try:
        netnl.rings_set({'header': {'dev-index': cfg.ifindex}, 'hds-thresh': rings['hds-thresh-max']})
    except NlError as e:
        if e.error == errno.EINVAL:
            raise KsftSkipEx("hds-thresh-set not supported by the device")
        elif e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("ring-set not supported by the device")
    rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    ksft_eq(rings['hds-thresh'], rings['hds-thresh-max'])

def set_hds_thresh_gt(cfg, netnl) -> None:
    try:
        rings = netnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    except NlError as e:
        raise KsftSkipEx('ring-get not supported by device')
    if 'hds-thresh' not in rings:
        raise KsftSkipEx('hds-thresh not supported by device')
    if 'hds-thresh-max' not in rings:
        raise KsftSkipEx('hds-thresh-max not defined by device')
    hds_gt = rings['hds-thresh-max'] + 1
    with ksft_raises(NlError) as e:
        netnl.rings_set({'header': {'dev-index': cfg.ifindex}, 'hds-thresh': hds_gt})
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)

def main() -> None:
    with NetDrvEnv(__file__, queue_count=3) as cfg:
        ksft_run([get_hds,
                  get_hds_thresh,
                  set_hds_disable,
                  set_hds_enable,
                  set_hds_thresh_zero,
                  set_hds_thresh_max,
                  set_hds_thresh_gt],
                 args=(cfg, EthtoolFamily()))
    ksft_exit()

if __name__ == "__main__":
    main()
