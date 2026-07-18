#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

set -e

if [[ $(id -u) -ne 0 ]]; then
  echo "This test must be run as root. Skipping..."
  exit $ksft_skip
fi

nr_hugepgs=$(cat /proc/sys/vm/nr_hugepages)
trap 'echo "$nr_hugepgs" > /proc/sys/vm/nr_hugepages' EXIT INT TERM

usage_file=usage_in_bytes

if [[ "$1" == "-cgroup-v2" ]]; then
  cgroup2=1
  usage_file=current
fi


if [[ $cgroup2 ]]; then
  CGROUP_ROOT=$(mount -t cgroup2 | head -1 | awk '{print $3}')
  if [[ -z "$CGROUP_ROOT" ]]; then
    CGROUP_ROOT=$(mktemp -d)
    mount -t cgroup2 none $CGROUP_ROOT
    do_umount=1
  fi
  echo "+hugetlb +memory" >$CGROUP_ROOT/cgroup.subtree_control
else
  CGROUP_ROOT=$(mount -t cgroup | grep ",hugetlb" | awk '{print $3}')
  if [[ -z "$CGROUP_ROOT" ]]; then
    CGROUP_ROOT=/dev/cgroup/memory
    mount -t cgroup memory,hugetlb $CGROUP_ROOT
    do_umount=1
  fi
fi
MNT='/mnt/huge'

function get_machine_hugepage_size() {
  hpz=$(grep -i hugepagesize /proc/meminfo)
  kb=${hpz:14:-3}
  mb=$(($kb / 1024))
  echo $mb
}

MB=$(get_machine_hugepage_size)
if (( MB >= 1024 )); then
  UNIT="GB"
  MB_DISPLAY=$((MB / 1024))
else
  UNIT="MB"
  MB_DISPLAY=$MB
fi

function cleanup() {
  echo cleanup
  set +e
  rm -rf "$MNT"/* 2>/dev/null
  umount "$MNT" 2>/dev/null
  rmdir "$MNT" 2>/dev/null
  rmdir "$CGROUP_ROOT"/a/b 2>/dev/null
  rmdir "$CGROUP_ROOT"/a 2>/dev/null
  rmdir "$CGROUP_ROOT"/test1 2>/dev/null
  set -e
}

function assert_with_retry() {
  local actual_path="$1"
  local expected="$2"
  local tolerance=$((7 * 1024 * 1024))
  local timeout=20
  local interval=1
  local start_time
  local now
  local elapsed
  local actual

  start_time=$(date +%s)

  while true; do
    actual="$(cat "$actual_path")"

    if [[ $actual -ge $(($expected - $tolerance)) ]] &&
        [[ $actual -le $(($expected + $tolerance)) ]]; then
      return 0
    fi

    now=$(date +%s)
    elapsed=$((now - start_time))

    if [[ $elapsed -ge $timeout ]]; then
      echo "actual = $((${actual%% *} / 1024 / 1024)) MB"
      echo "expected = $((${expected%% *} / 1024 / 1024)) MB"
      echo FAIL
      cleanup
      exit 1
    fi

    sleep $interval
  done
}

function assert_state() {
  local expected_a_hugetlb="$1"
  local expected_b_hugetlb=""

  if [ ! -z ${2:-} ]; then
    expected_b_hugetlb="$2"
  fi

  assert_with_retry \
	  "$CGROUP_ROOT/a/hugetlb.${MB_DISPLAY}${UNIT}.$usage_file" "$expected_a_hugetlb"

  if [[ -n "$expected_b_hugetlb" ]]; then
    assert_with_retry \
	  "$CGROUP_ROOT/a/b/hugetlb.${MB_DISPLAY}${UNIT}.$usage_file" "$expected_b_hugetlb"
  fi
}

function setup() {
  echo 100 >/proc/sys/vm/nr_hugepages
  mkdir "$CGROUP_ROOT"/a
  sleep 1
  if [[ $cgroup2 ]]; then
    echo "+hugetlb +memory" >$CGROUP_ROOT/a/cgroup.subtree_control
  else
    echo 0 >$CGROUP_ROOT/a/cpuset.mems
    echo 0 >$CGROUP_ROOT/a/cpuset.cpus
  fi

  mkdir "$CGROUP_ROOT"/a/b

  if [[ ! $cgroup2 ]]; then
    echo 0 >$CGROUP_ROOT/a/b/cpuset.mems
    echo 0 >$CGROUP_ROOT/a/b/cpuset.cpus
  fi

  mkdir -p "$MNT"
  mount -t hugetlbfs none "$MNT"
}

write_hugetlbfs() {
  local cgroup="$1"
  local path="$2"
  local size="$3"

  if [[ $cgroup2 ]]; then
    cg_file="$CGROUP_ROOT/$cgroup/cgroup.procs"
  else
    echo 0 >$CGROUP_ROOT/$cgroup/cpuset.mems
    echo 0 >$CGROUP_ROOT/$cgroup/cpuset.cpus
    cg_file="$CGROUP_ROOT/$cgroup/tasks"
  fi

  # Spawn helper to join cgroup before exec to ensure correct cgroup accounting
  bash -c 'echo $$ > "$1"; exec ./write_to_hugetlbfs -p "$2" -s "$3" -m 0 -o' _ \
	  "$cg_file" "$path" "$size" & pid=$!
  wait "$pid"
  echo
}

set -e

size=$((${MB} * 1024 * 1024 * 25)) # 50MB = 25 * 2MB hugepages.

cleanup

echo
echo Test charge, rmdir, uncharge
setup
echo mkdir
mkdir $CGROUP_ROOT/test1

echo write
write_hugetlbfs test1 "$MNT"/test $size

echo rmdir
rmdir $CGROUP_ROOT/test1
mkdir $CGROUP_ROOT/test1

echo uncharge
rm -rf /mnt/huge/*

cleanup

echo done
echo
if [[ ! $cgroup2 ]]; then
  echo "Test parent and child hugetlb usage"
  setup

  echo write
  write_hugetlbfs a "$MNT"/test $size

  echo Assert memory charged correctly for parent use.
  assert_state $size 0

  write_hugetlbfs a/b "$MNT"/test2 $size

  echo Assert memory charged correctly for child use.
  assert_state $(($size * 2)) $size

  rmdir "$CGROUP_ROOT"/a/b
  echo Assert memory reparent correctly.
  assert_state $(($size * 2))

  rm -rf "$MNT"/*
  umount "$MNT"
  echo Assert memory uncharged correctly.
  assert_state 0

  cleanup
fi

echo
echo "Test child only hugetlb usage"
echo setup
setup

echo write
write_hugetlbfs a/b "$MNT"/test2 $size

echo Assert memory charged correctly for child only use.
assert_state $(($size)) $size

rmdir "$CGROUP_ROOT"/a/b
echo Assert memory reparent correctly.
assert_state $size

rm -rf "$MNT"/*
umount "$MNT"
echo Assert memory uncharged correctly.
assert_state 0

cleanup

echo ALL PASS

if [[ $do_umount ]]; then
  umount $CGROUP_ROOT
  rm -rf $CGROUP_ROOT
fi

