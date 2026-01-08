#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Toeplitz Rx hashing test:
 - rxhash (the hash value calculation itself);
 - RSS mapping from rxhash to rx queue;
 - RPS mapping from rxhash to cpu.
"""

import glob
import os
import socket
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import NetDrvEpEnv, EthtoolFamily, NetdevFamily
from lib.py import cmd, bkg, rand_port, defer
from lib.py import ksft_in
from lib.py import ksft_variants, KsftNamedVariant, KsftSkipEx, KsftFailEx

# "define" for the ID of the Toeplitz hash function
ETH_RSS_HASH_TOP = 1


def _check_rps_and_rfs_not_configured(cfg):
    """Verify that RPS is not already configured."""

    for rps_file in glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*/rps_cpus"):
        with open(rps_file, "r", encoding="utf-8") as fp:
            val = fp.read().strip()
            if set(val) - {"0", ","}:
                raise KsftSkipEx(f"RPS already configured on {rps_file}: {val}")

    rfs_file = "/proc/sys/net/core/rps_sock_flow_entries"
    with open(rfs_file, "r", encoding="utf-8") as fp:
        val = fp.read().strip()
        if val != "0":
            raise KsftSkipEx(f"RFS already configured {rfs_file}: {val}")


def _get_cpu_for_irq(irq):
    with open(f"/proc/irq/{irq}/smp_affinity_list", "r",
              encoding="utf-8") as fp:
        data = fp.read().strip()
        if "," in data or "-" in data:
            raise KsftFailEx(f"IRQ{irq} is not mapped to a single core: {data}")
        return int(data)


def _get_irq_cpus(cfg):
    """
    Read the list of IRQs for the device Rx queues.
    """
    queues = cfg.netnl.queue_get({"ifindex": cfg.ifindex}, dump=True)
    napis = cfg.netnl.napi_get({"ifindex": cfg.ifindex}, dump=True)

    # Remap into ID-based dicts
    napis = {n["id"]: n for n in napis}
    queues = {f"{q['type']}{q['id']}": q for q in queues}

    cpus = []
    for rx in range(9999):
        name = f"rx{rx}"
        if name not in queues:
            break
        cpus.append(_get_cpu_for_irq(napis[queues[name]["napi-id"]]["irq"]))

    return cpus


def _get_unused_cpus(cfg, count=2):
    """
    Get CPUs that are not used by Rx queues.
    Returns a list of at least 'count' CPU numbers.
    """

    # Get CPUs used by Rx queues
    rx_cpus = set(_get_irq_cpus(cfg))

    # Get total number of CPUs
    num_cpus = os.cpu_count()

    # Find unused CPUs
    unused_cpus = [cpu for cpu in range(num_cpus) if cpu not in rx_cpus]

    if len(unused_cpus) < count:
        raise KsftSkipEx(f"Need at {count} CPUs not used by Rx queues, found {len(unused_cpus)}")

    return unused_cpus[:count]


def _configure_rps(cfg, rps_cpus):
    """Configure RPS for all Rx queues."""

    mask = 0
    for cpu in rps_cpus:
        mask |= (1 << cpu)

    mask = hex(mask)

    # Set RPS bitmap for all rx queues
    for rps_file in glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*/rps_cpus"):
        with open(rps_file, "w", encoding="utf-8") as fp:
            # sysfs expects hex without '0x' prefix, toeplitz.c needs the prefix
            fp.write(mask[2:])

    return mask


def _send_traffic(cfg, proto_flag, ipver, port):
    """Send 20 packets of requested type."""

    # Determine protocol and IP version for socat
    if proto_flag == "-u":
        proto = "UDP"
    else:
        proto = "TCP"

    baddr = f"[{cfg.addr_v['6']}]" if ipver == "6" else cfg.addr_v["4"]

    # Run socat in a loop to send traffic periodically
    # Use sh -c with a loop similar to toeplitz_client.sh
    socat_cmd = f"""
    for i in `seq 20`; do
        echo "msg $i" | socat -{ipver} -t 0.1 - {proto}:{baddr}:{port};
        sleep 0.001;
    done
    """

    cmd(socat_cmd, shell=True, host=cfg.remote)


def _test_variants():
    for grp in ["", "rss", "rps"]:
        for l4 in ["tcp", "udp"]:
            for l3 in ["4", "6"]:
                name = f"{l4}_ipv{l3}"
                if grp:
                    name = f"{grp}_{name}"
                yield KsftNamedVariant(name, "-" + l4[0], l3, grp)


@ksft_variants(_test_variants())
def test(cfg, proto_flag, ipver, grp):
    """Run a single toeplitz test."""

    cfg.require_ipver(ipver)

    # Check that rxhash is enabled
    ksft_in("receive-hashing: on", cmd(f"ethtool -k {cfg.ifname}").stdout)

    rss = cfg.ethnl.rss_get({"header": {"dev-index": cfg.ifindex}})
    # Make sure NIC is configured to use Toeplitz hash, and no key xfrm.
    if rss.get('hfunc') != ETH_RSS_HASH_TOP or rss.get('input-xfrm'):
        cfg.ethnl.rss_set({"header": {"dev-index": cfg.ifindex},
                           "hfunc": ETH_RSS_HASH_TOP,
                           "input-xfrm": {}})
        defer(cfg.ethnl.rss_set, {"header": {"dev-index": cfg.ifindex},
                                  "hfunc": rss.get('hfunc'),
                                  "input-xfrm": rss.get('input-xfrm', {})
                                  })

    port = rand_port(socket.SOCK_DGRAM)

    toeplitz_path = cfg.test_dir / "toeplitz"
    rx_cmd = [
        str(toeplitz_path),
        "-" + ipver,
        proto_flag,
        "-d", str(port),
        "-i", cfg.ifname,
        "-T", "4000",
        "-s",
        "-v"
    ]

    if grp:
        _check_rps_and_rfs_not_configured(cfg)
    if grp == "rss":
        irq_cpus = ",".join([str(x) for x in _get_irq_cpus(cfg)])
        rx_cmd += ["-C", irq_cpus]
        ksft_pr(f"RSS using CPUs: {irq_cpus}")
    elif grp == "rps":
        # Get CPUs not used by Rx queues and configure them for RPS
        rps_cpus = _get_unused_cpus(cfg, count=2)
        rps_mask = _configure_rps(cfg, rps_cpus)
        defer(_configure_rps, cfg, [])
        rx_cmd += ["-r", rps_mask]
        ksft_pr(f"RPS using CPUs: {rps_cpus}, mask: {rps_mask}")

    # Run rx in background, it will exit once it has seen enough packets
    with bkg(" ".join(rx_cmd), ksft_ready=True, exit_wait=True) as rx_proc:
        while rx_proc.proc.poll() is None:
            _send_traffic(cfg, proto_flag, ipver, port)

    # Check rx result
    ksft_pr("Receiver output:")
    ksft_pr(rx_proc.stdout.strip().replace('\n', '\n# '))
    if rx_proc.stderr:
        ksft_pr(rx_proc.stderr.strip().replace('\n', '\n# '))


def main() -> None:
    """Ksft boilerplate main."""

    with NetDrvEpEnv(__file__) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()
        ksft_run(cases=[test], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
