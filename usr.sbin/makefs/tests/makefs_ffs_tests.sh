#
# Copyright 2015 EMC Corp.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

MAKEFS="makefs -t ffs"
MOUNT="mount"

. "$(dirname "$0")/makefs_tests_common.sh"

TEST_TUNEFS_OUTPUT=$TMPDIR/tunefs.output

common_cleanup()
{
	if ! test_md_device=$(cat $TEST_MD_DEVICE_FILE); then
		echo "$TEST_MD_DEVICE_FILE could not be opened; has an md(4) device been attached?"
		return
	fi

	umount -f /dev/$test_md_device || :
	mdconfig -d -u $test_md_device || :
}

check_ffs_image_contents()
{
	atf_check -e save:$TEST_TUNEFS_OUTPUT -o empty -s exit:0 \
	    tunefs -p /dev/$(cat $TEST_MD_DEVICE_FILE)

	check_image_contents "$@"
}

# With no -M, -m, or -s options, makefs should autocalculate the image size
atf_test_case autocalculate_image_size cleanup
autocalculate_image_size_body()
{
	atf_expect_fail "PR 229929 makefs(8) can underestimate image size"
	create_test_inputs

	atf_check -e empty -o save:$TEST_SPEC_FILE -s exit:0 \
	    mtree -c -k "$DEFAULT_MTREE_KEYWORDS" -p $TEST_INPUTS_DIR

	cd $TEST_INPUTS_DIR
	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS $TEST_IMAGE $TEST_SPEC_FILE
	cd -

	mount_image
	check_ffs_image_contents
}
autocalculate_image_size_cleanup()
{
	common_cleanup
}

atf_test_case D_flag cleanup
D_flag_body()
{
	atf_skip "makefs crashes with SIGBUS with dupe mtree entries; see FreeBSD bug # 192839"

	create_test_inputs

	atf_check -e empty -o save:$TEST_SPEC_FILE -s exit:0 \
	    mtree -cp $TEST_INPUTS_DIR
	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -F $TEST_SPEC_FILE -M 1m $TEST_IMAGE $TEST_INPUTS_DIR

	atf_check -e empty -o empty -s exit:0 \
	    cp $TEST_SPEC_FILE spec2.mtree
	atf_check -e empty -o save:dupe_$TEST_SPEC_FILE -s exit:0 \
	    cat $TEST_SPEC_FILE spec2.mtree

	atf_check -e empty -o not-empty -s not-exit:0 \
	    $MAKEFS -F dupe_$TEST_SPEC_FILE -M 1m $TEST_IMAGE $TEST_INPUTS_DIR
	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -D -F dupe_$TEST_SPEC_FILE -M 1m $TEST_IMAGE $TEST_INPUTS_DIR
}
D_flag_cleanup()
{
	common_cleanup
}

atf_test_case F_flag cleanup
F_flag_body()
{
	create_test_inputs

	atf_check -e empty -o save:$TEST_SPEC_FILE -s exit:0 \
	    mtree -cp $TEST_INPUTS_DIR

	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -F $TEST_SPEC_FILE -M 1m $TEST_IMAGE $TEST_INPUTS_DIR

	mount_image
	check_ffs_image_contents
}
F_flag_cleanup()
{
	common_cleanup
}

atf_test_case from_mtree_spec_file cleanup
from_mtree_spec_file_body()
{
	create_test_inputs

	atf_check -e empty -o save:$TEST_SPEC_FILE -s exit:0 \
	    mtree -c -k "$DEFAULT_MTREE_KEYWORDS" -p $TEST_INPUTS_DIR

	cd $TEST_INPUTS_DIR
	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -M 1m $TEST_IMAGE $TEST_SPEC_FILE
	cd -

	mount_image
	check_ffs_image_contents
}
from_mtree_spec_file_cleanup()
{
	common_cleanup
}

atf_test_case from_multiple_dirs cleanup
from_multiple_dirs_body()
{
	test_inputs_dir2=$TMPDIR/inputs2

	create_test_inputs

	atf_check -e empty -o empty -s exit:0 mkdir -p $test_inputs_dir2
	atf_check -e empty -o empty -s exit:0 \
	    touch $test_inputs_dir2/multiple_dirs_test_file

	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -M 1m $TEST_IMAGE $TEST_INPUTS_DIR $test_inputs_dir2

	mount_image
	check_image_contents -d $test_inputs_dir2
}
from_multiple_dirs_cleanup()
{
	common_cleanup
}

atf_test_case from_single_dir cleanup
from_single_dir_body()
{
	create_test_inputs

	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -M 1m $TEST_IMAGE $TEST_INPUTS_DIR

	mount_image
	check_ffs_image_contents
}
from_single_dir_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_version_1 cleanup
o_flag_version_1_body()
{
	ffs_version=1

	platform=$(uname)
	case "$platform" in
	FreeBSD)
		ffs_label=UFS${ffs_version}
		;;
	NetBSD)
		ffs_label=FFSv${ffs_version}
		;;
	*)
		atf_skip "Unsupported platform"
		;;
	esac

	create_test_inputs

	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -M 1m -o version=$ffs_version $TEST_IMAGE $TEST_INPUTS_DIR

	mount_image
	atf_check -e ignore -o match:"$ffs_label" dumpfs $TEST_MOUNT_DIR
	check_ffs_image_contents
}
o_flag_version_1_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_version_2 cleanup
o_flag_version_2_body()
{
	ffs_version=2

	platform=$(uname)
	case "$platform" in
	FreeBSD)
		ffs_label=UFS${ffs_version}
		;;
	NetBSD)
		ffs_label=FFSv${ffs_version}
		;;
	*)
		atf_skip "Unsupported platform"
		;;
	esac

	create_test_inputs

	atf_check -e empty -o not-empty -s exit:0 \
	    $MAKEFS -M 1m -o version=$ffs_version $TEST_IMAGE $TEST_INPUTS_DIR

	mount_image
	atf_check -e ignore -o match:"$ffs_label" dumpfs $TEST_MOUNT_DIR
	check_ffs_image_contents
}
o_flag_version_2_cleanup()
{
	common_cleanup
}

atf_init_test_cases()
{

	atf_add_test_case autocalculate_image_size

	atf_add_test_case D_flag
	atf_add_test_case F_flag

	atf_add_test_case from_mtree_spec_file
	atf_add_test_case from_multiple_dirs
	atf_add_test_case from_single_dir

	atf_add_test_case o_flag_version_1
	atf_add_test_case o_flag_version_2
}
