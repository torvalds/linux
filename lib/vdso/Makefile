# SPDX-License-Identifier: GPL-2.0

GENERIC_VDSO_MK_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
GENERIC_VDSO_DIR := $(dir $(GENERIC_VDSO_MK_PATH))

c-gettimeofday-$(CONFIG_GENERIC_GETTIMEOFDAY) := $(addprefix $(GENERIC_VDSO_DIR), gettimeofday.c)

# This cmd checks that the vdso library does not contain absolute relocation
# It has to be called after the linking of the vdso library and requires it
# as a parameter.
#
# $(ARCH_REL_TYPE_ABS) is defined in the arch specific makefile and corresponds
# to the absolute relocation types printed by "objdump -R" and accepted by the
# dynamic linker.
ifndef ARCH_REL_TYPE_ABS
$(error ARCH_REL_TYPE_ABS is not set)
endif

quiet_cmd_vdso_check = VDSOCHK $@
      cmd_vdso_check = if $(OBJDUMP) -R $@ | egrep -h "$(ARCH_REL_TYPE_ABS)"; \
		       then (echo >&2 "$@: dynamic relocations are not supported"; \
			     rm -f $@; /bin/false); fi
