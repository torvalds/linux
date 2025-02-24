#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_ge, ksft_eq
from lib.py import KsftSkipEx
from lib.py import ksft_disruptive
from lib.py import EthtoolFamily, NetdevFamily
from lib.py import NetDrvEnv
from lib.py import cmd, ip, defer


def read_affinity(irq) -> str:
    with open(f'/proc/irq/{irq}/smp_affinity', 'r') as fp:
        return fp.read().lstrip("0,").strip()


def write_affinity(irq, what) -> str:
    if what != read_affinity(irq):
        with open(f'/proc/irq/{irq}/smp_affinity', 'w') as fp:
            fp.write(what)


def check_irqs_reported(cfg) -> None:
    """ Check that device reports IRQs for NAPI instances """
    napis = cfg.netnl.napi_get({"ifindex": cfg.ifindex}, dump=True)
    irqs = sum(['irq' in x for x in napis])

    ksft_ge(irqs, 1)
    ksft_eq(irqs, len(napis))


def _check_reconfig(cfg, reconfig_cb) -> None:
    napis = cfg.netnl.napi_get({"ifindex": cfg.ifindex}, dump=True)
    for n in reversed(napis):
        if 'irq' in n:
            break
    else:
        raise KsftSkipEx(f"Device has no NAPI with IRQ attribute (#napis: {len(napis)}")

    old = read_affinity(n['irq'])
    # pick an affinity that's not the current one
    new = "3" if old != "3" else "5"
    write_affinity(n['irq'], new)
    defer(write_affinity, n['irq'], old)

    reconfig_cb(cfg)

    ksft_eq(read_affinity(n['irq']), new, comment="IRQ affinity changed after reconfig")


def check_reconfig_queues(cfg) -> None:
    def reconfig(cfg) -> None:
        channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
        if channels['combined-count'] == 0:
            rx_type = 'rx'
        else:
            rx_type = 'combined'
        cur_queue_cnt = channels[f'{rx_type}-count']
        max_queue_cnt = channels[f'{rx_type}-max']

        cmd(f"ethtool -L {cfg.ifname} {rx_type} 1")
        cmd(f"ethtool -L {cfg.ifname} {rx_type} {max_queue_cnt}")
        cmd(f"ethtool -L {cfg.ifname} {rx_type} {cur_queue_cnt}")

    _check_reconfig(cfg, reconfig)


def check_reconfig_xdp(cfg) -> None:
    def reconfig(cfg) -> None:
        ip(f"link set dev %s xdp obj %s sec xdp" %
            (cfg.ifname, cfg.rpath("xdp_dummy.bpf.o")))
        ip(f"link set dev %s xdp off" % cfg.ifname)

    _check_reconfig(cfg, reconfig)


@ksft_disruptive
def check_down(cfg) -> None:
    def reconfig(cfg) -> None:
        ip("link set dev %s down" % cfg.ifname)
        ip("link set dev %s up" % cfg.ifname)

    _check_reconfig(cfg, reconfig)


def main() -> None:
    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()

        ksft_run([check_irqs_reported, check_reconfig_queues,
                  check_reconfig_xdp, check_down],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
