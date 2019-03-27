/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Isilon Inc http://www.isilon.com/
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
 */
/**
 * @file
 *
 * fail(9) Facility.
 *
 * @ingroup failpoint_private
 */
/**
 * @defgroup failpoint fail(9) Facility
 *
 * Failpoints allow for injecting fake errors into running code on the fly,
 * without modifying code or recompiling with flags.  Failpoints are always
 * present, and are very efficient when disabled.  Failpoints are described
 * in man fail(9).
 */
/**
 * @defgroup failpoint_private Private fail(9) Implementation functions
 *
 * Private implementations for the actual failpoint code.
 *
 * @ingroup failpoint
 */
/**
 * @addtogroup failpoint_private
 * @{
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_stack.h"

#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/fail.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sleepqueue.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <machine/atomic.h>
#include <machine/stdarg.h>

#ifdef ILOG_DEFINE_FOR_FILE
ILOG_DEFINE_FOR_FILE(L_ISI_FAIL_POINT, L_ILOG, fail_point);
#endif

static MALLOC_DEFINE(M_FAIL_POINT, "Fail Points", "fail points system");
#define fp_free(ptr) free(ptr, M_FAIL_POINT)
#define fp_malloc(size, flags) malloc((size), M_FAIL_POINT, (flags))
#define fs_free(ptr) fp_free(ptr)
#define fs_malloc() fp_malloc(sizeof(struct fail_point_setting), \
    M_WAITOK | M_ZERO)

/**
 * These define the wchans that are used for sleeping, pausing respectively.
 * They are chosen arbitrarily but need to be distinct to the failpoint and
 * the sleep/pause distinction.
 */
#define FP_SLEEP_CHANNEL(fp) (void*)(fp)
#define FP_PAUSE_CHANNEL(fp) __DEVOLATILE(void*, &fp->fp_setting)

/**
 * Don't allow more than this many entries in a fail point set by sysctl.
 * The 99.99...% case is to have 1 entry.  I can't imagine having this many
 * entries, so it should not limit us.  Saves on re-mallocs while holding
 * a non-sleepable lock.
 */
#define FP_MAX_ENTRY_COUNT 20

/* Used to drain sbufs to the sysctl output */
int fail_sysctl_drain_func(void *, const char *, int);

/* Head of tailq of struct fail_point_entry */
TAILQ_HEAD(fail_point_entry_queue, fail_point_entry);

/**
 * fp entries garbage list; outstanding entries are cleaned up in the
 * garbage collector
 */
STAILQ_HEAD(fail_point_setting_garbage, fail_point_setting);
static struct fail_point_setting_garbage fp_setting_garbage =
        STAILQ_HEAD_INITIALIZER(fp_setting_garbage);
static struct mtx mtx_garbage_list;
MTX_SYSINIT(mtx_garbage_list, &mtx_garbage_list, "fail point garbage mtx",
        MTX_SPIN);

static struct sx sx_fp_set;
SX_SYSINIT(sx_fp_set, &sx_fp_set, "fail point set sx");

/**
 * Failpoint types.
 * Don't change these without changing fail_type_strings in fail.c.
 * @ingroup failpoint_private
 */
enum fail_point_t {
	FAIL_POINT_OFF,		/**< don't fail */
	FAIL_POINT_PANIC,	/**< panic */
	FAIL_POINT_RETURN,	/**< return an errorcode */
	FAIL_POINT_BREAK,	/**< break into the debugger */
	FAIL_POINT_PRINT,	/**< print a message */
	FAIL_POINT_SLEEP,	/**< sleep for some msecs */
	FAIL_POINT_PAUSE,	/**< sleep until failpoint is set to off */
	FAIL_POINT_YIELD,	/**< yield the cpu */
	FAIL_POINT_DELAY,	/**< busy wait the cpu */
	FAIL_POINT_NUMTYPES,
	FAIL_POINT_INVALID = -1
};

static struct {
	const char *name;
	int	nmlen;
} fail_type_strings[] = {
#define	FP_TYPE_NM_LEN(s)	{ s, sizeof(s) - 1 }
	[FAIL_POINT_OFF] =	FP_TYPE_NM_LEN("off"),
	[FAIL_POINT_PANIC] =	FP_TYPE_NM_LEN("panic"),
	[FAIL_POINT_RETURN] =	FP_TYPE_NM_LEN("return"),
	[FAIL_POINT_BREAK] =	FP_TYPE_NM_LEN("break"),
	[FAIL_POINT_PRINT] =	FP_TYPE_NM_LEN("print"),
	[FAIL_POINT_SLEEP] =	FP_TYPE_NM_LEN("sleep"),
	[FAIL_POINT_PAUSE] =	FP_TYPE_NM_LEN("pause"),
	[FAIL_POINT_YIELD] =	FP_TYPE_NM_LEN("yield"),
	[FAIL_POINT_DELAY] =	FP_TYPE_NM_LEN("delay"),
};

