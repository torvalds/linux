# SPDX-License-Identifier: GPL-2.0
# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := hid-example

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_hid-example.o += -I$(objtree)/usr/include

all: hid-example
