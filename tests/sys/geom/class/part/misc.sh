# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2018 Alan Somers
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

MD_DEVS="md.devs"

atf_test_case blank_physpath cleanup
blank_physpath_head()
{
	atf_set "descr" "gpart shouldn't add physical paths to underlying providers that have none"
	atf_set "require.user" "root"
}
blank_physpath_body()
{
	load_gnop
	load_gpart
	md=$(alloc_md)
	atf_check -o empty -e ignore diskinfo -p ${md}
	atf_check -s exit:0 -o ignore gpart create -s bsd ${md}
	atf_check -s exit:0 -o ignore gpart add -t freebsd-ufs ${md}
	atf_check -o empty -e ignore diskinfo -p ${md}a
}
blank_physpath_cleanup()
{
	common_cleanup
}


atf_test_case bsd_physpath cleanup
bsd_physpath_head()
{
	atf_set "descr" "BSD partitions should append /X to the underlying device's physical path"
	atf_set "require.user" "root"
}
bsd_physpath_body()
{
	load_gnop
	load_gpart
	md=$(alloc_md)
	physpath="some/physical/path"
	atf_check gnop create -z $physpath /dev/${md}
	atf_check -s exit:0 -o ignore gpart create -s bsd ${md}.nop
	atf_check -s exit:0 -o ignore gpart add -t freebsd-ufs ${md}.nop
	gpart_physpath=$(diskinfo -p ${md}.nopa)
	atf_check_equal "${physpath}/a" "$gpart_physpath"
}
bsd_physpath_cleanup()
{
	common_cleanup
}

atf_test_case gpt_physpath cleanup
gpt_physpath_head()
{
	atf_set "descr" "GPT partitions should append /pX to the underlying device's physical path"
	atf_set "require.user" "root"
}
gpt_physpath_body()
{
	load_gnop
	load_gpart
	md=$(alloc_md)
	physpath="some/physical/path"
	atf_check gnop create -z $physpath /dev/${md}
	atf_check -s exit:0 -o ignore gpart create -s gpt ${md}.nop
	atf_check -s exit:0 -o ignore gpart add -t efi ${md}.nop
	gpart_physpath=$(diskinfo -p ${md}.nopp1)
	atf_check_equal "${physpath}/p1" "$gpart_physpath"
}
gpt_physpath_cleanup()
{
	common_cleanup
}

atf_test_case mbr_physpath cleanup
mbr_physpath_head()
{
	atf_set "descr" "MBR partitions should append /sX to the underlying device's physical path"
	atf_set "require.user" "root"
}
mbr_physpath_body()
{
	load_gnop
	load_gpart
	md=$(alloc_md)
	physpath="some/physical/path"
	atf_check gnop create -z $physpath /dev/${md}
	atf_check -s exit:0 -o ignore gpart create -s mbr ${md}.nop
	atf_check -s exit:0 -o ignore gpart add -t freebsd ${md}.nop
	gpart_physpath=$(diskinfo -p ${md}.nops1)
	atf_check_equal "${physpath}/s1" "$gpart_physpath"
}
mbr_physpath_cleanup()
{
	common_cleanup
}

atf_test_case mbr_bsd_physpath cleanup
mbr_bsd_physpath_head()
{
	atf_set "descr" "BSD partitions nested within MBR partitions should append /sX/Y to the underlying device's physical path"
	atf_set "require.user" "root"
}
mbr_bsd_physpath_body()
{
	load_gnop
	load_gpart
	md=$(alloc_md)
	physpath="some/physical/path"
	atf_check gnop create -z $physpath /dev/${md}
	atf_check -s exit:0 -o ignore gpart create -s mbr ${md}.nop
	atf_check -s exit:0 -o ignore gpart add -t freebsd ${md}.nop
	atf_check -s exit:0 -o ignore gpart create -s bsd ${md}.nops1
	atf_check -s exit:0 -o ignore gpart add -t freebsd-ufs ${md}.nops1
	gpart_physpath=$(diskinfo -p ${md}.nops1a)
	atf_check_equal "${physpath}/s1/a" "$gpart_physpath"
}
mbr_bsd_physpath_cleanup()
{
	common_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case blank_physpath
	atf_add_test_case bsd_physpath
	atf_add_test_case gpt_physpath
	atf_add_test_case mbr_physpath
	atf_add_test_case mbr_bsd_physpath
}

alloc_md()
{
	local md

	md=$(mdconfig -a -t swap -s 1M) || atf_fail "mdconfig -a failed"
	echo ${md} >> $MD_DEVS
	echo ${md}
}

common_cleanup()
{
	if [ -f "$MD_DEVS" ]; then
		while read test_md; do
			gnop destroy -f ${test_md}.nop 2>/dev/null
			mdconfig -d -u $test_md 2>/dev/null
		done < $MD_DEVS
		rm $MD_DEVS
	fi
	true
}

load_gpart()
{
	if ! kldstat -q -m g_part; then
		geom part load || atf_skip "could not load module for geom part"
	fi
}

load_gnop()
{
	if ! kldstat -q -m g_nop; then
		geom nop load || atf_skip "could not load module for geom nop"
	fi
}
