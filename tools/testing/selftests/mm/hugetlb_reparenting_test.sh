#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

set -e

if [[ $(id -u) -ne 0 ]]; then
  echo "This test must be run as root. Skipping..."
  exit $ksft_skip
fi

usage_file=usage_in_bytes

if [[ "$1" == "-cgroup-v2" ]]; then
  cgroup2=1
  usage_file=current
fi


if [[ $cgroup2 ]]; then
  CGROUP_ROOT=$(mount -t cgroup2 | head -1 | awk -e '{print $3}')
  if [[ -z "$CGROUP_ROOT" ]]; then
    CGROUP_ROOT=/dev/cgroup/memory
    mount -t cgroup2 none $CGROUP_ROOT
    do_umount=1
  fi
  echo "+hugetlb +memory" >$CGROUP_ROOT/cgroup.subtree_control
else
  CGROUP_ROOT=$(mount -t cgroup | grep ",hugetlb" | awk -e '{print $3}')
  if [[ -z "$CGROUP_ROOT" ]]; then
    CGROUP_ROOT=/dev/cgroup/memory
    mount -t cgroup memory,hugetlb $CGROUP_ROOT
    do_umount=1
  fi
fi
MNT='/mnt/huge/'

function get_machine_hugepage_size() {
  hpz=$(grep -i hugepagesize /proc/meminfo)
  kb=${hpz:14:-3}
  mb=$(($kb / 1024))
  echo $mb
}

MB=$(get_machine_hugepage_size)

function cleanup() {
  echo cleanup
  set +e
  rm -rf "$MNT"/* 2>/dev/null
  umount "$MNT" 2>/dev/null
  rmdir "$MNT" 2>/dev/null
  rmdir "$CGROUP_ROOT"/a/b 2>/dev/null
  rmdir "$CGROUP_ROOT"/a 2>/dev/null
  rmdir "$CGROUP_ROOT"/test1 2>/dev/null
  echo 0 >/proc/sys/vm/nr_hugepages
  set -e
}

function assert_state() {
  local expected_a="$1"
  local expected_a_hugetlb="$2"
  local expected_b=""
  local expected_b_hugetlb=""

  if [ ! -z ${3:-} ] && [ ! -z ${4:-} ]; then
    expected_b="$3"
    expected_b_hugetlb="$4"
  fi
  local tolerance=$((5 * 1024 * 1024))

  local actual_a
  actual_a="$(cat "$CGROUP_ROOT"/a/memory.$usage_file)"
  if [[ $actual_a -lt $(($expected_a - $tolerance)) ]] ||
    [[ $actual_a -gt $(($expected_a + $tolerance)) ]]; then
    echo actual a = $((${actual_a%% *} / 1024 / 1024)) MB
    echo expected a = $((${expected_a%% *} / 1024 / 1024)) MB
    echo fail

    cleanup
    exit 1
  fi

  local actual_a_hugetlb
  actual_a_hugetlb="$(cat "$CGROUP_ROOT"/a/hugetlb.${MB}MB.$usage_file)"
  if [[ $actual_a_hugetlb -lt $(($expected_a_hugetlb - $tolerance)) ]] ||
    [[ $actual_a_hugetlb -gt $(($expected_a_hugetlb + $tolerance)) ]]; then
    echo actual a hugetlb = $((${actual_a_hugetlb%% *} / 1024 / 1024)) MB
    echo expected a hugetlb = $((${expected_a_hugetlb%% *} / 1024 / 1024)) MB
    echo fail

    cleanup
    exit 1
  fi

  if [[ -z "$expected_b" || -z "$expected_b_hugetlb" ]]; then
    return
  fi

  local actual_b
  actual_b="$(cat "$CGROUP_ROOT"/a/b/memory.$usage_file)"
  if [[ $actual_b -lt $(($expected_b - $tolerance)) ]] ||
    [[ $actual_b -gt $(($expected_b + $tolerance)) ]]; then
    echo actual b = $((${actual_b%% *} / 1024 / 1024)) MB
    echo expected b = $((${expected_b%% *} / 1024 / 1024)) MB
    echo fail

    cleanup
    exit 1
  fi

  local actual_b_hugetlb
  actual_b_hugetlb="$(cat "$CGROUP_ROOT"/a/b/hugetlb.${MB}MB.$usage_file)"
  if [[ $actual_b_hugetlb -lt $(($expected_b_hugetlb - $tolerance)) ]] ||
    [[ $actual_b_hugetlb -gt $(($expected_b_hugetlb + $tolerance)) ]]; then
    echo actual b hugetlb = $((${actual_b_hugetlb%% *} / 1024 / 1024)) MB
    echo expected b hugetlb = $((${expected_b_hugetlb%% *} / 1024 / 1024)) MB
    echo fail

    cleanup
    exit 1
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
    echo $$ >$CGROUP_ROOT/$cgroup/cgroup.procs
  else
    echo 0 >$CGROUP_ROOT/$cgroup/cpuset.mems
    echo 0 >$CGROUP_ROOT/$cgroup/cpuset.cpus
    echo $$ >"$CGROUP_ROOT/$cgroup/tasks"
  fi
  ./write_to_hugetlbfs -p "$path" -s "$size" -m 0 -o
  if [[ $cgroup2 ]]; then
    echo $$ >$CGROUP_ROOT/cgroup.procs
  else
    echo $$ >"$CGROUP_ROOT/tasks"
  fi
  echo
}

set -e

size=$((${MB} * 1024 * 1024 * 25)) # 50MB = 25 * 2MB hugepages.

cleanup

echo
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
echo
if [[ ! $cgroup2 ]]; then
  echo "Test parent and child hugetlb usage"
  setup

  echo write
  write_hugetlbfs a "$MNT"/test $size

  echo Assert memory charged correctly for parent use.
  assert_state 0 $size 0 0

  write_hugetlbfs a/b "$MNT"/test2 $size

  echo Assert memory charged correctly for child use.
  assert_state 0 $(($size * 2)) 0 $size

  rmdir "$CGROUP_ROOT"/a/b
  sleep 5
  echo Assert memory reparent correctly.
  assert_state 0 $(($size * 2))

  rm -rf "$MNT"/*
  umount "$MNT"
  echo Assert memory uncharged correctly.
  assert_state 0 0

  cleanup
fi

echo
echo
echo "Test child only hugetlb usage"
echo setup
setup

echo write
write_hugetlbfs a/b "$MNT"/test2 $size

echo Assert memory charged correctly for child only use.
assert_state 0 $(($size)) 0 $size

rmdir "$CGROUP_ROOT"/a/b
echo Assert memory reparent correctly.
assert_state 0 $size

rm -rf "$MNT"/*
umount "$MNT"
echo Assert memory uncharged correctly.
assert_state 0 0

cleanup

echo ALL PASS

umount $CGROUP_ROOT
rm -rf $CGROUP_ROOT
