# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2016 Alan Somers
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
PLAINFILES=plainfiles

atf_test_case preserve_props cleanup
preserve_props_head()
{
	atf_set "descr" "gnop should preserve basic GEOM properties"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
preserve_props_body()
{
	load_gnop
	us=$(alloc_md)
	atf_check gnop create /dev/${us}
	md_secsize=$(diskinfo ${us} | cut -wf 2)
	md_mediasize=$(diskinfo ${us} | cut -wf 3)
	md_stripesize=$(diskinfo ${us} | cut -wf 5)
	nop_secsize=$(diskinfo ${us}.nop | cut -wf 2)
	nop_mediasize=$(diskinfo ${us}.nop | cut -wf 3)
	nop_stripesize=$(diskinfo ${us}.nop | cut -wf 5)
	atf_check_equal "$md_secsize" "$nop_secsize"
	atf_check_equal "$md_mediasize" "$nop_mediasize"
	atf_check_equal "$md_stripesize" "$nop_stripesize"
}
preserve_props_cleanup()
{
	common_cleanup
}

atf_test_case preserve_disk_props cleanup
preserve_disk_props_head()
{
	atf_set "descr" "gnop should preserve properties for disks"
	atf_set "require.user" "root"
	atf_set "require.config" "disks"
	atf_set "timeout" 15
}
preserve_disk_props_body()
{
	load_gnop
	disks=`atf_config_get disks`
	disk=${disks%% *}
	if [ -z "$disk" ]; then
		atf_skip "Must define disks (see tests(7))"
	fi
	atf_check gnop create ${disk}

	disk_ident=$(diskinfo -s ${disk})
	disk_physpath=$(diskinfo -p ${disk})
	disk_descr=$(diskinfo -v ${disk} | awk '/Disk descr/ {print $1}')
	disk_trim=$(diskinfo -v ${disk} | awk '/TRIM.UNMAP/ {print $1}')
	disk_rotrate=$(diskinfo -v ${disk} | awk '/Rotation rate/ {print $1}')
	disk_zonemode=$(diskinfo -v ${disk} | awk '/Zone Mode/ {print $1}')
	nop_ident=$(diskinfo -s ${disk}.nop)
	nop_physpath=$(diskinfo -p ${disk}.nop)
	nop_descr=$(diskinfo -v ${disk}.nop | awk '/Disk descr/ {print $1}')
	nop_trim=$(diskinfo -v ${disk}.nop | awk '/TRIM.UNMAP/ {print $1}')
	nop_rotrate=$(diskinfo -v ${disk}.nop | awk '/Rotation/ {print $1}')
	nop_zonemode=$(diskinfo -v ${disk}.nop | awk '/Zone Mode/ {print $1}')
	atf_check_equal "$disk_ident" "$nop_ident"
	atf_check_equal "$disk_physpath" "$nop_physpath"
	atf_check_equal "$disk_descr" "$nop_descr"
	atf_check_equal "$disk_trim" "$nop_trim"
	atf_check_equal "$disk_rotrate" "$nop_rotrate"
	atf_check_equal "$disk_zonemode" "$nop_zonemode"
}
preserve_disk_props_cleanup()
{
	disk_cleanup
	common_cleanup
}

atf_test_case io cleanup
io_head()
{
	atf_set "descr" "I/O works on gnop devices"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
io_body()
{
	load_gnop
	us=$(alloc_md)
	atf_check gnop create /dev/${us}

	echo src >> $PLAINFILES
	echo dst >> $PLAINFILES
	dd if=/dev/random of=src bs=1m count=1 >/dev/null 2>&1
	dd if=src of=/dev/${us}.nop bs=1m count=1 > /dev/null 2>&1
	dd if=/dev/${us}.nop of=dst bs=1m count=1 > /dev/null 2>&1

	atf_check_equal `md5 -q src` `md5 -q dst`
}
io_cleanup()
{
	common_cleanup
}

atf_test_case physpath cleanup
physpath_head()
{
	atf_set "descr" "Test gnop's -z option"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
physpath_body()
{
	load_gnop
	us=$(alloc_md)
	physpath="some/physical/path"
	atf_check gnop create -z $physpath /dev/${us}
	gnop_physpath=$(diskinfo -p ${us}.nop)
	atf_check_equal "$physpath" "$gnop_physpath"
}
physpath_cleanup()
{
	common_cleanup
}

atf_test_case physpath_blank cleanup
physpath_blank_head()
{
	atf_set "descr" "gnop can set physical path to the empty string"
	atf_set "require.user" "root"
	atf_set "require.config" "disks"
	atf_set "timeout" 15
}
physpath_blank_body()
{
	load_gnop
	disks=`atf_config_get disks`
	disk=${disks%% *}
	if [ -z "$disk" ]; then
		atf_skip "Must define disks (see tests(7))"
	fi

	atf_check gnop create -z "" ${disk}
	gnop_physpath=$(diskinfo -p ${disk}.nop)
	atf_check_equal "" "$gnop_physpath"
}
physpath_blank_cleanup()
{
	disk_cleanup
	common_cleanup
}

atf_test_case size cleanup
size_head()
{
	atf_set "descr" "Test gnop's -s option"
	atf_set "require.user" "root"
	atf_set "timeout" 15
}
size_body()
{
	load_gnop
	us=$(alloc_md)
	for mediasize in 65536 524288 1048576; do
		atf_check gnop create -s ${mediasize} /dev/${us}
		gnop_mediasize=`diskinfo /dev/${us}.nop | cut -wf 3`
		atf_check_equal "${mediasize}" "${gnop_mediasize}"
		atf_check gnop destroy /dev/${us}.nop
	done
	# We shouldn't be able to extend the provider's size
	atf_check -s not-exit:0 -e ignore gnop create -s 2097152 /dev/${us}
}
size_cleanup()
{
	common_cleanup
}

atf_test_case stripesize cleanup
stripesize_head()
{
	atf_set "descr" "Test gnop's -p and -P options"
	atf_set "require.user" "root"
	atf_set "timeout" 120
}
stripesize_body()
{
	load_gnop
	us=$(alloc_md)
	for ss in 512 1024 2048 4096 8192; do
		for sofs in `seq 0 512 ${ss}`; do
			[ "$sofs" -eq "$ss" ] && continue
			atf_check gnop create -p ${ss} -P ${sofs} /dev/${us}
			gnop_ss=`diskinfo /dev/${us}.nop | cut -wf 5`
			gnop_sofs=`diskinfo /dev/${us}.nop | cut -wf 6`
			atf_check_equal "${ss}" "${gnop_ss}"
			atf_check_equal "${sofs}" "${gnop_sofs}"
			atf_check gnop destroy /dev/${us}.nop
		done
	done
}
stripesize_cleanup()
{
	common_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case io
	atf_add_test_case physpath
	atf_add_test_case physpath_blank
	atf_add_test_case preserve_props
	atf_add_test_case preserve_disk_props
	atf_add_test_case stripesize
	atf_add_test_case size
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
		gnop destroy -f ${disk}.nop 2>/dev/null
	fi
}

load_gnop()
{
	if ! kldstat -q -m g_nop; then
		geom nop load || atf_skip "could not load module for geom nop"
	fi
}
