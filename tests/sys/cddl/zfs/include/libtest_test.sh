# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2016 Spectra Logic.  All rights reserved.
# Use is subject to license terms.
#

atf_test_case raidz_dva_to_block_addr
raidz_dva_to_block_addr_head()
{
	atf_set "descr" "Unit tests for raidz_dva_to_block_addr"
}
raidz_dva_to_block_addr_body()
{
	. $(atf_get_srcdir)/default.cfg

	# These test cases were determined by hand on an actual filesystem
	atf_check_equal 3211 `raidz_dva_to_block_addr 0:3f40000:4000 3 13`
}

atf_init_test_cases()
{
	atf_add_test_case raidz_dva_to_block_addr
}
