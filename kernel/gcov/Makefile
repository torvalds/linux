ccflags-y := -DSRCTREE='"$(srctree)"' -DOBJTREE='"$(objtree)"'

# if-lt
# Usage VAR := $(call if-lt, $(a), $(b))
# Returns 1 if (a < b)
if-lt = $(shell [ $(1) -lt $(2) ] && echo 1)

ifeq ($(CONFIG_GCOV_FORMAT_3_4),y)
  cc-ver := 0304
else ifeq ($(CONFIG_GCOV_FORMAT_4_7),y)
  cc-ver := 0407
else
  cc-ver := $(cc-version)
endif

obj-$(CONFIG_GCOV_KERNEL) := base.o fs.o

ifeq ($(call if-lt, $(cc-ver), 0407),1)
  obj-$(CONFIG_GCOV_KERNEL) += gcc_3_4.o
else
  obj-$(CONFIG_GCOV_KERNEL) += gcc_4_7.o
endif
