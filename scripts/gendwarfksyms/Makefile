# SPDX-License-Identifier: GPL-2.0
hostprogs-always-y += gendwarfksyms

gendwarfksyms-objs += gendwarfksyms.o
gendwarfksyms-objs += cache.o
gendwarfksyms-objs += die.o
gendwarfksyms-objs += dwarf.o
gendwarfksyms-objs += symbols.o

HOSTLDLIBS_gendwarfksyms := -ldw -lelf
