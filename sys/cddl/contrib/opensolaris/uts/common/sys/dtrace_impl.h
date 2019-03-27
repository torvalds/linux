/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright 2016 Joyent, Inc.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef _SYS_DTRACE_IMPL_H
#define	_SYS_DTRACE_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * DTrace Dynamic Tracing Software: Kernel Implementation Interfaces
 *
 * Note: The contents of this file are private to the implementation of the
 * Solaris system and DTrace subsystem and are subject to change at any time
 * without notice.  Applications and drivers using these interfaces will fail
 * to run on future releases.  These interfaces should not be used for any
 * purpose except those expressly outlined in dtrace(7D) and libdtrace(3LIB).
 * Please refer to the "Solaris Dynamic Tracing Guide" for more information.
 */

#include <sys/dtrace.h>

#ifndef illumos
#ifdef __sparcv9
typedef uint32_t		pc_t;
#else
typedef uintptr_t		pc_t;
#endif
typedef	u_long			greg_t;
#endif

/*
 * DTrace Implementation Constants and Typedefs
 */
#define	DTRACE_MAXPROPLEN		128
#define	DTRACE_DYNVAR_CHUNKSIZE		256

#ifdef __FreeBSD__
#define	NCPU		MAXCPU
#endif /* __FreeBSD__ */

struct dtrace_probe;
struct dtrace_ecb;
struct dtrace_predicate;
struct dtrace_action;
struct dtrace_provider;
struct dtrace_state;

typedef struct dtrace_probe dtrace_probe_t;
typedef struct dtrace_ecb dtrace_ecb_t;
typedef struct dtrace_predicate dtrace_predicate_t;
typedef struct dtrace_action dtrace_action_t;
typedef struct dtrace_provider dtrace_provider_t;
typedef struct dtrace_meta dtrace_meta_t;
typedef struct dtrace_state dtrace_state_t;
typedef uint32_t dtrace_optid_t;
typedef uint32_t dtrace_specid_t;
typedef uint64_t dtrace_genid_t;

/*
 * DTrace Probes
 *
 * The probe is the fundamental unit of the DTrace architecture.  Probes are
 * created by DTrace providers, and managed by the DTrace framework.  A probe
 * is identified by a unique <provider, module, function, name> tuple, and has
 * a unique probe identifier assigned to it.  (Some probes are not associated
 * with a specific point in text; these are called _unanchored probes_ and have
 * no module or function associated with them.)  Probes are represented as a
 * dtrace_probe structure.  To allow quick lookups based on each element of the
 * probe tuple, probes are hashed by each of provider, module, function and
 * name.  (If a lookup is performed based on a regular expression, a
 * dtrace_probekey is prepared, and a linear search is performed.) Each probe
 * is additionally pointed to by a linear array indexed by its identifier.  The
 * identifier is the provider's mechanism for indicating to the DTrace
 * framework that a probe has fired:  the identifier is passed as the first
 * argument to dtrace_probe(), where it is then mapped into the corresponding
 * dtrace_probe structure.  From the dtrace_probe structure, dtrace_probe() can
 * iterate over the probe's list of enabling control blocks; see "DTrace
 * Enabling Control Blocks", below.)
 */
struct dtrace_probe {
	dtrace_id_t dtpr_id;			/* probe identifier */
	dtrace_ecb_t *dtpr_ecb;			/* ECB list; see below */
	dtrace_ecb_t *dtpr_ecb_last;		/* last ECB in list */
	void *dtpr_arg;				/* provider argument */
	dtrace_cacheid_t dtpr_predcache;	/* predicate cache ID */
	int dtpr_aframes;			/* artificial frames */
	dtrace_provider_t *dtpr_provider;	/* pointer to provider */
	char *dtpr_mod;				/* probe's module name */
	char *dtpr_func;			/* probe's function name */
	char *dtpr_name;			/* probe's name */
	dtrace_probe_t *dtpr_nextmod;		/* next in module hash */
	dtrace_probe_t *dtpr_prevmod;		/* previous in module hash */
	dtrace_probe_t *dtpr_nextfunc;		/* next in function hash */
	dtrace_probe_t *dtpr_prevfunc;		/* previous in function hash */
	dtrace_probe_t *dtpr_nextname;		/* next in name hash */
	dtrace_probe_t *dtpr_prevname;		/* previous in name hash */
	dtrace_genid_t dtpr_gen;		/* probe generation ID */
};

typedef int dtrace_probekey_f(const char *, const char *, int);

typedef struct dtrace_probekey {
	char *dtpk_prov;			/* provider name to match */
	dtrace_probekey_f *dtpk_pmatch;		/* provider matching function */
	char *dtpk_mod;				/* module name to match */
	dtrace_probekey_f *dtpk_mmatch;		/* module matching function */
	char *dtpk_func;			/* func name to match */
	dtrace_probekey_f *dtpk_fmatch;		/* func matching function */
	char *dtpk_name;			/* name to match */
	dtrace_probekey_f *dtpk_nmatch;		/* name matching function */
	dtrace_id_t dtpk_id;			/* identifier to match */
} dtrace_probekey_t;

typedef struct dtrace_hashbucket {
	struct dtrace_hashbucket *dthb_next;	/* next on hash chain */
	dtrace_probe_t *dthb_chain;		/* chain of probes */
	int dthb_len;				/* number of probes here */
} dtrace_hashbucket_t;

typedef struct dtrace_hash {
	dtrace_hashbucket_t **dth_tab;		/* hash table */
	int dth_size;				/* size of hash table */
	int dth_mask;				/* mask to index into table */
	int dth_nbuckets;			/* total number of buckets */
	uintptr_t dth_nextoffs;			/* offset of next in probe */
	uintptr_t dth_prevoffs;			/* offset of prev in probe */
	uintptr_t dth_stroffs;			/* offset of str in probe */
} dtrace_hash_t;

/*
 * DTrace Enabling Control Blocks
 *
 * When a provider wishes to fire a probe, it calls into dtrace_probe(),
 * passing the probe identifier as the first argument.  As described above,
 * dtrace_probe() maps the identifier into a pointer to a dtrace_probe_t
 * structure.  This structure contains information about the probe, and a
 * pointer to the list of Enabling Control Blocks (ECBs).  Each ECB points to
 * DTrace consumer state, and contains an optional predicate, and a list of
 * actions.  (Shown schematically below.)  The ECB abstraction allows a single
 * probe to be multiplexed across disjoint consumers, or across disjoint
 * enablings of a single probe within one consumer.
 *
 *   Enabling Control Block
 *        dtrace_ecb_t
 * +------------------------+
 * | dtrace_epid_t ---------+--------------> Enabled Probe ID (EPID)
 * | dtrace_state_t * ------+--------------> State associated with this ECB
 * | dtrace_predicate_t * --+---------+
 * | dtrace_action_t * -----+----+    |
 * | dtrace_ecb_t * ---+    |    |    |       Predicate (if any)
 * +-------------------+----+    |    |       dtrace_predicate_t
 *                     |         |    +---> +--------------------+
 *                     |         |          | dtrace_difo_t * ---+----> DIFO
 *                     |         |          +--------------------+
 *                     |         |
 *            Next ECB |         |           Action
 *            (if any) |         |       dtrace_action_t
 *                     :         +--> +-------------------+
 *                     :              | dtrace_actkind_t -+------> kind
 *                     v              | dtrace_difo_t * --+------> DIFO (if any)
 *                                    | dtrace_recdesc_t -+------> record descr.
 *                                    | dtrace_action_t * +------+
 *                                    +-------------------+      |
 *                                                               | Next action
 *                               +-------------------------------+  (if any)
 *                               |
 *                               |           Action
 *                               |       dtrace_action_t
 *                               +--> +-------------------+
 *                                    | dtrace_actkind_t -+------> kind
 *                                    | dtrace_difo_t * --+------> DIFO (if any)
 *                                    | dtrace_action_t * +------+
 *                                    +-------------------+      |
 *                                                               | Next action
 *                               +-------------------------------+  (if any)
 *                               |
 *                               :
 *                               v
 *
 *
 * dtrace_probe() iterates over the ECB list.  If the ECB needs less space
 * than is available in the principal buffer, the ECB is processed:  if the
 * predicate is non-NULL, the DIF object is executed.  If the result is
 * non-zero, the action list is processed, with each action being executed
 * accordingly.  When the action list has been completely executed, processing
 * advances to the next ECB. The ECB abstraction allows disjoint consumers
 * to multiplex on single probes.
 *
 * Execution of the ECB results in consuming dte_size bytes in the buffer
 * to record data.  During execution, dte_needed bytes must be available in
 * the buffer.  This space is used for both recorded data and tuple data.
 */
