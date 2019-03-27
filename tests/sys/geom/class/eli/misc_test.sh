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

. $(atf_get_srcdir)/conf.sh

atf_test_case preserve_props cleanup
preserve_props_head()
{
	atf_set "descr" "geli should preserve basic GEOM properties"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
preserve_props_body()
{
	geli_test_setup

	md=$(attach_md -s1m)
	atf_check geli onetime /dev/${md}
	md_secsize=$(diskinfo ${md} | cut -wf 2)
	md_stripesize=$(diskinfo ${md} | cut -wf 5)
	eli_secsize=$(diskinfo ${md}.eli | cut -wf 2)
	eli_stripesize=$(diskinfo ${md}.eli | cut -wf 5)
	atf_check_equal "$md_secsize" "$eli_secsize"
	atf_check_equal "$md_stripesize" "$eli_stripesize"
}
preserve_props_cleanup()
{
	geli_test_cleanup
}

atf_test_case preserve_disk_props cleanup
preserve_disk_props_head()
{
	atf_set "descr" "geli should preserve properties for disks"
	atf_set "require.user" "root"
	atf_set "require.config" "disks"
	atf_set "timeout" 15
}
preserve_disk_props_body()
{
	geli_test_setup

	disks=`atf_config_get disks`
	disk=${disks%% *}
	if [ -z "$disk" ]; then
		atf_skip "Must define disks (see tests(7))"
	fi
	atf_check geli onetime ${disk}

	disk_ident=$(diskinfo -s ${disk})
	disk_descr=$(diskinfo -v ${disk} | awk '/Disk descr/ {print $1}')
	disk_rotrate=$(diskinfo -v ${disk} | awk '/Rotation rate/ {print $1}')
	disk_zonemode=$(diskinfo -v ${disk} | awk '/Zone Mode/ {print $1}')
	eli_ident=$(diskinfo -s ${disk}.eli)
	eli_descr=$(diskinfo -v ${disk}.eli | awk '/Disk descr/ {print $1}')
	eli_rotrate=$(diskinfo -v ${disk}.eli | awk '/Rotation/ {print $1}')
	eli_zonemode=$(diskinfo -v ${disk}.eli | awk '/Zone Mode/ {print $1}')
	atf_check_equal "$disk_ident" "$eli_ident"
	atf_check_equal "$disk_descr" "$eli_descr"
	atf_check_equal "$disk_rotrate" "$eli_rotrate"
	atf_check_equal "$disk_zonemode" "$eli_zonemode"
}
preserve_disk_props_cleanup()
{
	disk_cleanup
	geli_test_cleanup
}

atf_test_case physpath cleanup
physpath_head()
{
	atf_set "descr" "geli should append /eli to the underlying device's physical path"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
physpath_body()
{
	geli_test_setup
	if ! error_message=$(geom_load_class_if_needed nop); then
		atf_skip "$error_message"
	fi

	md=$(attach_md -s1m)
	# If the underlying device has no physical path, then geli should not
	# create one.
	atf_check -o empty -e ignore diskinfo -p $md
	atf_check -s exit:0 geli onetime $md
	atf_check -o empty -e ignore diskinfo -p $md.eli
	atf_check -s exit:0 geli kill $md

	# If the underlying device does have a physical path, then geli should
	# append "/eli"
	physpath="some/physical/path"
	atf_check gnop create -z $physpath ${md}
	atf_check -s exit:0 geli onetime $md.nop
	atf_check -o match:"^${physpath}/eli$" diskinfo -p $md.nop.eli
}
physpath_cleanup()
{
	if [ -f "$TEST_MDS_FILE" ]; then
		while read md; do
			[ -c /dev/${md}.nop.eli ] && \
				geli detach $md.nop.eli 2>/dev/null
			[ -c /dev/${md}.nop ] && \
				gnop destroy -f $md.nop 2>/dev/null
			[ -c /dev/${md}.eli ] && \
				geli detach $md.eli 2>/dev/null
			mdconfig -d -u $md 2>/dev/null
		done < $TEST_MDS_FILE
	fi
	true
}

atf_init_test_cases()
{
	atf_add_test_case physpath
	atf_add_test_case preserve_props
	atf_add_test_case preserve_disk_props
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

	if [ -f "$PLAINFILES" ]; then
		while read f; do
			rm -f ${f}
		done < ${PLAINFILES}
		rm ${PLAINFILES}
	fi
	true
}

disk_cleanup()
{
	disks=`atf_config_get disks`
	disk=${disks%% *}
	if [ -n "$disk" ]; then
		geli kill ${disk} 2>/dev/null
	fi
}
