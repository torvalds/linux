# SPDX-License-Identifier: GPL-2.0
# scripts/dtc makefile

hostprogs		:= dtc
always-$(CONFIG_DTC)	:= $(hostprogs)

dtc-objs	:= dtc.o flattree.o fstree.o data.o livetree.o treesource.o \
		   srcpos.o checks.o util.o
dtc-objs	+= dtc-lexer.lex.o dtc-parser.tab.o

# Source files need to get at the userspace version of libfdt_env.h to compile
HOST_EXTRACFLAGS := -I $(srctree)/$(src)/libfdt

ifeq ($(shell pkg-config --exists yaml-0.1 2>/dev/null && echo yes),)
ifneq ($(CHECK_DTBS),)
$(error dtc needs libyaml for DT schema validation support. \
	Install the necessary libyaml development package.)
endif
HOST_EXTRACFLAGS += -DNO_YAML
else
dtc-objs	+= yamltree.o
HOSTLDLIBS_dtc	:= $(shell pkg-config yaml-0.1 --libs)
endif

# Generated files need one more search path to include headers in source tree
HOSTCFLAGS_dtc-lexer.lex.o := -I $(srctree)/$(src)
HOSTCFLAGS_dtc-parser.tab.o := -I $(srctree)/$(src)

# dependencies on generated files need to be listed explicitly
$(obj)/dtc-lexer.lex.o: $(obj)/dtc-parser.tab.h
