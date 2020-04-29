# SPDX-License-Identifier: GPL-2.0-only
# List of programs to build
hostprogs := \
	test-fsmount \
	test-statx

always-y := $(hostprogs)

HOSTCFLAGS_test-fsmount.o += -I$(objtree)/usr/include
HOSTCFLAGS_test-statx.o += -I$(objtree)/usr/include
