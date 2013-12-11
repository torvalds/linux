prefix = /usr

CC = gcc

all : bpf_jit_disasm bpf_dbg

bpf_jit_disasm : CFLAGS = -Wall -O2
bpf_jit_disasm : LDLIBS = -lopcodes -lbfd -ldl
bpf_jit_disasm : bpf_jit_disasm.o

bpf_dbg : CFLAGS = -Wall -O2
bpf_dbg : LDLIBS = -lreadline
bpf_dbg : bpf_dbg.o

clean :
	rm -rf *.o bpf_jit_disasm bpf_dbg

install :
	install bpf_jit_disasm $(prefix)/bin/bpf_jit_disasm
	install bpf_dbg $(prefix)/bin/bpf_dbg