struct dtrace_ecb {
	dtrace_epid_t dte_epid;			/* enabled probe ID */
	uint32_t dte_alignment;			/* required alignment */
	size_t dte_needed;			/* space needed for execution */
	size_t dte_size;			/* size of recorded payload */
	dtrace_predicate_t *dte_predicate;	/* predicate, if any */
	dtrace_action_t *dte_action;		/* actions, if any */
	dtrace_ecb_t *dte_next;			/* next ECB on probe */
	dtrace_state_t *dte_state;		/* pointer to state */
	uint32_t dte_cond;			/* security condition */
	dtrace_probe_t *dte_probe;		/* pointer to probe */
	dtrace_action_t *dte_action_last;	/* last action on ECB */
	uint64_t dte_uarg;			/* library argument */
};

struct dtrace_predicate {
	dtrace_difo_t *dtp_difo;		/* DIF object */
	dtrace_cacheid_t dtp_cacheid;		/* cache identifier */
	int dtp_refcnt;				/* reference count */
};

struct dtrace_action {
	dtrace_actkind_t dta_kind;		/* kind of action */
	uint16_t dta_intuple;			/* boolean:  in aggregation */
	uint32_t dta_refcnt;			/* reference count */
	dtrace_difo_t *dta_difo;		/* pointer to DIFO */
	dtrace_recdesc_t dta_rec;		/* record description */
	dtrace_action_t *dta_prev;		/* previous action */
	dtrace_action_t *dta_next;		/* next action */
};

typedef struct dtrace_aggregation {
	dtrace_action_t dtag_action;		/* action; must be first */
	dtrace_aggid_t dtag_id;			/* identifier */
	dtrace_ecb_t *dtag_ecb;			/* corresponding ECB */
	dtrace_action_t *dtag_first;		/* first action in tuple */
	uint32_t dtag_base;			/* base of aggregation */
	uint8_t dtag_hasarg;			/* boolean:  has argument */
	uint64_t dtag_initial;			/* initial value */
	void (*dtag_aggregate)(uint64_t *, uint64_t, uint64_t);
} dtrace_aggregation_t;

/*
 * DTrace Buffers
 *
 * Principal buffers, aggregation buffers, and speculative buffers are all
 * managed with the dtrace_buffer structure.  By default, this structure
 * includes twin data buffers -- dtb_tomax and dtb_xamot -- that serve as the
 * active and passive buffers, respectively.  For speculative buffers,
 * dtb_xamot will be NULL; for "ring" and "fill" buffers, dtb_xamot will point
 * to a scratch buffer.  For all buffer types, the dtrace_buffer structure is
 * always allocated on a per-CPU basis; a single dtrace_buffer structure is
 * never shared among CPUs.  (That is, there is never true sharing of the
 * dtrace_buffer structure; to prevent false sharing of the structure, it must
 * always be aligned to the coherence granularity -- generally 64 bytes.)
 *
 * One of the critical design decisions of DTrace is that a given ECB always
 * stores the same quantity and type of data.  This is done to assure that the
 * only metadata required for an ECB's traced data is the EPID.  That is, from
 * the EPID, the consumer can determine the data layout.  (The data buffer
 * layout is shown schematically below.)  By assuring that one can determine
 * data layout from the EPID, the metadata stream can be separated from the
 * data stream -- simplifying the data stream enormously.  The ECB always
 * proceeds the recorded data as part of the dtrace_rechdr_t structure that
 * includes the EPID and a high-resolution timestamp used for output ordering
 * consistency.
 *
 *      base of data buffer --->  +--------+--------------------+--------+
 *                                | rechdr | data               | rechdr |
 *                                +--------+------+--------+----+--------+
 *                                | data          | rechdr | data        |
 *                                +---------------+--------+-------------+
 *                                | data, cont.                          |
 *                                +--------+--------------------+--------+
 *                                | rechdr | data               |        |
 *                                +--------+--------------------+        |
 *                                |                ||                    |
 *                                |                ||                    |
 *                                |                \/                    |
 *                                :                                      :
 *                                .                                      .
 *                                .                                      .
 *                                .                                      .
 *                                :                                      :
 *                                |                                      |
 *     limit of data buffer --->  +--------------------------------------+
 *
 * When evaluating an ECB, dtrace_probe() determines if the ECB's needs of the
 * principal buffer (both scratch and payload) exceed the available space.  If
 * the ECB's needs exceed available space (and if the principal buffer policy
 * is the default "switch" policy), the ECB is dropped, the buffer's drop count
 * is incremented, and processing advances to the next ECB.  If the ECB's needs
 * can be met with the available space, the ECB is processed, but the offset in
 * the principal buffer is only advanced if the ECB completes processing
 * without error.
 *
 * When a buffer is to be switched (either because the buffer is the principal
 * buffer with a "switch" policy or because it is an aggregation buffer), a
 * cross call is issued to the CPU associated with the buffer.  In the cross
 * call context, interrupts are disabled, and the active and the inactive
 * buffers are atomically switched.  This involves switching the data pointers,
 * copying the various state fields (offset, drops, errors, etc.) into their
 * inactive equivalents, and clearing the state fields.  Because interrupts are
 * disabled during this procedure, the switch is guaranteed to appear atomic to
 * dtrace_probe().
 *
 * DTrace Ring Buffering
 *
 * To process a ring buffer correctly, one must know the oldest valid record.
 * Processing starts at the oldest record in the buffer and continues until
 * the end of the buffer is reached.  Processing then resumes starting with
 * the record stored at offset 0 in the buffer, and continues until the
 * youngest record is processed.  If trace records are of a fixed-length,
 * determining the oldest record is trivial:
 *
 *   - If the ring buffer has not wrapped, the oldest record is the record
 *     stored at offset 0.
 *
 *   - If the ring buffer has wrapped, the oldest record is the record stored
 *     at the current offset.
 *
 * With variable length records, however, just knowing the current offset
 * doesn't suffice for determining the oldest valid record:  assuming that one
 * allows for arbitrary data, one has no way of searching forward from the
 * current offset to find the oldest valid record.  (That is, one has no way
 * of separating data from metadata.) It would be possible to simply refuse to
 * process any data in the ring buffer between the current offset and the
 * limit, but this leaves (potentially) an enormous amount of otherwise valid
 * data unprocessed.
 *
 * To effect ring buffering, we track two offsets in the buffer:  the current
 * offset and the _wrapped_ offset.  If a request is made to reserve some
 * amount of data, and the buffer has wrapped, the wrapped offset is
 * incremented until the wrapped offset minus the current offset is greater
 * than or equal to the reserve request.  This is done by repeatedly looking
 * up the ECB corresponding to the EPID at the current wrapped offset, and
 * incrementing the wrapped offset by the size of the data payload
 * corresponding to that ECB.  If this offset is greater than or equal to the
 * limit of the data buffer, the wrapped offset is set to 0.  Thus, the
 * current offset effectively "chases" the wrapped offset around the buffer.
 * Schematically:
 *
 *      base of data buffer --->  +------+--------------------+------+
 *                                | EPID | data               | EPID |
 *                                +------+--------+------+----+------+
 *                                | data          | EPID | data      |
 *                                +---------------+------+-----------+
 *                                | data, cont.                      |
 *                                +------+---------------------------+
 *                                | EPID | data                      |
 *           current offset --->  +------+---------------------------+
 *                                | invalid data                     |
 *           wrapped offset --->  +------+--------------------+------+
 *                                | EPID | data               | EPID |
 *                                +------+--------+------+----+------+
 *                                | data          | EPID | data      |
 *                                +---------------+------+-----------+
 *                                :                                  :
 *                                .                                  .
 *                                .        ... valid data ...        .
 *                                .                                  .
 *                                :                                  :
 *                                +------+-------------+------+------+
 *                                | EPID | data        | EPID | data |
 *                                +------+------------++------+------+
 *                                | data, cont.       | leftover     |
 *     limit of data buffer --->  +-------------------+--------------+
 *
 * If the amount of requested buffer space exceeds the amount of space
 * available between the current offset and the end of the buffer:
 *
 *  (1)  all words in the data buffer between the current offset and the limit
 *       of the data buffer (marked "leftover", above) are set to
 *       DTRACE_EPIDNONE
 *
 *  (2)  the wrapped offset is set to zero
 *
 *  (3)  the iteration process described above occurs until the wrapped offset
 *       is greater than the amount of desired space.
 *
 * The wrapped offset is implemented by (re-)using the inactive offset.
 * In a "switch" buffer policy, the inactive offset stores the offset in
 * the inactive buffer; in a "ring" buffer policy, it stores the wrapped
 * offset.
 *
 * DTrace Scratch Buffering
 *
 * Some ECBs may wish to allocate dynamically-sized temporary scratch memory.
 * To accommodate such requests easily, scratch memory may be allocated in
 * the buffer beyond the current offset plus the needed memory of the current
 * ECB.  If there isn't sufficient room in the buffer for the requested amount
 * of scratch space, the allocation fails and an error is generated.  Scratch
 * memory is tracked in the dtrace_mstate_t and is automatically freed when
 * the ECB ceases processing.  Note that ring buffers cannot allocate their
 * scratch from the principal buffer -- lest they needlessly overwrite older,
 * valid data.  Ring buffers therefore have their own dedicated scratch buffer
 * from which scratch is allocated.
 */
