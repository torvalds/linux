ccflags-y := -DSRCTREE='"$(srctree)"' -DOBJTREE='"$(objtree)"'

obj-$(CONFIG_GCOV_KERNEL) := base.o fs.o

obj-$(CONFIG_GCOV_KERNEL) += $(call cc-ifversion, -ge, 0407, gcc_4_7.o)
obj-$(CONFIG_GCOV_KERNEL) += $(call cc-ifversion, -lt, 0407, gcc_3_4.o)
