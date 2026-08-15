#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Tests for the netdev netlink family.
"""

import errno
from os import system
from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_ge, ksft_ne, ksft_raises, ksft_busy_wait
from lib.py import NetdevFamily, NetdevSimDev, NlError, defer, ip


def empty_check(nf) -> None:
    devs = nf.dev_get({}, dump=True)
    ksft_ge(len(devs), 1)


def lo_check(nf) -> None:
    lo_info = nf.dev_get({"ifindex": 1})
    ksft_eq(len(lo_info['xdp-features']), 0)
    ksft_eq(len(lo_info['xdp-rx-metadata-features']), 0)


def dev_dump_reject_attr(nf) -> None:
    """Test that dev-get dump rejects attributes (no dump request policy)."""
    with ksft_raises(NlError) as cm:
        nf.dev_get({'ifindex': 1}, dump=True)
    ksft_eq(cm.exception.nl_msg.error, -errno.EINVAL)
    ksft_eq(cm.exception.nl_msg.extack['msg'], 'Unknown attribute type')
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.ifindex')


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

def napi_set_threaded(nf) -> None:
    """
    Test that verifies various cases of napi threaded
    set and unset at napi and device level.
    """
    with NetdevSimDev(queue_count=2) as nsimdev:
        nsim = nsimdev.nsims[0]

        ip(f"link set dev {nsim.ifname} up")

        napis = nf.napi_get({'ifindex': nsim.ifindex}, dump=True)
        ksft_eq(len(napis), 2)

        napi0_id = napis[0]['id']
        napi1_id = napis[1]['id']

        # set napi threaded and verify
        nf.napi_set({'id': napi0_id, 'threaded': "enabled"})
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "enabled")
        ksft_ne(napi0.get('pid'), None)

        # check it is not set for napi1
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "disabled")
        ksft_eq(napi1.get('pid'), None)

        ip(f"link set dev {nsim.ifname} down")
        ip(f"link set dev {nsim.ifname} up")

        # verify if napi threaded is still set
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "enabled")
        ksft_ne(napi0.get('pid'), None)

        # check it is still not set for napi1
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "disabled")
        ksft_eq(napi1.get('pid'), None)

        # unset napi threaded and verify
        nf.napi_set({'id': napi0_id, 'threaded': "disabled"})
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "disabled")
        ksft_eq(napi0.get('pid'), None)

        # set threaded at device level
        system(f"echo 1 > /sys/class/net/{nsim.ifname}/threaded")

        # check napi threaded is set for both napis
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "enabled")
        ksft_ne(napi0.get('pid'), None)
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "enabled")
        ksft_ne(napi1.get('pid'), None)

        # unset threaded at device level
        system(f"echo 0 > /sys/class/net/{nsim.ifname}/threaded")

        # check napi threaded is unset for both napis
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "disabled")
        ksft_eq(napi0.get('pid'), None)
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "disabled")
        ksft_eq(napi1.get('pid'), None)

        # set napi threaded for napi0
        nf.napi_set({'id': napi0_id, 'threaded': 1})
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "enabled")
        ksft_ne(napi0.get('pid'), None)

        # unset threaded at device level
        system(f"echo 0 > /sys/class/net/{nsim.ifname}/threaded")

        # check napi threaded is unset for both napis
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "disabled")
        ksft_eq(napi0.get('pid'), None)
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "disabled")
        ksft_eq(napi1.get('pid'), None)

def dev_set_threaded(nf) -> None:
    """
    Test that verifies various cases of napi threaded
    set and unset at device level using sysfs.
    """
    with NetdevSimDev(queue_count=2) as nsimdev:
        nsim = nsimdev.nsims[0]

        ip(f"link set dev {nsim.ifname} up")

        napis = nf.napi_get({'ifindex': nsim.ifindex}, dump=True)
        ksft_eq(len(napis), 2)

        napi0_id = napis[0]['id']
        napi1_id = napis[1]['id']

        # set threaded
        system(f"echo 1 > /sys/class/net/{nsim.ifname}/threaded")

        # check napi threaded is set for both napis
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "enabled")
        ksft_ne(napi0.get('pid'), None)
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "enabled")
        ksft_ne(napi1.get('pid'), None)

        # unset threaded
        system(f"echo 0 > /sys/class/net/{nsim.ifname}/threaded")

        # check napi threaded is unset for both napis
        napi0 = nf.napi_get({'id': napi0_id})
        ksft_eq(napi0['threaded'], "disabled")
        ksft_eq(napi0.get('pid'), None)
        napi1 = nf.napi_get({'id': napi1_id})
        ksft_eq(napi1['threaded'], "disabled")
        ksft_eq(napi1.get('pid'), None)

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


def page_pool_dump_ifindex(nf) -> None:
    """Test page pool dump filtering by ifindex."""
    nsimdev1 = NetdevSimDev(queue_count=3)
    rm_nsim1 = defer(nsimdev1.remove)
    nsimdev2 = NetdevSimDev(queue_count=5)
    defer(nsimdev2.remove)

    nsim1 = nsimdev1.nsims[0]
    nsim2 = nsimdev2.nsims[0]

    ip(f"link set dev {nsim1.ifname} up")
    ip(f"link set dev {nsim2.ifname} up")

    # Unfiltered dump should have pools from both devices
    all_pp = nf.page_pool_get({}, dump=True)
    pp1_all = [pp for pp in all_pp
               if pp.get("ifindex") == nsim1.ifindex]
    pp2_all = [pp for pp in all_pp
               if pp.get("ifindex") == nsim2.ifindex]
    ksft_ge(len(pp1_all), 1)
    ksft_ge(len(pp2_all), 1)

    # Filtered dump should only return pools for that device
    pp1_flt = nf.page_pool_get({'ifindex': nsim1.ifindex}, dump=True)
    ksft_eq(pp1_flt, pp1_all)

    pp2_flt = nf.page_pool_get({'ifindex': nsim2.ifindex}, dump=True)
    ksft_eq(pp2_flt, pp2_all)

    # Non-existent ifindex should return empty dump
    pp_none = nf.page_pool_get({'ifindex': 12345678}, dump=True)
    ksft_eq(len(pp_none), 0)

    # Device down - no pools for that ifindex
    ip(f"link set dev {nsim1.ifname} down")
    pp1_down = nf.page_pool_get({'ifindex': nsim1.ifindex}, dump=True)
    ksft_eq(len(pp1_down), 0)

    # Remove device, dump by its old ifindex should return empty
    old_ifindex = nsim1.ifindex
    rm_nsim1.exec()
    pp1_gone = nf.page_pool_get({'ifindex': old_ifindex}, dump=True)
    ksft_eq(len(pp1_gone), 0)


def page_pool_ifindex_leak_check(nf) -> None:
    """Test that zombie page pools don't show up under the original ifindex."""
    nsimdev = NetdevSimDev()
    rm_nsim = defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]

    ip(f"link set dev {nsim.ifname} up")
    nsim.dfs_write("pp_hold", "y")

    pp_up = nf.page_pool_get({'ifindex': nsim.ifindex}, dump=True)
    ksft_ge(len(pp_up), 1)

    # Remove device with leaked page - pool becomes zombie (orphaned to lo)
    old_ifindex = nsim.ifindex
    rm_nsim.exec()

    # Zombie pool should NOT appear under the original device
    pp_down = nf.page_pool_get({'ifindex': old_ifindex}, dump=True)
    ksft_eq(len(pp_down), 0)

    # But it should appear in an unfiltered dump (under loopback)
    pp_all = nf.page_pool_get({}, dump=True)
    orphans = [pp for pp in pp_all
               if "detach-time" in pp and "ifindex" not in pp]
    ksft_ge(len(orphans), 1)


