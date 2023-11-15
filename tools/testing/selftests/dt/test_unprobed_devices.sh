#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Collabora Ltd
#
# Based on Frank Rowand's dt_stat script.
#
# This script tests for devices that were declared on the Devicetree and are
# expected to bind to a driver, but didn't.
#
# To achieve this, two lists are used:
# * a list of the compatibles that can be matched by a Devicetree node
# * a list of compatibles that should be ignored
#

DIR="$(dirname $(readlink -f "$0"))"

source "${DIR}"/ktap_helpers.sh

PDT=/proc/device-tree/
COMPAT_LIST="${DIR}"/compatible_list
IGNORE_LIST="${DIR}"/compatible_ignore_list

KSFT_PASS=0
KSFT_FAIL=1
KSFT_SKIP=4

ktap_print_header

if [[ ! -d "${PDT}" ]]; then
	ktap_skip_all "${PDT} doesn't exist."
	exit "${KSFT_SKIP}"
fi

nodes_compatible=$(
	for node_compat in $(find ${PDT} -name compatible); do
		node=$(dirname "${node_compat}")
		# Check if node is available
		if [[ -e "${node}"/status ]]; then
			status=$(tr -d '\000' < "${node}"/status)
			[[ "${status}" != "okay" && "${status}" != "ok" ]] && continue
		fi
		echo "${node}" | sed -e 's|\/proc\/device-tree||'
	done | sort
	)

nodes_dev_bound=$(
	IFS=$'\n'
	for uevent in $(find /sys/devices -name uevent); do
		if [[ -d "$(dirname "${uevent}")"/driver ]]; then
			grep '^OF_FULLNAME=' "${uevent}" | sed -e 's|OF_FULLNAME=||'
		fi
	done
	)

num_tests=$(echo ${nodes_compatible} | wc -w)
ktap_set_plan "${num_tests}"

retval="${KSFT_PASS}"
for node in ${nodes_compatible}; do
	if ! echo "${nodes_dev_bound}" | grep -E -q "(^| )${node}( |\$)"; then
		compatibles=$(tr '\000' '\n' < "${PDT}"/"${node}"/compatible)

		for compatible in ${compatibles}; do
			if grep -x -q "${compatible}" "${IGNORE_LIST}"; then
				continue
			fi

			if grep -x -q "${compatible}" "${COMPAT_LIST}"; then
				ktap_test_fail "${node}"
				retval="${KSFT_FAIL}"
				continue 2
			fi
		done
		ktap_test_skip "${node}"
	else
		ktap_test_pass "${node}"
	fi

done

ktap_print_totals
exit "${retval}"
