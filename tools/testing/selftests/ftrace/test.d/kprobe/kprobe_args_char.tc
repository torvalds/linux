#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe event char type argument
# requires: kprobe_events available_filter_functions

case `uname -m` in
x86_64)
  ARG1=%di
;;
i[3456]86)
  ARG1=%ax
;;
aarch64)
  ARG1=%x0
;;
arm*)
  ARG1=%r0
;;
ppc64*)
  ARG1=%r3
;;
ppc*)
  ARG1=%r3
;;
s390*)
  ARG1=%r2
;;
mips*)
  ARG1=%r4
;;
loongarch*)
  ARG1=%r4
;;
riscv*)
  ARG1=%a0
;;
*)
  echo "Please implement other architecture here"
  exit_untested
esac

: "Test get argument (1)"
if grep -q eventfs_create_dir available_filter_functions; then
  DIR_NAME="eventfs_create_dir"
elif grep -q eventfs_add_dir available_filter_functions; then
  DIR_NAME="eventfs_add_dir"
else
  DIR_NAME="tracefs_create_dir"
fi
echo "p:testprobe ${DIR_NAME} arg1=+0(${ARG1}):char" > kprobe_events
echo 1 > events/kprobes/testprobe/enable
echo "p:test $FUNCTION_FORK" >> kprobe_events
grep -qe "testprobe.* arg1='t'" trace

echo 0 > events/kprobes/testprobe/enable
: "Test get argument (2)"
echo "p:testprobe ${DIR_NAME} arg1=+0(${ARG1}):char arg2=+0(${ARG1}):char[4]" > kprobe_events
echo 1 > events/kprobes/testprobe/enable
echo "p:test $FUNCTION_FORK" >> kprobe_events
grep -qe "testprobe.* arg1='t' arg2={'t','e','s','t'}" trace
