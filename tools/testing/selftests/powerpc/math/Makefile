TEST_GEN_PROGS := fpu_syscall fpu_preempt fpu_signal vmx_syscall vmx_preempt vmx_signal vsx_preempt

include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c
$(TEST_GEN_PROGS): CFLAGS += -O2 -g -pthread -m64 -maltivec

$(OUTPUT)/pu_syscall: fpu_asm.S
$(OUTPUT)/pu_preempt: fpu_asm.S
$(OUTPUT)/pu_signal:  fpu_asm.S

$(OUTPUT)/mx_syscall: vmx_asm.S
$(OUTPUT)/mx_preempt: vmx_asm.S
$(OUTPUT)/mx_signal: vmx_asm.S

vsx_preempt: CFLAGS += -mvsx
vsx_preempt: vsx_asm.S
