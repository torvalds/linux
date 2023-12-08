#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

efivarfs_mount=/sys/firmware/efi/efivars
test_guid=210be57c-9849-4fc7-a635-e6382d1aec27

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

file_cleanup()
{
	chattr -i $1
	rm -f $1
}

check_prereqs()
{
	local msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	if ! grep -q "^\S\+ $efivarfs_mount efivarfs" /proc/mounts; then
		echo $msg efivarfs is not mounted on $efivarfs_mount >&2
		exit $ksft_skip
	fi
}

run_test()
{
	local test="$1"

	echo "--------------------"
	echo "running $test"
	echo "--------------------"

	if [ "$(type -t $test)" = 'function' ]; then
		( $test )
	else
		( ./$test )
	fi

	if [ $? -ne 0 ]; then
		echo "  [FAIL]"
		rc=1
	else
		echo "  [PASS]"
	fi
}

test_create()
{
	local attrs='\x07\x00\x00\x00'
	local file=$efivarfs_mount/$FUNCNAME-$test_guid

	printf "$attrs\x00" > $file

	if [ ! -e $file ]; then
		echo "$file couldn't be created" >&2
		exit 1
	fi

	if [ $(stat -c %s $file) -ne 5 ]; then
		echo "$file has invalid size" >&2
		file_cleanup $file
		exit 1
	fi
	file_cleanup $file
}

test_create_empty()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid

	: > $file

	if [ ! -e $file ]; then
		echo "$file can not be created without writing" >&2
		exit 1
	fi
	file_cleanup $file
}

test_create_read()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid
	./create-read $file
	if [ $? -ne 0 ]; then
		echo "create and read $file failed"
		file_cleanup $file
		exit 1
	fi
	file_cleanup $file
}

test_delete()
{
	local attrs='\x07\x00\x00\x00'
	local file=$efivarfs_mount/$FUNCNAME-$test_guid

	printf "$attrs\x00" > $file

	if [ ! -e $file ]; then
		echo "$file couldn't be created" >&2
		exit 1
	fi

	file_cleanup $file

	if [ -e $file ]; then
		echo "$file couldn't be deleted" >&2
		exit 1
	fi

}

# test that we can remove a variable by issuing a write with only
# attributes specified
test_zero_size_delete()
{
	local attrs='\x07\x00\x00\x00'
	local file=$efivarfs_mount/$FUNCNAME-$test_guid

	printf "$attrs\x00" > $file

	if [ ! -e $file ]; then
		echo "$file does not exist" >&2
		exit 1
	fi

	chattr -i $file
	printf "$attrs" > $file

	if [ -e $file ]; then
		echo "$file should have been deleted" >&2
		exit 1
	fi
}

test_open_unlink()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid
	./open-unlink $file
}

# test that we can create a range of filenames
test_valid_filenames()
{
	local attrs='\x07\x00\x00\x00'
	local ret=0

	local file_list="abc dump-type0-11-1-1362436005 1234 -"
	for f in $file_list; do
		local file=$efivarfs_mount/$f-$test_guid

		printf "$attrs\x00" > $file

		if [ ! -e $file ]; then
			echo "$file could not be created" >&2
			ret=1
		else
			file_cleanup $file
		fi
	done

	exit $ret
}

test_invalid_filenames()
{
	local attrs='\x07\x00\x00\x00'
	local ret=0

	local file_list="
		-1234-1234-1234-123456789abc
		foo
		foo-bar
		-foo-
		foo-barbazba-foob-foob-foob-foobarbazfoo
		foo-------------------------------------
		-12345678-1234-1234-1234-123456789abc
		a-12345678=1234-1234-1234-123456789abc
		a-12345678-1234=1234-1234-123456789abc
		a-12345678-1234-1234=1234-123456789abc
		a-12345678-1234-1234-1234=123456789abc
		1112345678-1234-1234-1234-123456789abc"

	for f in $file_list; do
		local file=$efivarfs_mount/$f

		printf "$attrs\x00" 2>/dev/null > $file

		if [ -e $file ]; then
			echo "Creating $file should have failed" >&2
			file_cleanup $file
			ret=1
		fi
	done

	exit $ret
}

check_prereqs

rc=0

run_test test_create
run_test test_create_empty
run_test test_create_read
run_test test_delete
run_test test_zero_size_delete
run_test test_open_unlink
run_test test_valid_filenames
run_test test_invalid_filenames

exit $rc
