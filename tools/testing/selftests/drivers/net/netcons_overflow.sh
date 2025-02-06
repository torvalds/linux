#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

# This test verifies that users can successfully create up to
# MAX_USERDATA_ITEMS userdata entries without encountering any failures.
#
# Additionally, it tests for expected failure when attempting to exceed this
# maximum limit.
#
# Author: Breno Leitao <leitao@debian.org>

set -euo pipefail

SCRIPTDIR=$(dirname "$(readlink -e "${BASH_SOURCE[0]}")")

source "${SCRIPTDIR}"/lib/sh/lib_netcons.sh
# This is coming from netconsole code. Check for it in drivers/net/netconsole.c
MAX_USERDATA_ITEMS=16

# Function to create userdata entries
function create_userdata_max_entries() {
	# All these keys should be created without any error
	for i in $(seq $MAX_USERDATA_ITEMS)
	do
		# USERDATA_KEY is used by set_user_data
		USERDATA_KEY="key"${i}
		set_user_data
	done
}

# Function to verify the entry limit
function verify_entry_limit() {
	# Allowing the test to fail without exiting, since the next command
	# will fail
	set +e
	mkdir "${NETCONS_PATH}/userdata/key_that_will_fail" 2> /dev/null
	ret="$?"
	set -e
	if [ "$ret" -eq 0 ];
	then
		echo "Adding more than ${MAX_USERDATA_ITEMS} entries in userdata should fail, but it didn't" >&2
		ls "${NETCONS_PATH}/userdata/" >&2
		exit "${ksft_fail}"
	fi
}

# ========== #
# Start here #
# ========== #

modprobe netdevsim 2> /dev/null || true
modprobe netconsole 2> /dev/null || true

# Check for basic system dependency and exit if not found
check_for_dependencies

# Remove the namespace, interfaces and netconsole target on exit
trap cleanup EXIT
# Create one namespace and two interfaces
set_network
# Create a dynamic target for netconsole
create_dynamic_target
# populate the maximum number of supported keys in userdata
create_userdata_max_entries
# Verify an additional entry is not allowed
verify_entry_limit
exit "${ksft_pass}"
