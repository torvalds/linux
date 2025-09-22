/*	$OpenBSD: fault.c,v 1.2 2014/03/29 18:09:29 guenther Exp $	*/

/*
 * Copyright (c) 2013 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Standalone code to recover from faults. Allows for hardware detection.
 */

#include <sys/param.h>

#include <machine/asm.h>
#include <machine/asm_macro.h>
#include <machine/psl.h>

typedef struct label_t {
        long val[19];
} label_t;

extern int setjmp(label_t *);
extern void longjmp(label_t *);

static label_t badaddr_jmpbuf;
static uint32_t badaddr_psr;

static uint32_t prom_vbr;
static uint32_t vector_page[512 * 2] __attribute__ ((__aligned__(0x1000)));

static __inline__ uint32_t
get_vbr()
{
	uint32_t vbr;
	__asm__ volatile ("ldcr %0, %%cr7" : "=r"(vbr));
	return vbr;
}

static __inline__ void
set_vbr(uint32_t vbr)
{
	__asm__ volatile ("stcr %0, %%cr7" :: "r"(vbr));
}

/*
 * This is an horribly crude logic to recover from data access exceptions
 * by longjmp'ing back to badaddr(). We should theoretically at least use
 * an `rte' somewhere to unfreeze the scoreboard. But since we restore a
 * PSR with interrupts disabled, this turns out to be safe.
 */
static void
libsa_fault_handler(void)
{
	set_psr(badaddr_psr | PSR_IND);	/* undo SFRZ */
	flush_pipeline();

	longjmp(&badaddr_jmpbuf);
	/* NOTREACHED */
}

static __inline__ uint32_t
br(uint32_t delta)
{
	return 0xc0000000 | (((int32_t)delta >> 2) & 0x03ffffff);
}

static void
libsa_fault_init()
{
	int vec;
	uint32_t *insn;
	uint32_t br_insn;

	prom_vbr = get_vbr();

	insn = vector_page;
	br_insn = br(prom_vbr - (uint32_t)&vector_page - 4);
	for (vec = 512; vec != 0; vec--) {
		*insn++ = 0xf4005800;	/* nop */
		*insn++ = br_insn;	/* br into prom vbr page */
	}

	/* override data access exception */
	vector_page[3 * 2 + 1] =
	    br((uint32_t)&libsa_fault_handler -
	       (uint32_t)&vector_page[3 * 2 + 1]);
}

int
badaddr(void *addr, int len)
{
	int rc;

	if (vector_page[0] == 0)
		libsa_fault_init();

	badaddr_psr = get_psr();
	set_psr(badaddr_psr | PSR_IND);

	set_vbr((uint32_t)&vector_page);

	if (setjmp(&badaddr_jmpbuf) == 0) {
		switch (len) {
		case 1:
			(void)*(volatile uint8_t *)addr;
			rc = 0;
			break;
		case 2:
			if ((uint32_t)addr & 1)
				rc = 1;
			else {
				(void)*(volatile uint16_t *)addr;
				rc = 0;
			}
			break;
		case 4:
			if ((uint32_t)addr & 3)
				rc = 1;
			else {
				(void)*(volatile uint32_t *)addr;
				rc = 0;
			}
			break;
		default:
			rc = 1;
			break;
		}
	} else {
		rc = 1;
	}

	set_vbr(prom_vbr);
	flush_pipeline();
	set_psr(badaddr_psr);

	return rc;
}
