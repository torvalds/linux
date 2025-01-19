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

	if [ -e $file ]; then
		echo "$file can be created without writing" >&2
		file_cleanup $file
		exit 1
	fi
}

test_create_read()
{
	local file=$efivarfs_mount/$FUNCNAME-$test_guid
	./create-read $file
	if [ $? -ne 0 ]; then
		echo "create and read $file failed"
		exit 1
	fi
	if [ -e $file ]; then
		echo "file still exists and should not"
		file_cleanup $file
		exit 1
	fi
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

test_no_set_size()
{
	local attrs='\x07\x00\x00\x00'
	local file=$efivarfs_mount/$FUNCNAME-$test_guid
	local ret=0

	printf "$attrs\x00" > $file
	[ -e $file -a -s $file ] || exit 1
	chattr -i $file
	: > $file
	if [ $? != 0 ]; then
		echo "variable file failed to accept truncation"
		ret=1
	elif [ -e $file -a ! -s $file ]; then
		echo "file can be truncated to zero size"
		ret=1
	fi
	rm $file || exit 1

	exit $ret
}

setup_test_multiple()
{
       ##
       # we're going to do multi-threaded tests, so create a set of
       # pipes for synchronization.  We use pipes 1..3 to start the
       # stalled shell job and pipes 4..6 as indicators that the job
       # has started.  If you need more than 3 jobs the two +3's below
       # need increasing
       ##

       declare -ag p

       # empty is because arrays number from 0 but jobs number from 1
       p[0]=""

       for f in 1 2 3 4 5 6; do
               p[$f]=/tmp/efivarfs_pipe${f}
               mknod ${p[$f]} p
       done

       declare -g var=$efivarfs_mount/test_multiple-$test_guid

       cleanup() {
               for f in ${p[@]}; do
                       rm -f ${f}
               done
               if [ -e $var ]; then
                       file_cleanup $var
               fi
       }
       trap cleanup exit

       waitstart() {
               cat ${p[$[$1+3]]} > /dev/null
       }

       waitpipe() {
               echo 1 > ${p[$[$1+3]]}
               cat ${p[$1]} > /dev/null
       }

       endjob() {
               echo 1 > ${p[$1]}
               wait -n %$1
       }
}

test_multiple_zero_size()
{
       ##
       # check for remove on last close, set up three threads all
       # holding the variable (one write and two reads) and then
       # close them sequentially (waiting for completion) and check
       # the state of the variable
       ##

       { waitpipe 1; echo 1; } > $var 2> /dev/null &
       waitstart 1
       # zero length file should exist
       [ -e $var ] || exit 1
       # second and third delayed close
       { waitpipe 2; } < $var &
       waitstart 2
       { waitpipe 3; } < $var &
       waitstart 3
       # close first fd
       endjob 1
       # var should only be deleted on last close
       [ -e $var ] || exit 1
       # close second fd
       endjob 2
       [ -e $var ] || exit 1
       # file should go on last close
       endjob 3
       [ ! -e $var ] || exit 1
}

test_multiple_create()
{
       ##
       # set multiple threads to access the variable but delay
       # the final write to check the close of 2 and 3.  The
       # final write should succeed in creating the variable
       ##
       { waitpipe 1; printf '\x07\x00\x00\x00\x54'; } > $var &
       waitstart 1
       [ -e $var -a ! -s $var ] || exit 1
       { waitpipe 2; } < $var &
       waitstart 2
       { waitpipe 3; } < $var &
       waitstart 3
       # close second and third fds
       endjob 2
       # var should only be created (have size) on last close
       [ -e $var -a ! -s $var ] || exit 1
       endjob 3
       [ -e $var -a ! -s $var ] || exit 1
       # close first fd
       endjob 1
       # variable should still exist
       [ -s $var ] || exit 1
       file_cleanup $var
}

test_multiple_delete_on_write() {
       ##
       # delete the variable on final write; seqencing similar
       # to test_multiple_create()
       ##
       printf '\x07\x00\x00\x00\x54' > $var
       chattr -i $var
       { waitpipe 1; printf '\x07\x00\x00\x00'; } > $var &
       waitstart 1
       [ -e $var -a -s $var ] || exit 1
       { waitpipe 2; } < $var &
       waitstart 2
       { waitpipe 3; } < $var &
       waitstart 3
       # close first fd; write should set variable size to zero
       endjob 1
       # var should only be deleted on last close
       [ -e $var -a ! -s $var ] || exit 1
       endjob 2
       [ -e $var ] || exit 1
       # close last fd
       endjob 3
       # variable should now be removed
       [ ! -e $var ] || exit 1
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
run_test test_no_set_size
setup_test_multiple
run_test test_multiple_zero_size
run_test test_multiple_create
run_test test_multiple_delete_on_write

exit $rc