#define	DTRACEBUF_RING		0x0001		/* bufpolicy set to "ring" */
#define	DTRACEBUF_FILL		0x0002		/* bufpolicy set to "fill" */
#define	DTRACEBUF_NOSWITCH	0x0004		/* do not switch buffer */
#define	DTRACEBUF_WRAPPED	0x0008		/* ring buffer has wrapped */
#define	DTRACEBUF_DROPPED	0x0010		/* drops occurred */
#define	DTRACEBUF_ERROR		0x0020		/* errors occurred */
#define	DTRACEBUF_FULL		0x0040		/* "fill" buffer is full */
#define	DTRACEBUF_CONSUMED	0x0080		/* buffer has been consumed */
#define	DTRACEBUF_INACTIVE	0x0100		/* buffer is not yet active */

typedef struct dtrace_buffer {
	uint64_t dtb_offset;			/* current offset in buffer */
	uint64_t dtb_size;			/* size of buffer */
	uint32_t dtb_flags;			/* flags */
	uint32_t dtb_drops;			/* number of drops */
	caddr_t dtb_tomax;			/* active buffer */
	caddr_t dtb_xamot;			/* inactive buffer */
	uint32_t dtb_xamot_flags;		/* inactive flags */
	uint32_t dtb_xamot_drops;		/* drops in inactive buffer */
	uint64_t dtb_xamot_offset;		/* offset in inactive buffer */
	uint32_t dtb_errors;			/* number of errors */
	uint32_t dtb_xamot_errors;		/* errors in inactive buffer */
#ifndef _LP64
	uint64_t dtb_pad1;			/* pad out to 64 bytes */
#endif
	uint64_t dtb_switched;			/* time of last switch */
	uint64_t dtb_interval;			/* observed switch interval */
	uint64_t dtb_pad2[6];			/* pad to avoid false sharing */
} dtrace_buffer_t;

/*
 * DTrace Aggregation Buffers
 *
 * Aggregation buffers use much of the same mechanism as described above
 * ("DTrace Buffers").  However, because an aggregation is fundamentally a
 * hash, there exists dynamic metadata associated with an aggregation buffer
 * that is not associated with other kinds of buffers.  This aggregation
 * metadata is _only_ relevant for the in-kernel implementation of
 * aggregations; it is not actually relevant to user-level consumers.  To do
 * this, we allocate dynamic aggregation data (hash keys and hash buckets)
 * starting below the _limit_ of the buffer, and we allocate data from the
 * _base_ of the buffer.  When the aggregation buffer is copied out, _only_ the
 * data is copied out; the metadata is simply discarded.  Schematically,
 * aggregation buffers look like:
 *
 *      base of data buffer --->  +-------+------+-----------+-------+
 *                                | aggid | key  | value     | aggid |
 *                                +-------+------+-----------+-------+
 *                                | key                              |
 *                                +-------+-------+-----+------------+
 *                                | value | aggid | key | value      |
 *                                +-------+------++-----+------+-----+
 *                                | aggid | key  | value       |     |
 *                                +-------+------+-------------+     |
 *                                |                ||                |
 *                                |                ||                |
 *                                |                \/                |
 *                                :                                  :
 *                                .                                  .
 *                                .                                  .
 *                                .                                  .
 *                                :                                  :
 *                                |                /\                |
 *                                |                ||   +------------+
 *                                |                ||   |            |
 *                                +---------------------+            |
 *                                | hash keys                        |
 *                                | (dtrace_aggkey structures)       |
 *                                |                                  |
 *                                +----------------------------------+
 *                                | hash buckets                     |
 *                                | (dtrace_aggbuffer structure)     |
 *                                |                                  |
 *     limit of data buffer --->  +----------------------------------+
 *
 *
 * As implied above, just as we assure that ECBs always store a constant
 * amount of data, we assure that a given aggregation -- identified by its
 * aggregation ID -- always stores data of a constant quantity and type.
 * As with EPIDs, this allows the aggregation ID to serve as the metadata for a
 * given record.
 *
 * Note that the size of the dtrace_aggkey structure must be sizeof (uintptr_t)
 * aligned.  (If this the structure changes such that this becomes false, an
 * assertion will fail in dtrace_aggregate().)
 */
typedef struct dtrace_aggkey {
	uint32_t dtak_hashval;			/* hash value */
	uint32_t dtak_action:4;			/* action -- 4 bits */
	uint32_t dtak_size:28;			/* size -- 28 bits */
	caddr_t dtak_data;			/* data pointer */
	struct dtrace_aggkey *dtak_next;	/* next in hash chain */
} dtrace_aggkey_t;

typedef struct dtrace_aggbuffer {
	uintptr_t dtagb_hashsize;		/* number of buckets */
	uintptr_t dtagb_free;			/* free list of keys */
	dtrace_aggkey_t **dtagb_hash;		/* hash table */
} dtrace_aggbuffer_t;

