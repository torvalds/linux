# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_INCREMENTAL_FS)	+= incrementalfs.o

incrementalfs-y := \
	data_mgmt.o \
	format.o \
	integrity.o \
	main.o \
	pseudo_files.o \
	sysfs.o \
	vfs.o

incrementalfs-$(CONFIG_FS_VERITY) += verity.o
