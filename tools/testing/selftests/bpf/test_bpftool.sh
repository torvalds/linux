#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2020 SUSE LLC.

# 'make -C tools/testing/selftests/bpf install' will install to SCRIPT_DIR
SCRIPT_DIR=$(dirname $(realpath $0))

# 'make -C tools/testing/selftests/bpf' will install to BPFTOOL_INSTALL_PATH
BPFTOOL_INSTALL_PATH="$SCRIPT_DIR"/tools/sbin
export PATH=$SCRIPT_DIR:$BPFTOOL_INSTALL_PATH:$PATH
python3 -m unittest -v test_bpftool.TestBpftool
