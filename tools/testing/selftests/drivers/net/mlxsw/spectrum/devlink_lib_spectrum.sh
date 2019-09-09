#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source "../../../../net/forwarding/devlink_lib.sh"

if [ "$DEVLINK_VIDDID" != "15b3:cb84" ]; then
	echo "SKIP: test is tailored for Mellanox Spectrum"
	exit 1
fi

# Needed for returning to default
declare -A KVD_DEFAULTS

KVD_CHILDREN="linear hash_single hash_double"
KVDL_CHILDREN="singles chunks large_chunks"

devlink_sp_resource_minimize()
{
	local size
	local i

	for i in $KVD_CHILDREN; do
		size=$(devlink_resource_get kvd "$i" | jq '.["size_min"]')
		devlink_resource_size_set "$size" kvd "$i"
	done

	for i in $KVDL_CHILDREN; do
		size=$(devlink_resource_get kvd linear "$i" | \
		       jq '.["size_min"]')
		devlink_resource_size_set "$size" kvd linear "$i"
	done
}

devlink_sp_size_kvd_to_default()
{
	local need_reload=0
	local i

	for i in $KVD_CHILDREN; do
		local size=$(echo "${KVD_DEFAULTS[kvd_$i]}" | jq '.["size"]')
		current_size=$(devlink_resource_size_get kvd "$i")

		if [ "$size" -ne "$current_size" ]; then
			devlink_resource_size_set "$size" kvd "$i"
			need_reload=1
		fi
	done

	for i in $KVDL_CHILDREN; do
		local size=$(echo "${KVD_DEFAULTS[kvd_linear_$i]}" | \
			     jq '.["size"]')
		current_size=$(devlink_resource_size_get kvd linear "$i")

		if [ "$size" -ne "$current_size" ]; then
			devlink_resource_size_set "$size" kvd linear "$i"
			need_reload=1
		fi
	done

	if [ "$need_reload" -ne "0" ]; then
		devlink_reload
	fi
}

devlink_sp_read_kvd_defaults()
{
	local key
	local i

	KVD_DEFAULTS[kvd]=$(devlink_resource_get "kvd")
	for i in $KVD_CHILDREN; do
		key=kvd_$i
		KVD_DEFAULTS[$key]=$(devlink_resource_get kvd "$i")
	done

	for i in $KVDL_CHILDREN; do
		key=kvd_linear_$i
		KVD_DEFAULTS[$key]=$(devlink_resource_get kvd linear "$i")
	done
}

KVD_PROFILES="default scale ipv4_max"

devlink_sp_resource_kvd_profile_set()
{
	local profile=$1

	case "$profile" in
	scale)
		devlink_resource_size_set 64000 kvd linear
		devlink_resource_size_set 15616 kvd linear singles
		devlink_resource_size_set 32000 kvd linear chunks
		devlink_resource_size_set 16384 kvd linear large_chunks
		devlink_resource_size_set 128000 kvd hash_single
		devlink_resource_size_set 48000 kvd hash_double
		devlink_reload
		;;
	ipv4_max)
		devlink_resource_size_set 64000 kvd linear
		devlink_resource_size_set 15616 kvd linear singles
		devlink_resource_size_set 32000 kvd linear chunks
		devlink_resource_size_set 16384 kvd linear large_chunks
		devlink_resource_size_set 144000 kvd hash_single
		devlink_resource_size_set 32768 kvd hash_double
		devlink_reload
		;;
	default)
		devlink_resource_size_set 98304 kvd linear
		devlink_resource_size_set 16384 kvd linear singles
		devlink_resource_size_set 49152 kvd linear chunks
		devlink_resource_size_set 32768 kvd linear large_chunks
		devlink_resource_size_set 87040 kvd hash_single
		devlink_resource_size_set 60416 kvd hash_double
		devlink_reload
		;;
	*)
		check_err 1 "Unknown profile $profile"
	esac
}
