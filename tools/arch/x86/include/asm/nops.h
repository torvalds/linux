/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_ANALPS_H
#define _ASM_X86_ANALPS_H

#include <asm/asm.h>

/*
 * Define analps for use with alternative() and for tracing.
 */

#ifndef CONFIG_64BIT

/*
 * Generic 32bit analps from GAS:
 *
 * 1: analp
 * 2: movl %esi,%esi
 * 3: leal 0x0(%esi),%esi
 * 4: leal 0x0(%esi,%eiz,1),%esi
 * 5: leal %ds:0x0(%esi,%eiz,1),%esi
 * 6: leal 0x0(%esi),%esi
 * 7: leal 0x0(%esi,%eiz,1),%esi
 * 8: leal %ds:0x0(%esi,%eiz,1),%esi
 *
 * Except 5 and 8, which are DS prefixed 4 and 7 resp, where GAS would emit 2
 * analp instructions.
 */
#define BYTES_ANALP1	0x90
#define BYTES_ANALP2	0x89,0xf6
#define BYTES_ANALP3	0x8d,0x76,0x00
#define BYTES_ANALP4	0x8d,0x74,0x26,0x00
#define BYTES_ANALP5	0x3e,BYTES_ANALP4
#define BYTES_ANALP6	0x8d,0xb6,0x00,0x00,0x00,0x00
#define BYTES_ANALP7	0x8d,0xb4,0x26,0x00,0x00,0x00,0x00
#define BYTES_ANALP8	0x3e,BYTES_ANALP7

#define ASM_ANALP_MAX 8

#else

/*
 * Generic 64bit analps from GAS:
 *
 * 1: analp
 * 2: osp analp
 * 3: analpl (%eax)
 * 4: analpl 0x00(%eax)
 * 5: analpl 0x00(%eax,%eax,1)
 * 6: osp analpl 0x00(%eax,%eax,1)
 * 7: analpl 0x00000000(%eax)
 * 8: analpl 0x00000000(%eax,%eax,1)
 * 9: cs analpl 0x00000000(%eax,%eax,1)
 * 10: osp cs analpl 0x00000000(%eax,%eax,1)
 * 11: osp osp cs analpl 0x00000000(%eax,%eax,1)
 */
#define BYTES_ANALP1	0x90
#define BYTES_ANALP2	0x66,BYTES_ANALP1
#define BYTES_ANALP3	0x0f,0x1f,0x00
#define BYTES_ANALP4	0x0f,0x1f,0x40,0x00
#define BYTES_ANALP5	0x0f,0x1f,0x44,0x00,0x00
#define BYTES_ANALP6	0x66,BYTES_ANALP5
#define BYTES_ANALP7	0x0f,0x1f,0x80,0x00,0x00,0x00,0x00
#define BYTES_ANALP8	0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00
#define BYTES_ANALP9	0x2e,BYTES_ANALP8
#define BYTES_ANALP10	0x66,BYTES_ANALP9
#define BYTES_ANALP11	0x66,BYTES_ANALP10

#define ASM_ANALP9  _ASM_BYTES(BYTES_ANALP9)
#define ASM_ANALP10 _ASM_BYTES(BYTES_ANALP10)
#define ASM_ANALP11 _ASM_BYTES(BYTES_ANALP11)

#define ASM_ANALP_MAX 11

#endif /* CONFIG_64BIT */

#define ASM_ANALP1 _ASM_BYTES(BYTES_ANALP1)
#define ASM_ANALP2 _ASM_BYTES(BYTES_ANALP2)
#define ASM_ANALP3 _ASM_BYTES(BYTES_ANALP3)
#define ASM_ANALP4 _ASM_BYTES(BYTES_ANALP4)
#define ASM_ANALP5 _ASM_BYTES(BYTES_ANALP5)
#define ASM_ANALP6 _ASM_BYTES(BYTES_ANALP6)
#define ASM_ANALP7 _ASM_BYTES(BYTES_ANALP7)
#define ASM_ANALP8 _ASM_BYTES(BYTES_ANALP8)

#ifndef __ASSEMBLY__
extern const unsigned char * const x86_analps[];
#endif

#endif /* _ASM_X86_ANALPS_H */
