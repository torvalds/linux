/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/reboot.h>

#include <machine/gdb_machdep.h>
#include <machine/kdb.h>

#include <gdb/gdb.h>
#include <gdb/gdb_int.h>

static dbbe_init_f gdb_init;
static dbbe_trap_f gdb_trap;

KDB_BACKEND(gdb, gdb_init, NULL, NULL, gdb_trap);

static struct gdb_dbgport null_gdb_dbgport;
DATA_SET(gdb_dbgport_set, null_gdb_dbgport);
SET_DECLARE(gdb_dbgport_set, struct gdb_dbgport);

struct gdb_dbgport *gdb_cur = NULL;
int gdb_listening = 0;

static unsigned char gdb_bindata[64];

static int
gdb_init(void)
{
	struct gdb_dbgport *dp, **iter;
	int cur_pri, pri;

	gdb_cur = NULL;
	cur_pri = -1;
	SET_FOREACH(iter, gdb_dbgport_set) {
		dp = *iter;
		pri = (dp->gdb_probe != NULL) ? dp->gdb_probe() : -1;
		dp->gdb_active = (pri >= 0) ? 0 : -1;
		if (pri > cur_pri) {
			cur_pri = pri;
			gdb_cur = dp;
		}
	}
	if (gdb_cur != NULL) {
		printf("GDB: debug ports:");
		SET_FOREACH(iter, gdb_dbgport_set) {
			dp = *iter;
			if (dp->gdb_active == 0)
				printf(" %s", dp->gdb_name);
		}
		printf("\n");
	} else
		printf("GDB: no debug ports present\n");
	if (gdb_cur != NULL) {
		gdb_cur->gdb_init();
		printf("GDB: current port: %s\n", gdb_cur->gdb_name);
	}
	if (gdb_cur != NULL) {
		cur_pri = (boothowto & RB_GDB) ? 2 : 0;
		gdb_consinit();
	} else
		cur_pri = -1;
	return (cur_pri);
}

static void
gdb_do_mem_search(void)
{
	size_t patlen;
	intmax_t addr, size;
	const unsigned char *found;

	if (gdb_rx_varhex(&addr) || gdb_rx_char() != ';' ||
	    gdb_rx_varhex(&size) || gdb_rx_char() != ';' ||
	    gdb_rx_bindata(gdb_bindata, sizeof(gdb_bindata), &patlen)) {
		gdb_tx_err(EINVAL);
		return;
	}
	if (gdb_search_mem((char *)(uintptr_t)addr, size, gdb_bindata,
	    patlen, &found)) {
		if (found == 0ULL)
			gdb_tx_begin('0');
		else {
			gdb_tx_begin('1');
			gdb_tx_char(',');
			gdb_tx_hex((intmax_t)(uintptr_t)found, 8);
		}
		gdb_tx_end();
	} else
		gdb_tx_err(EIO);
}