def page_pool_stats_ifindex_check(nf) -> None:
    """Test page pool stats dump filtering by ifindex."""
    nsimdev1 = NetdevSimDev(queue_count=3)
    defer(nsimdev1.remove)
    nsimdev2 = NetdevSimDev(queue_count=5)
    defer(nsimdev2.remove)

    nsim1 = nsimdev1.nsims[0]
    nsim2 = nsimdev2.nsims[0]

    ip(f"link set dev {nsim1.ifname} up")
    ip(f"link set dev {nsim2.ifname} up")

    # Unfiltered stats dump
    all_stats = nf.page_pool_stats_get({}, dump=True)
    s1_all = [s for s in all_stats
              if s.get("info", {}).get("ifindex") == nsim1.ifindex]
    s2_all = [s for s in all_stats
              if s.get("info", {}).get("ifindex") == nsim2.ifindex]
    ksft_ge(len(s1_all), 1)
    ksft_ge(len(s2_all), 1)

    # Filtered stats dump
    s1_flt = nf.page_pool_stats_get({'info': {'ifindex': nsim1.ifindex}},
                                    dump=True)
    ksft_eq(s1_flt, s1_all)

    # Non-existent ifindex should return empty
    s_none = nf.page_pool_stats_get({'info': {'ifindex': 12345678}}, dump=True)
    ksft_eq(len(s_none), 0)

    # info.id should be rejected for stats dump
    with ksft_raises(NlError) as cm:
        nf.page_pool_stats_get({'info': {'id': s1_all[0]['info']['id']}},
                               dump=True)
    ksft_eq(cm.exception.nl_msg.error, -errno.EINVAL)
    ksft_eq(cm.exception.nl_msg.extack['bad-attr'], '.info.id')


def main() -> None:
    """ Ksft boiler plate main """
    nf = NetdevFamily()
    ksft_run([empty_check,
              lo_check,
              dev_dump_reject_attr,
              napi_list_check,
              napi_set_threaded,
              dev_set_threaded,
              nsim_rxq_reset_down,
              page_pool_check,
              page_pool_dump_ifindex,
              page_pool_ifindex_leak_check,
              page_pool_stats_ifindex_check
              ],
             args=(nf, ))
    ksft_exit()


if __name__ == "__main__":
    main()
