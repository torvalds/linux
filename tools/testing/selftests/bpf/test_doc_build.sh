#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

# Assume script is located under tools/testing/selftests/bpf/. We want to start
# build attempts from the top of kernel repository.
SCRIPT_REL_PATH=$(realpath --relative-to=$PWD $0)
SCRIPT_REL_DIR=$(dirname $SCRIPT_REL_PATH)
KDIR_ROOT_DIR=$(realpath $PWD/$SCRIPT_REL_DIR/../../../../)
cd $KDIR_ROOT_DIR

for tgt in docs docs-clean; do
	make -s -C $PWD/$SCRIPT_REL_DIR $tgt;
done
