# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for Linux filesystem routines that are shared by client and server.
#

obj-$(CONFIG_NFS_ACL_SUPPORT) += nfs_acl.o
nfs_acl-objs := nfsacl.o

obj-$(CONFIG_NFS_COMMON_LOCALIO_SUPPORT) += nfs_localio.o
nfs_localio-objs := nfslocalio.o

obj-$(CONFIG_GRACE_PERIOD) += grace.o
obj-$(CONFIG_NFS_V4_2_SSC_HELPER) += nfs_ssc.o

obj-$(CONFIG_NFS_COMMON) += common.o
