#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Fprobe event VFS type argument
# requires: dynamic_events "%pd/%pD":README "f[:[<group>/][<event>]] <func-name>[%return] [<args>]":README


: "Test argument %pd with name for fprobe"
echo 'f:testprobe dput name=$arg1:%pd' > dynamic_events
echo 1 > events/fprobes/testprobe/enable
grep -q "1" events/fprobes/testprobe/enable
echo 0 > events/fprobes/testprobe/enable
grep "dput" trace | grep -q "enable"
echo "" > dynamic_events
echo "" > trace

: "Test argument %pd without name for fprobe"
echo 'f:testprobe dput $arg1:%pd' > dynamic_events
echo 1 > events/fprobes/testprobe/enable
grep -q "1" events/fprobes/testprobe/enable
echo 0 > events/fprobes/testprobe/enable
grep "dput" trace | grep -q "enable"
echo "" > dynamic_events
echo "" > trace

: "Test argument %pD with name for fprobe"
echo 'f:testprobe vfs_read name=$arg1:%pD' > dynamic_events
echo 1 > events/fprobes/testprobe/enable
grep -q "1" events/fprobes/testprobe/enable
echo 0 > events/fprobes/testprobe/enable
grep "vfs_read" trace | grep -q "enable"
echo "" > dynamic_events
echo "" > trace

: "Test argument %pD without name for fprobe"
echo 'f:testprobe vfs_read $arg1:%pD' > dynamic_events
echo 1 > events/fprobes/testprobe/enable
grep -q "1"  events/fprobes/testprobe/enable
echo 0 > events/fprobes/testprobe/enable
grep "vfs_read" trace | grep -q "enable"
echo "" > dynamic_events
echo "" > trace
