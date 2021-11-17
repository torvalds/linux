# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := fpu_syscall fpu_preempt fpu_signal fpu_denormal vmx_syscall vmx_preempt vmx_signal vsx_preempt

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c
$(TEST_GEN_PROGS): CFLAGS += -O2 -g -pthread -m64 -maltivec

$(OUTPUT)/fpu_syscall: fpu_asm.S
$(OUTPUT)/fpu_preempt: fpu_asm.S
$(OUTPUT)/fpu_signal:  fpu_asm.S

$(OUTPUT)/vmx_syscall: vmx_asm.S ../utils.c
$(OUTPUT)/vmx_preempt: vmx_asm.S ../utils.c
$(OUTPUT)/vmx_signal: vmx_asm.S ../utils.c

$(OUTPUT)/vsx_preempt: CFLAGS += -mvsx
$(OUTPUT)/vsx_preempt: vsx_asm.S ../utils.c
