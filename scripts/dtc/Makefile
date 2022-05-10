# SPDX-License-Identifier: GPL-2.0
# scripts/dtc makefile

# *** Also keep .gitignore in sync when changing ***
hostprogs-always-$(CONFIG_DTC)		+= dtc fdtoverlay
hostprogs-always-$(CHECK_DT_BINDING)	+= dtc

dtc-objs	:= dtc.o flattree.o fstree.o data.o livetree.o treesource.o \
		   srcpos.o checks.o util.o
dtc-objs	+= dtc-lexer.lex.o dtc-parser.tab.o

# The upstream project builds libfdt as a separate library.  We are choosing to
# instead directly link the libfdt object files into fdtoverlay.
libfdt-objs	:= fdt.o fdt_ro.o fdt_wip.o fdt_sw.o fdt_rw.o fdt_strerror.o fdt_empty_tree.o fdt_addresses.o fdt_overlay.o
libfdt		= $(addprefix libfdt/,$(libfdt-objs))
fdtoverlay-objs	:= $(libfdt) fdtoverlay.o util.o

# Source files need to get at the userspace version of libfdt_env.h to compile
HOST_EXTRACFLAGS += -I $(srctree)/$(src)/libfdt

ifeq ($(shell pkg-config --exists yaml-0.1 2>/dev/null && echo yes),)
ifneq ($(CHECK_DT_BINDING)$(CHECK_DTBS),)
$(error dtc needs libyaml for DT schema validation support. \
	Install the necessary libyaml development package.)
endif
HOST_EXTRACFLAGS += -DNO_YAML
else
dtc-objs	+= yamltree.o
# To include <yaml.h> installed in a non-default path
HOSTCFLAGS_yamltree.o := $(shell pkg-config --cflags yaml-0.1)
# To link libyaml installed in a non-default path
HOSTLDLIBS_dtc	:= $(shell pkg-config --libs yaml-0.1)
endif

# Generated files need one more search path to include headers in source tree
HOSTCFLAGS_dtc-lexer.lex.o := -I $(srctree)/$(src)
HOSTCFLAGS_dtc-parser.tab.o := -I $(srctree)/$(src)

# dependencies on generated files need to be listed explicitly
$(obj)/dtc-lexer.lex.o: $(obj)/dtc-parser.tab.h
