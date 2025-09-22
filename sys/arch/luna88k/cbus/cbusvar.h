/*	$OpenBSD: cbusvar.h,v 1.6 2024/06/01 00:48:16 aoyama Exp $	*/

/*
 * Copyright (c) 2014 Kenji Aoyama.
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
 * PC-9801 extension board slot bus ('C-bus') driver for LUNA-88K2.
 */

#include <sys/evcount.h>
#include <sys/queue.h>

#include <arch/luna88k/include/board.h>

#define	PCEXMEM_BASE	PC_BASE
#define	PCEXIO_BASE	(PC_BASE + 0x1000000)

/*
 * Currently 7 level C-bus interrupts (INT0 - INT6) are supported.
 */
#define NCBUSISR	7

/*
 * C-bus interrupt handler
 */
struct cbus_isr_t {
	int		(*isr_func)(void *);
	void		*isr_arg;
	int		isr_intlevel;
	int		isr_ipl;
	struct evcount	isr_count;
};

int	cbus_isrlink(int (*)(void *), void *, int, int, const char *);
int	cbus_isrunlink(int (*)(void *), int);
u_int8_t	cbus_intr_registered(void);

struct cbus_attach_args {
	char		*ca_name;
	u_int32_t	ca_iobase;
	u_int32_t	ca_iosize;
	u_int32_t	ca_maddr;
	u_int32_t	ca_msize;
	u_int32_t	ca_int;
};

#define	cf_iobase	cf_loc[0]
#define	cf_iosize	cf_loc[1]
#define	cf_maddr	cf_loc[2]
#define	cf_msize	cf_loc[3]
#define	cf_int		cf_loc[4]
