#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2022 Red Hat, Inc.
# Author: Vladis Dronov <vdronoff@gmail.com>
#
# This script runs (via instmod) test-cipher.ko module which invokes
# generic and s390-native ChaCha20 encryprion algorithms with different
# size of data. Check 'dmesg' for results.
#
# The insmod error is expected:
# insmod: ERROR: could not insert module test_cipher.ko: Operation not permitted

lsmod | grep chacha | cut -f1 -d' ' | xargs rmmod
modprobe chacha_generic
modprobe chacha_s390

# run encryption for different data size, including whole block(s) +/- 1
insmod test_cipher.ko size=63
insmod test_cipher.ko size=64
insmod test_cipher.ko size=65
insmod test_cipher.ko size=127
insmod test_cipher.ko size=128
insmod test_cipher.ko size=129
insmod test_cipher.ko size=511
insmod test_cipher.ko size=512
insmod test_cipher.ko size=513
insmod test_cipher.ko size=4096
insmod test_cipher.ko size=65611
insmod test_cipher.ko size=6291456
insmod test_cipher.ko size=62914560

# print test logs
dmesg | tail -170
