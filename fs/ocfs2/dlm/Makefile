# SPDX-License-Identifier: GPL-2.0-only
ccflags-y := -I $(srctree)/$(src)/..

obj-$(CONFIG_OCFS2_FS_O2CB) += ocfs2_dlm.o

ocfs2_dlm-objs := dlmdomain.o dlmdebug.o dlmthread.o dlmrecovery.o \
	dlmmaster.o dlmast.o dlmconvert.o dlmlock.o dlmunlock.o
