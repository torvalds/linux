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

# A note on specs:
# - A copy of the ISO-9660 spec can be found here:
#   https://www.ecma-international.org/publications/files/ECMA-ST/Ecma-119.pdf
# - Any references to `rockridge` are referring to the `Rock Ridge` extensions
#   of the ISO-9660 spec. A copy of the draft `IEEE-P1282` spec can be found
#   here:
#   http://www.ymi.com/ymi/sites/default/files/pdf/Rockridge.pdf

MAKEFS="makefs -t cd9660"
MOUNT="mount_cd9660"

. "$(dirname "$0")/makefs_tests_common.sh"

common_cleanup()
{
	if ! test_md_device=$(cat $TEST_MD_DEVICE_FILE); then
		echo "$TEST_MD_DEVICE_FILE could not be opened; has an md(4) device been attached?"
		return
	fi

	umount -f /dev/$test_md_device || :
	mdconfig -d -u $test_md_device || :
}

check_base_iso9660_image_contents()
{
	# Symlinks are treated like files when rockridge support isn't
	# specified
	check_image_contents "$@" -X c

	atf_check -e empty -o empty -s exit:0 test -L $TEST_INPUTS_DIR/c
	atf_check -e empty -o empty -s exit:0 test -f $TEST_MOUNT_DIR/c
}

check_cd9660_support() {
	kldstat -m cd9660 || \
		atf_skip "Requires cd9660 filesystem support to be present in the kernel"
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

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -F $TEST_SPEC_FILE -M 1m $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
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
	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS $TEST_IMAGE $TEST_SPEC_FILE
	cd -

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
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

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS $TEST_IMAGE $TEST_INPUTS_DIR $test_inputs_dir2

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents -d $test_inputs_dir2
}
from_multiple_dirs_cleanup()
{
	common_cleanup
}

atf_test_case from_single_dir cleanup
from_single_dir_body()
{
	create_test_inputs

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
}
from_single_dir_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_allow_deep_trees cleanup
o_flag_allow_deep_trees_body()
{
	create_test_inputs

	# Make sure the "more than 8 levels deep" requirement is met.
	atf_check -e empty -o empty -s exit:0 \
	    mkdir -p $TEST_INPUTS_DIR/a/b/c/d/e/f/g/h/i/j

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o allow-deep-trees $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
}
o_flag_allow_deep_trees_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_allow_max_name cleanup
o_flag_allow_max_name_body()
{
	atf_expect_fail "-o allow-max-name doesn't appear to be implemented on FreeBSD's copy of makefs [yet]"

	create_test_inputs

	long_path=$TEST_INPUTS_DIR/$(jot -s '' -b 0 37)

	# Make sure the "37 char name" limit requirement is met.
	atf_check -e empty -o empty -s exit:0 touch $long_path

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o allow-max-name $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
}
o_flag_allow_max_name_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_isolevel_1 cleanup
o_flag_isolevel_1_body()
{
	atf_expect_fail "this testcase needs work; the filenames generated seem incorrect/corrupt"

	create_test_inputs

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o isolevel=1 $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
}
o_flag_isolevel_1_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_isolevel_2 cleanup
o_flag_isolevel_2_body()
{
	create_test_inputs

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o isolevel=2 $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
}
o_flag_isolevel_2_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_isolevel_3 cleanup
o_flag_isolevel_3_body()
{
	create_test_inputs

	# XXX: isolevel=3 isn't implemented yet. See FreeBSD bug # 203645
	if true; then
	atf_check -e match:'makefs: ISO Level 3 is greater than 2\.' -o empty -s not-exit:0 \
	    $MAKEFS -o isolevel=3 $TEST_IMAGE $TEST_INPUTS_DIR
	else
	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o isolevel=3 $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_base_iso9660_image_contents
	fi
}
o_flag_isolevel_3_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_preparer
o_flag_preparer_head()
{
	atf_set "require.progs" "strings"
}
o_flag_preparer_body()
{
	create_test_dirs

	preparer='My Very First ISO'
	preparer_uppercase="$(echo $preparer | tr '[[:lower:]]' '[[:upper:]]')"

	atf_check -e empty -o empty -s exit:0 touch $TEST_INPUTS_DIR/dummy_file
	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o preparer="$preparer" $TEST_IMAGE $TEST_INPUTS_DIR
	atf_check -e empty -o match:"$preparer_uppercase" -s exit:0 \
	    strings $TEST_IMAGE
}

