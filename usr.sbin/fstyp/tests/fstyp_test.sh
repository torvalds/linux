#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2015 Alan Somers
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

# $FreeBSD$

atf_test_case cd9660
cd9660_head() {
	atf_set "descr" "fstyp(8) should detect cd9660 filesystems"
}
cd9660_body() {
	atf_check -s exit:0 mkdir -p dir/emptydir	# makefs requires a nonempty directory
	atf_check -s exit:0 -o ignore makefs -t cd9660 -Z -s 64m cd9660.img dir
	atf_check -s exit:0 -o inline:"cd9660\n" fstyp cd9660.img
	atf_check -s exit:0 -o inline:"cd9660\n" fstyp -l cd9660.img
}

atf_test_case cd9660_label
cd9660_label_head() {
	atf_set "descr" "fstyp(8) can read the label on a cd9660 filesystem"
}
cd9660_label_body() {
	atf_check -s exit:0 mkdir -p dir/emptydir	# makefs requires a nonempty directory
	atf_check -s exit:0 -o ignore makefs -t cd9660 -o label=Foo -Z -s 64m cd9660.img dir
	atf_check -s exit:0 -o inline:"cd9660\n" fstyp cd9660.img
	# Note: cd9660 labels are always upper case
	atf_check -s exit:0 -o inline:"cd9660 FOO\n" fstyp -l cd9660.img
}

atf_test_case dir
dir_head() {
	atf_set "descr" "fstyp(8) should fail on a directory"
}
dir_body() {
	atf_check -s exit:0 mkdir dir
	atf_check -s exit:1 -e match:"not a disk" fstyp dir
}

atf_test_case exfat
exfat_head() {
	atf_set "descr" "fstyp(8) can detect exFAT filesystems"
}
exfat_body() {
	bzcat $(atf_get_srcdir)/dfr-01-xfat.img.bz2 > exfat.img
	atf_check -s exit:0 -o inline:"exfat\n" fstyp -u exfat.img
}

atf_test_case empty
empty_head() {
	atf_set "descr" "fstyp(8) should fail on an empty file"
}
empty_body() {
	atf_check -s exit:0 touch empty
	atf_check -s exit:1 -e match:"filesystem not recognized" fstyp empty
}

atf_test_case ext2
ext2_head() {
	atf_set "descr" "fstyp(8) can detect ext2 filesystems"
}
ext2_body() {
	bzcat $(atf_get_srcdir)/ext2.img.bz2 > ext2.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp ext2.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp -l ext2.img
}

atf_test_case ext3
ext3_head() {
	atf_set "descr" "fstyp(8) can detect ext3 filesystems"
}
ext3_body() {
	bzcat $(atf_get_srcdir)/ext3.img.bz2 > ext3.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp ext3.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp -l ext3.img
}

atf_test_case ext4
ext4_head() {
	atf_set "descr" "fstyp(8) can detect ext4 filesystems"
}
ext4_body() {
	bzcat $(atf_get_srcdir)/ext4.img.bz2 > ext4.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp ext4.img
	atf_check -s exit:0 -o inline:"ext2fs\n" fstyp -l ext4.img
}

atf_test_case ext4_label
ext4_label_head() {
	atf_set "descr" "fstyp(8) can read the label on an ext4 filesystem"
}
ext4_label_body() {
	bzcat $(atf_get_srcdir)/ext4_with_label.img.bz2 > ext4_with_label.img
	atf_check -s exit:0 -o inline:"ext2fs foo\n" fstyp -l ext4_with_label.img
}

atf_test_case fat12
fat12_head() {
	atf_set "descr" "fstyp(8) can detect FAT12 filesystems"
}
fat12_body() {
	atf_check -s exit:0 truncate -s 64m msdos.img
	atf_check -s exit:0 -o ignore -e ignore newfs_msdos -F 12 ./msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp -l msdos.img
}

atf_test_case fat16
fat16_head() {
	atf_set "descr" "fstyp(8) can detect FAT16 filesystems"
}
fat16_body() {
	atf_check -s exit:0 truncate -s 64m msdos.img
	atf_check -s exit:0 -o ignore -e ignore newfs_msdos -F 16 ./msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp -l msdos.img
}

atf_test_case fat32
fat32_head() {
	atf_set "descr" "fstyp(8) can detect FAT32 filesystems"
}
fat32_body() {
	atf_check -s exit:0 truncate -s 64m msdos.img
	atf_check -s exit:0 -o ignore -e ignore newfs_msdos -F 32 -c 1 \
		./msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp -l msdos.img
}

