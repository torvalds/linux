#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

# This is intended to be run on a virtio-net guest interface.
# The test binds the XDP socket to the interface without setting
# the fill ring to trigger delayed refill_work. This helps to
# make it easier to reproduce the deadlock when XDP program,
# XDP socket bind/unbind, rx ring resize race with refill_work on
# the buggy kernel.
#
# The Qemu command to setup virtio-net
# -netdev tap,id=hostnet1,vhost=on,script=no,downscript=no
# -device virtio-net-pci,netdev=hostnet1,iommu_platform=on,disable-legacy=on

from lib.py import ksft_exit, ksft_run
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import NetDrvEnv
from lib.py import bkg, ip, cmd, ethtool
import time

def _get_rx_ring_entries(cfg):
    output = ethtool(f"-g {cfg.ifname}", json=True)
    return output[0]["rx"]

def setup_xsk(cfg, xdp_queue_id = 0) -> bkg:
    # Probe for support
    xdp = cmd(f'{cfg.net_lib_dir / "xdp_helper"} - -', fail=False)
    if xdp.ret == 255:
        raise KsftSkipEx('AF_XDP unsupported')
    elif xdp.ret > 0:
        raise KsftFailEx('unable to create AF_XDP socket')

    try:
        return bkg(f'{cfg.net_lib_dir / "xdp_helper"} {cfg.ifindex} ' \
                   '{xdp_queue_id} -z', ksft_wait=3)
    except:
        raise KsftSkipEx('Failed to bind XDP socket in zerocopy.\n' \
                         'Please consider adding iommu_platform=on ' \
                         'when setting up virtio-net-pci')

def check_xdp_bind(cfg):
    with setup_xsk(cfg):
        ip(f"link set dev %s xdp obj %s sec xdp" %
           (cfg.ifname, cfg.net_lib_dir / "xdp_dummy.bpf.o"))
        ip(f"link set dev %s xdp off" % cfg.ifname)

def check_rx_resize(cfg):
    with setup_xsk(cfg):
        rx_ring = _get_rx_ring_entries(cfg)
        ethtool(f"-G %s rx %d" % (cfg.ifname, rx_ring // 2))
        ethtool(f"-G %s rx %d" % (cfg.ifname, rx_ring))

def main():
    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        ksft_run([check_xdp_bind, check_rx_resize],
                 args=(cfg, ))
    ksft_exit()

if __name__ == "__main__":
    main()
