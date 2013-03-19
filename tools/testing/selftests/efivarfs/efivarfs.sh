#!/bin/bash

efivarfs_mount=/sys/firmware/efi/efivars
test_guid=210be57c-9849-4fc7-a635-e6382d1aec27

check_prereqs()
{
	local msg="skip all tests:"

	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit 0
	fi

	if ! grep -q "^\S\+ $efivarfs_mount efivarfs" /proc/mounts; then
		echo $msg efivarfs is not mounted on $efivarfs_mount >&2
		exit 0
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
		exit 1
	fi
}

test_create_empty()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid

	: > $file

	if [ ! -e $file ]; then
		echo "$file can not be created without writing" >&2
		exit 1
	fi
}

test_create_read()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid
	./create-read $file
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

	rm $file

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

check_prereqs

rc=0

run_test test_create
run_test test_create_empty
run_test test_create_read
run_test test_delete
run_test test_zero_size_delete
run_test test_open_unlink

exit $rc
