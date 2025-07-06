#!/bin/sh
# perf record sample filtering (by BPF) tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup() {
  rm -f "${perfdata}"
  rm -f "${perfdata}".old
  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

test_bpf_filter_priv() {
  echo "Checking BPF-filter privilege"

  if ! perf record -e task-clock --filter 'period > 1' \
	  -o /dev/null --quiet true 2>&1
  then
    if [ "$(id -u)" != 0 ]
    then
      echo "try 'sudo perf record --setup-filter pin' first."
      echo "bpf-filter test [Skipped permission]"
      err=2
      return
    fi
    echo "bpf-filter test [Skipped missing BPF support]"
    err=2
    return
  fi
}

test_bpf_filter_basic() {
  echo "Basic bpf-filter test"

  if ! perf record -e task-clock -c 10000 --filter 'ip < 0xffffffff00000000' \
	  -o "${perfdata}" true 2> /dev/null
  then
    echo "Basic bpf-filter test [Failed record]"
    err=1
    return
  fi
  if perf script -i "${perfdata}" -F ip | grep 'ffffffff[0-9a-f]*'
  then
    if uname -r | grep -q ^6.2
    then
      echo "Basic bpf-filter test [Skipped unsupported kernel]"
      err=2
      return
    fi
    echo "Basic bpf-filter test [Failed invalid output]"
    err=1
    return
  fi
  echo "Basic bpf-filter test [Success]"
}

test_bpf_filter_fail() {
  echo "Failing bpf-filter test"

  # 'cpu' requires PERF_SAMPLE_CPU flag
  if ! perf record -e task-clock --filter 'cpu > 0' \
	  -o /dev/null true 2>&1 | grep -q PERF_SAMPLE_CPU
  then
    echo "Failing bpf-filter test [Failed forbidden CPU]"
    err=1
    return
  fi

  if ! perf record --sample-cpu -e task-clock --filter 'cpu > 0' \
	  -o /dev/null true 2>/dev/null
  then
    echo "Failing bpf-filter test [Failed should succeed]"
    err=1
    return
  fi

  echo "Failing bpf-filter test [Success]"
}

test_bpf_filter_group() {
  echo "Group bpf-filter test"

  if ! perf record -e task-clock --filter 'period > 1000, ip > 0' \
	  -o /dev/null true 2>/dev/null
  then
    echo "Group bpf-filter test [Failed should succeed]"
    err=1
    return
  fi

  if ! perf record -e task-clock --filter 'period > 1000 , cpu > 0 || ip > 0' \
	  -o /dev/null true 2>&1 | grep -q PERF_SAMPLE_CPU
  then
    echo "Group bpf-filter test [Failed forbidden CPU]"
    err=1
    return
  fi

  if ! perf record -e task-clock --filter 'period > 0 || code_pgsz > 4096' \
	  -o /dev/null true 2>&1 | grep -q PERF_SAMPLE_CODE_PAGE_SIZE
  then
    echo "Group bpf-filter test [Failed forbidden CODE_PAGE_SIZE]"
    err=1
    return
  fi

  echo "Group bpf-filter test [Success]"
}

test_bpf_filter_multi() {
  echo "Multiple bpf-filter test"

  if ! perf record -e task-clock --filter 'period > 100000' \
       -e page-faults --filter 'ip < 0xffffffff00000000' \
       -o "${perfdata}" true 2> /dev/null
  then
    echo "Multiple bpf-filter test [Failed record]"
    err=1
    return
  fi

  if ! perf script -i "${perfdata}" -F period,event | grep task-clock | \
	  awk '{ if (int($1) <= 100000) { print $0; exit(1); } }'
  then
    echo "Multiple bpf-filter test [Failed task-clock period]"
    err=1
    return
  fi

  if perf script -i "${perfdata}" -F event,ip | grep page-fault | \
	  grep 'ffffffff[0-9a-f]*'
  then
    echo "Multiple bpf-filter test [Failed page-faults ip]"
    err=1
    return
  fi

  echo "Multiple bpf-filter test [Success]"
}

test_bpf_filter_cgroup() {
  echo "Cgroup bpf-filter test"

  if ! perf record -e task-clock --filter 'cgroup == /' \
       -a --all-cgroups --synth=cgroup -o "${perfdata}" true 2> /dev/null
  then
    echo "Cgroup bpf-filter test [Skipped cgroup not supported]"
    return
  fi

  # 'cgroup' requires PERF_SAMPLE_CGROUP flag
  if ! perf record -e task-clock --filter 'cgroup == /' \
	  -o /dev/null true 2>&1 | grep -q PERF_SAMPLE_CGROUP
  then
    echo "Cgroup bpf-filter test [Failed CGROUP requires --all-cgroups]"
    err=1
    return
  fi

  if ! perf report -i "${perfdata}" -s cgroup -q | grep -q -F '100.00%'
  then
    echo "Cgroup bpf-filter test [Failed root cgroup does not have 100%]"
    err=1
    return
  fi

  echo "Cgroup bpf-filter test [Success]"
}

test_bpf_filter_priv

if [ $err = 0 ]; then
  test_bpf_filter_basic
fi

if [ $err = 0 ]; then
  test_bpf_filter_fail
fi

if [ $err = 0 ]; then
  test_bpf_filter_group
fi

if [ $err = 0 ]; then
  test_bpf_filter_multi
fi

if [ $err = 0 ]; then
  test_bpf_filter_cgroup
fi

cleanup
exit $err
