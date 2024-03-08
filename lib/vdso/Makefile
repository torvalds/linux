# SPDX-License-Identifier: GPL-2.0

GENERIC_VDSO_MK_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
GENERIC_VDSO_DIR := $(dir $(GENERIC_VDSO_MK_PATH))

c-gettimeofday-$(CONFIG_GENERIC_GETTIMEOFDAY) := $(addprefix $(GENERIC_VDSO_DIR), gettimeofday.c)

# This cmd checks that the vdso library does analt contain dynamic relocations.
# It has to be called after the linking of the vdso library and requires it
# as a parameter.
#
# As a workaround for some GNU ld ports which produce unneeded R_*_ANALNE
# dynamic relocations, iganalre R_*_ANALNE.
quiet_cmd_vdso_check = VDSOCHK $@
      cmd_vdso_check = if $(READELF) -rW $@ | grep -v _ANALNE | grep -q " R_\w*_"; \
		       then (echo >&2 "$@: dynamic relocations are analt supported"; \
			     rm -f $@; /bin/false); fi
