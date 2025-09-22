/*	$OpenBSD: db_trace.c,v 1.7 2024/11/07 16:02:29 miod Exp $	*/

/*
 * Copyright (c) 2000, 2001 Ben Harris
 * Copyright (c) 1996 Scott K. Stevens
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_access.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>

extern unsigned char	cpu_exception_handler_supervisor[];
extern unsigned char	cpu_exception_handler_user[];

db_regs_t ddb_regs;

#define INKERNEL(va)	(((vaddr_t)(va)) & (1ULL << 63))

void
db_stack_trace_print(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif, int (*pr)(const char *, ...))
{
	vaddr_t		frame, lastframe, ra, subr;
	char		c, *cp = modif;
	db_expr_t	offset;
	Elf_Sym *	sym;
	const char	*name;
	int		kernel_only = 1;

	while ((c = *cp++) != 0) {
		if (c == 'u')
			kernel_only = 0;
		if (c == 't') {
			db_printf("tracing threads not yet supported\n");
			return;
		}
	}

	if (!have_addr) {
		ra = ddb_regs.tf_ra;
		frame = ddb_regs.tf_s[0];
	} else {
		db_read_bytes(addr - 16, sizeof(vaddr_t), (char *)&frame);
		db_read_bytes(addr - 8, sizeof(vaddr_t), (char *)&ra);
	}

	while (count != 0 && frame != 0) {
		if (INKERNEL(frame)) {
			sym = db_search_symbol(ra, DB_STGY_ANY, &offset);
			db_symbol_values(sym, &name, NULL);
		} else {
			sym = NULL;
			name = NULL;
		}

		if (name == NULL || strcmp(name, "end") == 0) {
			(*pr)("%llx() at 0x%lx", ra, ra);
		} else {
			(*pr)("%s() at ", name);
			db_printsym(ra, DB_STGY_PROC, pr);
		}
		(*pr)("\n");

		if ((frame & 0x7) != 0) {
			(*pr)("bad frame pointer: 0x%lx\n", frame);
			break;
		}

		subr = 0;
		if (sym != NULL)
			subr = ra - (vaddr_t)offset;

		lastframe = frame;
		if (subr == (vaddr_t)cpu_exception_handler_supervisor ||
		    subr == (vaddr_t)cpu_exception_handler_user) {
			struct trapframe *tf = (struct trapframe *)frame;

			db_read_bytes((vaddr_t)&tf->tf_ra, sizeof(ra),
			    (char *)&ra);
			db_read_bytes((vaddr_t)&tf->tf_s[0], sizeof(frame),
			    (char *)&frame);
		} else {
			db_read_bytes(frame - 16, sizeof(frame),
			    (char *)&frame);
			if (frame == 0)
				break;
			if ((frame & 0x7) != 0) {
				(*pr)("bad frame pointer: 0x%lx\n", frame);
				break;
			}
			db_read_bytes(frame - 8, sizeof(ra), (char *)&ra);
		}

		if (INKERNEL(frame)) {
			if (frame <= lastframe) {
				(*pr)("bad frame pointer: 0x%lx\n", frame);
				break;
			}
		} else {
			if (kernel_only) {
				(*pr)("end of kernel\n");
				break;
			}
		}

		--count;
	}
	(*pr)("end trace frame: 0x%lx, count: %d\n", frame, count);
}
