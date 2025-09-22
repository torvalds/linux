/*	$OpenBSD: db_mp.c,v 1.14 2023/01/30 10:49:05 jsg Exp $	*/

/*
 * Copyright (c) 2003, 2004 Andreas Gunnarsson <andreas@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/mutex.h>

#include <machine/db_machdep.h>

struct db_mutex ddb_mp_mutex = DB_MUTEX_INITIALIZER;

volatile int ddb_state = DDB_STATE_NOT_RUNNING;	/* protected by ddb_mp_mutex */
volatile cpuid_t ddb_active_cpu;		/* protected by ddb_mp_mutex */

extern volatile int		db_switch_cpu;
extern volatile long		db_switch_to_cpu;

/*
 * All processors wait in db_enter_ddb() (unless explicitly started from
 * ddb) but only one owns ddb.  If the current processor should own ddb,
 * db_enter_ddb() returns 1.  If the current processor should keep
 * executing as usual (if ddb is exited or the processor is explicitly
 * started), db_enter_ddb returns 0.
 * If this is the first CPU entering ddb, db_enter_ddb() will stop all
 * other CPUs by sending IPIs.
 */
int
db_enter_ddb(void)
{
	int i;

	db_mtx_enter(&ddb_mp_mutex);

	/* If we are first in, grab ddb and stop all other CPUs */
	if (ddb_state == DDB_STATE_NOT_RUNNING) {
		ddb_active_cpu = cpu_number();
		ddb_state = DDB_STATE_RUNNING;
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL && i != cpu_number() &&
			    cpu_info[i]->ci_ddb_paused != CI_DDB_STOPPED) {
				cpu_info[i]->ci_ddb_paused = CI_DDB_SHOULDSTOP;
				i386_send_ipi(cpu_info[i], I386_IPI_DDB);
			}
		}
		return (1);
	}

	/* Leaving ddb completely.  Start all other CPUs and return 0 */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_EXITING) {
		for (i = 0; i < MAXCPUS; i++) {
			if (cpu_info[i] != NULL) {
				cpu_info[i]->ci_ddb_paused = CI_DDB_RUNNING;
			}
		}
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}

	/* We're switching to another CPU.  db_ddbproc_cmd() has made sure
	 * it is waiting for ddb, we just have to set ddb_active_cpu. */
	if (ddb_active_cpu == cpu_number() && db_switch_cpu) {
		curcpu()->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_switch_cpu = 0;
		ddb_active_cpu = db_switch_to_cpu;
		cpu_info[db_switch_to_cpu]->ci_ddb_paused = CI_DDB_ENTERDDB;
	}

	/* Wait until we should enter ddb or resume */
	while (ddb_active_cpu != cpu_number() &&
	    curcpu()->ci_ddb_paused != CI_DDB_RUNNING) {
		if (curcpu()->ci_ddb_paused == CI_DDB_SHOULDSTOP)
			curcpu()->ci_ddb_paused = CI_DDB_STOPPED;
		db_mtx_leave(&ddb_mp_mutex);

		/* Busy wait without locking, we'll confirm with lock later */
		while (ddb_active_cpu != cpu_number() &&
		    curcpu()->ci_ddb_paused != CI_DDB_RUNNING)
			;	/* Do nothing */

		db_mtx_enter(&ddb_mp_mutex);
	}

	/* Either enter ddb or exit */
	if (ddb_active_cpu == cpu_number() && ddb_state == DDB_STATE_RUNNING) {
		curcpu()->ci_ddb_paused = CI_DDB_INDDB;
		db_mtx_leave(&ddb_mp_mutex);
		return (1);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
		return (0);
	}
}

void
db_startcpu(int cpu)
{
	if (cpu != cpu_number() && cpu_info[cpu] != NULL) {
		db_mtx_enter(&ddb_mp_mutex);
		cpu_info[cpu]->ci_ddb_paused = CI_DDB_RUNNING;
		db_mtx_leave(&ddb_mp_mutex);
	}
}

void
db_stopcpu(int cpu)
{
	db_mtx_enter(&ddb_mp_mutex);
	if (cpu != cpu_number() && cpu_info[cpu] != NULL &&
	    cpu_info[cpu]->ci_ddb_paused != CI_DDB_STOPPED) {
		cpu_info[cpu]->ci_ddb_paused = CI_DDB_SHOULDSTOP;
		db_mtx_leave(&ddb_mp_mutex);
		i386_send_ipi(cpu_info[cpu], I386_IPI_DDB);
	} else {
		db_mtx_leave(&ddb_mp_mutex);
	}
}

void
i386_ipi_db(struct cpu_info *ci)
{
	db_enter();
}
