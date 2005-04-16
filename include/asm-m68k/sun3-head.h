/* $Id: head.h,v 1.32 1996/12/04 00:12:48 ecd Exp $ */
#ifndef __SUN3_HEAD_H
#define __SUN3_HEAD_H

#define KERNBASE        0xE000000  /* First address the kernel will eventually be */
#define LOAD_ADDR       0x4000      /* prom jumps to us here unless this is elf /boot */
#define BI_START (KERNBASE + 0x3000) /* beginning of the bootinfo records */
#define FC_CONTROL  3
#define FC_SUPERD    5
#define FC_CPU      7

#endif /* __SUN3_HEAD_H */
