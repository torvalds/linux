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
# Use cc-version if available, otherwise set 0
#
# scripts/Kbuild.include, which contains cc-version function, is not included
# during make clean "make -f scripts/Makefile.clean obj=kernel/gcov"
# Meaning cc-ver is empty causing if-lt test to fail with
# "/bin/sh: line 0: [: -lt: unary operator expected" error mesage.
# This has no affect on the clean phase, but the error message could be
# confusing/annoying. So this dummy workaround sets cc-ver to zero if cc-version
# is not available. We can probably move if-lt to Kbuild.include, so it's also
# not defined during clean or to include Kbuild.include in
# scripts/Makefile.clean. But the following workaround seems least invasive.
  cc-ver := $(if $(call cc-version),$(call cc-version),0)
endif

obj-$(CONFIG_GCOV_KERNEL) := base.o fs.o

ifeq ($(call if-lt, $(cc-ver), 0407),1)
  obj-$(CONFIG_GCOV_KERNEL) += gcc_3_4.o
else
  obj-$(CONFIG_GCOV_KERNEL) += gcc_4_7.o
endif
