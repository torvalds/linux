# SPDX-License-Identifier: GPL-2.0
hostprogs-y	:= genheaders
HOST_EXTRACFLAGS += \
	-I$(srctree)/include/uapi -I$(srctree)/include \
	-I$(srctree)/security/selinux/include

always		:= $(hostprogs-y)