/*
 * DTrace Speculations
 *
 * Speculations have a per-CPU buffer and a global state.  Once a speculation
 * buffer has been comitted or discarded, it cannot be reused until all CPUs
 * have taken the same action (commit or discard) on their respective
 * speculative buffer.  However, because DTrace probes may execute in arbitrary
 * context, other CPUs cannot simply be cross-called at probe firing time to
 * perform the necessary commit or discard.  The speculation states thus
 * optimize for the case that a speculative buffer is only active on one CPU at
 * the time of a commit() or discard() -- for if this is the case, other CPUs
 * need not take action, and the speculation is immediately available for
 * reuse.  If the speculation is active on multiple CPUs, it must be
 * asynchronously cleaned -- potentially leading to a higher rate of dirty
 * speculative drops.  The speculation states are as follows:
 *
 *  DTRACESPEC_INACTIVE       <= Initial state; inactive speculation
 *  DTRACESPEC_ACTIVE         <= Allocated, but not yet speculatively traced to
 *  DTRACESPEC_ACTIVEONE      <= Speculatively traced to on one CPU
 *  DTRACESPEC_ACTIVEMANY     <= Speculatively traced to on more than one CPU
 *  DTRACESPEC_COMMITTING     <= Currently being commited on one CPU
 *  DTRACESPEC_COMMITTINGMANY <= Currently being commited on many CPUs
 *  DTRACESPEC_DISCARDING     <= Currently being discarded on many CPUs
 *
 * The state transition diagram is as follows:
 *
 *     +----------------------------------------------------------+
 *     |                                                          |
 *     |                      +------------+                      |
 *     |  +-------------------| COMMITTING |<-----------------+   |
 *     |  |                   +------------+                  |   |
 *     |  | copied spec.            ^             commit() on |   | discard() on
 *     |  | into principal          |              active CPU |   | active CPU
 *     |  |                         | commit()                |   |
 *     V  V                         |                         |   |
 * +----------+                 +--------+                +-----------+
 * | INACTIVE |---------------->| ACTIVE |--------------->| ACTIVEONE |
 * +----------+  speculation()  +--------+  speculate()   +-----------+
 *     ^  ^                         |                         |   |
 *     |  |                         | discard()               |   |
 *     |  | asynchronously          |            discard() on |   | speculate()
 *     |  | cleaned                 V            inactive CPU |   | on inactive
 *     |  |                   +------------+                  |   | CPU
 *     |  +-------------------| DISCARDING |<-----------------+   |
 *     |                      +------------+                      |
 *     | asynchronously             ^                             |
 *     | copied spec.               |       discard()             |
 *     | into principal             +------------------------+    |
 *     |                                                     |    V
 *  +----------------+             commit()              +------------+
 *  | COMMITTINGMANY |<----------------------------------| ACTIVEMANY |
 *  +----------------+                                   +------------+
 */
typedef enum dtrace_speculation_state {
	DTRACESPEC_INACTIVE = 0,
	DTRACESPEC_ACTIVE,
	DTRACESPEC_ACTIVEONE,
	DTRACESPEC_ACTIVEMANY,
	DTRACESPEC_COMMITTING,
	DTRACESPEC_COMMITTINGMANY,
	DTRACESPEC_DISCARDING
} dtrace_speculation_state_t;

typedef struct dtrace_speculation {
	dtrace_speculation_state_t dtsp_state;	/* current speculation state */
	int dtsp_cleaning;			/* non-zero if being cleaned */
	dtrace_buffer_t *dtsp_buffer;		/* speculative buffer */
} dtrace_speculation_t;

/*
 * DTrace Dynamic Variables
 *
 * The dynamic variable problem is obviously decomposed into two subproblems:
 * allocating new dynamic storage, and freeing old dynamic storage.  The
 * presence of the second problem makes the first much more complicated -- or
 * rather, the absence of the second renders the first trivial.  This is the
 * case with aggregations, for which there is effectively no deallocation of
 * dynamic storage.  (Or more accurately, all dynamic storage is deallocated
 * when a snapshot is taken of the aggregation.)  As DTrace dynamic variables
 * allow for both dynamic allocation and dynamic deallocation, the
 * implementation of dynamic variables is quite a bit more complicated than
 * that of their aggregation kin.
 *
 * We observe that allocating new dynamic storage is tricky only because the
 * size can vary -- the allocation problem is much easier if allocation sizes
 * are uniform.  We further observe that in D, the size of dynamic variables is
 * actually _not_ dynamic -- dynamic variable sizes may be determined by static
 * analysis of DIF text.  (This is true even of putatively dynamically-sized
 * objects like strings and stacks, the sizes of which are dictated by the
 * "stringsize" and "stackframes" variables, respectively.)  We exploit this by
 * performing this analysis on all DIF before enabling any probes.  For each
 * dynamic load or store, we calculate the dynamically-allocated size plus the
 * size of the dtrace_dynvar structure plus the storage required to key the
 * data.  For all DIF, we take the largest value and dub it the _chunksize_.
 * We then divide dynamic memory into two parts:  a hash table that is wide
 * enough to have every chunk in its own bucket, and a larger region of equal
 * chunksize units.  Whenever we wish to dynamically allocate a variable, we
 * always allocate a single chunk of memory.  Depending on the uniformity of
 * allocation, this will waste some amount of memory -- but it eliminates the
 * non-determinism inherent in traditional heap fragmentation.
 *
 * Dynamic objects are allocated by storing a non-zero value to them; they are
 * deallocated by storing a zero value to them.  Dynamic variables are
 * complicated enormously by being shared between CPUs.  In particular,
 * consider the following scenario:
 *
 *                 CPU A                                 CPU B
 *  +---------------------------------+   +---------------------------------+
 *  |                                 |   |                                 |
 *  | allocates dynamic object a[123] |   |                                 |
 *  | by storing the value 345 to it  |   |                                 |
 *  |                               --------->                              |
 *  |                                 |   | wishing to load from object     |
 *  |                                 |   | a[123], performs lookup in      |
 *  |                                 |   | dynamic variable space          |
 *  |                               <---------                              |
 *  | deallocates object a[123] by    |   |                                 |
 *  | storing 0 to it                 |   |                                 |
 *  |                                 |   |                                 |
 *  | allocates dynamic object b[567] |   | performs load from a[123]       |
 *  | by storing the value 789 to it  |   |                                 |
 *  :                                 :   :                                 :
 *  .                                 .   .                                 .
 *
 * This is obviously a race in the D program, but there are nonetheless only
 * two valid values for CPU B's load from a[123]:  345 or 0.  Most importantly,
 * CPU B may _not_ see the value 789 for a[123].
 *
 * There are essentially two ways to deal with this:
 *
 *  (1)  Explicitly spin-lock variables.  That is, if CPU B wishes to load
 *       from a[123], it needs to lock a[123] and hold the lock for the
 *       duration that it wishes to manipulate it.
 *
 *  (2)  Avoid reusing freed chunks until it is known that no CPU is referring
 *       to them.
 *
 * The implementation of (1) is rife with complexity, because it requires the
 * user of a dynamic variable to explicitly decree when they are done using it.
 * Were all variables by value, this perhaps wouldn't be debilitating -- but
 * dynamic variables of non-scalar types are tracked by reference.  That is, if
 * a dynamic variable is, say, a string, and that variable is to be traced to,
 * say, the principal buffer, the DIF emulation code returns to the main
 * dtrace_probe() loop a pointer to the underlying storage, not the contents of
 * the storage.  Further, code calling on DIF emulation would have to be aware
 * that the DIF emulation has returned a reference to a dynamic variable that
 * has been potentially locked.  The variable would have to be unlocked after
 * the main dtrace_probe() loop is finished with the variable, and the main
 * dtrace_probe() loop would have to be careful to not call any further DIF
 * emulation while the variable is locked to avoid deadlock.  More generally,
 * if one were to implement (1), DIF emulation code dealing with dynamic
 * variables could only deal with one dynamic variable at a time (lest deadlock
 * result).  To sum, (1) exports too much subtlety to the users of dynamic
 * variables -- increasing maintenance burden and imposing serious constraints
 * on future DTrace development.
 *
 * The implementation of (2) is also complex, but the complexity is more
 * manageable.  We need to be sure that when a variable is deallocated, it is
 * not placed on a traditional free list, but rather on a _dirty_ list.  Once a
 * variable is on a dirty list, it cannot be found by CPUs performing a
 * subsequent lookup of the variable -- but it may still be in use by other
 * CPUs.  To assure that all CPUs that may be seeing the old variable have
 * cleared out of probe context, a dtrace_sync() can be issued.  Once the
 * dtrace_sync() has completed, it can be known that all CPUs are done
 * manipulating the dynamic variable -- the dirty list can be atomically
 * appended to the free list.  Unfortunately, there's a slight hiccup in this
 * mechanism:  dtrace_sync() may not be issued from probe context.  The
 * dtrace_sync() must be therefore issued asynchronously from non-probe
 * context.  For this we rely on the DTrace cleaner, a cyclic that runs at the
 * "cleanrate" frequency.  To ease this implementation, we define several chunk
 * lists:
 *
 *   - Dirty.  Deallocated chunks, not yet cleaned.  Not available.
 *
 *   - Rinsing.  Formerly dirty chunks that are currently being asynchronously
 *     cleaned.  Not available, but will be shortly.  Dynamic variable
 *     allocation may not spin or block for availability, however.
 *
 *   - Clean.  Clean chunks, ready for allocation -- but not on the free list.
 *
 *   - Free.  Available for allocation.
 *
 * Moreover, to avoid absurd contention, _each_ of these lists is implemented
 * on a per-CPU basis.  This is only for performance, not correctness; chunks
 * may be allocated from another CPU's free list.  The algorithm for allocation
 * then is this:
 *
 *   (1)  Attempt to atomically allocate from current CPU's free list.  If list
 *        is non-empty and allocation is successful, allocation is complete.
 *
 *   (2)  If the clean list is non-empty, atomically move it to the free list,
 *        and reattempt (1).
 *
 *   (3)  If the dynamic variable space is in the CLEAN state, look for free
 *        and clean lists on other CPUs by setting the current CPU to the next
 *        CPU, and reattempting (1).  If the next CPU is the current CPU (that
 *        is, if all CPUs have been checked), atomically switch the state of
 *        the dynamic variable space based on the following:
 *
 *        - If no free chunks were found and no dirty chunks were found,
 *          atomically set the state to EMPTY.
 *
 *        - If dirty chunks were found, atomically set the state to DIRTY.
 *
 *        - If rinsing chunks were found, atomically set the state to RINSING.
 *
 *   (4)  Based on state of dynamic variable space state, increment appropriate
 *        counter to indicate dynamic drops (if in EMPTY state) vs. dynamic
 *        dirty drops (if in DIRTY state) vs. dynamic rinsing drops (if in
 *        RINSING state).  Fail the allocation.
 *
 * The cleaning cyclic operates with the following algorithm:  for all CPUs
 * with a non-empty dirty list, atomically move the dirty list to the rinsing
 * list.  Perform a dtrace_sync().  For all CPUs with a non-empty rinsing list,
 * atomically move the rinsing list to the clean list.  Perform another
 * dtrace_sync().  By this point, all CPUs have seen the new clean list; the
 * state of the dynamic variable space can be restored to CLEAN.
 *
 * There exist two final races that merit explanation.  The first is a simple
 * allocation race:
 *
 *                 CPU A                                 CPU B
 *  +---------------------------------+   +---------------------------------+
 *  |                                 |   |                                 |
 *  | allocates dynamic object a[123] |   | allocates dynamic object a[123] |
 *  | by storing the value 345 to it  |   | by storing the value 567 to it  |
 *  |                                 |   |                                 |
 *  :                                 :   :                                 :
 *  .                                 .   .                                 .
 *
 * Again, this is a race in the D program.  It can be resolved by having a[123]
 * hold the value 345 or a[123] hold the value 567 -- but it must be true that
 * a[123] have only _one_ of these values.  (That is, the racing CPUs may not
 * put the same element twice on the same hash chain.)  This is resolved
 * simply:  before the allocation is undertaken, the start of the new chunk's
 * hash chain is noted.  Later, after the allocation is complete, the hash
 * chain is atomically switched to point to the new element.  If this fails
 * (because of either concurrent allocations or an allocation concurrent with a
 * deletion), the newly allocated chunk is deallocated to the dirty list, and
 * the whole process of looking up (and potentially allocating) the dynamic
 * variable is reattempted.
 *
 * The final race is a simple deallocation race:
 *
 *                 CPU A                                 CPU B
 *  +---------------------------------+   +---------------------------------+
 *  |                                 |   |                                 |
 *  | deallocates dynamic object      |   | deallocates dynamic object      |
 *  | a[123] by storing the value 0   |   | a[123] by storing the value 0   |
 *  | to it                           |   | to it                           |
 *  |                                 |   |                                 |
 *  :                                 :   :                                 :
 *  .                                 .   .                                 .
 *
 * Once again, this is a race in the D program, but it is one that we must
 * handle without corrupting the underlying data structures.  Because
 * deallocations require the deletion of a chunk from the middle of a hash
 * chain, we cannot use a single-word atomic operation to remove it.  For this,
 * we add a spin lock to the hash buckets that is _only_ used for deallocations
 * (allocation races are handled as above).  Further, this spin lock is _only_
 * held for the duration of the delete; before control is returned to the DIF
 * emulation code, the hash bucket is unlocked.
 */
