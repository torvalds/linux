#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

TESTNAME="bpftool_map"
BPF_FILE="security_bpf_map.bpf.o"
BPF_ITER_FILE="bpf_iter_map_elem.bpf.o"
PROTECTED_MAP_NAME="prot_map"
NOT_PROTECTED_MAP_NAME="not_prot_map"
BPF_FS_TMP_PARENT="/tmp"
BPF_FS_PARENT=$(awk '$3 == "bpf" {print $2; exit}' /proc/mounts)
BPF_FS_PARENT=${BPF_FS_PARENT:-$BPF_FS_TMP_PARENT}
# bpftool will mount bpf file system under BPF_DIR if it is not mounted
# under BPF_FS_PARENT.
BPF_DIR="$BPF_FS_PARENT/test_$TESTNAME"
SCRIPT_DIR=$(dirname $(realpath "$0"))
BPF_FILE_PATH="$SCRIPT_DIR/$BPF_FILE"
BPF_ITER_FILE_PATH="$SCRIPT_DIR/$BPF_ITER_FILE"
BPFTOOL_PATH="bpftool"
# Assume the script is located under tools/testing/selftests/bpf/
KDIR_ROOT_DIR=$(realpath "$SCRIPT_DIR"/../../../../)

_cleanup()
{
	set +eu

	# If BPF_DIR is a mount point this will not remove the mount point itself.
	[ -d "$BPF_DIR" ] && rm -rf "$BPF_DIR" 2> /dev/null

	# Unmount if BPF filesystem was temporarily created.
	if [ "$BPF_FS_PARENT" = "$BPF_FS_TMP_PARENT" ]; then
		# A loop and recursive unmount are required as bpftool might
		# create multiple mounts. For example, a bind mount of the directory
		# to itself. The bind mount is created to change mount propagation
		# flags on an actual mount point.
		max_attempts=3
		attempt=0
		while mountpoint -q "$BPF_DIR" && [ $attempt -lt $max_attempts ]; do
			umount -R "$BPF_DIR" 2>/dev/null
			attempt=$((attempt+1))
		done

		# The directory still exists. Remove it now.
		[ -d "$BPF_DIR" ] && rm -rf "$BPF_DIR" 2>/dev/null
	fi
}

cleanup_skip()
{
	echo "selftests: $TESTNAME [SKIP]"
	_cleanup

	exit $ksft_skip
}

cleanup()
{
	if [ "$?" = 0 ]; then
		echo "selftests: $TESTNAME [PASS]"
	else
		echo "selftests: $TESTNAME [FAILED]"
	fi
	_cleanup
}

check_root_privileges() {
	if [ $(id -u) -ne 0 ]; then
		echo "Need root privileges"
		exit $ksft_skip
	fi
}

# Function to verify bpftool path.
# Parameters:
#   $1: bpftool path
verify_bpftool_path() {
	local bpftool_path="$1"
	if ! "$bpftool_path" version > /dev/null 2>&1; then
		echo "Could not run test without bpftool"
		exit $ksft_skip
	fi
}

# Function to verify BTF support.
# The test requires BTF support for fmod_ret programs.
verify_btf_support() {
	if [ ! -f /sys/kernel/btf/vmlinux ]; then
		echo "Could not run test without BTF support"
		exit $ksft_skip
	fi
}

# Function to initialize map entries with keys [0..2] and values set to 0.
# Parameters:
#  $1: Map name
#  $2: bpftool path
initialize_map_entries() {
	local map_name="$1"
	local bpftool_path="$2"

	for key in 0 1 2; do
		"$bpftool_path" map update name "$map_name" key $key 0 0 0 value 0 0 0 $key
	done
}

# Test read access to the map.
# Parameters:
#   $1: Name command (name/pinned)
#   $2: Map name
#   $3: bpftool path
#   $4: key
access_for_read() {
	local name_cmd="$1"
	local map_name="$2"
	local bpftool_path="$3"
	local key="$4"

	# Test read access to the map.
	if ! "$bpftool_path" map lookup "$name_cmd" "$map_name" key $key 1>/dev/null; then
		echo " Read access to $key in $map_name failed"
		exit 1
	fi

	# Test read access to map's BTF data.
	if ! "$bpftool_path" btf dump map "$name_cmd" "$map_name" 1>/dev/null; then
		echo " Read access to $map_name for BTF data failed"
		exit 1
	fi
}

