#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Author: Breno Leitao <leitao@debian.org>
"""
 This test aims to evaluate the netpoll polling mechanism (as in
 netpoll_poll_dev()). It presents a complex scenario where the network
 attempts to send a packet but fails, prompting it to poll the NIC from within
 the netpoll TX side.

 This has been a crucial path in netpoll that was previously untested. Jakub
 suggested using a single RX/TX queue, pushing traffic to the NIC, and then
 sending netpoll messages (via netconsole) to trigger the poll.

 In parallel, bpftrace is used to detect if netpoll_poll_dev() was called. If
 so, the test passes, otherwise it will be skipped. This test is very dependent on
 the driver and environment, given we are trying to trigger a tricky scenario.
"""

import errno
import logging
import os
import random
import string
import threading
import time
from typing import Optional

from lib.py import (
    bpftrace,
    CmdExitFailure,
    defer,
    ethtool,
    GenerateTraffic,
    ksft_exit,
    ksft_pr,
    ksft_run,
    KsftFailEx,
    KsftSkipEx,
    NetDrvEpEnv,
    KsftXfailEx,
)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(levelname)s - %(message)s",
)

NETCONSOLE_CONFIGFS_PATH: str = "/sys/kernel/config/netconsole"
NETCONS_REMOTE_PORT: int = 6666
NETCONS_LOCAL_PORT: int = 1514

# Max number of netcons messages to send. Each iteration will setup
# netconsole and send MAX_WRITES messages
ITERATIONS: int = 20
# Number of writes to /dev/kmsg per iteration
MAX_WRITES: int = 40
# MAPS contains the information coming from bpftrace it will have only one
# key: "hits", which tells the number of times netpoll_poll_dev() was called
MAPS: dict[str, int] = {}
# Thread to run bpftrace in parallel
BPF_THREAD: Optional[threading.Thread] = None
# Time bpftrace will be running in parallel.
BPFTRACE_TIMEOUT: int = 10


def ethtool_get_ringsize(interface_name: str) -> tuple[int, int]:
    """
    Read the ringsize using ethtool. This will be used to restore it after the test
    """
    try:
        ethtool_result = ethtool(f"-g {interface_name}", json=True)[0]
        rxs = ethtool_result["rx"]
        txs = ethtool_result["tx"]
    except (KeyError, IndexError) as exception:
        raise KsftSkipEx(
            f"Failed to read RX/TX ringsize: {exception}. Not going to mess with them."
        ) from exception

    return rxs, txs


def ethtool_set_ringsize(interface_name: str, ring_size: tuple[int, int]) -> bool:
    """Try to the number of RX and TX ringsize."""
    rxs = ring_size[0]
    txs = ring_size[1]

    logging.debug("Setting ring size to %d/%d", rxs, txs)
    try:
        ethtool(f"-G {interface_name} rx {rxs} tx {txs}")
    except CmdExitFailure:
        # This might fail on real device, retry with a higher value,
        # worst case, keep it as it is.
        return False

    return True


def ethtool_get_queues_cnt(interface_name: str) -> tuple[int, int, int]:
    """Read the number of RX, TX and combined queues using ethtool"""

    try:
        ethtool_result = ethtool(f"-l {interface_name}", json=True)[0]
        rxq = ethtool_result.get("rx", -1)
        txq = ethtool_result.get("tx", -1)
        combined = ethtool_result.get("combined", -1)

    except IndexError as exception:
        raise KsftSkipEx(
            f"Failed to read queues numbers: {exception}. Not going to mess with them."
        ) from exception

    return rxq, txq, combined


def ethtool_set_queues_cnt(interface_name: str, queues: tuple[int, int, int]) -> None:
    """Set the number of RX, TX and combined queues using ethtool"""
    rxq, txq, combined = queues

    cmdline = f"-L {interface_name}"

    if rxq != -1:
        cmdline += f" rx {rxq}"
    if txq != -1:
        cmdline += f" tx {txq}"
    if combined != -1:
        cmdline += f" combined {combined}"

    logging.debug("calling: ethtool %s", cmdline)

    try:
        ethtool(cmdline)
    except CmdExitFailure as exception:
        raise KsftSkipEx(
            f"Failed to configure RX/TX queues: {exception}. Ethtool not available?"
        ) from exception


