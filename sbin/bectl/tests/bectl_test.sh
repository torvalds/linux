#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

ZPOOL_NAME_FILE=zpool_name
get_zpool_name()
{
	cat $ZPOOL_NAME_FILE
}
make_zpool_name()
{
	mktemp -u bectl_test_XXXXXX > $ZPOOL_NAME_FILE
	get_zpool_name
}

# Establishes a bectl_create zpool that can be used for some light testing; contains
# a 'default' BE and not much else.
bectl_create_setup()
{
	zpool=$1
	disk=$2
	mnt=$3

	# Sanity check to make sure `make_zpool_name` succeeded
	atf_check test -n "$zpool"

	kldload -n -q zfs || atf_skip "ZFS module not loaded on the current system"
	atf_check mkdir -p ${mnt}
	atf_check truncate -s 1G ${disk}
	atf_check zpool create -o altroot=${mnt} ${zpool} ${disk}
	atf_check zfs create -o mountpoint=none ${zpool}/ROOT
	atf_check zfs create -o mountpoint=/ -o canmount=noauto \
	    ${zpool}/ROOT/default
}
bectl_create_deep_setup()
{
	zpool=$1
	disk=$2
	mnt=$3

	# Sanity check to make sure `make_zpool_name` succeeded
	atf_check test -n "$zpool"

	bectl_create_setup ${zpool} ${disk} ${mnt}
	atf_check mkdir -p ${root}
	atf_check -o ignore bectl -r ${zpool}/ROOT mount default ${root}
	atf_check mkdir -p ${root}/usr
	atf_check zfs create -o mountpoint=/usr -o canmount=noauto \
	    ${zpool}/ROOT/default/usr
	atf_check -o ignore bectl -r ${zpool}/ROOT umount default
}

bectl_cleanup()
{
	zpool=$1
	if [ -z "$zpool" ]; then
		echo "Skipping cleanup; zpool not set up"
	elif zpool get health ${zpool} >/dev/null 2>&1; then
		zpool destroy -f ${zpool}
	fi
}

atf_test_case bectl_create cleanup
bectl_create_head()
{

	atf_set "descr" "Check the various forms of bectl create"
	atf_set "require.user" root
}
bectl_create_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt

	bectl_create_setup ${zpool} ${disk} ${mount}
	# Test standard creation, creation of a snapshot, and creation from a
	# snapshot.
	atf_check bectl -r ${zpool}/ROOT create -e default default2
	atf_check bectl -r ${zpool}/ROOT create default2@test_snap
	atf_check bectl -r ${zpool}/ROOT create -e default2@test_snap default3
}
bectl_create_cleanup()
{
	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_destroy cleanup
bectl_destroy_head()
{

	atf_set "descr" "Check bectl destroy"
	atf_set "require.user" root
}
bectl_destroy_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt

	bectl_create_setup ${zpool} ${disk} ${mount}
	atf_check bectl -r ${zpool}/ROOT create -e default default2
	atf_check -o not-empty zfs get mountpoint ${zpool}/ROOT/default2
	atf_check -e ignore bectl -r ${zpool}/ROOT destroy default2
	atf_check -e not-empty -s not-exit:0 zfs get mountpoint ${zpool}/ROOT/default2
}
bectl_destroy_cleanup()
{

	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_export_import cleanup
bectl_export_import_head()
{

	atf_set "descr" "Check bectl export and import"
	atf_set "require.user" root
}
bectl_export_import_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt

	bectl_create_setup ${zpool} ${disk} ${mount}
	atf_check -o save:exported bectl -r ${zpool}/ROOT export default
	atf_check -x "bectl -r ${zpool}/ROOT import default2 < exported"
	atf_check -o not-empty zfs get mountpoint ${zpool}/ROOT/default2
	atf_check -e ignore bectl -r ${zpool}/ROOT destroy default2
	atf_check -e not-empty -s not-exit:0 zfs get mountpoint \
	    ${zpool}/ROOT/default2
}
bectl_export_import_cleanup()
{

	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_list cleanup
bectl_list_head()
{

	atf_set "descr" "Check bectl list"
	atf_set "require.user" root
}
bectl_list_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt

	bectl_create_setup ${zpool} ${disk} ${mount}
	# Test the list functionality, including that BEs come and go away
	# as they're created and destroyed.  Creation and destruction tests
	# use the 'zfs' utility to verify that they're actually created, so
	# these are just light tests that 'list' is picking them up.
	atf_check -o save:list.out bectl -r ${zpool}/ROOT list
	atf_check -o not-empty grep 'default' list.out
	atf_check bectl -r ${zpool}/ROOT create -e default default2
	atf_check -o save:list.out bectl -r ${zpool}/ROOT list
	atf_check -o not-empty grep 'default2' list.out
	atf_check -e ignore bectl -r ${zpool}/ROOT destroy default2
	atf_check -o save:list.out bectl -r ${zpool}/ROOT list
	atf_check -s not-exit:0 grep 'default2' list.out
	# XXX TODO: Formatting checks
}
bectl_list_cleanup()
{

	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_mount cleanup
bectl_mount_head()
{

	atf_set "descr" "Check bectl mount/unmount"
	atf_set "require.user" root
}
bectl_mount_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt
	root=${mount}/root

	bectl_create_deep_setup ${zpool} ${disk} ${mount}
	atf_check mkdir -p ${root}
	# Test unmount first...
	atf_check -o not-empty bectl -r ${zpool}/ROOT mount default ${root}
	atf_check -o not-empty -x "mount | grep '^${zpool}/ROOT/default'"
	atf_check bectl -r ${zpool}/ROOT unmount default
	atf_check -s not-exit:0 -x "mount | grep '^${zpool}/ROOT/default'"
	# Then umount!
	atf_check -o not-empty bectl -r ${zpool}/ROOT mount default ${root}
	atf_check -o not-empty -x "mount | grep '^${zpool}/ROOT/default'"
	atf_check bectl -r ${zpool}/ROOT umount default
	atf_check -s not-exit:0 -x "mount | grep '^${zpool}/ROOT/default'"
}
bectl_mount_cleanup()
{

	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_rename cleanup
bectl_rename_head()
{

	atf_set "descr" "Check bectl rename"
	atf_set "require.user" root
}
bectl_rename_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt

	bectl_create_setup ${zpool} ${disk} ${mount}
	atf_check bectl -r ${zpool}/ROOT rename default default2
	atf_check -o not-empty zfs get mountpoint ${zpool}/ROOT/default2
	atf_check -e not-empty -s not-exit:0 zfs get mountpoint \
	    ${zpool}/ROOT/default
}
bectl_rename_cleanup()
{

	bectl_cleanup $(get_zpool_name)
}

