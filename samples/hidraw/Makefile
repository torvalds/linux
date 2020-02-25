# SPDX-License-Identifier: GPL-2.0
# List of programs to build
hostprogs := hid-example
always-y := $(hostprogs)

HOSTCFLAGS_hid-example.o += -I$(objtree)/usr/include

all: hid-example