typedef struct dtrace_key {
	uint64_t dttk_value;			/* data value or data pointer */
	uint64_t dttk_size;			/* 0 if by-val, >0 if by-ref */
} dtrace_key_t;

typedef struct dtrace_tuple {
	uint32_t dtt_nkeys;			/* number of keys in tuple */
	uint32_t dtt_pad;			/* padding */
	dtrace_key_t dtt_key[1];		/* array of tuple keys */
} dtrace_tuple_t;

typedef struct dtrace_dynvar {
	uint64_t dtdv_hashval;			/* hash value -- 0 if free */
	struct dtrace_dynvar *dtdv_next;	/* next on list or hash chain */
	void *dtdv_data;			/* pointer to data */
	dtrace_tuple_t dtdv_tuple;		/* tuple key */
} dtrace_dynvar_t;

typedef enum dtrace_dynvar_op {
	DTRACE_DYNVAR_ALLOC,
	DTRACE_DYNVAR_NOALLOC,
	DTRACE_DYNVAR_DEALLOC
} dtrace_dynvar_op_t;

typedef struct dtrace_dynhash {
	dtrace_dynvar_t *dtdh_chain;		/* hash chain for this bucket */
	uintptr_t dtdh_lock;			/* deallocation lock */
#ifdef _LP64
	uintptr_t dtdh_pad[6];			/* pad to avoid false sharing */
#else
	uintptr_t dtdh_pad[14];			/* pad to avoid false sharing */
#endif
} dtrace_dynhash_t;

typedef struct dtrace_dstate_percpu {
	dtrace_dynvar_t *dtdsc_free;		/* free list for this CPU */
	dtrace_dynvar_t *dtdsc_dirty;		/* dirty list for this CPU */
	dtrace_dynvar_t *dtdsc_rinsing;		/* rinsing list for this CPU */
	dtrace_dynvar_t *dtdsc_clean;		/* clean list for this CPU */
	uint64_t dtdsc_drops;			/* number of capacity drops */
	uint64_t dtdsc_dirty_drops;		/* number of dirty drops */
	uint64_t dtdsc_rinsing_drops;		/* number of rinsing drops */
#ifdef _LP64
	uint64_t dtdsc_pad;			/* pad to avoid false sharing */
#else
	uint64_t dtdsc_pad[2];			/* pad to avoid false sharing */
#endif
} dtrace_dstate_percpu_t;

typedef enum dtrace_dstate_state {
	DTRACE_DSTATE_CLEAN = 0,
	DTRACE_DSTATE_EMPTY,
	DTRACE_DSTATE_DIRTY,
	DTRACE_DSTATE_RINSING
} dtrace_dstate_state_t;

typedef struct dtrace_dstate {
	void *dtds_base;			/* base of dynamic var. space */
	size_t dtds_size;			/* size of dynamic var. space */
	size_t dtds_hashsize;			/* number of buckets in hash */
	size_t dtds_chunksize;			/* size of each chunk */
	dtrace_dynhash_t *dtds_hash;		/* pointer to hash table */
	dtrace_dstate_state_t dtds_state;	/* current dynamic var. state */
	dtrace_dstate_percpu_t *dtds_percpu;	/* per-CPU dyn. var. state */
} dtrace_dstate_t;

