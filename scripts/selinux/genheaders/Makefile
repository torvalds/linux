# SPDX-License-Identifier: GPL-2.0
hostprogs	:= genheaders
HOST_EXTRACFLAGS += \
	-I$(srctree)/include/uapi -I$(srctree)/include \
	-I$(srctree)/security/selinux/include

always-y	:= $(hostprogs)
