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
HOST_EXTRACFLAGS += -I $(src)/libfdt
HOST_EXTRACFLAGS += -DNO_YAML

# Generated files need one more search path to include headers in source tree
HOSTCFLAGS_dtc-lexer.lex.o := -I $(src)
HOSTCFLAGS_dtc-parser.tab.o := -I $(src)

# dependencies on generated files need to be listed explicitly
$(obj)/dtc-lexer.lex.o: $(obj)/dtc-parser.tab.h
