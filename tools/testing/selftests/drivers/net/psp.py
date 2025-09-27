#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Test suite for PSP capable drivers."""

import errno

from lib.py import defer
from lib.py import ksft_run, ksft_exit
from lib.py import ksft_true, ksft_eq
from lib.py import KsftSkipEx
from lib.py import NetDrvEpEnv, PSPFamily, NlError

#
# Test case boiler plate
#

def _init_psp_dev(cfg):
    if not hasattr(cfg, 'psp_dev_id'):
        # Figure out which local device we are testing against
        for dev in cfg.pspnl.dev_get({}, dump=True):
            if dev['ifindex'] == cfg.ifindex:
                cfg.psp_info = dev
                cfg.psp_dev_id = cfg.psp_info['id']
                break
        else:
            raise KsftSkipEx("No PSP devices found")

    # Enable PSP if necessary
    cap = cfg.psp_info['psp-versions-cap']
    ena = cfg.psp_info['psp-versions-ena']
    if cap != ena:
        cfg.pspnl.dev_set({'id': cfg.psp_dev_id, 'psp-versions-ena': cap})
        defer(cfg.pspnl.dev_set, {'id': cfg.psp_dev_id,
                                  'psp-versions-ena': ena })

#
# Test cases
#

def dev_list_devices(cfg):
    """ Dump all devices """
    _init_psp_dev(cfg)

    devices = cfg.pspnl.dev_get({}, dump=True)

    found = False
    for dev in devices:
        found |= dev['id'] == cfg.psp_dev_id
    ksft_true(found)


def dev_get_device(cfg):
    """ Get the device we intend to use """
    _init_psp_dev(cfg)

    dev = cfg.pspnl.dev_get({'id': cfg.psp_dev_id})
    ksft_eq(dev['id'], cfg.psp_dev_id)


def dev_get_device_bad(cfg):
    """ Test getting device which doesn't exist """
    raised = False
    try:
        cfg.pspnl.dev_get({'id': 1234567})
    except NlError as e:
        ksft_eq(e.nl_msg.error, -errno.ENODEV)
        raised = True
    ksft_true(raised)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__) as cfg:
        cfg.pspnl = PSPFamily()

        ksft_run(globs=globals(), case_pfx={"dev_",}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
