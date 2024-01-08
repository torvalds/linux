#!/bin/sh
# perf record sideband tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup()
{
    rm -rf ${perfdata}
    trap - EXIT TERM INT
}

trap_cleanup()
{
    cleanup
    exit 1
}
trap trap_cleanup EXIT TERM INT

can_cpu_wide()
{
    if ! perf record -o ${perfdata} -BN --no-bpf-event -C $1 true > /dev/null 2>&1
    then
        echo "record sideband test [Skipped cannot record cpu$1]"
        err=2
    fi

    rm -f ${perfdata}
    return $err
}

test_system_wide_tracking()
{
    # Need CPU 0 and CPU 1
    can_cpu_wide 0 || return 0
    can_cpu_wide 1 || return 0

    # Record on CPU 0 a task running on CPU 1
    perf record -BN --no-bpf-event -o ${perfdata} -C 0 -- taskset --cpu-list 1 true

    # Should get MMAP events from CPU 1
    mmap_cnt=`perf script -i ${perfdata} --show-mmap-events -C 1 2>/dev/null | grep MMAP | wc -l`

    if [ ${mmap_cnt} -gt 0 ] ; then
        return 0
    fi

    echo "Failed to record MMAP events on CPU 1 when tracing CPU 0"
    return 1
}

test_system_wide_tracking

cleanup
exit $err
