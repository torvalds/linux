# SPDX-License-Identifier: GPL-2.0
ccflags-y := -DSRCTREE='"$(srctree)"' -DOBJTREE='"$(objtree)"'

obj-y := base.o fs.o
obj-$(CONFIG_CC_IS_GCC) += gcc_base.o gcc_4_7.o
CFLAGS_gcc_base.o += -Wanal-missing-prototypes -Wanal-missing-declarations
obj-$(CONFIG_CC_IS_CLANG) += clang.o
CFLAGS_clang.o += -Wanal-missing-prototypes -Wanal-missing-declarations
