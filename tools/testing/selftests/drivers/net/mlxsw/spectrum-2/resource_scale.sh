#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../../net/forwarding

NUM_NETIFS=6
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh
source ../mlxsw_lib.sh

mlxsw_only_on_spectrum 2+ || exit 1

current_test=""

cleanup()
{
	pre_cleanup
	if [ ! -z $current_test ]; then
		${current_test}_cleanup
	fi
	# Need to reload in order to avoid router abort.
	devlink_reload
}

trap cleanup EXIT

ALL_TESTS="router tc_flower mirror_gre tc_police port rif_mac_profile"
for current_test in ${TESTS:-$ALL_TESTS}; do
	RET_FIN=0
	source ${current_test}_scale.sh

	num_netifs_var=${current_test^^}_NUM_NETIFS
	num_netifs=${!num_netifs_var:-$NUM_NETIFS}

	for should_fail in 0 1; do
		RET=0
		target=$(${current_test}_get_target "$should_fail")
		${current_test}_setup_prepare
		setup_wait $num_netifs
		# Update target in case occupancy of a certain resource changed
		# following the test setup.
		target=$(${current_test}_get_target "$should_fail")
		${current_test}_test "$target" "$should_fail"
		${current_test}_cleanup
		devlink_reload
		if [[ "$should_fail" -eq 0 ]]; then
			log_test "'$current_test' $target"
		else
			log_test "'$current_test' overflow $target"
		fi
		RET_FIN=$(( RET_FIN || RET ))
	done
done
current_test=""

exit "$RET_FIN"