#define FE_COUNT_UNTRACKED (INT_MIN)

/**
 * Internal structure tracking a single term of a complete failpoint.
 * @ingroup failpoint_private
 */
struct fail_point_entry {
	volatile bool	fe_stale;
	enum fail_point_t	fe_type;	/**< type of entry */
	int		fe_arg;		/**< argument to type (e.g. return value) */
	int		fe_prob;	/**< likelihood of firing in millionths */
	int32_t		fe_count;	/**< number of times to fire, -1 means infinite */
	pid_t		fe_pid;		/**< only fail for this process */
	struct fail_point	*fe_parent;	/**< backpointer to fp */
	TAILQ_ENTRY(fail_point_entry)	fe_entries; /**< next entry ptr */
};

struct fail_point_setting {
	STAILQ_ENTRY(fail_point_setting) fs_garbage_link;
	struct fail_point_entry_queue fp_entry_queue;
	struct fail_point * fs_parent;
	struct mtx feq_mtx; /* Gives fail_point_pause something to do.  */
};

/**
 * Defines stating the equivalent of probablilty one (100%)
 */
enum {
	PROB_MAX = 1000000,	/* probability between zero and this number */
	PROB_DIGITS = 6		/* number of zero's in above number */
};

/* Get a ref on an fp's fp_setting */
static inline struct fail_point_setting *fail_point_setting_get_ref(
        struct fail_point *fp);
/* Release a ref on an fp_setting */
static inline void fail_point_setting_release_ref(struct fail_point *fp);
/* Allocate and initialize a struct fail_point_setting */
static struct fail_point_setting *fail_point_setting_new(struct
        fail_point *);
/* Free a struct fail_point_setting */
static void fail_point_setting_destroy(struct fail_point_setting *fp_setting);
/* Allocate and initialize a struct fail_point_entry */
static struct fail_point_entry *fail_point_entry_new(struct
        fail_point_setting *);
/* Free a struct fail_point_entry */
static void fail_point_entry_destroy(struct fail_point_entry *fp_entry);
/* Append fp setting to garbage list */
static inline void fail_point_setting_garbage_append(
        struct fail_point_setting *fp_setting);
/* Swap fp's setting with fp_setting_new */
static inline struct fail_point_setting *
        fail_point_swap_settings(struct fail_point *fp,
        struct fail_point_setting *fp_setting_new);
/* Free up any zero-ref setting in the garbage queue */
static void fail_point_garbage_collect(void);
/* If this fail point's setting are empty, then swap it out to NULL. */
static inline void fail_point_eval_swap_out(struct fail_point *fp,
        struct fail_point_setting *fp_setting);

bool
fail_point_is_off(struct fail_point *fp)
{
	bool return_val;
	struct fail_point_setting *fp_setting;
	struct fail_point_entry *ent;

	return_val = true;

	fp_setting = fail_point_setting_get_ref(fp);
	if (fp_setting != NULL) {
		TAILQ_FOREACH(ent, &fp_setting->fp_entry_queue,
		    fe_entries) {
			if (!ent->fe_stale) {
				return_val = false;
				break;
			}
		}
	}
	fail_point_setting_release_ref(fp);

	return (return_val);
}

/* Allocate and initialize a struct fail_point_setting */
static struct fail_point_setting *
fail_point_setting_new(struct fail_point *fp)
{
	struct fail_point_setting *fs_new;

	fs_new = fs_malloc();
	fs_new->fs_parent = fp;
	TAILQ_INIT(&fs_new->fp_entry_queue);
	mtx_init(&fs_new->feq_mtx, "fail point entries", NULL, MTX_SPIN);

	fail_point_setting_garbage_append(fs_new);

	return (fs_new);
}

/* Free a struct fail_point_setting */
static void
fail_point_setting_destroy(struct fail_point_setting *fp_setting)
{
	struct fail_point_entry *ent;

	while (!TAILQ_EMPTY(&fp_setting->fp_entry_queue)) {
		ent = TAILQ_FIRST(&fp_setting->fp_entry_queue);
		TAILQ_REMOVE(&fp_setting->fp_entry_queue, ent, fe_entries);
		fail_point_entry_destroy(ent);
	}

	fs_free(fp_setting);
}

