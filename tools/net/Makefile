prefix = /usr

CC = gcc
LEX = flex
YACC = bison

%.yacc.c: %.y
	$(YACC) -o $@ -d $<

%.lex.c: %.l
	$(LEX) -o $@ $<

all : bpf_jit_disasm bpf_dbg bpf_asm

bpf_jit_disasm : CFLAGS = -Wall -O2 -DPACKAGE='bpf_jit_disasm'
bpf_jit_disasm : LDLIBS = -lopcodes -lbfd -ldl
bpf_jit_disasm : bpf_jit_disasm.o

bpf_dbg : CFLAGS = -Wall -O2
bpf_dbg : LDLIBS = -lreadline
bpf_dbg : bpf_dbg.o

bpf_asm : CFLAGS = -Wall -O2 -I.
bpf_asm : LDLIBS =
bpf_asm : bpf_asm.o bpf_exp.yacc.o bpf_exp.lex.o
bpf_exp.lex.o : bpf_exp.yacc.c

clean :
	rm -rf *.o bpf_jit_disasm bpf_dbg bpf_asm bpf_exp.yacc.* bpf_exp.lex.*

install :
	install bpf_jit_disasm $(prefix)/bin/bpf_jit_disasm
	install bpf_dbg $(prefix)/bin/bpf_dbg
	install bpf_asm $(prefix)/bin/bpf_asm