# Test write access to the map.
# Parameters:
#   $1: Name command (name/pinned)
#   $2: Map name
#   $3: bpftool path
#   $4: key
#   $5: Whether write should succeed (true/false)
access_for_write() {
	local name_cmd="$1"
	local map_name="$2"
	local bpftool_path="$3"
	local key="$4"
	local write_should_succeed="$5"
	local value="1 1 1 1"

	if "$bpftool_path" map update "$name_cmd" "$map_name" key $key value \
			$value 2>/dev/null; then
		if [ "$write_should_succeed" = "false" ]; then
			echo " Write access to $key in $map_name succeeded but should have failed"
			exit 1
		fi
	else
		if [ "$write_should_succeed" = "true" ]; then
			echo " Write access to $key in $map_name failed but should have succeeded"
			exit 1
		fi
	fi
}

# Test entry deletion for the map.
# Parameters:
#   $1: Name command (name/pinned)
#   $2: Map name
#   $3: bpftool path
#   $4: key
#   $5: Whether write should succeed (true/false)
access_for_deletion() {
	local name_cmd="$1"
	local map_name="$2"
	local bpftool_path="$3"
	local key="$4"
	local write_should_succeed="$5"
	local value="1 1 1 1"

	# Test deletion by key for the map.
	# Before deleting, check the key exists.
	if ! "$bpftool_path" map lookup "$name_cmd" "$map_name" key $key 1>/dev/null; then
		echo " Key $key does not exist in $map_name"
		exit 1
	fi

	# Delete by key.
	if "$bpftool_path" map delete "$name_cmd" "$map_name" key $key 2>/dev/null; then
		if [ "$write_should_succeed" = "false" ]; then
			echo " Deletion for $key in $map_name succeeded but should have failed"
			exit 1
		fi
	else
		if [ "$write_should_succeed" = "true" ]; then
			echo " Deletion for $key in $map_name failed but should have succeeded"
			exit 1
		fi
	fi

	# After deleting, check the entry existence according to the expected status.
	if "$bpftool_path" map lookup "$name_cmd" "$map_name" key $key 1>/dev/null; then
		if [ "$write_should_succeed" = "true" ]; then
			echo " Key $key for $map_name was not deleted but should have been deleted"
			exit 1
		fi
	else
		if [ "$write_should_succeed" = "false" ]; then
			echo "Key $key for $map_name was deleted but should have not been deleted"
			exit 1
		fi
	fi

	# Test creation of map's deleted entry, if deletion was successful.
	# Otherwise, the entry exists.
	if "$bpftool_path" map update "$name_cmd" "$map_name" key $key value \
				$value 2>/dev/null; then
		if [ "$write_should_succeed" = "false" ]; then
			echo " Write access to $key in $map_name succeeded after deletion attempt but should have failed"
			exit 1
		fi
	else
		if [ "$write_should_succeed" = "true" ]; then
			echo " Write access to $key in $map_name failed after deletion attempt but should have succeeded"
			exit 1
		fi
	fi
}

# Test map elements iterator.
# Parameters:
#   $1: Name command (name/pinned)
#   $2: Map name
#   $3: bpftool path
#   $4: BPF_DIR
#   $5: bpf iterator object file path
iterate_map_elem() {
	local name_cmd="$1"
	local map_name="$2"
	local bpftool_path="$3"
	local bpf_dir="$4"
	local bpf_file="$5"
	local pin_path="$bpf_dir/map_iterator"

	"$bpftool_path" iter pin "$bpf_file" "$pin_path" map "$name_cmd" "$map_name"
	if [ ! -f "$pin_path" ]; then
		echo " Failed to pin iterator to $pin_path"
		exit 1
	fi

	cat "$pin_path" 1>/dev/null
	rm "$pin_path" 2>/dev/null
}

# Function to test map access with configurable write expectations
# Parameters:
#   $1: Name command (name/pinned)
#   $2: Map name
#   $3: bpftool path
#   $4: key for rw
#   $5: key to delete
#   $6: Whether write should succeed (true/false)
#   $7: BPF_DIR
#   $8: bpf iterator object file path
access_map() {
	local name_cmd="$1"
	local map_name="$2"
	local bpftool_path="$3"
	local key_for_rw="$4"
	local key_to_del="$5"
	local write_should_succeed="$6"
	local bpf_dir="$7"
	local bpf_iter_file_path="$8"

	access_for_read "$name_cmd" "$map_name" "$bpftool_path" "$key_for_rw"
	access_for_write "$name_cmd" "$map_name" "$bpftool_path" "$key_for_rw" \
		"$write_should_succeed"
	access_for_deletion "$name_cmd" "$map_name" "$bpftool_path" "$key_to_del" \
		"$write_should_succeed"
	iterate_map_elem "$name_cmd" "$map_name" "$bpftool_path" "$bpf_dir" \
		"$bpf_iter_file_path"
}

