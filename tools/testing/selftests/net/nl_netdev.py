#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import time
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_eq, ksft_ge, ksft_busy_wait
from lib.py import NetdevFamily, NetdevSimDev, ip


def empty_check(nf) -> None:
    devs = nf.dev_get({}, dump=True)
    ksft_ge(len(devs), 1)


def lo_check(nf) -> None:
    lo_info = nf.dev_get({"ifindex": 1})
    ksft_eq(len(lo_info['xdp-features']), 0)
    ksft_eq(len(lo_info['xdp-rx-metadata-features']), 0)


def napi_list_check(nf) -> None:
    with NetdevSimDev(queue_count=100) as nsimdev:
        nsim = nsimdev.nsims[0]

        ip(f"link set dev {nsim.ifname} up")

        napis = nf.napi_get({'ifindex': nsim.ifindex}, dump=True)
        ksft_eq(len(napis), 100)

        for q in [50, 0, 99]:
            for i in range(4):
                nsim.dfs_write("queue_reset", f"{q} {i}")
                napis = nf.napi_get({'ifindex': nsim.ifindex}, dump=True)
                ksft_eq(len(napis), 100,
                        comment=f"queue count after reset queue {q} mode {i}")


def nsim_rxq_reset_down(nf) -> None:
    """
    Test that the queue API supports resetting a queue
    while the interface is down. We should convert this
    test to testing real HW once more devices support
    queue API.
    """
    with NetdevSimDev(queue_count=4) as nsimdev:
        nsim = nsimdev.nsims[0]

        ip(f"link set dev {nsim.ifname} down")
        for i in [0, 2, 3]:
            nsim.dfs_write("queue_reset", f"1 {i}")


def page_pool_check(nf) -> None:
    with NetdevSimDev() as nsimdev:
        nsim = nsimdev.nsims[0]

        def up():
            ip(f"link set dev {nsim.ifname} up")

        def down():
            ip(f"link set dev {nsim.ifname} down")

        def get_pp():
            pp_list = nf.page_pool_get({}, dump=True)
            return [pp for pp in pp_list if pp.get("ifindex") == nsim.ifindex]

        # No page pools when down
        down()
        ksft_eq(len(get_pp()), 0)

        # Up, empty page pool appears
        up()
        pp_list = get_pp()
        ksft_ge(len(pp_list), 0)
        refs = sum([pp["inflight"] for pp in pp_list])
        ksft_eq(refs, 0)

        # Down, it disappears, again
        down()
        pp_list = get_pp()
        ksft_eq(len(pp_list), 0)

        # Up, allocate a page
        up()
        nsim.dfs_write("pp_hold", "y")
        pp_list = nf.page_pool_get({}, dump=True)
        refs = sum([pp["inflight"] for pp in pp_list if pp.get("ifindex") == nsim.ifindex])
        ksft_ge(refs, 1)

        # Now let's leak a page
        down()
        pp_list = get_pp()
        ksft_eq(len(pp_list), 1)
        refs = sum([pp["inflight"] for pp in pp_list])
        ksft_eq(refs, 1)
        attached = [pp for pp in pp_list if "detach-time" not in pp]
        ksft_eq(len(attached), 0)

        # New pp can get created, and we'll have two
        up()
        pp_list = get_pp()
        attached = [pp for pp in pp_list if "detach-time" not in pp]
        detached = [pp for pp in pp_list if "detach-time" in pp]
        ksft_eq(len(attached), 1)
        ksft_eq(len(detached), 1)

        # Free the old page and the old pp is gone
        nsim.dfs_write("pp_hold", "n")
        # Freeing check is once a second so we may need to retry
        ksft_busy_wait(lambda: len(get_pp()) == 1, deadline=2)

        # And down...
        down()
        ksft_eq(len(get_pp()), 0)

        # Last, leave the page hanging for destroy, nothing to check
        # we're trying to exercise the orphaning path in the kernel
        up()
        nsim.dfs_write("pp_hold", "y")


def main() -> None:
    nf = NetdevFamily()
    ksft_run([empty_check, lo_check, page_pool_check, napi_list_check,
              nsim_rxq_reset_down],
             args=(nf, ))
    ksft_exit()


if __name__ == "__main__":
    main()
