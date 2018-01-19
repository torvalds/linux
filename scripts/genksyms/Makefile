# SPDX-License-Identifier: GPL-2.0

hostprogs-y	:= genksyms
always		:= $(hostprogs-y)

genksyms-objs	:= genksyms.o parse.tab.o lex.lex.o

# -I needed for generated C source (shipped source)
HOSTCFLAGS_parse.tab.o := -I$(src)
HOSTCFLAGS_lex.lex.o := -I$(src)

# dependencies on generated files need to be listed explicitly
$(obj)/lex.lex.o: $(obj)/parse.tab.h

clean-files	:= lex.lex.c parse.tab.c parse.tab.h
