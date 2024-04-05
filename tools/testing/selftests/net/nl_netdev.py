#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_pr, ksft_eq, ksft_ge, NetdevFamily


def empty_check(nf) -> None:
    devs = nf.dev_get({}, dump=True)
    ksft_ge(len(devs), 1)


def lo_check(nf) -> None:
    lo_info = nf.dev_get({"ifindex": 1})
    ksft_eq(len(lo_info['xdp-features']), 0)
    ksft_eq(len(lo_info['xdp-rx-metadata-features']), 0)


def main() -> None:
    nf = NetdevFamily()
    ksft_run([empty_check, lo_check], args=(nf, ))


if __name__ == "__main__":
    main()
