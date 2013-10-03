prefix = /usr

CC = gcc

all : bpf_jit_disasm

bpf_jit_disasm : CFLAGS = -Wall -O2
bpf_jit_disasm : LDLIBS = -lopcodes -lbfd -ldl
bpf_jit_disasm : bpf_jit_disasm.o

clean :
	rm -rf *.o bpf_jit_disasm

install :
	install bpf_jit_disasm $(prefix)/bin/bpf_jit_disasm
