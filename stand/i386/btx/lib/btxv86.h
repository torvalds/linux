/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

/*
 * $FreeBSD$
 */

#ifndef _BTXV86_H_
#define _BTXV86_H_

#include <sys/types.h>
#include <machine/psl.h>

/*
 * Memory buffer space for real mode IO.
 * Just one page is not much, but the space is rather limited.
 * See ../btx/btx.S for details.
 */
#define	V86_IO_BUFFER		0x8000
#define	V86_IO_BUFFER_SIZE	0x1000

#define V86_ADDR   0x10000	/* Segment:offset address */
#define V86_CALLF  0x20000	/* Emulate far call */
#define V86_FLAGS  0x40000	/* Return flags */

struct __v86 {
    uint32_t ctl;		/* Control flags */
    uint32_t addr;		/* Interrupt number or address */
    uint32_t es;		/* V86 ES register */
    uint32_t ds;		/* V86 DS register */
    uint32_t fs;		/* V86 FS register */
    uint32_t gs;		/* V86 GS register */
    uint32_t eax;		/* V86 EAX register */
    uint32_t ecx;		/* V86 ECX register */
    uint32_t edx;		/* V86 EDX register */
    uint32_t ebx;		/* V86 EBX register */
    uint32_t efl;		/* V86 eflags register */
    uint32_t ebp;		/* V86 EBP register */
    uint32_t esi;		/* V86 ESI register */
    uint32_t edi;		/* V86 EDI register */
};

extern struct __v86 __v86;	/* V86 interface structure */
void __v86int(void);

#define v86	__v86
#define v86int	__v86int

extern uint32_t		__base;
extern uint32_t		__args;

#define	PTOV(pa)	((caddr_t)(pa) - __base)
#define	VTOP(va)	((vm_offset_t)(va) + __base)
#define	VTOPSEG(va)	(uint16_t)(VTOP((caddr_t)va) >> 4)
#define	VTOPOFF(va)	(uint16_t)(VTOP((caddr_t)va) & 0xf)

#define	V86_CY(x)	((x) & PSL_C)
#define	V86_ZR(x)	((x) & PSL_Z)

void __exit(int) __attribute__((__noreturn__));
void __exec(caddr_t, ...);

#endif /* !_BTXV86_H_ */