/* Allocate and initialize a struct fail_point_entry */
static struct fail_point_entry *
fail_point_entry_new(struct fail_point_setting *fp_setting)
{
	struct fail_point_entry *fp_entry;

	fp_entry = fp_malloc(sizeof(struct fail_point_entry),
	        M_WAITOK | M_ZERO);
	fp_entry->fe_parent = fp_setting->fs_parent;
	fp_entry->fe_prob = PROB_MAX;
	fp_entry->fe_pid = NO_PID;
	fp_entry->fe_count = FE_COUNT_UNTRACKED;
	TAILQ_INSERT_TAIL(&fp_setting->fp_entry_queue, fp_entry,
	        fe_entries);

	return (fp_entry);
}

/* Free a struct fail_point_entry */
static void
fail_point_entry_destroy(struct fail_point_entry *fp_entry)
{

	fp_free(fp_entry);
}

/* Get a ref on an fp's fp_setting */
static inline struct fail_point_setting *
fail_point_setting_get_ref(struct fail_point *fp)
{
	struct fail_point_setting *fp_setting;

	/* Invariant: if we have a ref, our pointer to fp_setting is safe */
	atomic_add_acq_32(&fp->fp_ref_cnt, 1);
	fp_setting = fp->fp_setting;

	return (fp_setting);
}

/* Release a ref on an fp_setting */
static inline void
fail_point_setting_release_ref(struct fail_point *fp)
{

	KASSERT(&fp->fp_ref_cnt > 0, ("Attempting to deref w/no refs"));
	atomic_subtract_rel_32(&fp->fp_ref_cnt, 1);
}

/* Append fp entries to fp garbage list */
static inline void
fail_point_setting_garbage_append(struct fail_point_setting *fp_setting)
{

	mtx_lock_spin(&mtx_garbage_list);
	STAILQ_INSERT_TAIL(&fp_setting_garbage, fp_setting,
	        fs_garbage_link);
	mtx_unlock_spin(&mtx_garbage_list);
}

/* Swap fp's entries with fp_setting_new */
static struct fail_point_setting *
fail_point_swap_settings(struct fail_point *fp,
        struct fail_point_setting *fp_setting_new)
{
	struct fail_point_setting *fp_setting_old;

	fp_setting_old = fp->fp_setting;
	fp->fp_setting = fp_setting_new;

	return (fp_setting_old);
}

static inline void
fail_point_eval_swap_out(struct fail_point *fp,
        struct fail_point_setting *fp_setting)
{

	/* We may have already been swapped out and replaced; ignore. */
	if (fp->fp_setting == fp_setting)
		fail_point_swap_settings(fp, NULL);
}

/* Free up any zero-ref entries in the garbage queue */
static void
fail_point_garbage_collect(void)
{
	struct fail_point_setting *fs_current, *fs_next;
	struct fail_point_setting_garbage fp_ents_free_list;

	/**
	  * We will transfer the entries to free to fp_ents_free_list while holding
	  * the spin mutex, then free it after we drop the lock. This avoids
	  * triggering witness due to sleepable mutexes in the memory
	  * allocator.
	  */
	STAILQ_INIT(&fp_ents_free_list);

	mtx_lock_spin(&mtx_garbage_list);
	STAILQ_FOREACH_SAFE(fs_current, &fp_setting_garbage, fs_garbage_link,
	    fs_next) {
		if (fs_current->fs_parent->fp_setting != fs_current &&
		        fs_current->fs_parent->fp_ref_cnt == 0) {
			STAILQ_REMOVE(&fp_setting_garbage, fs_current,
			        fail_point_setting, fs_garbage_link);
			STAILQ_INSERT_HEAD(&fp_ents_free_list, fs_current,
			        fs_garbage_link);
		}
	}
	mtx_unlock_spin(&mtx_garbage_list);

	STAILQ_FOREACH_SAFE(fs_current, &fp_ents_free_list, fs_garbage_link,
	        fs_next)
		fail_point_setting_destroy(fs_current);
}

