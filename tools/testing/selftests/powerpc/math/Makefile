TEST_PROGS := fpu_syscall fpu_preempt fpu_signal vmx_syscall vmx_preempt vmx_signal vsx_preempt

all: $(TEST_PROGS)

$(TEST_PROGS): ../harness.c
$(TEST_PROGS): CFLAGS += -O2 -g -pthread -m64 -maltivec

fpu_syscall: fpu_asm.S
fpu_preempt: fpu_asm.S
fpu_signal:  fpu_asm.S

vmx_syscall: vmx_asm.S
vmx_preempt: vmx_asm.S
vmx_signal: vmx_asm.S

vsx_preempt: CFLAGS += -mvsx
vsx_preempt: vsx_asm.S

include ../../lib.mk

clean:
	rm -f $(TEST_PROGS) *.o
