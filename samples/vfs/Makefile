# SPDX-License-Identifier: GPL-2.0-only
# List of programs to build
hostprogs-y := \
	test-fsmount \
	test-statx

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_test-fsmount.o += -I$(objtree)/usr/include
HOSTCFLAGS_test-statx.o += -I$(objtree)/usr/include