/*
 * DTrace Variable State
 *
 * The DTrace variable state tracks user-defined variables in its dtrace_vstate
 * structure.  Each DTrace consumer has exactly one dtrace_vstate structure,
 * but some dtrace_vstate structures may exist without a corresponding DTrace
 * consumer (see "DTrace Helpers", below).  As described in <sys/dtrace.h>,
 * user-defined variables can have one of three scopes:
 *
 *  DIFV_SCOPE_GLOBAL  =>  global scope
 *  DIFV_SCOPE_THREAD  =>  thread-local scope (i.e. "self->" variables)
 *  DIFV_SCOPE_LOCAL   =>  clause-local scope (i.e. "this->" variables)
 *
 * The variable state tracks variables by both their scope and their allocation
 * type:
 *
 *  - The dtvs_globals and dtvs_locals members each point to an array of
 *    dtrace_statvar structures.  These structures contain both the variable
 *    metadata (dtrace_difv structures) and the underlying storage for all
 *    statically allocated variables, including statically allocated
 *    DIFV_SCOPE_GLOBAL variables and all DIFV_SCOPE_LOCAL variables.
 *
 *  - The dtvs_tlocals member points to an array of dtrace_difv structures for
 *    DIFV_SCOPE_THREAD variables.  As such, this array tracks _only_ the
 *    variable metadata for DIFV_SCOPE_THREAD variables; the underlying storage
 *    is allocated out of the dynamic variable space.
 *
 *  - The dtvs_dynvars member is the dynamic variable state associated with the
 *    variable state.  The dynamic variable state (described in "DTrace Dynamic
 *    Variables", above) tracks all DIFV_SCOPE_THREAD variables and all
 *    dynamically-allocated DIFV_SCOPE_GLOBAL variables.
 */
typedef struct dtrace_statvar {
	uint64_t dtsv_data;			/* data or pointer to it */
	size_t dtsv_size;			/* size of pointed-to data */
	int dtsv_refcnt;			/* reference count */
	dtrace_difv_t dtsv_var;			/* variable metadata */
} dtrace_statvar_t;

typedef struct dtrace_vstate {
	dtrace_state_t *dtvs_state;		/* back pointer to state */
	dtrace_statvar_t **dtvs_globals;	/* statically-allocated glbls */
	int dtvs_nglobals;			/* number of globals */
	dtrace_difv_t *dtvs_tlocals;		/* thread-local metadata */
	int dtvs_ntlocals;			/* number of thread-locals */
	dtrace_statvar_t **dtvs_locals;		/* clause-local data */
	int dtvs_nlocals;			/* number of clause-locals */
	dtrace_dstate_t dtvs_dynvars;		/* dynamic variable state */
} dtrace_vstate_t;

/*
 * DTrace Machine State
 *
 * In the process of processing a fired probe, DTrace needs to track and/or
 * cache some per-CPU state associated with that particular firing.  This is
 * state that is always discarded after the probe firing has completed, and
 * much of it is not specific to any DTrace consumer, remaining valid across
 * all ECBs.  This state is tracked in the dtrace_mstate structure.
 */
#define	DTRACE_MSTATE_ARGS		0x00000001
#define	DTRACE_MSTATE_PROBE		0x00000002
#define	DTRACE_MSTATE_EPID		0x00000004
#define	DTRACE_MSTATE_TIMESTAMP		0x00000008
#define	DTRACE_MSTATE_STACKDEPTH	0x00000010
#define	DTRACE_MSTATE_CALLER		0x00000020
#define	DTRACE_MSTATE_IPL		0x00000040
#define	DTRACE_MSTATE_FLTOFFS		0x00000080
#define	DTRACE_MSTATE_WALLTIMESTAMP	0x00000100
#define	DTRACE_MSTATE_USTACKDEPTH	0x00000200
#define	DTRACE_MSTATE_UCALLER		0x00000400

typedef struct dtrace_mstate {
	uintptr_t dtms_scratch_base;		/* base of scratch space */
	uintptr_t dtms_scratch_ptr;		/* current scratch pointer */
	size_t dtms_scratch_size;		/* scratch size */
	uint32_t dtms_present;			/* variables that are present */
	uint64_t dtms_arg[5];			/* cached arguments */
	dtrace_epid_t dtms_epid;		/* current EPID */
	uint64_t dtms_timestamp;		/* cached timestamp */
	hrtime_t dtms_walltimestamp;		/* cached wall timestamp */
	int dtms_stackdepth;			/* cached stackdepth */
	int dtms_ustackdepth;			/* cached ustackdepth */
	struct dtrace_probe *dtms_probe;	/* current probe */
	uintptr_t dtms_caller;			/* cached caller */
	uint64_t dtms_ucaller;			/* cached user-level caller */
	int dtms_ipl;				/* cached interrupt pri lev */
	int dtms_fltoffs;			/* faulting DIFO offset */
	uintptr_t dtms_strtok;			/* saved strtok() pointer */
	uintptr_t dtms_strtok_limit;		/* upper bound of strtok ptr */
	uint32_t dtms_access;			/* memory access rights */
	dtrace_difo_t *dtms_difo;		/* current dif object */
	file_t *dtms_getf;			/* cached rval of getf() */
} dtrace_mstate_t;

#define	DTRACE_COND_OWNER	0x1
#define	DTRACE_COND_USERMODE	0x2
#define	DTRACE_COND_ZONEOWNER	0x4

#define	DTRACE_PROBEKEY_MAXDEPTH	8	/* max glob recursion depth */

/*
 * Access flag used by dtrace_mstate.dtms_access.
 */
#define	DTRACE_ACCESS_KERNEL	0x1		/* the priv to read kmem */


/*
 * DTrace Activity
 *
 * Each DTrace consumer is in one of several states, which (for purposes of
 * avoiding yet-another overloading of the noun "state") we call the current
 * _activity_.  The activity transitions on dtrace_go() (from DTRACIOCGO), on
 * dtrace_stop() (from DTRACIOCSTOP) and on the exit() action.  Activities may
 * only transition in one direction; the activity transition diagram is a
 * directed acyclic graph.  The activity transition diagram is as follows:
 *
 *
 * +----------+                   +--------+                   +--------+
 * | INACTIVE |------------------>| WARMUP |------------------>| ACTIVE |
 * +----------+   dtrace_go(),    +--------+   dtrace_go(),    +--------+
 *                before BEGIN        |        after BEGIN       |  |  |
 *                                    |                          |  |  |
 *                      exit() action |                          |  |  |
 *                     from BEGIN ECB |                          |  |  |
 *                                    |                          |  |  |
 *                                    v                          |  |  |
 *                               +----------+     exit() action  |  |  |
 * +-----------------------------| DRAINING |<-------------------+  |  |
 * |                             +----------+                       |  |
 * |                                  |                             |  |
 * |                   dtrace_stop(), |                             |  |
 * |                     before END   |                             |  |
 * |                                  |                             |  |
 * |                                  v                             |  |
 * | +---------+                 +----------+                       |  |
 * | | STOPPED |<----------------| COOLDOWN |<----------------------+  |
 * | +---------+  dtrace_stop(), +----------+     dtrace_stop(),       |
 * |                after END                       before END         |
 * |                                                                   |
 * |                              +--------+                           |
 * +----------------------------->| KILLED |<--------------------------+
 *       deadman timeout or       +--------+     deadman timeout or
 *        killed consumer                         killed consumer
 *
 * Note that once a DTrace consumer has stopped tracing, there is no way to
 * restart it; if a DTrace consumer wishes to restart tracing, it must reopen
 * the DTrace pseudodevice.
 */
typedef enum dtrace_activity {
	DTRACE_ACTIVITY_INACTIVE = 0,		/* not yet running */
	DTRACE_ACTIVITY_WARMUP,			/* while starting */
	DTRACE_ACTIVITY_ACTIVE,			/* running */
	DTRACE_ACTIVITY_DRAINING,		/* before stopping */
	DTRACE_ACTIVITY_COOLDOWN,		/* while stopping */
	DTRACE_ACTIVITY_STOPPED,		/* after stopping */
	DTRACE_ACTIVITY_KILLED			/* killed */
} dtrace_activity_t;