/* Drain out all refs from this fail point */
static inline void
fail_point_drain(struct fail_point *fp, int expected_ref)
{
	struct fail_point_setting *entries;

	entries = fail_point_swap_settings(fp, NULL);
	/**
	 * We have unpaused all threads; so we will wait no longer
	 * than the time taken for the longest remaining sleep, or
	 * the length of time of a long-running code block.
	 */
	while (fp->fp_ref_cnt > expected_ref) {
		wakeup(FP_PAUSE_CHANNEL(fp));
		tsleep(&fp, PWAIT, "fail_point_drain", hz / 100);
	}
	fail_point_swap_settings(fp, entries);
}

static inline void
fail_point_pause(struct fail_point *fp, enum fail_point_return_code *pret,
        struct mtx *mtx_sleep)
{

	if (fp->fp_pre_sleep_fn)
		fp->fp_pre_sleep_fn(fp->fp_pre_sleep_arg);

	msleep_spin(FP_PAUSE_CHANNEL(fp), mtx_sleep, "failpt", 0);

	if (fp->fp_post_sleep_fn)
		fp->fp_post_sleep_fn(fp->fp_post_sleep_arg);
}

static inline void
fail_point_sleep(struct fail_point *fp, int msecs,
        enum fail_point_return_code *pret)
{
	int timo;

	/* Convert from millisecs to ticks, rounding up */
	timo = howmany((int64_t)msecs * hz, 1000L);

	if (timo > 0) {
		if (!(fp->fp_flags & FAIL_POINT_USE_TIMEOUT_PATH)) {
			if (fp->fp_pre_sleep_fn)
				fp->fp_pre_sleep_fn(fp->fp_pre_sleep_arg);

			tsleep(FP_SLEEP_CHANNEL(fp), PWAIT, "failpt", timo);

			if (fp->fp_post_sleep_fn)
				fp->fp_post_sleep_fn(fp->fp_post_sleep_arg);
		} else {
			if (fp->fp_pre_sleep_fn)
				fp->fp_pre_sleep_fn(fp->fp_pre_sleep_arg);

			timeout(fp->fp_post_sleep_fn, fp->fp_post_sleep_arg,
			    timo);
			*pret = FAIL_POINT_RC_QUEUED;
		}
	}
}

static char *parse_fail_point(struct fail_point_setting *, char *);
static char *parse_term(struct fail_point_setting *, char *);
static char *parse_number(int *out_units, int *out_decimal, char *);
static char *parse_type(struct fail_point_entry *, char *);

/**
 * Initialize a fail_point.  The name is formed in a printf-like fashion
 * from "fmt" and subsequent arguments.  This function is generally used
 * for custom failpoints located at odd places in the sysctl tree, and is
 * not explicitly needed for standard in-line-declared failpoints.
 *
 * @ingroup failpoint
 */
void
fail_point_init(struct fail_point *fp, const char *fmt, ...)
{
	va_list ap;
	char *name;
	int n;

	fp->fp_setting = NULL;
	fp->fp_flags = 0;

	/* Figure out the size of the name. */
	va_start(ap, fmt);
	n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the name and fill it in. */
	name = fp_malloc(n + 1, M_WAITOK);
	if (name != NULL) {
		va_start(ap, fmt);
		vsnprintf(name, n + 1, fmt, ap);
		va_end(ap);
	}
	fp->fp_name = name;
	fp->fp_location = "";
	fp->fp_flags |= FAIL_POINT_DYNAMIC_NAME;
	fp->fp_pre_sleep_fn = NULL;
	fp->fp_pre_sleep_arg = NULL;
	fp->fp_post_sleep_fn = NULL;
	fp->fp_post_sleep_arg = NULL;
}

/**
 * Free the resources held by a fail_point, and wake any paused threads.
 * Thou shalt not allow threads to hit this fail point after you enter this
 * function, nor shall you call this multiple times for a given fp.
 * @ingroup failpoint
 */
void
fail_point_destroy(struct fail_point *fp)
{

	fail_point_drain(fp, 0);

	if ((fp->fp_flags & FAIL_POINT_DYNAMIC_NAME) != 0) {
		fp_free(__DECONST(void *, fp->fp_name));
		fp->fp_name = NULL;
	}
	fp->fp_flags = 0;

	sx_xlock(&sx_fp_set);
	fail_point_garbage_collect();
	sx_xunlock(&sx_fp_set);
}

/**
 * This does the real work of evaluating a fail point. If the fail point tells
 * us to return a value, this function returns 1 and fills in 'return_value'
 * (return_value is allowed to be null). If the fail point tells us to panic,
 * we never return. Otherwise we just return 0 after doing some work, which
 * means "keep going".
 */
