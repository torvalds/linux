# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RESCTRL_FS)		+= rdtgroup.o ctrlmondata.o monitor.o
obj-$(CONFIG_RESCTRL_FS_PSEUDO_LOCK)	+= pseudo_lock.o

# To allow define_trace.h's recursive include:
CFLAGS_monitor.o = -I$(src)