def netcons_generate_random_target_name() -> str:
    """Generate a random target name starting with 'netcons'"""
    random_suffix = "".join(random.choices(string.ascii_lowercase + string.digits, k=8))
    return f"netcons_{random_suffix}"


def netcons_create_target(
    config_data: dict[str, str],
    target_name: str,
) -> None:
    """Create a netconsole dynamic target against the interfaces"""
    logging.debug("Using netconsole name: %s", target_name)
    try:
        os.makedirs(f"{NETCONSOLE_CONFIGFS_PATH}/{target_name}", exist_ok=True)
        logging.debug(
            "Created target directory: %s/%s", NETCONSOLE_CONFIGFS_PATH, target_name
        )
    except OSError as exception:
        if exception.errno != errno.EEXIST:
            raise KsftFailEx(
                f"Failed to create netconsole target directory: {exception}"
            ) from exception

    try:
        for key, value in config_data.items():
            path = f"{NETCONSOLE_CONFIGFS_PATH}/{target_name}/{key}"
            logging.debug("Writing %s to %s", key, path)
            with open(path, "w", encoding="utf-8") as file:
                # Always convert to string to write to file
                file.write(str(value))

        # Read all configuration values for debugging purposes
        for debug_key in config_data.keys():
            with open(
                f"{NETCONSOLE_CONFIGFS_PATH}/{target_name}/{debug_key}",
                "r",
                encoding="utf-8",
            ) as file:
                content = file.read()
                logging.debug(
                    "%s/%s/%s : %s",
                    NETCONSOLE_CONFIGFS_PATH,
                    target_name,
                    debug_key,
                    content.strip(),
                )

    except Exception as exception:
        raise KsftFailEx(
            f"Failed to configure netconsole target: {exception}"
        ) from exception


def netcons_configure_target(
    cfg: NetDrvEpEnv, interface_name: str, target_name: str
) -> None:
    """Configure netconsole on the interface with the given target name"""
    config_data = {
        "extended": "1",
        "dev_name": interface_name,
        "local_port": NETCONS_LOCAL_PORT,
        "remote_port": NETCONS_REMOTE_PORT,
        "local_ip": cfg.addr,
        "remote_ip": cfg.remote_addr,
        "remote_mac": "00:00:00:00:00:00",  # Not important for this test
        "enabled": "1",
    }

    netcons_create_target(config_data, target_name)
    logging.debug(
        "Created netconsole target: %s on interface %s", target_name, interface_name
    )


def netcons_delete_target(name: str) -> None:
    """Delete a netconsole dynamic target"""
    target_path = f"{NETCONSOLE_CONFIGFS_PATH}/{name}"
    try:
        if os.path.exists(target_path):
            os.rmdir(target_path)
    except OSError as exception:
        raise KsftFailEx(
            f"Failed to delete netconsole target: {exception}"
        ) from exception


def netcons_load_module() -> None:
    """Try to load the netconsole module"""
    os.system("modprobe netconsole")


def bpftrace_call() -> None:
    """Call bpftrace to find how many times netpoll_poll_dev() is called.
    Output is saved in the global variable `maps`"""

    # This is going to update the global variable, that will be seen by the
    # main function
    global MAPS  # pylint: disable=W0603

    # This will be passed to bpftrace as in bpftrace -e "expr"
    expr = "kprobe:netpoll_poll_dev { @hits = count(); }"

    MAPS = bpftrace(expr, timeout=BPFTRACE_TIMEOUT, json=True)
    logging.debug("BPFtrace output: %s", MAPS)


def bpftrace_start():
    """Start a thread to call `call_bpf` in a parallel thread"""
    global BPF_THREAD  # pylint: disable=W0603

    BPF_THREAD = threading.Thread(target=bpftrace_call)
    BPF_THREAD.start()
    if not BPF_THREAD.is_alive():
        raise KsftSkipEx("BPFtrace thread is not alive. Skipping test")


def bpftrace_stop() -> None:
    """Stop the bpftrace thread"""
    if BPF_THREAD:
        BPF_THREAD.join()


def bpftrace_any_hit(join: bool) -> bool:
    """Check if netpoll_poll_dev() was called by checking the global variable `maps`"""
    if not BPF_THREAD:
        raise KsftFailEx("BPFtrace didn't start")

    if BPF_THREAD.is_alive():
        if join:
            # Wait for bpftrace to finish
            BPF_THREAD.join()
        else:
            # bpftrace is still running, so, we will not check the result yet
            return False

    logging.debug("MAPS coming from bpftrace = %s", MAPS)
    if "hits" not in MAPS.keys():
        raise KsftFailEx(f"bpftrace failed to run!?: {MAPS}")

    logging.debug("Got a total of %d hits", MAPS["hits"])
    return MAPS["hits"] > 0


