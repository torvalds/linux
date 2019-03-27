/*-
 * Copyright (c) 2014 Mateusz Guzik <mjg@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_SEQC_H_
#define _SYS_SEQC_H_

#ifdef _KERNEL
#include <sys/systm.h>
#endif
#include <sys/types.h>

/*
 * seqc_t may be included in structs visible to userspace
 */
typedef uint32_t seqc_t;

#ifdef _KERNEL

/*
 * seqc allows readers and writers to work with a consistent snapshot. Modifying
 * operations must be enclosed within a transaction delineated by
 * seqc_write_beg/seqc_write_end. The trick works by having the writer increment
 * the seqcuence number twice, at the beginning and end of the transaction.
 * The reader detects that the seqcuence number has not changed between its start
 * and end, and that the seqcuence number is even, to validate consistency.
 *
 * Some fencing (both hard fencing and compiler barriers) may be needed,
 * depending on the cpu. Modern AMD cpus provide strong enough guarantees to not
 * require any fencing by the reader or writer.
 *
 * Example usage:
 *
 * writers:
 *     lock_exclusive(&obj->lock);
 *     seqc_write_begin(&obj->seqc);
 *     obj->var1 = ...;
 *     obj->var2 = ...;
 *     seqc_write_end(&obj->seqc);
 *     unlock_exclusive(&obj->lock);
 *
 * readers:
 *    int var1, var2;
 *    seqc_t seqc;
 *
 *    for (;;) {
 *    	      seqc = seqc_read(&obj->seqc);
 *            var1 = obj->var1;
 *            var2 = obj->var2;
 *            if (seqc_consistent(&obj->seqc, seqc))
 *                   break;
 *    }
 *    .....
 *
 * Writers may not block or sleep in any way.
 *
 * There are 2 minor caveats in this implementation:
 *
 * 1. There is no guarantee of progress. That is, a large number of writers can
 * interfere with the execution of the readers and cause the code to live-lock
 * in a loop trying to acquire a consistent snapshot.
 *
 * 2. If the reader loops long enough, the counter may overflow and eventually
 * wrap back to its initial value, fooling the reader into accepting the
 * snapshot.  Given that this needs 4 billion transactional writes across a
 * single contended reader, it is unlikely to ever happen.
 */		

/* A hack to get MPASS macro */
#include <sys/lock.h>

#include <machine/cpu.h>

static __inline bool
seqc_in_modify(seqc_t seqcp)
{

	return (seqcp & 1);
}

static __inline void
seqc_write_begin(seqc_t *seqcp)
{

	critical_enter();
	MPASS(!seqc_in_modify(*seqcp));
	*seqcp += 1;
	atomic_thread_fence_rel();
}

static __inline void
seqc_write_end(seqc_t *seqcp)
{

	atomic_store_rel_int(seqcp, *seqcp + 1);
	MPASS(!seqc_in_modify(*seqcp));
	critical_exit();
}

static __inline seqc_t
seqc_read(const seqc_t *seqcp)
{
	seqc_t ret;

	for (;;) {
		ret = atomic_load_acq_int(__DECONST(seqc_t *, seqcp));
		if (seqc_in_modify(ret)) {
			cpu_spinwait();
			continue;
		}
		break;
	}

	return (ret);
}

static __inline seqc_t
seqc_consistent_nomb(const seqc_t *seqcp, seqc_t oldseqc)
{

	return (*seqcp == oldseqc);
}

static __inline seqc_t
seqc_consistent(const seqc_t *seqcp, seqc_t oldseqc)
{

	atomic_thread_fence_acq();
	return (seqc_consistent_nomb(seqcp, oldseqc));
}

#endif	/* _KERNEL */
#endif	/* _SYS_SEQC_H_ */
