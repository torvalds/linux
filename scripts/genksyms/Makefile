# SPDX-License-Identifier: GPL-2.0

hostprogs-always-y	+= genksyms

genksyms-objs	:= genksyms.o parse.tab.o lex.lex.o

# -I needed for generated C source to include headers in source tree
HOSTCFLAGS_parse.tab.o := -I $(src)
HOSTCFLAGS_lex.lex.o := -I $(src)

# dependencies on generated files need to be listed explicitly
$(obj)/lex.lex.o: $(obj)/parse.tab.h
