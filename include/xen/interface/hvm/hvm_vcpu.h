/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2015, Roger Pau Monne <roger.pau@citrix.com>
 */

#ifndef __XEN_PUBLIC_HVM_HVM_VCPU_H__
#define __XEN_PUBLIC_HVM_HVM_VCPU_H__

#include "../xen.h"

struct vcpu_hvm_x86_32 {
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t eip;
    uint32_t eflags;

    uint32_t cr0;
    uint32_t cr3;
    uint32_t cr4;

    uint32_t pad1;

    /*
     * EFER should only be used to set the NXE bit (if required)
     * when starting a vCPU in 32bit mode with paging enabled or
     * to set the LME/LMA bits in order to start the vCPU in
     * compatibility mode.
     */
    uint64_t efer;

    uint32_t cs_base;
    uint32_t ds_base;
    uint32_t ss_base;
    uint32_t es_base;
    uint32_t tr_base;
    uint32_t cs_limit;
    uint32_t ds_limit;
    uint32_t ss_limit;
    uint32_t es_limit;
    uint32_t tr_limit;
    uint16_t cs_ar;
    uint16_t ds_ar;
    uint16_t ss_ar;
    uint16_t es_ar;
    uint16_t tr_ar;

    uint16_t pad2[3];
};

/*
 * The layout of the _ar fields of the segment registers is the
 * following:
 *
 * Bits   [0,3]: type (bits 40-43).
 * Bit        4: s    (descriptor type, bit 44).
 * Bit    [5,6]: dpl  (descriptor privilege level, bits 45-46).
 * Bit        7: p    (segment-present, bit 47).
 * Bit        8: avl  (available for system software, bit 52).
 * Bit        9: l    (64-bit code segment, bit 53).
 * Bit       10: db   (meaning depends on the segment, bit 54).
 * Bit       11: g    (granularity, bit 55)
 * Bits [12,15]: unused, must be blank.
 *
 * A more complete description of the meaning of this fields can be
 * obtained from the Intel SDM, Volume 3, section 3.4.5.
 */

struct vcpu_hvm_x86_64 {
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rip;
    uint64_t rflags;

    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t efer;

    /*
     * Using VCPU_HVM_MODE_64B implies that the vCPU is launched
     * directly in long mode, so the cached parts of the segment
     * registers get set to match that environment.
     *
     * If the user wants to launch the vCPU in compatibility mode
     * the 32-bit structure should be used instead.
     */
};

struct vcpu_hvm_context {
#define VCPU_HVM_MODE_32B 0  /* 32bit fields of the structure will be used. */
#define VCPU_HVM_MODE_64B 1  /* 64bit fields of the structure will be used. */
    uint32_t mode;

    uint32_t pad;

    /* CPU registers. */
    union {
        struct vcpu_hvm_x86_32 x86_32;
        struct vcpu_hvm_x86_64 x86_64;
    } cpu_regs;
};
typedef struct vcpu_hvm_context vcpu_hvm_context_t;

#endif /* __XEN_PUBLIC_HVM_HVM_VCPU_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