enum fail_point_return_code
fail_point_eval_nontrivial(struct fail_point *fp, int *return_value)
{
	bool execute = false;
	struct fail_point_entry *ent;
	struct fail_point_setting *fp_setting;
	enum fail_point_return_code ret;
	int cont;
	int count;
	int msecs;
	int usecs;

	ret = FAIL_POINT_RC_CONTINUE;
	cont = 0; /* don't continue by default */

	fp_setting = fail_point_setting_get_ref(fp);
	if (fp_setting == NULL)
		goto abort;

	TAILQ_FOREACH(ent, &fp_setting->fp_entry_queue, fe_entries) {

		if (ent->fe_stale)
			continue;

		if (ent->fe_prob < PROB_MAX &&
		    ent->fe_prob < random() % PROB_MAX)
			continue;

		if (ent->fe_pid != NO_PID && ent->fe_pid != curproc->p_pid)
			continue;

		if (ent->fe_count != FE_COUNT_UNTRACKED) {
			count = ent->fe_count;
			while (count > 0) {
				if (atomic_cmpset_32(&ent->fe_count, count, count - 1)) {
					count--;
					execute = true;
					break;
				}
				count = ent->fe_count;
			}
			if (execute == false)
				/* We lost the race; consider the entry stale and bail now */
				continue;
			if (count == 0)
				ent->fe_stale = true;
		}

		switch (ent->fe_type) {
		case FAIL_POINT_PANIC:
			panic("fail point %s panicking", fp->fp_name);
			/* NOTREACHED */

		case FAIL_POINT_RETURN:
			if (return_value != NULL)
				*return_value = ent->fe_arg;
			ret = FAIL_POINT_RC_RETURN;
			break;

		case FAIL_POINT_BREAK:
			printf("fail point %s breaking to debugger\n",
			        fp->fp_name);
			breakpoint();
			break;

		case FAIL_POINT_PRINT:
			printf("fail point %s executing\n", fp->fp_name);
			cont = ent->fe_arg;
			break;

		case FAIL_POINT_SLEEP:
			msecs = ent->fe_arg;
			if (msecs)
				fail_point_sleep(fp, msecs, &ret);
			break;

		case FAIL_POINT_PAUSE:
			/**
			 * Pausing is inherently strange with multiple
			 * entries given our design.  That is because some
			 * entries could be unreachable, for instance in cases like:
			 * pause->return. We can never reach the return entry.
			 * The sysctl layer actually truncates all entries after
			 * a pause for this reason.
			 */
			mtx_lock_spin(&fp_setting->feq_mtx);
			fail_point_pause(fp, &ret, &fp_setting->feq_mtx);
			mtx_unlock_spin(&fp_setting->feq_mtx);
			break;

		case FAIL_POINT_YIELD:
			kern_yield(PRI_UNCHANGED);
			break;

		case FAIL_POINT_DELAY:
			usecs = ent->fe_arg;
			DELAY(usecs);
			break;

		default:
			break;
		}

		if (cont == 0)
			break;
	}

	if (fail_point_is_off(fp))
		fail_point_eval_swap_out(fp, fp_setting);

abort:
	fail_point_setting_release_ref(fp);

	return (ret);
}

/**
 * Translate internal fail_point structure into human-readable text.
 */
