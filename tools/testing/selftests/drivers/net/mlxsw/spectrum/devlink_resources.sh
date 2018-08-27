#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NUM_NETIFS=1
source devlink_lib_spectrum.sh

setup_prepare()
{
	devlink_sp_read_kvd_defaults
}

cleanup()
{
	pre_cleanup
	devlink_sp_size_kvd_to_default
}

trap cleanup EXIT

setup_prepare

profiles_test()
{
	local i

	log_info "Running profile tests"

	for i in $KVD_PROFILES; do
		RET=0
		devlink_sp_resource_kvd_profile_set $i
		log_test "'$i' profile"
	done

	# Default is explicitly tested at end to ensure it's actually applied
	RET=0
	devlink_sp_resource_kvd_profile_set "default"
	log_test "'default' profile"
}

resources_min_test()
{
	local size
	local i
	local j

	log_info "Running KVD-minimum tests"

	for i in $KVD_CHILDREN; do
		RET=0
		size=$(devlink_resource_get kvd "$i" | jq '.["size_min"]')
		devlink_resource_size_set "$size" kvd "$i"

		# In case of linear, need to minimize sub-resources as well
		if [[ "$i" == "linear" ]]; then
			for j in $KVDL_CHILDREN; do
				devlink_resource_size_set 0 kvd linear "$j"
			done
		fi

		devlink_reload
		devlink_sp_size_kvd_to_default
		log_test "'$i' minimize [$size]"
	done
}

resources_max_test()
{
	local min_size
	local size
	local i
	local j

	log_info "Running KVD-maximum tests"
	for i in $KVD_CHILDREN; do
		RET=0
		devlink_sp_resource_minimize

		# Calculate the maximum possible size for the given partition
		size=$(devlink_resource_size_get kvd)
		for j in $KVD_CHILDREN; do
			if [ "$i" != "$j" ]; then
				min_size=$(devlink_resource_get kvd "$j" | \
					   jq '.["size_min"]')
				size=$((size - min_size))
			fi
		done

		# Test almost maximum size
		devlink_resource_size_set "$((size - 128))" kvd "$i"
		devlink_reload
		log_test "'$i' almost maximize [$((size - 128))]"

		# Test above maximum size
		devlink resource set "$DEVLINK_DEV" \
			path "kvd/$i" size $((size + 128)) &> /dev/null
		check_fail $? "Set kvd/$i to size $((size + 128)) should fail"
		log_test "'$i' Overflow rejection [$((size + 128))]"

		# Test maximum size
		if [ "$i" == "hash_single" ] || [ "$i" == "hash_double" ]; then
			echo "SKIP: Observed problem with exact max $i"
			continue
		fi

		devlink_resource_size_set "$size" kvd "$i"
		devlink_reload
		log_test "'$i' maximize [$size]"

		devlink_sp_size_kvd_to_default
	done
}

profiles_test
resources_min_test
resources_max_test

exit "$RET"