/*
 * DTrace Helper Implementation
 *
 * A description of the helper architecture may be found in <sys/dtrace.h>.
 * Each process contains a pointer to its helpers in its p_dtrace_helpers
 * member.  This is a pointer to a dtrace_helpers structure, which contains an
 * array of pointers to dtrace_helper structures, helper variable state (shared
 * among a process's helpers) and a generation count.  (The generation count is
 * used to provide an identifier when a helper is added so that it may be
 * subsequently removed.)  The dtrace_helper structure is self-explanatory,
 * containing pointers to the objects needed to execute the helper.  Note that
 * helpers are _duplicated_ across fork(2), and destroyed on exec(2).  No more
 * than dtrace_helpers_max are allowed per-process.
 */
#define	DTRACE_HELPER_ACTION_USTACK	0
#define	DTRACE_NHELPER_ACTIONS		1

typedef struct dtrace_helper_action {
	int dtha_generation;			/* helper action generation */
	int dtha_nactions;			/* number of actions */
	dtrace_difo_t *dtha_predicate;		/* helper action predicate */
	dtrace_difo_t **dtha_actions;		/* array of actions */
	struct dtrace_helper_action *dtha_next;	/* next helper action */
} dtrace_helper_action_t;

typedef struct dtrace_helper_provider {
	int dthp_generation;			/* helper provider generation */
	uint32_t dthp_ref;			/* reference count */
	dof_helper_t dthp_prov;			/* DOF w/ provider and probes */
} dtrace_helper_provider_t;

typedef struct dtrace_helpers {
	dtrace_helper_action_t **dthps_actions;	/* array of helper actions */
	dtrace_vstate_t dthps_vstate;		/* helper action var. state */
	dtrace_helper_provider_t **dthps_provs;	/* array of providers */
	uint_t dthps_nprovs;			/* count of providers */
	uint_t dthps_maxprovs;			/* provider array size */
	int dthps_generation;			/* current generation */
	pid_t dthps_pid;			/* pid of associated proc */
	int dthps_deferred;			/* helper in deferred list */
	struct dtrace_helpers *dthps_next;	/* next pointer */
	struct dtrace_helpers *dthps_prev;	/* prev pointer */
} dtrace_helpers_t;

/*
 * DTrace Helper Action Tracing
 *
 * Debugging helper actions can be arduous.  To ease the development and
 * debugging of helpers, DTrace contains a tracing-framework-within-a-tracing-
 * framework: helper tracing.  If dtrace_helptrace_enabled is non-zero (which
 * it is by default on DEBUG kernels), all helper activity will be traced to a
 * global, in-kernel ring buffer.  Each entry includes a pointer to the specific
 * helper, the location within the helper, and a trace of all local variables.
 * The ring buffer may be displayed in a human-readable format with the
 * ::dtrace_helptrace mdb(1) dcmd.
 */
#define	DTRACE_HELPTRACE_NEXT	(-1)
#define	DTRACE_HELPTRACE_DONE	(-2)
#define	DTRACE_HELPTRACE_ERR	(-3)

typedef struct dtrace_helptrace {
	dtrace_helper_action_t	*dtht_helper;	/* helper action */
	int dtht_where;				/* where in helper action */
	int dtht_nlocals;			/* number of locals */
	int dtht_fault;				/* type of fault (if any) */
	int dtht_fltoffs;			/* DIF offset */
	uint64_t dtht_illval;			/* faulting value */
	uint64_t dtht_locals[1];		/* local variables */
} dtrace_helptrace_t;

/*
 * DTrace Credentials
 *
 * In probe context, we have limited flexibility to examine the credentials
 * of the DTrace consumer that created a particular enabling.  We use
 * the Least Privilege interfaces to cache the consumer's cred pointer and
 * some facts about that credential in a dtrace_cred_t structure. These
 * can limit the consumer's breadth of visibility and what actions the
 * consumer may take.
 */
#define	DTRACE_CRV_ALLPROC		0x01
#define	DTRACE_CRV_KERNEL		0x02
#define	DTRACE_CRV_ALLZONE		0x04

#define	DTRACE_CRV_ALL		(DTRACE_CRV_ALLPROC | DTRACE_CRV_KERNEL | \
	DTRACE_CRV_ALLZONE)

#define	DTRACE_CRA_PROC				0x0001
#define	DTRACE_CRA_PROC_CONTROL			0x0002
#define	DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER	0x0004
#define	DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE	0x0008
#define	DTRACE_CRA_PROC_DESTRUCTIVE_CREDCHG	0x0010
#define	DTRACE_CRA_KERNEL			0x0020
#define	DTRACE_CRA_KERNEL_DESTRUCTIVE		0x0040

#define	DTRACE_CRA_ALL		(DTRACE_CRA_PROC | \
	DTRACE_CRA_PROC_CONTROL | \
	DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER | \
	DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE | \
	DTRACE_CRA_PROC_DESTRUCTIVE_CREDCHG | \
	DTRACE_CRA_KERNEL | \
	DTRACE_CRA_KERNEL_DESTRUCTIVE)

typedef struct dtrace_cred {
	cred_t			*dcr_cred;
	uint8_t			dcr_destructive;
	uint8_t			dcr_visible;
	uint16_t		dcr_action;
} dtrace_cred_t;

/*
 * DTrace Consumer State
 *
 * Each DTrace consumer has an associated dtrace_state structure that contains
 * its in-kernel DTrace state -- including options, credentials, statistics and
 * pointers to ECBs, buffers, speculations and formats.  A dtrace_state
 * structure is also allocated for anonymous enablings.  When anonymous state
 * is grabbed, the grabbing consumers dts_anon pointer is set to the grabbed
 * dtrace_state structure.
 */
struct dtrace_state {
#ifdef illumos
	dev_t dts_dev;				/* device */
#else
	struct cdev *dts_dev;			/* device */
#endif
	int dts_necbs;				/* total number of ECBs */
	dtrace_ecb_t **dts_ecbs;		/* array of ECBs */
	dtrace_epid_t dts_epid;			/* next EPID to allocate */
	size_t dts_needed;			/* greatest needed space */
	struct dtrace_state *dts_anon;		/* anon. state, if grabbed */
	dtrace_activity_t dts_activity;		/* current activity */
	dtrace_vstate_t dts_vstate;		/* variable state */
	dtrace_buffer_t *dts_buffer;		/* principal buffer */
	dtrace_buffer_t *dts_aggbuffer;		/* aggregation buffer */
	dtrace_speculation_t *dts_speculations;	/* speculation array */
	int dts_nspeculations;			/* number of speculations */
	int dts_naggregations;			/* number of aggregations */
	dtrace_aggregation_t **dts_aggregations; /* aggregation array */
#ifdef illumos
	vmem_t *dts_aggid_arena;		/* arena for aggregation IDs */
#else
	struct unrhdr *dts_aggid_arena;		/* arena for aggregation IDs */
#endif
	uint64_t dts_errors;			/* total number of errors */
	uint32_t dts_speculations_busy;		/* number of spec. busy */
	uint32_t dts_speculations_unavail;	/* number of spec unavail */
	uint32_t dts_stkstroverflows;		/* stack string tab overflows */
	uint32_t dts_dblerrors;			/* errors in ERROR probes */
	uint32_t dts_reserve;			/* space reserved for END */
	hrtime_t dts_laststatus;		/* time of last status */
#ifdef illumos
	cyclic_id_t dts_cleaner;		/* cleaning cyclic */
	cyclic_id_t dts_deadman;		/* deadman cyclic */
#else
	struct callout dts_cleaner;		/* Cleaning callout. */
	struct callout dts_deadman;		/* Deadman callout. */
#endif
	hrtime_t dts_alive;			/* time last alive */
	char dts_speculates;			/* boolean: has speculations */
	char dts_destructive;			/* boolean: has dest. actions */
	int dts_nformats;			/* number of formats */
	char **dts_formats;			/* format string array */
	dtrace_optval_t dts_options[DTRACEOPT_MAX]; /* options */
	dtrace_cred_t dts_cred;			/* credentials */
	size_t dts_nretained;			/* number of retained enabs */
	int dts_getf;				/* number of getf() calls */
	uint64_t dts_rstate[NCPU][2];		/* per-CPU random state */
};

