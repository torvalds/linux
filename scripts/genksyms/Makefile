
hostprogs-y	:= genksyms
always		:= $(hostprogs-y)

genksyms-objs	:= genksyms.o parse.o lex.o

# -I needed for generated C source (shipped source)
HOSTCFLAGS_parse.o := -Wno-uninitialized -I$(src)

# dependencies on generated files need to be listed explicitly
$(obj)/lex.o: $(obj)/parse.h $(obj)/keywords.c

# -I needed for generated C source (shipped source)
HOSTCFLAGS_lex.o := -I$(src)

ifdef GENERATE_PARSER

# gperf

quiet_cmd_keywords.c = GPERF   $@
      cmd_keywords.c = gperf -L ANSI-C -a -C -E -g -H is_reserved_hash	\
		       -k 1,3,$$ -N is_reserved_word -p -t $< > $@

$(obj)/keywords.c: $(obj)/keywords.gperf FORCE
	$(call if_changed,keywords.c)
	cp $@ $@_shipped

# flex

quiet_cmd_lex.c = FLEX    $@
      cmd_lex.c = flex -o$@ -d $< $(obj)/parse.h

$(obj)/lex.c: $(obj)/lex.l $(obj)/parse.h $(obj)/keywords.c FORCE
	$(call if_changed,lex.c)
	cp $@ $@_shipped

# bison

quiet_cmd_parse.c = BISON   $@
      cmd_parse.c = bison -o$@ -dtv $(filter-out FORCE,$^)

$(obj)/parse.c: $(obj)/parse.y FORCE
	$(call if_changed,parse.c)
	cp $@ $@_shipped
	cp $(@:.c=.h) $(@:.c=.h)_shipped

$(obj)/parse.h: $(obj)/parse.c ;

clean-files	+= parse.output

endif

targets += keywords.c lex.c parse.c parse.h
