# SPDX-License-Identifier: GPL-2.0
ccflags-y := -DSRCTREE='"$(srctree)"' -DOBJTREE='"$(objtree)"'

obj-y := base.o fs.o
obj-$(CONFIG_GCOV_FORMAT_3_4) += gcc_3_4.o
obj-$(CONFIG_GCOV_FORMAT_4_7) += gcc_4_7.o
obj-$(CONFIG_GCOV_FORMAT_AUTODETECT) += $(call cc-ifversion, -lt, 0407, \
							gcc_3_4.o, gcc_4_7.o)