# Function to test map access with configurable write expectations
# Parameters:
#   $1: Map name
#   $2: bpftool path
#   $3: BPF_DIR
#   $4: Whether write should succeed (true/false)
#   $5: bpf iterator object file path
test_map_access() {
	local map_name="$1"
	local bpftool_path="$2"
	local bpf_dir="$3"
	local pin_path="$bpf_dir/${map_name}_pinned"
	local write_should_succeed="$4"
	local bpf_iter_file_path="$5"

	# Test access to the map by name.
	access_map "name" "$map_name" "$bpftool_path" "0 0 0 0" "1 0 0 0" \
		"$write_should_succeed" "$bpf_dir" "$bpf_iter_file_path"

	# Pin the map to the BPF filesystem
	"$bpftool_path" map pin name "$map_name" "$pin_path"
	if [ ! -e "$pin_path" ]; then
		echo " Failed to pin $map_name"
		exit 1
	fi

	# Test access to the pinned map.
	access_map "pinned" "$pin_path" "$bpftool_path" "0 0 0 0" "2 0 0 0" \
		"$write_should_succeed" "$bpf_dir" "$bpf_iter_file_path"
}

# Function to test map creation and map-of-maps
# Parameters:
#   $1: bpftool path
#   $2: BPF_DIR
test_map_creation_and_map_of_maps() {
	local bpftool_path="$1"
	local bpf_dir="$2"
	local outer_map_name="outer_map_tt"
	local inner_map_name="inner_map_tt"

	"$bpftool_path" map create "$bpf_dir/$inner_map_name" type array key 4 \
		value 4 entries 4 name "$inner_map_name"
	if [ ! -f "$bpf_dir/$inner_map_name" ]; then
		echo " Failed to create inner map file at $bpf_dir/$outer_map_name"
		return 1
	fi

	"$bpftool_path" map create "$bpf_dir/$outer_map_name" type hash_of_maps \
		key 4 value 4 entries 2 name "$outer_map_name" inner_map name "$inner_map_name"
	if [ ! -f "$bpf_dir/$outer_map_name" ]; then
		echo " Failed to create outer map file at $bpf_dir/$outer_map_name"
		return 1
	fi

	# Add entries to the outer map by name and by pinned path.
	"$bpftool_path" map update pinned "$bpf_dir/$outer_map_name" key 0 0 0 0 \
		value pinned "$bpf_dir/$inner_map_name"
	"$bpftool_path" map update name "$outer_map_name" key 1 0 0 0 value \
		name "$inner_map_name"

	# The outer map should be full by now.
	# The following map update command is expected to fail.
	if "$bpftool_path" map update name "$outer_map_name" key 2 0 0 0 value name \
		"$inner_map_name" 2>/dev/null; then
		echo " Update for $outer_map_name succeeded but should have failed"
		exit 1
	fi
}

# Function to test map access with the btf list command
# Parameters:
#   $1: bpftool path
test_map_access_with_btf_list() {
	local bpftool_path="$1"

	# The btf list command iterates over maps for
	# loaded BPF programs.
	if ! "$bpftool_path" btf list 1>/dev/null; then
		echo " Failed to access btf data"
		exit 1
	fi
}

set -eu

trap cleanup_skip EXIT

check_root_privileges

verify_bpftool_path "$BPFTOOL_PATH"

verify_btf_support

trap cleanup EXIT

# Load and attach the BPF programs to control maps access.
"$BPFTOOL_PATH" prog loadall "$BPF_FILE_PATH" "$BPF_DIR" autoattach

initialize_map_entries "$PROTECTED_MAP_NAME" "$BPFTOOL_PATH"
initialize_map_entries "$NOT_PROTECTED_MAP_NAME" "$BPFTOOL_PATH"

# Activate the map protection mechanism. Protection status is controlled
# by a value stored in the prot_status_map at index 0.
"$BPFTOOL_PATH" map update name prot_status_map key 0 0 0 0 value 1 0 0 0

# Test protected map (write should fail).
test_map_access "$PROTECTED_MAP_NAME" "$BPFTOOL_PATH" "$BPF_DIR" "false" \
 "$BPF_ITER_FILE_PATH"

# Test not protected map (write should succeed).
test_map_access "$NOT_PROTECTED_MAP_NAME" "$BPFTOOL_PATH" "$BPF_DIR" "true" \
 "$BPF_ITER_FILE_PATH"

test_map_creation_and_map_of_maps "$BPFTOOL_PATH" "$BPF_DIR"

test_map_access_with_btf_list "$BPFTOOL_PATH"

exit 0