static void
fail_point_get(struct fail_point *fp, struct sbuf *sb,
        bool verbose)
{
	struct fail_point_entry *ent;
	struct fail_point_setting *fp_setting;
	struct fail_point_entry *fp_entry_cpy;
	int cnt_sleeping;
	int idx;
	int printed_entry_count;

	cnt_sleeping = 0;
	idx = 0;
	printed_entry_count = 0;

	fp_entry_cpy = fp_malloc(sizeof(struct fail_point_entry) *
	        (FP_MAX_ENTRY_COUNT + 1), M_WAITOK);

	fp_setting = fail_point_setting_get_ref(fp);

	if (fp_setting != NULL) {
		TAILQ_FOREACH(ent, &fp_setting->fp_entry_queue, fe_entries) {
			if (ent->fe_stale)
				continue;

			KASSERT(printed_entry_count < FP_MAX_ENTRY_COUNT,
			        ("FP entry list larger than allowed"));

			fp_entry_cpy[printed_entry_count] = *ent;
			++printed_entry_count;
		}
	}
	fail_point_setting_release_ref(fp);

	/* This is our equivalent of a NULL terminator */
	fp_entry_cpy[printed_entry_count].fe_type = FAIL_POINT_INVALID;

	while (idx < printed_entry_count) {
		ent = &fp_entry_cpy[idx];
		++idx;
		if (ent->fe_prob < PROB_MAX) {
			int decimal = ent->fe_prob % (PROB_MAX / 100);
			int units = ent->fe_prob / (PROB_MAX / 100);
			sbuf_printf(sb, "%d", units);
			if (decimal) {
				int digits = PROB_DIGITS - 2;
				while (!(decimal % 10)) {
					digits--;
					decimal /= 10;
				}
				sbuf_printf(sb, ".%0*d", digits, decimal);
			}
			sbuf_printf(sb, "%%");
		}
		if (ent->fe_count >= 0)
			sbuf_printf(sb, "%d*", ent->fe_count);
		sbuf_printf(sb, "%s", fail_type_strings[ent->fe_type].name);
		if (ent->fe_arg)
			sbuf_printf(sb, "(%d)", ent->fe_arg);
		if (ent->fe_pid != NO_PID)
			sbuf_printf(sb, "[pid %d]", ent->fe_pid);
		if (TAILQ_NEXT(ent, fe_entries))
			sbuf_printf(sb, "->");
	}
	if (!printed_entry_count)
		sbuf_printf(sb, "off");

	fp_free(fp_entry_cpy);
	if (verbose) {
#ifdef STACK
		/* Print number of sleeping threads. queue=0 is the argument
		 * used by msleep when sending our threads to sleep. */
		sbuf_printf(sb, "\nsleeping_thread_stacks = {\n");
		sleepq_sbuf_print_stacks(sb, FP_SLEEP_CHANNEL(fp), 0,
		        &cnt_sleeping);

		sbuf_printf(sb, "},\n");
#endif
		sbuf_printf(sb, "sleeping_thread_count = %d,\n",
		        cnt_sleeping);

#ifdef STACK
		sbuf_printf(sb, "paused_thread_stacks = {\n");
		sleepq_sbuf_print_stacks(sb, FP_PAUSE_CHANNEL(fp), 0,
		        &cnt_sleeping);

		sbuf_printf(sb, "},\n");
#endif
		sbuf_printf(sb, "paused_thread_count = %d\n",
		        cnt_sleeping);
	}
}

/**
 * Set an internal fail_point structure from a human-readable failpoint string
 * in a lock-safe manner.
 */
static int
fail_point_set(struct fail_point *fp, char *buf)
{
	struct fail_point_entry *ent, *ent_next;
	struct fail_point_setting *entries;
	bool should_wake_paused;
	bool should_truncate;
	int error;

	error = 0;
	should_wake_paused = false;
	should_truncate = false;

	/* Parse new entries. */
	/**
	 * ref protects our new malloc'd stuff from being garbage collected
	 * before we link it.
	 */
	fail_point_setting_get_ref(fp);
	entries = fail_point_setting_new(fp);
	if (parse_fail_point(entries, buf) == NULL) {
		STAILQ_REMOVE(&fp_setting_garbage, entries,
		        fail_point_setting, fs_garbage_link);
		fail_point_setting_destroy(entries);
		error = EINVAL;
		goto end;
	}

	/**
	 * Transfer the entries we are going to keep to a new list.
	 * Get rid of useless zero probability entries, and entries with hit
	 * count 0.
	 * If 'off' is present, and it has no hit count set, then all entries
	 *       after it are discarded since they are unreachable.
	 */
	TAILQ_FOREACH_SAFE(ent, &entries->fp_entry_queue, fe_entries, ent_next) {
		if (ent->fe_prob == 0 || ent->fe_count == 0) {
			printf("Discarding entry which cannot execute %s\n",
			        fail_type_strings[ent->fe_type].name);
			TAILQ_REMOVE(&entries->fp_entry_queue, ent,
			        fe_entries);
			fp_free(ent);
			continue;
		} else if (should_truncate) {
			printf("Discarding unreachable entry %s\n",
			        fail_type_strings[ent->fe_type].name);
			TAILQ_REMOVE(&entries->fp_entry_queue, ent,
			        fe_entries);
			fp_free(ent);
			continue;
		}

		if (ent->fe_type == FAIL_POINT_OFF) {
			should_wake_paused = true;
			if (ent->fe_count == FE_COUNT_UNTRACKED) {
				should_truncate = true;
				TAILQ_REMOVE(&entries->fp_entry_queue, ent,
				        fe_entries);
				fp_free(ent);
			}
		} else if (ent->fe_type == FAIL_POINT_PAUSE) {
			should_truncate = true;
		} else if (ent->fe_type == FAIL_POINT_SLEEP && (fp->fp_flags &
		        FAIL_POINT_NONSLEEPABLE)) {
			/**
			 * If this fail point is annotated as being in a
			 * non-sleepable ctx, convert sleep to delay and
			 * convert the msec argument to usecs.
			 */
			printf("Sleep call request on fail point in "
			        "non-sleepable context; using delay instead "
			        "of sleep\n");
			ent->fe_type = FAIL_POINT_DELAY;
			ent->fe_arg *= 1000;
		}
	}

	if (TAILQ_EMPTY(&entries->fp_entry_queue)) {
		entries = fail_point_swap_settings(fp, NULL);
		if (entries != NULL)
			wakeup(FP_PAUSE_CHANNEL(fp));
	} else {
		if (should_wake_paused)
			wakeup(FP_PAUSE_CHANNEL(fp));
		fail_point_swap_settings(fp, entries);
	}

end:
#ifdef IWARNING
	if (error)
		IWARNING("Failed to set %s %s to %s",
		    fp->fp_name, fp->fp_location, buf);
	else
		INOTICE("Set %s %s to %s",
		    fp->fp_name, fp->fp_location, buf);
#endif /* IWARNING */

	fail_point_setting_release_ref(fp);
	return (error);
}

