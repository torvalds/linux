#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Change the ringbuffer size
# flags: instance

rb_size_test() {
ORIG=`cat buffer_size_kb`

expr $ORIG / 2 > buffer_size_kb

expr $ORIG \* 2 > buffer_size_kb

echo $ORIG > buffer_size_kb
}

rb_size_test

: "If per-cpu buffer is supported, imbalance it"
if [ -d per_cpu/cpu0 ]; then
  cd per_cpu/cpu0
  rb_size_test
fi
