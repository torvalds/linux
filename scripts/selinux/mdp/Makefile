# SPDX-License-Identifier: GPL-2.0
hostprogs-always-y += mdp
HOST_EXTRACFLAGS += \
	-I$(srctree)/include \
	-I$(srctree)/security/selinux/include -I$(objtree)/include

clean-files	:= policy.* file_contexts