#define MAX_FAIL_POINT_BUF	1023

/**
 * Handle kernel failpoint set/get.
 */
int
fail_point_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct fail_point *fp;
	char *buf;
	struct sbuf sb, *sb_check;
	int error;

	buf = NULL;
	error = 0;
	fp = arg1;

	sb_check = sbuf_new(&sb, NULL, 1024, SBUF_AUTOEXTEND);
	if (sb_check != &sb)
		return (ENOMEM);

	sbuf_set_drain(&sb, (sbuf_drain_func *)fail_sysctl_drain_func, req);

	/* Setting */
	/**
	 * Lock protects any new entries from being garbage collected before we
	 * can link them to the fail point.
	 */
	sx_xlock(&sx_fp_set);
	if (req->newptr) {
		if (req->newlen > MAX_FAIL_POINT_BUF) {
			error = EINVAL;
			goto out;
		}

		buf = fp_malloc(req->newlen + 1, M_WAITOK);

		error = SYSCTL_IN(req, buf, req->newlen);
		if (error)
			goto out;
		buf[req->newlen] = '\0';

		error = fail_point_set(fp, buf);
	}

	fail_point_garbage_collect();
	sx_xunlock(&sx_fp_set);

	/* Retrieving. */
	fail_point_get(fp, &sb, false);

out:
	sbuf_finish(&sb);
	sbuf_delete(&sb);

	if (buf)
		fp_free(buf);

	return (error);
}

int
fail_point_sysctl_status(SYSCTL_HANDLER_ARGS)
{
	struct fail_point *fp;
	struct sbuf sb, *sb_check;

	fp = arg1;

	sb_check = sbuf_new(&sb, NULL, 1024, SBUF_AUTOEXTEND);
	if (sb_check != &sb)
		return (ENOMEM);

	sbuf_set_drain(&sb, (sbuf_drain_func *)fail_sysctl_drain_func, req);

	/* Retrieving. */
	fail_point_get(fp, &sb, true);

	sbuf_finish(&sb);
	sbuf_delete(&sb);

	/**
	 * Lock protects any new entries from being garbage collected before we
	 * can link them to the fail point.
	 */
	sx_xlock(&sx_fp_set);
	fail_point_garbage_collect();
	sx_xunlock(&sx_fp_set);

	return (0);
}

int
fail_sysctl_drain_func(void *sysctl_args, const char *buf, int len)
{
	struct sysctl_req *sa;
	int error;

	sa = sysctl_args;

	error = SYSCTL_OUT(sa, buf, len);

	if (error == ENOMEM)
		return (-1);
	else
		return (len);
}

/**
 * Internal helper function to translate a human-readable failpoint string
 * into a internally-parsable fail_point structure.
 */
static char *
parse_fail_point(struct fail_point_setting *ents, char *p)
{
	/*  <fail_point> ::
	 *      <term> ( "->" <term> )*
	 */
	uint8_t term_count;

	term_count = 1;

	p = parse_term(ents, p);
	if (p == NULL)
		return (NULL);

	while (*p != '\0') {
		term_count++;
		if (p[0] != '-' || p[1] != '>' ||
		        (p = parse_term(ents, p+2)) == NULL ||
		        term_count > FP_MAX_ENTRY_COUNT)
			return (NULL);
	}
	return (p);
}

