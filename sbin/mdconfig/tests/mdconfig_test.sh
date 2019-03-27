# Copyright (c) 2012 Edward Tomasz Napierała <trasz@FreeBSD.org>
# All rights reserved.
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
#

check_diskinfo()
{
	local md=$1
	local mediasize_in_bytes=$2
	local mediasize_in_sectors=$3
	local sectorsize=${4:-512}
	local stripesize=${5:-0}
	local stripeoffset=${6:-0}

	atf_check -s exit:0 \
	    -o match:"/dev/$md *$sectorsize *$mediasize_in_bytes *$mediasize_in_sectors *$stripesize *$stripeoffset" \
	    -x "diskinfo /dev/$md | expand"
}

cleanup_common()
{
	if [ -f mdconfig.out ]; then
		mdconfig -d -u $(sed -e 's/md//' mdconfig.out)
	fi
}

atf_test_case attach_vnode_non_explicit_type cleanup
attach_vnode_non_explicit_type_head()
{
	atf_set "descr" "Tests out -a / -f without -t"
}
attach_vnode_non_explicit_type_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -x "truncate -s ${size_in_mb}m xxx"
	atf_check -s exit:0 -o save:mdconfig.out -x 'mdconfig -af xxx'
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "2097152"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_non_explicit_type_cleanup()
{
	cleanup_common
}

atf_test_case attach_vnode_implicit_a_f cleanup
attach_vnode_implicit_a_f_head()
{
	atf_set "descr" "Tests out implied -a / -f without -t"
}
attach_vnode_implicit_a_f_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -x "truncate -s ${size_in_mb}m xxx"
	atf_check -s exit:0 -o save:mdconfig.out -x 'mdconfig xxx'
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "2097152"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_implicit_a_f_cleanup()
{
	cleanup_common
}

atf_test_case attach_vnode_explicit_type cleanup
attach_vnode_explicit_type_head()
{
	atf_set "descr" "Tests out implied -a / -f with -t vnode"
}
attach_vnode_explicit_type_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -x "truncate -s ${size_in_mb}m xxx"
	atf_check -s exit:0 -o save:mdconfig.out -x 'mdconfig -af xxx -t vnode'
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "2097152"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_explicit_type_cleanup()
{
	[ -f mdconfig.out ] && mdconfig -d -u $(sed -e 's/md//' mdconfig.out)
	rm -f mdconfig.out xxx
}

atf_test_case attach_vnode_smaller_than_file cleanup
attach_vnode_smaller_than_file_head()
{
	atf_set "descr" "Tests mdconfig -s with size less than the file size"
}
attach_vnode_smaller_than_file_body()
{
	local md
	local size_in_mb=128

	atf_check -s exit:0 -x "truncate -s 1024m xxx"
	atf_check -s exit:0 -o save:mdconfig.out \
	    -x "mdconfig -af xxx -s ${size_in_mb}m"
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "134217728" "262144"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_smaller_than_file_cleanup()
{
	cleanup_common
}

atf_test_case attach_vnode_larger_than_file cleanup
attach_vnode_larger_than_file_head()
{
	atf_set "descr" "Tests mdconfig -s with size greater than the file size"
}
attach_vnode_larger_than_file_body()
{
	local md
	local size_in_gb=128

	atf_check -s exit:0 -x "truncate -s 1024m xxx"
	atf_check -s exit:0 -o save:mdconfig.out \
	    -x "mdconfig -af xxx -s ${size_in_gb}g"
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "137438953472" "268435456"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_gb}G$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_larger_than_file_cleanup()
{
	cleanup_common
}

atf_test_case attach_vnode_sector_size cleanup
attach_vnode_sector_size_head()
{
	atf_set "descr" "Tests mdconfig -s with size greater than the file size"
}
attach_vnode_sector_size_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -x "truncate -s ${size_in_mb}m xxx"
	atf_check -s exit:0 -o save:mdconfig.out \
	    -x "mdconfig -af xxx -S 2048"
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "524288" "2048"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md vnode ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_vnode_sector_size_cleanup()
{
	cleanup_common
}

atf_test_case attach_malloc cleanup
attach_malloc_head()
{
	atf_set "descr" "Tests mdconfig with -t malloc"
}
attach_malloc_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -o save:mdconfig.out \
	    -x 'mdconfig -a -t malloc -s 1g'
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "2097152"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md malloc ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_malloc_cleanup()
{
	cleanup_common
}

atf_test_case attach_swap cleanup
attach_swap_head()
{
	atf_set "descr" "Tests mdconfig with -t swap"
}
attach_swap_body()
{
	local md
	local size_in_mb=1024

	atf_check -s exit:0 -o save:mdconfig.out \
	    -x 'mdconfig -a -t swap -s 1g'
	md=$(cat mdconfig.out)
	atf_check -s exit:0 -o match:'^md[0-9]+$' -x "echo $md"
	check_diskinfo "$md" "1073741824" "2097152"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md swap ${size_in_mb}M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_swap_cleanup()
{
	cleanup_common
}

atf_test_case attach_with_specific_unit_number cleanup
attach_with_specific_unit_number_head()
{
	atf_set "descr" "Tests mdconfig with a unit specified by -u"
}
attach_with_specific_unit_number_body()
{
	local md_unit=99
	local size_in_mb=10

	local md="md${md_unit}"

	echo "$md" > mdconfig.out

	atf_check -s exit:0 -o empty \
	    -x "mdconfig -a -t malloc -s ${size_in_mb}m -u $md_unit"
	check_diskinfo "$md" "10485760" "20480"
	# This awk strips the file path.
	atf_check -s exit:0 -o match:"^$md malloc "$size_in_mb"M$" \
	    -x "mdconfig -lv | awk '\$1 == \"$md\" { print \$1, \$2, \$3 }'"
}
attach_with_specific_unit_number_cleanup()
{
	cleanup_common
}

atf_init_test_cases()
{
	atf_add_test_case attach_vnode_non_explicit_type
	atf_add_test_case attach_vnode_explicit_type
	atf_add_test_case attach_vnode_smaller_than_file
	atf_add_test_case attach_vnode_larger_than_file
	atf_add_test_case attach_vnode_sector_size
	atf_add_test_case attach_malloc
	atf_add_test_case attach_swap
	atf_add_test_case attach_with_specific_unit_number
}
