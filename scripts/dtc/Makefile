# SPDX-License-Identifier: GPL-2.0
# scripts/dtc makefile

hostprogs-always-$(CONFIG_DTC)		+= dtc
hostprogs-always-$(CHECK_DT_BINDING)	+= dtc

dtc-objs	:= dtc.o flattree.o fstree.o data.o livetree.o treesource.o \
		   srcpos.o checks.o util.o
dtc-objs	+= dtc-lexer.lex.o dtc-parser.tab.o

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