/**
 * Internal helper function to parse an individual term from a failpoint.
 */
static char *
parse_term(struct fail_point_setting *ents, char *p)
{
	struct fail_point_entry *ent;

	ent = fail_point_entry_new(ents);

	/*
	 * <term> ::
	 *     ( (<float> "%") | (<integer> "*" ) )*
	 *     <type>
	 *     [ "(" <integer> ")" ]
	 *     [ "[pid " <integer> "]" ]
	 */

	/* ( (<float> "%") | (<integer> "*" ) )* */
	while (isdigit(*p) || *p == '.') {
		int units, decimal;

		p = parse_number(&units, &decimal, p);
		if (p == NULL)
			return (NULL);

		if (*p == '%') {
			if (units > 100) /* prevent overflow early */
				units = 100;
			ent->fe_prob = units * (PROB_MAX / 100) + decimal;
			if (ent->fe_prob > PROB_MAX)
				ent->fe_prob = PROB_MAX;
		} else if (*p == '*') {
			if (!units || units < 0 || decimal)
				return (NULL);
			ent->fe_count = units;
		} else
			return (NULL);
		p++;
	}

	/* <type> */
	p = parse_type(ent, p);
	if (p == NULL)
		return (NULL);
	if (*p == '\0')
		return (p);

	/* [ "(" <integer> ")" ] */
	if (*p != '(')
		return (p);
	p++;
	if (!isdigit(*p) && *p != '-')
		return (NULL);
	ent->fe_arg = strtol(p, &p, 0);
	if (*p++ != ')')
		return (NULL);

	/* [ "[pid " <integer> "]" ] */
#define PID_STRING "[pid "
	if (strncmp(p, PID_STRING, sizeof(PID_STRING) - 1) != 0)
		return (p);
	p += sizeof(PID_STRING) - 1;
	if (!isdigit(*p))
		return (NULL);
	ent->fe_pid = strtol(p, &p, 0);
	if (*p++ != ']')
		return (NULL);

	return (p);
}

/**
 * Internal helper function to parse a numeric for a failpoint term.
 */
static char *
parse_number(int *out_units, int *out_decimal, char *p)
{
	char *old_p;

	/**
	 *  <number> ::
	 *      <integer> [ "." <integer> ] |
	 *      "." <integer>
	 */

	/* whole part */
	old_p = p;
	*out_units = strtol(p, &p, 10);
	if (p == old_p && *p != '.')
		return (NULL);

	/* fractional part */
	*out_decimal = 0;
	if (*p == '.') {
		int digits = 0;
		p++;
		while (isdigit(*p)) {
			int digit = *p - '0';
			if (digits < PROB_DIGITS - 2)
				*out_decimal = *out_decimal * 10 + digit;
			else if (digits == PROB_DIGITS - 2 && digit >= 5)
				(*out_decimal)++;
			digits++;
			p++;
		}
		if (!digits) /* need at least one digit after '.' */
			return (NULL);
		while (digits++ < PROB_DIGITS - 2) /* add implicit zeros */
			*out_decimal *= 10;
	}

	return (p); /* success */
}

/**
 * Internal helper function to parse an individual type for a failpoint term.
 */
static char *
parse_type(struct fail_point_entry *ent, char *beg)
{
	enum fail_point_t type;
	int len;

	for (type = FAIL_POINT_OFF; type < FAIL_POINT_NUMTYPES; type++) {
		len = fail_type_strings[type].nmlen;
		if (strncmp(fail_type_strings[type].name, beg, len) == 0) {
			ent->fe_type = type;
			return (beg + len);
		}
	}
	return (NULL);
}

/* The fail point sysctl tree. */
SYSCTL_NODE(_debug, OID_AUTO, fail_point, CTLFLAG_RW, 0, "fail points");

/* Debugging/testing stuff for fail point */
static int
sysctl_test_fail_point(SYSCTL_HANDLER_ARGS)
{

	KFAIL_POINT_RETURN(DEBUG_FP, test_fail_point);
	return (0);
}
SYSCTL_OID(_debug_fail_point, OID_AUTO, test_trigger_fail_point,
        CTLTYPE_STRING | CTLFLAG_RD, NULL, 0, sysctl_test_fail_point, "A",
        "Trigger test fail points");