atf_test_case fat32_label
fat32_label_head() {
	atf_set "descr" "fstyp(8) can read the label on an msdos filesystem"
}
fat32_label_body() {
	atf_check -s exit:0 truncate -s 64m msdos.img
	atf_check -s exit:0 -o ignore -e ignore newfs_msdos -F 32 -L Foo -c 1 \
		./msdos.img
	atf_check -s exit:0 -o inline:"msdosfs\n" fstyp msdos.img
	# Note: msdos labels are always upper case
	atf_check -s exit:0 -o inline:"msdosfs FOO\n" fstyp -l msdos.img
}

atf_test_case ntfs
ntfs_head() {
	atf_set "descr" "fstyp(8) can detect ntfs filesystems"
}
ntfs_body() {
	bzcat $(atf_get_srcdir)/ntfs.img.bz2 > ntfs.img
	atf_check -s exit:0 -o inline:"ntfs\n" fstyp ntfs.img
	atf_check -s exit:0 -o inline:"ntfs\n" fstyp -l ntfs.img
}

atf_test_case ntfs_with_label
ntfs_with_label_head() {
	atf_set "descr" "fstyp(8) can read labels on ntfs filesystems"
}
ntfs_with_label_body() {
	bzcat $(atf_get_srcdir)/ntfs_with_label.img.bz2 > ntfs_with_label.img
	atf_check -s exit:0 -o inline:"ntfs\n" fstyp ntfs_with_label.img
	atf_check -s exit:0 -o inline:"ntfs Foo\n" fstyp -l ntfs_with_label.img
}

atf_test_case ufs1
ufs1_head() {
	atf_set "descr" "fstyp(8) should detect UFS version 1 filesystems"
}
ufs1_body() {
	atf_check -s exit:0 mkdir dir
	atf_check -s exit:0 -o ignore makefs -Z -s 64m ufs.img dir
	atf_check -s exit:0 -o inline:"ufs\n" fstyp ufs.img
	atf_check -s exit:0 -o inline:"ufs\n" fstyp -l ufs.img
}

atf_test_case ufs2
ufs2_head() {
	atf_set "descr" "fstyp(8) should detect UFS version 2 filesystems"
}
ufs2_body() {
	atf_check -s exit:0 mkdir dir
	atf_check -s exit:0 -o ignore makefs -o version=2 -Z -s 64m ufs.img dir
	atf_check -s exit:0 -o inline:"ufs\n" fstyp ufs.img
	atf_check -s exit:0 -o inline:"ufs\n" fstyp -l ufs.img
}

atf_test_case ufs2_label
ufs2_label_head() {
	atf_set "descr" "fstyp(8) can read the label on a UFS v2 filesystem"
}
ufs2_label_body() {
	atf_check -s exit:0 mkdir dir
	atf_check -s exit:0 -o ignore makefs -o version=2,label="foo" -Z -s 64m ufs.img dir
	atf_check -s exit:0 -o inline:"ufs foo\n" fstyp -l ufs.img
}

atf_test_case ufs_on_device cleanup
ufs_on_device_head() {
	atf_set "descr" "fstyp(8) should work on device nodes"
	atf_set "require.user" "root"
}
ufs_on_device_body() {
	mdconfig -a -t swap -s 64m > mdname
	md=$(cat mdname)
	if [ -z "$md" ]; then
		atf_fail "Failed to create md(4) device"
	fi
	atf_check -s exit:0 -o ignore newfs -L foo /dev/$md
	atf_check -s exit:0 -o inline:"ufs\n" fstyp /dev/$md
	atf_check -s exit:0 -o inline:"ufs foo\n" fstyp -l /dev/$md
}
ufs_on_device_cleanup() {
	md=$(cat mdname)
	if [ -n "$md" ]; then
		mdconfig -d -u "$md"
	fi
}

atf_test_case zeros
zeros_head() {
	atf_set "descr" "fstyp(8) should fail on a zero-filled file"
}
zeros_body() {
	atf_check -s exit:0 truncate -s 256m zeros
	atf_check -s exit:1 -e match:"filesystem not recognized" fstyp zeros
}


atf_init_test_cases() {
	atf_add_test_case cd9660
	atf_add_test_case cd9660_label
	atf_add_test_case dir
	atf_add_test_case empty
	atf_add_test_case exfat
	atf_add_test_case ext2
	atf_add_test_case ext3
	atf_add_test_case ext4
	atf_add_test_case ext4_label
	atf_add_test_case fat12
	atf_add_test_case fat16
	atf_add_test_case fat32
	atf_add_test_case fat32_label
	atf_add_test_case ntfs
	atf_add_test_case ntfs_with_label
	atf_add_test_case ufs1
	atf_add_test_case ufs2
	atf_add_test_case ufs2_label
	atf_add_test_case ufs_on_device
	atf_add_test_case zeros
}