atf_test_case o_flag_publisher
o_flag_publisher_head()
{
	atf_set "require.progs" "strings"
}
o_flag_publisher_body()
{
	create_test_dirs

	publisher='My Super Awesome Publishing Company LTD'
	publisher_uppercase="$(echo $publisher | tr '[[:lower:]]' '[[:upper:]]')"

	atf_check -e empty -o empty -s exit:0 touch $TEST_INPUTS_DIR/dummy_file
	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o publisher="$publisher" $TEST_IMAGE $TEST_INPUTS_DIR
	atf_check -e empty -o match:"$publisher_uppercase" -s exit:0 \
	    strings $TEST_IMAGE
}

atf_test_case o_flag_rockridge cleanup
o_flag_rockridge_body()
{
	create_test_dirs

	# Make sure the "more than 8 levels deep" requirement is met.
	atf_check -e empty -o empty -s exit:0 \
	    mkdir -p $TEST_INPUTS_DIR/a/b/c/d/e/f/g/h/i/j

	# Make sure the "pathname larger than 255 chars" requirement is met.
	#
	# $long_path's needs to be nested in a directory, as creating it
	# outright as a 256 char filename via touch will fail with ENAMETOOLONG
	long_path=$TEST_INPUTS_DIR/$(jot -s '/' -b "$(jot -s '' -b 0 64)" 4)
	atf_check -e empty -o empty -s exit:0 mkdir -p "$(dirname $long_path)"
	atf_check -e empty -o empty -s exit:0 touch "$long_path"

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o rockridge $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_image_contents -X .rr_moved

	# .rr_moved is a special directory created when you have deep directory
	# trees with rock ridge extensions on
	atf_check -e empty -o empty -s exit:0 \
	    test -d $TEST_MOUNT_DIR/.rr_moved
}
o_flag_rockridge_cleanup()
{
	common_cleanup
}

atf_test_case o_flag_rockridge_dev_nodes cleanup
o_flag_rockridge_dev_nodes_head()
{
	atf_set "descr" "Functional tests to ensure that dev nodes are handled properly with rockridge extensions (NetBSD kern/48852; FreeBSD bug 203648)"
}
o_flag_rockridge_dev_nodes_body()
{
	create_test_dirs

	(tar -cvf - -C /dev null && touch .tar_ok) | \
	atf_check -e not-empty -o empty -s exit:0 tar -xvf - -C "$TEST_INPUTS_DIR"

	atf_check -e empty -o empty -s exit:0 test -c $TEST_INPUTS_DIR/null
	atf_check -e empty -o empty -s exit:0 test -f .tar_ok

	atf_check -e empty -o empty -s exit:0 \
	    $MAKEFS -o rockridge $TEST_IMAGE $TEST_INPUTS_DIR

	check_cd9660_support
	mount_image
	check_image_contents
}
o_flag_rockridge_dev_nodes_cleanup()
{
	common_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case D_flag
	atf_add_test_case F_flag

	atf_add_test_case from_mtree_spec_file
	atf_add_test_case from_multiple_dirs
	atf_add_test_case from_single_dir

	atf_add_test_case o_flag_allow_deep_trees
	atf_add_test_case o_flag_allow_max_name
	atf_add_test_case o_flag_isolevel_1
	atf_add_test_case o_flag_isolevel_2
	atf_add_test_case o_flag_isolevel_3
	atf_add_test_case o_flag_preparer
	atf_add_test_case o_flag_publisher
	atf_add_test_case o_flag_rockridge
	atf_add_test_case o_flag_rockridge_dev_nodes
}