static int
gdb_trap(int type, int code)
{
	jmp_buf jb;
	struct thread *thr_iter;
	void *prev_jb;

	prev_jb = kdb_jmpbuf(jb);
	if (setjmp(jb) != 0) {
		printf("%s bailing, hopefully back to ddb!\n", __func__);
		gdb_listening = 0;
		(void)kdb_jmpbuf(prev_jb);
		return (1);
	}

	gdb_listening = 0;
	/*
	 * Send a T packet. We currently do not support watchpoints (the
	 * awatch, rwatch or watch elements).
	 */
	gdb_tx_begin('T');
	gdb_tx_hex(gdb_cpu_signal(type, code), 2);
	gdb_tx_varhex(GDB_REG_PC);
	gdb_tx_char(':');
	gdb_tx_reg(GDB_REG_PC);
	gdb_tx_char(';');
	gdb_tx_str("thread:");
	gdb_tx_varhex((long)kdb_thread->td_tid);
	gdb_tx_char(';');
	gdb_tx_end();			/* XXX check error condition. */

	thr_iter = NULL;
	while (gdb_rx_begin() == 0) {
		/* printf("GDB: got '%s'\n", gdb_rxp); */
		switch (gdb_rx_char()) {
		case '?':	/* Last signal. */
			gdb_tx_begin('S');
			gdb_tx_hex(gdb_cpu_signal(type, code), 2);
			gdb_tx_end();
			break;
		case 'c': {	/* Continue. */
			uintmax_t addr;
			register_t pc;
			if (!gdb_rx_varhex(&addr)) {
				pc = addr;
				gdb_cpu_setreg(GDB_REG_PC, &pc);
			}
			kdb_cpu_clear_singlestep();
			gdb_listening = 1;
			return (1);
		}
		case 'C': {	/* Continue with signal. */
			uintmax_t addr, sig;
			register_t pc;
			if (!gdb_rx_varhex(&sig) && gdb_rx_char() == ';' &&
			    !gdb_rx_varhex(&addr)) {
				pc = addr;
				gdb_cpu_setreg(GDB_REG_PC, &pc);
			}
			kdb_cpu_clear_singlestep();
			gdb_listening = 1;
			return (1);
		}
		case 'D': {     /* Detach */
			gdb_tx_ok();
			kdb_cpu_clear_singlestep();
			return (1);
		}
		case 'g': {	/* Read registers. */
			size_t r;
			gdb_tx_begin(0);
			for (r = 0; r < GDB_NREGS; r++)
				gdb_tx_reg(r);
			gdb_tx_end();
			break;
		}
		case 'G':	/* Write registers. */
			gdb_tx_err(0);
			break;
		case 'H': {	/* Set thread. */
			intmax_t tid;
			struct thread *thr;
			gdb_rx_char();
			if (gdb_rx_varhex(&tid)) {
				gdb_tx_err(EINVAL);
				break;
			}
			if (tid > 0) {
				thr = kdb_thr_lookup(tid);
				if (thr == NULL) {
					gdb_tx_err(ENOENT);
					break;
				}
				kdb_thr_select(thr);
			}
			gdb_tx_ok();
			break;
		}
		case 'k':	/* Kill request. */
			kdb_cpu_clear_singlestep();
			gdb_listening = 1;
			return (1);
		case 'm': {	/* Read memory. */
			uintmax_t addr, size;
			if (gdb_rx_varhex(&addr) || gdb_rx_char() != ',' ||
			    gdb_rx_varhex(&size)) {
				gdb_tx_err(EINVAL);
				break;
			}
			gdb_tx_begin(0);
			if (gdb_tx_mem((char *)(uintptr_t)addr, size))
				gdb_tx_end();
			else
				gdb_tx_err(EIO);
			break;
		}
		case 'M': {	/* Write memory. */
			uintmax_t addr, size;
			if (gdb_rx_varhex(&addr) || gdb_rx_char() != ',' ||
			    gdb_rx_varhex(&size) || gdb_rx_char() != ':') {
				gdb_tx_err(EINVAL);
				break;
			}
			if (gdb_rx_mem((char *)(uintptr_t)addr, size) == 0)
				gdb_tx_err(EIO);
			else
				gdb_tx_ok();
			break;
		}
		case 'P': {	/* Write register. */
			char *val;
			uintmax_t reg;
			val = gdb_rxp;
			if (gdb_rx_varhex(&reg) || gdb_rx_char() != '=' ||
			    !gdb_rx_mem(val, gdb_cpu_regsz(reg))) {
				gdb_tx_err(EINVAL);
				break;
			}
			gdb_cpu_setreg(reg, val);
			gdb_tx_ok();
			break;
		}
		case 'q':	/* General query. */
			if (gdb_rx_equal("fThreadInfo")) {
				thr_iter = kdb_thr_first();
				gdb_tx_begin('m');
				gdb_tx_hex((long)thr_iter->td_tid, 8);
				gdb_tx_end();
			} else if (gdb_rx_equal("sThreadInfo")) {
				if (thr_iter == NULL) {
					gdb_tx_err(ENXIO);
					break;
				}
				thr_iter = kdb_thr_next(thr_iter);
				if (thr_iter != NULL) {
					gdb_tx_begin('m');
					gdb_tx_hex((long)thr_iter->td_tid, 8);
					gdb_tx_end();
				} else {
					gdb_tx_begin('l');
					gdb_tx_end();
				}
			} else if (gdb_rx_equal("Search:memory:")) {
				gdb_do_mem_search();
			} else if (!gdb_cpu_query())
				gdb_tx_empty();
			break;
		case 's': {	/* Step. */
			uintmax_t addr;
			register_t pc;
			if (!gdb_rx_varhex(&addr)) {
				pc = addr;
				gdb_cpu_setreg(GDB_REG_PC, &pc);
			}
			kdb_cpu_set_singlestep();
			gdb_listening = 1;
			return (1);
		}
		case 'S': {	/* Step with signal. */
			uintmax_t addr, sig;
			register_t pc;
			if (!gdb_rx_varhex(&sig) && gdb_rx_char() == ';' &&
			    !gdb_rx_varhex(&addr)) {
				pc = addr;
				gdb_cpu_setreg(GDB_REG_PC, &pc);
			}
			kdb_cpu_set_singlestep();
			gdb_listening = 1;
			return (1);
		}
		case 'T': {	/* Thread alive. */
			intmax_t tid;
			if (gdb_rx_varhex(&tid)) {
				gdb_tx_err(EINVAL);
				break;
			}
			if (kdb_thr_lookup(tid) != NULL)
				gdb_tx_ok();
			else
				gdb_tx_err(ENOENT);
			break;
		}
		case -1:
			/* Empty command. Treat as unknown command. */
			/* FALLTHROUGH */
		default:
			/* Unknown command. Send empty response. */
			gdb_tx_empty();
			break;
		}
	}
	(void)kdb_jmpbuf(prev_jb);
	return (0);
}