struct dtrace_provider {
	dtrace_pattr_t dtpv_attr;		/* provider attributes */
	dtrace_ppriv_t dtpv_priv;		/* provider privileges */
	dtrace_pops_t dtpv_pops;		/* provider operations */
	char *dtpv_name;			/* provider name */
	void *dtpv_arg;				/* provider argument */
	hrtime_t dtpv_defunct;			/* when made defunct */
	struct dtrace_provider *dtpv_next;	/* next provider */
};

struct dtrace_meta {
	dtrace_mops_t dtm_mops;			/* meta provider operations */
	char *dtm_name;				/* meta provider name */
	void *dtm_arg;				/* meta provider user arg */
	uint64_t dtm_count;			/* no. of associated provs. */
};

/*
 * DTrace Enablings
 *
 * A dtrace_enabling structure is used to track a collection of ECB
 * descriptions -- before they have been turned into actual ECBs.  This is
 * created as a result of DOF processing, and is generally used to generate
 * ECBs immediately thereafter.  However, enablings are also generally
 * retained should the probes they describe be created at a later time; as
 * each new module or provider registers with the framework, the retained
 * enablings are reevaluated, with any new match resulting in new ECBs.  To
 * prevent probes from being matched more than once, the enabling tracks the
 * last probe generation matched, and only matches probes from subsequent
 * generations.
 */
typedef struct dtrace_enabling {
	dtrace_ecbdesc_t **dten_desc;		/* all ECB descriptions */
	int dten_ndesc;				/* number of ECB descriptions */
	int dten_maxdesc;			/* size of ECB array */
	dtrace_vstate_t *dten_vstate;		/* associated variable state */
	dtrace_genid_t dten_probegen;		/* matched probe generation */
	dtrace_ecbdesc_t *dten_current;		/* current ECB description */
	int dten_error;				/* current error value */
	int dten_primed;			/* boolean: set if primed */
	struct dtrace_enabling *dten_prev;	/* previous enabling */
	struct dtrace_enabling *dten_next;	/* next enabling */
} dtrace_enabling_t;

/*
 * DTrace Anonymous Enablings
 *
 * Anonymous enablings are DTrace enablings that are not associated with a
 * controlling process, but rather derive their enabling from DOF stored as
 * properties in the dtrace.conf file.  If there is an anonymous enabling, a
 * DTrace consumer state and enabling are created on attach.  The state may be
 * subsequently grabbed by the first consumer specifying the "grabanon"
 * option.  As long as an anonymous DTrace enabling exists, dtrace(7D) will
 * refuse to unload.
 */
typedef struct dtrace_anon {
	dtrace_state_t *dta_state;		/* DTrace consumer state */
	dtrace_enabling_t *dta_enabling;	/* pointer to enabling */
	processorid_t dta_beganon;		/* which CPU BEGIN ran on */
} dtrace_anon_t;

/*
 * DTrace Error Debugging
 */
#ifdef DEBUG
#define	DTRACE_ERRDEBUG
#endif

#ifdef DTRACE_ERRDEBUG

typedef struct dtrace_errhash {
	const char	*dter_msg;	/* error message */
	int		dter_count;	/* number of times seen */
} dtrace_errhash_t;

#define	DTRACE_ERRHASHSZ	256	/* must be > number of err msgs */

#endif	/* DTRACE_ERRDEBUG */

/*
 * DTrace Toxic Ranges
 *
 * DTrace supports safe loads from probe context; if the address turns out to
 * be invalid, a bit will be set by the kernel indicating that DTrace
 * encountered a memory error, and DTrace will propagate the error to the user
 * accordingly.  However, there may exist some regions of memory in which an
 * arbitrary load can change system state, and from which it is impossible to
 * recover from such a load after it has been attempted.  Examples of this may
 * include memory in which programmable I/O registers are mapped (for which a
 * read may have some implications for the device) or (in the specific case of
 * UltraSPARC-I and -II) the virtual address hole.  The platform is required
 * to make DTrace aware of these toxic ranges; DTrace will then check that
 * target addresses are not in a toxic range before attempting to issue a
 * safe load.
 */
typedef struct dtrace_toxrange {
	uintptr_t	dtt_base;		/* base of toxic range */
	uintptr_t	dtt_limit;		/* limit of toxic range */
} dtrace_toxrange_t;

#ifdef illumos
extern uint64_t dtrace_getarg(int, int);
#else
extern uint64_t __noinline dtrace_getarg(int, int);
#endif
extern greg_t dtrace_getfp(void);
extern int dtrace_getipl(void);
extern uintptr_t dtrace_caller(int);
extern uint32_t dtrace_cas32(uint32_t *, uint32_t, uint32_t);
extern void *dtrace_casptr(volatile void *, volatile void *, volatile void *);
extern void dtrace_copyin(uintptr_t, uintptr_t, size_t, volatile uint16_t *);
extern void dtrace_copyinstr(uintptr_t, uintptr_t, size_t, volatile uint16_t *);
extern void dtrace_copyout(uintptr_t, uintptr_t, size_t, volatile uint16_t *);
extern void dtrace_copyoutstr(uintptr_t, uintptr_t, size_t,
    volatile uint16_t *);
extern void dtrace_getpcstack(pc_t *, int, int, uint32_t *);
extern ulong_t dtrace_getreg(struct trapframe *, uint_t);
extern int dtrace_getstackdepth(int);
extern void dtrace_getupcstack(uint64_t *, int);
extern void dtrace_getufpstack(uint64_t *, uint64_t *, int);
extern int dtrace_getustackdepth(void);
extern uintptr_t dtrace_fulword(void *);
extern uint8_t dtrace_fuword8(void *);
extern uint16_t dtrace_fuword16(void *);
extern uint32_t dtrace_fuword32(void *);
extern uint64_t dtrace_fuword64(void *);
extern void dtrace_probe_error(dtrace_state_t *, dtrace_epid_t, int, int,
    int, uintptr_t);
extern int dtrace_assfail(const char *, const char *, int);
extern int dtrace_attached(void);
#ifdef illumos
extern hrtime_t dtrace_gethrestime(void);
#endif

#ifdef __sparc
extern void dtrace_flush_windows(void);
extern void dtrace_flush_user_windows(void);
extern uint_t dtrace_getotherwin(void);
extern uint_t dtrace_getfprs(void);
#else
extern void dtrace_copy(uintptr_t, uintptr_t, size_t);
extern void dtrace_copystr(uintptr_t, uintptr_t, size_t, volatile uint16_t *);
#endif

/*
 * DTrace Assertions
 *
 * DTrace calls ASSERT and VERIFY from probe context.  To assure that a failed
 * ASSERT or VERIFY does not induce a markedly more catastrophic failure (e.g.,
 * one from which a dump cannot be gleaned), DTrace must define its own ASSERT
 * and VERIFY macros to be ones that may safely be called from probe context.
 * This header file must thus be included by any DTrace component that calls
 * ASSERT and/or VERIFY from probe context, and _only_ by those components.
 * (The only exception to this is kernel debugging infrastructure at user-level
 * that doesn't depend on calling ASSERT.)
 */
#undef ASSERT
#undef VERIFY
#define	VERIFY(EX)	((void)((EX) || \
			dtrace_assfail(#EX, __FILE__, __LINE__)))
#ifdef DEBUG
#define	ASSERT(EX)	((void)((EX) || \
			dtrace_assfail(#EX, __FILE__, __LINE__)))
#else
#define	ASSERT(X)	((void)0)
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DTRACE_IMPL_H */
