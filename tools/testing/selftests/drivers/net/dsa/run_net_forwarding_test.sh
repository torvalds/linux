#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

libdir=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
testname=$(basename "${BASH_SOURCE[0]}")

source "$libdir"/forwarding.config
cd "$libdir"/../../../net/forwarding/ || exit 1
source "./$testname" "$@"