def do_netpoll_flush_monitored(cfg: NetDrvEpEnv, ifname: str, target_name: str) -> None:
    """Print messages to the console, trying to trigger a netpoll poll"""
    # Start bpftrace in parallel, so, it is watching
    # netpoll_poll_dev() while we are sending netconsole messages
    bpftrace_start()
    defer(bpftrace_stop)

    do_netpoll_flush(cfg, ifname, target_name)

    if bpftrace_any_hit(join=True):
        ksft_pr("netpoll_poll_dev() was called. Success")
        return

    raise KsftXfailEx("netpoll_poll_dev() was not called during the test...")


def do_netpoll_flush(cfg: NetDrvEpEnv, ifname: str, target_name: str) -> None:
    """Print messages to the console, trying to trigger a netpoll poll"""
    netcons_configure_target(cfg, ifname, target_name)
    retry = 0

    for i in range(int(ITERATIONS)):
        if not BPF_THREAD.is_alive() or bpftrace_any_hit(join=False):
            # bpftrace is done, stop sending messages
            break

        msg = f"netcons test #{i}"
        with open("/dev/kmsg", "w", encoding="utf-8") as kmsg:
            for j in range(MAX_WRITES):
                try:
                    kmsg.write(f"{msg}-{j}\n")
                except OSError as exception:
                    # in some cases, kmsg can be busy, so, we will retry
                    time.sleep(1)
                    retry += 1
                    if retry < 5:
                        logging.info("Failed to write to kmsg. Retrying")
                        # Just retry a few times
                        continue
                    raise KsftFailEx(
                        f"Failed to write to kmsg: {exception}"
                    ) from exception

        netcons_delete_target(target_name)
        netcons_configure_target(cfg, ifname, target_name)
        # If we sleep here, we will have a better chance of triggering
        # This number is based on a few tests I ran while developing this test
        time.sleep(0.4)


def configure_network(ifname: str) -> None:
    """Configure ring size and queue numbers"""

    # Set defined queues to 1 to force congestion
    prev_queues = ethtool_get_queues_cnt(ifname)
    logging.debug("RX/TX/combined queues: %s", prev_queues)
    # Only set the queues to 1 if they exists in the device. I.e, they are > 0
    ethtool_set_queues_cnt(ifname, tuple(1 if x > 0 else x for x in prev_queues))
    defer(ethtool_set_queues_cnt, ifname, prev_queues)

    # Try to set the ring size to some low value.
    # Do not fail if the hardware do not accepted desired values
    prev_ring_size = ethtool_get_ringsize(ifname)
    for size in [(1, 1), (128, 128), (256, 256)]:
        if ethtool_set_ringsize(ifname, size):
            # hardware accepted the desired ringsize
            logging.debug("Set RX/TX ringsize to: %s from %s", size, prev_ring_size)
            break
    defer(ethtool_set_ringsize, ifname, prev_ring_size)


def test_netpoll(cfg: NetDrvEpEnv) -> None:
    """
    Test netpoll by sending traffic to the interface and then sending
    netconsole messages to trigger a poll
    """

    ifname = cfg.ifname
    configure_network(ifname)
    target_name = netcons_generate_random_target_name()
    traffic = None

    try:
        traffic = GenerateTraffic(cfg)
        do_netpoll_flush_monitored(cfg, ifname, target_name)
    finally:
        if traffic:
            traffic.stop()

        # Revert RX/TX queues
        netcons_delete_target(target_name)


def test_check_dependencies() -> None:
    """Check if the dependencies are met"""
    if not os.path.exists(NETCONSOLE_CONFIGFS_PATH):
        raise KsftSkipEx(
            f"Directory {NETCONSOLE_CONFIGFS_PATH} does not exist. CONFIG_NETCONSOLE_DYNAMIC might not be set."  # pylint: disable=C0301
        )


def main() -> None:
    """Main function to run the test"""
    netcons_load_module()
    test_check_dependencies()
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(
            [test_netpoll],
            args=(cfg,),
        )
    ksft_exit()


if __name__ == "__main__":
    main()
