# SPDX-License-Identifier: GPL-2.0
#
# Makefile for CEPH filesystem.
#

obj-$(CONFIG_CEPH_FS) += ceph.o

ceph-y := super.o inode.o dir.o file.o locks.o addr.o ioctl.o \
	export.o caps.o snap.o xattr.o \
	mds_client.o mdsmap.o strings.o ceph_frag.o \
	debugfs.o

ceph-$(CONFIG_CEPH_FSCACHE) += cache.o
ceph-$(CONFIG_CEPH_FS_POSIX_ACL) += acl.o
