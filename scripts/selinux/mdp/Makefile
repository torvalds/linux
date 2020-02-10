# SPDX-License-Identifier: GPL-2.0
hostprogs	:= mdp
HOST_EXTRACFLAGS += \
	-I$(srctree)/include/uapi -I$(srctree)/include \
	-I$(srctree)/security/selinux/include -I$(objtree)/include

always-y	:= $(hostprogs)
clean-files	:= policy.* file_contexts
