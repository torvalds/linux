# SPDX-License-Identifier: GPL-2.0-only
# List of programs to build
hostprogs-y := uhid-example

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_uhid-example.o += -I$(objtree)/usr/include
