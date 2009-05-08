# scripts/dtc makefile

hostprogs-y	:= dtc
always		:= $(hostprogs-y)

dtc-objs	:= dtc.o flattree.o fstree.o data.o livetree.o treesource.o \
		   srcpos.o checks.o
dtc-objs	+= dtc-lexer.lex.o dtc-parser.tab.o

# Source files need to get at the userspace version of libfdt_env.h to compile

HOSTCFLAGS_DTC := -I$(src) -I$(src)/libfdt

HOSTCFLAGS_checks.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_data.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_dtc.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_flattree.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_fstree.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_livetree.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_srcpos.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_treesource.o := $(HOSTCFLAGS_DTC)

HOSTCFLAGS_dtc-lexer.lex.o := $(HOSTCFLAGS_DTC)
HOSTCFLAGS_dtc-parser.tab.o := $(HOSTCFLAGS_DTC)

# dependencies on generated files need to be listed explicitly
$(obj)/dtc-parser.tab.o: $(obj)/dtc-parser.tab.c $(obj)/dtc-parser.tab.h
$(obj)/dtc-lexer.lex.o:  $(obj)/dtc-lexer.lex.c $(obj)/dtc-parser.tab.h

targets += dtc-parser.tab.c dtc-lexer.lex.c

clean-files += dtc-parser.tab.h

# GENERATE_PARSER := 1		# Uncomment to rebuild flex/bison output

ifdef GENERATE_PARSER

BISON = bison
FLEX = flex

quiet_cmd_bison = BISON   $@
      cmd_bison = $(BISON) -o$@ -d $<; cp $@ $@_shipped
quiet_cmd_flex = FLEX    $@
      cmd_flex = $(FLEX) -o$@ $<; cp $@ $@_shipped

$(obj)/dtc-parser.tab.c: $(src)/dtc-parser.y FORCE
        $(call if_changed,bison)

$(obj)/dtc-parser.tab.h: $(obj)/dtc-parser.tab.c

$(obj)/dtc-lexer.lex.c: $(src)/dtc-lexer.l FORCE
        $(call if_changed,flex)

endif
