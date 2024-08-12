#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Kprobe event VFS type argument
# requires: kprobe_events "%pd/%pD":README

: "Test argument %pd with name"
echo 'p:testprobe dput name=$arg1:%pd' > kprobe_events
echo 1 > events/kprobes/testprobe/enable
grep -q "1" events/kprobes/testprobe/enable
echo 0 > events/kprobes/testprobe/enable
grep "dput" trace | grep -q "enable"
echo "" > kprobe_events
echo "" > trace

: "Test argument %pd without name"
echo 'p:testprobe dput $arg1:%pd' > kprobe_events
echo 1 > events/kprobes/testprobe/enable
grep -q "1" events/kprobes/testprobe/enable
echo 0 > events/kprobes/testprobe/enable
grep "dput" trace | grep -q "enable"
echo "" > kprobe_events
echo "" > trace

: "Test argument %pD with name"
echo 'p:testprobe vfs_read name=$arg1:%pD' > kprobe_events
echo 1 > events/kprobes/testprobe/enable
grep -q "1" events/kprobes/testprobe/enable
echo 0 > events/kprobes/testprobe/enable
grep "vfs_read" trace | grep -q "enable"
echo "" > kprobe_events
echo "" > trace

: "Test argument %pD without name"
echo 'p:testprobe vfs_read $arg1:%pD' > kprobe_events
echo 1 > events/kprobes/testprobe/enable
grep -q "1"  events/kprobes/testprobe/enable
echo 0 > events/kprobes/testprobe/enable
grep "vfs_read" trace | grep -q "enable"
echo "" > kprobe_events
echo "" > trace
