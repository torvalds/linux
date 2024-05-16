#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Collabora Ltd
#
# This script tests whether the rust sample modules can
# be added and removed correctly.
#
DIR="$(dirname "$(readlink -f "$0")")"

KTAP_HELPERS="${DIR}/../kselftest/ktap_helpers.sh"
if [ -e "$KTAP_HELPERS" ]; then
    source "$KTAP_HELPERS"
else
    echo "$KTAP_HELPERS file not found [SKIP]"
    exit 4
fi

rust_sample_modules=("rust_minimal" "rust_print")

ktap_print_header

for sample in "${rust_sample_modules[@]}"; do
    if ! /sbin/modprobe -n -q "$sample"; then
        ktap_skip_all "module $sample is not found in /lib/modules/$(uname -r)"
        exit "$KSFT_SKIP"
    fi
done

ktap_set_plan "${#rust_sample_modules[@]}"

for sample in "${rust_sample_modules[@]}"; do
    if /sbin/modprobe -q "$sample"; then
        /sbin/modprobe -q -r "$sample"
        ktap_test_pass "$sample"
    else
        ktap_test_fail "$sample"
    fi
done

ktap_finished
