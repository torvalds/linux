#include "asan_mapping.h"
#include "sanitizer_common/sanitizer_asm.h"

#if defined(__x86_64__)
#include "sanitizer_common/sanitizer_platform.h"

.file "asan_rtl_x86_64.S"

#define NAME(n, reg, op, s, i) n##_##op##_##i##_##s##_##reg

#define FNAME(reg, op, s, i) NAME(__asan_check, reg, op, s, i)
#define RLABEL(reg, op, s, i) NAME(.return, reg, op, s, i)
#define CLABEL(reg, op, s, i) NAME(.check, reg, op, s, i)
#define FLABEL(reg, op, s, i) NAME(.fail, reg, op, s, i)

#define BEGINF(reg, op, s, i) \
.section .text.FNAME(reg, op, s, i),"ax",@progbits ;\
.globl  FNAME(reg, op, s, i) ;\
.hidden  FNAME(reg, op, s, i) ;\
ASM_TYPE_FUNCTION(FNAME(reg, op, s, i)) ;\
.cfi_startproc ;\
FNAME(reg, op, s, i): ;\

#define ENDF .cfi_endproc ;\

// Access check functions for 1,2 and 4 byte types, which require extra checks.
#define ASAN_MEMORY_ACCESS_INITIAL_CHECK_ADD(reg, op, s) \
        mov    %##reg,%r10 ;\
        shr    $0x3,%r10 ;\
        .if ASAN_SHADOW_OFFSET_CONST < 0x80000000   ;\
        movsbl ASAN_SHADOW_OFFSET_CONST(%r10),%r10d ;\
        .else                                       ;\
        movabsq $ASAN_SHADOW_OFFSET_CONST,%r11      ;\
        movsbl (%r10,%r11),%r10d                    ;\
        .endif                                      ;\
        test   %r10d,%r10d ;\
        jne    CLABEL(reg, op, s, add) ;\
RLABEL(reg, op, s, add): ;\
        retq  ;\

#define ASAN_MEMORY_ACCESS_EXTRA_CHECK_1(reg, op, i) \
CLABEL(reg, op, 1, i): ;\
        mov    %##reg,%r11 ;\
        and    $0x7,%r11d ;\
        cmp    %r10d,%r11d ;\
        jl     RLABEL(reg, op, 1, i);\
        mov    %##reg,%rdi ;\
        jmp    __asan_report_##op##1_asm ;\

#define ASAN_MEMORY_ACCESS_EXTRA_CHECK_2(reg, op, i) \
CLABEL(reg, op, 2, i): ;\
        mov    %##reg,%r11 ;\
        and    $0x7,%r11d ;\
        add    $0x1,%r11d ;\
        cmp    %r10d,%r11d ;\
        jl     RLABEL(reg, op, 2, i);\
        mov    %##reg,%rdi ;\
        jmp    __asan_report_##op##2_asm ;\

#define ASAN_MEMORY_ACCESS_EXTRA_CHECK_4(reg, op, i) \
CLABEL(reg, op, 4, i): ;\
        mov    %##reg,%r11 ;\
        and    $0x7,%r11d ;\
        add    $0x3,%r11d ;\
        cmp    %r10d,%r11d ;\
        jl     RLABEL(reg, op, 4, i);\
        mov    %##reg,%rdi ;\
        jmp    __asan_report_##op##4_asm ;\

#define ASAN_MEMORY_ACCESS_CALLBACK_ADD_1(reg, op) \
BEGINF(reg, op, 1, add) ;\
        ASAN_MEMORY_ACCESS_INITIAL_CHECK_ADD(reg, op, 1) ;\
        ASAN_MEMORY_ACCESS_EXTRA_CHECK_1(reg, op, add) ;\
ENDF

#define ASAN_MEMORY_ACCESS_CALLBACK_ADD_2(reg, op) \
BEGINF(reg, op, 2, add) ;\
        ASAN_MEMORY_ACCESS_INITIAL_CHECK_ADD(reg, op, 2) ;\
        ASAN_MEMORY_ACCESS_EXTRA_CHECK_2(reg, op, add) ;\
ENDF

#define ASAN_MEMORY_ACCESS_CALLBACK_ADD_4(reg, op) \
BEGINF(reg, op, 4, add) ;\
        ASAN_MEMORY_ACCESS_INITIAL_CHECK_ADD(reg, op, 4) ;\
        ASAN_MEMORY_ACCESS_EXTRA_CHECK_4(reg, op, add) ;\
ENDF

// Access check functions for 8 and 16 byte types: no extra checks required.
#define ASAN_MEMORY_ACCESS_CHECK_ADD(reg, op, s, c) \
        mov    %##reg,%r10 ;\
        shr    $0x3,%r10 ;\
        .if ASAN_SHADOW_OFFSET_CONST < 0x80000000  ;\
        ##c    $0x0,ASAN_SHADOW_OFFSET_CONST(%r10) ;\
        .else                                      ;\
        movabsq $ASAN_SHADOW_OFFSET_CONST,%r11     ;\
        ##c    $0x0,(%r10,%r11)                    ;\
        .endif                                     ;\
        jne    FLABEL(reg, op, s, add) ;\
        retq  ;\

#define ASAN_MEMORY_ACCESS_FAIL(reg, op, s, i) \
FLABEL(reg, op, s, i): ;\
        mov    %##reg,%rdi ;\
        jmp    __asan_report_##op##s##_asm;\

#define ASAN_MEMORY_ACCESS_CALLBACK_ADD_8(reg, op) \
BEGINF(reg, op, 8, add) ;\
        ASAN_MEMORY_ACCESS_CHECK_ADD(reg, op, 8, cmpb) ;\
        ASAN_MEMORY_ACCESS_FAIL(reg, op, 8, add) ;\
ENDF

#define ASAN_MEMORY_ACCESS_CALLBACK_ADD_16(reg, op) \
BEGINF(reg, op, 16, add) ;\
        ASAN_MEMORY_ACCESS_CHECK_ADD(reg, op, 16, cmpw) ;\
        ASAN_MEMORY_ACCESS_FAIL(reg, op, 16, add) ;\
ENDF

#define ASAN_MEMORY_ACCESS_CALLBACKS_ADD(reg) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_1(reg, load) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_1(reg, store) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_2(reg, load) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_2(reg, store) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_4(reg, load) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_4(reg, store) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_8(reg, load) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_8(reg, store) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_16(reg, load) \
ASAN_MEMORY_ACCESS_CALLBACK_ADD_16(reg, store) \


// Instantiate all but R10 and R11 callbacks. We are using PLTSafe class with
// the intrinsic, which guarantees that the code generation will never emit
// R10 or R11 callback.
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RAX)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RBX)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RCX)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RDX)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RSI)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RDI)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(RBP)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R8)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R9)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R12)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R13)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R14)
ASAN_MEMORY_ACCESS_CALLBACKS_ADD(R15)

#endif

NO_EXEC_STACK_DIRECTIVE
