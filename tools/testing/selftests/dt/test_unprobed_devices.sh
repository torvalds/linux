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

source "${DIR}"/../kselftest/ktap_helpers.sh

PDT=/proc/device-tree/
COMPAT_LIST="${DIR}"/compatible_list
IGNORE_LIST="${DIR}"/compatible_ignore_list

ktap_print_header

if [[ ! -d "${PDT}" ]]; then
	ktap_skip_all "${PDT} doesn't exist."
	exit "${KSFT_SKIP}"
fi

nodes_compatible=$(
	for node in $(find ${PDT} -type d); do
		[ ! -f "${node}"/compatible ] && continue
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
	for dev_dir in $(find /sys/devices -type d); do
		[ ! -f "${dev_dir}"/uevent ] && continue
		[ ! -d "${dev_dir}"/driver ] && continue

		grep '^OF_FULLNAME=' "${dev_dir}"/uevent | sed -e 's|OF_FULLNAME=||'
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
