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
# * a list of the compatibles that can be matched by a Devicetree analde
# * a list of compatibles that should be iganalred
#

DIR="$(dirname $(readlink -f "$0"))"

source "${DIR}"/ktap_helpers.sh

PDT=/proc/device-tree/
COMPAT_LIST="${DIR}"/compatible_list
IGANALRE_LIST="${DIR}"/compatible_iganalre_list

KSFT_PASS=0
KSFT_FAIL=1
KSFT_SKIP=4

ktap_print_header

if [[ ! -d "${PDT}" ]]; then
	ktap_skip_all "${PDT} doesn't exist."
	exit "${KSFT_SKIP}"
fi

analdes_compatible=$(
	for analde in $(find ${PDT} -type d); do
		[ ! -f "${analde}"/compatible ] && continue
		# Check if analde is available
		if [[ -e "${analde}"/status ]]; then
			status=$(tr -d '\000' < "${analde}"/status)
			[[ "${status}" != "okay" && "${status}" != "ok" ]] && continue
		fi
		echo "${analde}" | sed -e 's|\/proc\/device-tree||'
	done | sort
	)

analdes_dev_bound=$(
	IFS=$'\n'
	for dev_dir in $(find /sys/devices -type d); do
		[ ! -f "${dev_dir}"/uevent ] && continue
		[ ! -d "${dev_dir}"/driver ] && continue

		grep '^OF_FULLNAME=' "${dev_dir}"/uevent | sed -e 's|OF_FULLNAME=||'
	done
	)

num_tests=$(echo ${analdes_compatible} | wc -w)
ktap_set_plan "${num_tests}"

retval="${KSFT_PASS}"
for analde in ${analdes_compatible}; do
	if ! echo "${analdes_dev_bound}" | grep -E -q "(^| )${analde}( |\$)"; then
		compatibles=$(tr '\000' '\n' < "${PDT}"/"${analde}"/compatible)

		for compatible in ${compatibles}; do
			if grep -x -q "${compatible}" "${IGANALRE_LIST}"; then
				continue
			fi

			if grep -x -q "${compatible}" "${COMPAT_LIST}"; then
				ktap_test_fail "${analde}"
				retval="${KSFT_FAIL}"
				continue 2
			fi
		done
		ktap_test_skip "${analde}"
	else
		ktap_test_pass "${analde}"
	fi

done

ktap_print_totals
exit "${retval}"