atf_test_case bectl_jail cleanup
bectl_jail_head()
{

	atf_set "descr" "Check bectl rename"
	atf_set "require.user" root
}
bectl_jail_body()
{
	cwd=$(realpath .)
	zpool=$(make_zpool_name)
	disk=${cwd}/disk.img
	mount=${cwd}/mnt
	root=${mount}/root

	if [ ! -f /rescue/rescue ]; then
		atf_skip "This test requires a rescue binary"
	fi
	bectl_create_deep_setup ${zpool} ${disk} ${mount}
	# Prepare our minimal BE... plop a rescue binary into it
	atf_check mkdir -p ${root}
	atf_check -o ignore bectl -r ${zpool}/ROOT mount default ${root}
	atf_check mkdir -p ${root}/rescue
	atf_check cp /rescue/rescue ${root}/rescue/rescue
	atf_check bectl -r ${zpool}/ROOT umount default

	# Prepare a second boot environment
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT create -e default target

	# When a jail name is not explicit, it should match the jail id.
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT jail -b -o jid=233637 default
	atf_check -o inline:"233637\n" -s exit:0 -x "jls -j 233637 name"
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT unjail default

	# Basic command-mode tests, with and without jail cleanup
	atf_check -o inline:"rescue\nusr\n" bectl -r ${zpool}/ROOT \
	    jail default /rescue/rescue ls -1
	atf_check -o inline:"rescue\nusr\n" bectl -r ${zpool}/ROOT \
	    jail -Uo path=${root} default /rescue/rescue ls -1
	atf_check [ -f ${root}/rescue/rescue ]
	atf_check bectl -r ${zpool}/ROOT ujail default

	# Batch mode tests
	atf_check bectl -r ${zpool}/ROOT jail -bo path=${root} default
	atf_check -o not-empty -x "jls | grep -F \"${root}\""
	atf_check bectl -r ${zpool}/ROOT ujail default
	atf_check -s not-exit:0 -x "jls | grep -F \"${root}\""
	# 'unjail' naming
	atf_check bectl -r ${zpool}/ROOT jail -b default
	atf_check bectl -r ${zpool}/ROOT unjail default
	atf_check -s not-exit:0 -x "jls | grep -F \"${root}\""
	# 'unjail' by BE name. Force bectl to lookup jail id by the BE name.
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT jail -b default
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT jail -b -o name=bectl_test target
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT unjail target
	atf_check -o empty -s exit:0 bectl -r ${zpool}/ROOT unjail default
	# cannot unjail an unjailed BE (by either command name)
	atf_check -e ignore -s not-exit:0 bectl -r ${zpool}/ROOT ujail default
	atf_check -e ignore -s not-exit:0 bectl -r ${zpool}/ROOT unjail default

	# set+unset
	atf_check bectl -r ${zpool}/ROOT jail -b -o path=${root} -u path default
	# Ensure that it didn't mount at ${root}
	atf_check -s not-exit:0 -x "mount | grep -F '${root}'"
	atf_check bectl -r ${zpool}/ROOT ujail default
}

# If a test has failed, it's possible that the boot environment hasn't
# been 'unjail'ed. We want to remove the jail before 'bectl_cleanup'
# attempts to destroy the zpool.
bectl_jail_cleanup()
{
	for bootenv in "default" "target"; do
		# mountpoint of the boot environment
		mountpoint="$(bectl -r bectl_test/ROOT list -H | grep ${bootenv} | awk '{print $3}')"

		# see if any jail paths match the boot environment mountpoint
		jailid="$(jls | grep ${mountpoint} | awk '{print $1}')"

		if [ -z "$jailid" ]; then
		       continue;
		fi
		jail -r ${jailid}
	done;

	bectl_cleanup $(get_zpool_name)
}

atf_init_test_cases()
{
	atf_add_test_case bectl_create
	atf_add_test_case bectl_destroy
	atf_add_test_case bectl_export_import
	atf_add_test_case bectl_list
	atf_add_test_case bectl_mount
	atf_add_test_case bectl_rename
	atf_add_test_case bectl_jail
}
