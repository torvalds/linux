#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../../net/forwarding

NUM_NETIFS=6
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source devlink_lib_spectrum.sh

current_test=""

cleanup()
{
	pre_cleanup
	if [ ! -z $current_test ]; then
		${current_test}_cleanup
	fi
	devlink_sp_size_kvd_to_default
}

devlink_sp_read_kvd_defaults
trap cleanup EXIT

ALL_TESTS="
	router
	tc_flower
	mirror_gre
	tc_police
	port
	rif_mac_profile
	rif_counter
	port_range
"

for current_test in ${TESTS:-$ALL_TESTS}; do
	RET_FIN=0
	source ${current_test}_scale.sh

	num_netifs_var=${current_test^^}_NUM_NETIFS
	num_netifs=${!num_netifs_var:-$NUM_NETIFS}

	for profile in $KVD_PROFILES; do
		RET=0
		devlink_sp_resource_kvd_profile_set $profile
		if [[ $RET -gt 0 ]]; then
			log_test "'$current_test' [$profile] setting"
			continue
		fi

		for should_fail in 0 1; do
			RET=0
			target=$(${current_test}_get_target "$should_fail")
			if ((target == 0)); then
				continue
			fi
			${current_test}_setup_prepare
			setup_wait $num_netifs
			# Update target in case occupancy of a certain resource
			# changed following the test setup.
			target=$(${current_test}_get_target "$should_fail")
			${current_test}_test "$target" "$should_fail"
			if [[ "$should_fail" -eq 0 ]]; then
				log_test "'$current_test' [$profile] $target"

				if ((!RET)); then
					tt=${current_test}_traffic_test
					if [[ $(type -t $tt) == "function" ]]
					then
						$tt "$target"
						log_test "'$current_test' [$profile] $target traffic test"
					fi
				fi
			else
				log_test "'$current_test' [$profile] overflow $target"
			fi
			${current_test}_cleanup $target
			RET_FIN=$(( RET_FIN || RET ))
		done
	done
done
current_test=""

exit "$RET_FIN"
