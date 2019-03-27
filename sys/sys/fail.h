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
 *
 * $FreeBSD$
 */
/**
 * @file
 *
 * Main header for failpoint facility.
 */
#ifndef _SYS_FAIL_H_
#define _SYS_FAIL_H_

#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/linker_set.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>

/**
 * Failpoint return codes, used internally.
 * @ingroup failpoint_private
 */
enum fail_point_return_code {
	FAIL_POINT_RC_CONTINUE = 0,	/**< Continue with normal execution */
	FAIL_POINT_RC_RETURN,		/**< FP evaluated to 'return' */
	FAIL_POINT_RC_QUEUED,		/**< sleep_fn will be called */
};

struct fail_point_entry;
struct fail_point_setting;

/**
 * Internal failpoint structure, tracking all the current details of the
 * failpoint.  This structure is the core component shared between the
 * failure-injection code and the user-interface.
 * @ingroup failpoint_private
 */
struct fail_point {
	const char *fp_name;		/* name of fail point */
	const char *fp_location;	/* file:line of fail point */
	volatile int fp_ref_cnt;	/**
	                             * protects fp_setting: while holding
	                             * a ref, fp_setting points to an
	                             * unfreed fail_point_setting
	                             */
	struct fail_point_setting * volatile fp_setting;
	int fp_flags;

	/**< Function to call before sleep or pause */
	void (*fp_pre_sleep_fn)(void *);
	/**< Arg for fp_pre_sleep_fn */
	void *fp_pre_sleep_arg;

	/**< Function to call after waking from sleep or pause */
	void (*fp_post_sleep_fn)(void *);
	/**< Arg for fp_post_sleep_fn */
	void *fp_post_sleep_arg;
};

#define	FAIL_POINT_DYNAMIC_NAME	0x01	/**< Must free name on destroy */
/**< Use timeout path for sleep instead of msleep */
#define FAIL_POINT_USE_TIMEOUT_PATH 0x02
/**< If fail point is set to sleep, replace the sleep call with delay */
#define FAIL_POINT_NONSLEEPABLE 0x04

#define FAIL_POINT_CV_DESC "fp cv no iterators"
#define FAIL_POINT_IS_OFF(fp) (__predict_true((fp)->fp_setting == NULL) || \
        __predict_true(fail_point_is_off(fp)))

__BEGIN_DECLS

/* Private failpoint eval function -- use fail_point_eval() instead. */
enum fail_point_return_code fail_point_eval_nontrivial(struct fail_point *,
        int *ret);

/**
 * @addtogroup failpoint
 * @{
 */
/*
 * Initialize a fail-point.  The name is formed in printf-like fashion
 * from "fmt" and the subsequent arguments.
 * Pair with fail_point_destroy().
 */
void fail_point_init(struct fail_point *, const char *fmt, ...)
    __printflike(2, 3);

/* Return true iff this fail point is set to off, false otherwise */
bool fail_point_is_off(struct fail_point *fp);

/**
 * Set the pre-sleep function for a fail point
 * If fp_post_sleep_fn is specified, then FAIL_POINT_SLEEP will result in a
 * (*fp->fp_pre_sleep_fn)(fp->fp_pre_sleep_arg) call by the thread.
 */
static inline void
fail_point_sleep_set_pre_func(struct fail_point *fp, void (*sleep_fn)(void *))
{
	fp->fp_pre_sleep_fn = sleep_fn;
}

static inline void
fail_point_sleep_set_pre_arg(struct fail_point *fp, void *sleep_arg)
{
	fp->fp_pre_sleep_arg = sleep_arg;
}

/**
 * Set the post-sleep function.  This will be passed to timeout if we take
 * the timeout path. This must be set if you sleep using the timeout path.
 */
static inline void
fail_point_sleep_set_post_func(struct fail_point *fp, void (*sleep_fn)(void *))
{
	fp->fp_post_sleep_fn = sleep_fn;
}

static inline void
fail_point_sleep_set_post_arg(struct fail_point *fp, void *sleep_arg)
{
	fp->fp_post_sleep_arg = sleep_arg;
}
/**
 * If the FAIL_POINT_USE_TIMEOUT flag is set on a failpoint, then
 * FAIL_POINT_SLEEP will result in a call to timeout instead of
 * msleep. Note that if you sleep while this flag is set, you must
 * set fp_post_sleep_fn or an error will occur upon waking.
 */
static inline void
fail_point_use_timeout_path(struct fail_point *fp, bool use_timeout,
        void (*post_sleep_fn)(void *))
{
	KASSERT(!use_timeout || post_sleep_fn != NULL ||
	        (post_sleep_fn == NULL && fp->fp_post_sleep_fn != NULL),
	        ("Setting fp to use timeout, but not setting post_sleep_fn\n"));

	if (use_timeout)
		fp->fp_flags |= FAIL_POINT_USE_TIMEOUT_PATH;
	else
		fp->fp_flags &= ~FAIL_POINT_USE_TIMEOUT_PATH;

	if (post_sleep_fn != NULL)
		fp->fp_post_sleep_fn = post_sleep_fn;
}

/**
 * Free the resources used by a fail-point.  Pair with fail_point_init().
 */
void fail_point_destroy(struct fail_point *);

/**
 * Evaluate a failpoint.
 */
static inline enum fail_point_return_code
fail_point_eval(struct fail_point *fp, int *ret)
{
	if (__predict_true(fp->fp_setting == NULL))
		return (FAIL_POINT_RC_CONTINUE);
	return (fail_point_eval_nontrivial(fp, ret));
}

__END_DECLS

/* Declare a fail_point and its sysctl in a function. */
#define _FAIL_POINT_NAME(name) _fail_point_##name
#define _FAIL_POINT_LOCATION() "(" __FILE__ ":" __XSTRING(__LINE__) ")"
#define _FAIL_POINT_INIT(parent, name, flags) \
	static struct fail_point _FAIL_POINT_NAME(name) = { \
	        .fp_name = #name, \
	        .fp_location = _FAIL_POINT_LOCATION(), \
	        .fp_ref_cnt = 0, \
	        .fp_setting = NULL, \
	        .fp_flags = (flags), \
	        .fp_pre_sleep_fn = NULL, \
	        .fp_pre_sleep_arg = NULL, \
	        .fp_post_sleep_fn = NULL, \
	        .fp_post_sleep_arg = NULL, \
	}; \
	SYSCTL_OID(parent, OID_AUTO, name, \
	        CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, \
	        &_FAIL_POINT_NAME(name), 0, fail_point_sysctl, \
	        "A", ""); \
	SYSCTL_OID(parent, OID_AUTO, status_##name, \
	        CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, \
	        &_FAIL_POINT_NAME(name), 0, \
	        fail_point_sysctl_status, "A", "");
#define _FAIL_POINT_EVAL(name, cond, code...) \
	int RETURN_VALUE; \
 \
	if (__predict_false(cond && \
	        fail_point_eval(&_FAIL_POINT_NAME(name), &RETURN_VALUE))) { \
 \
		code; \
 \
	}


/**
 * Instantiate a failpoint which returns "RETURN_VALUE" from the function
 * when triggered.
 * @param parent  The parent sysctl under which to locate the fp's sysctl
 * @param name    The name of the failpoint in the sysctl tree (and printouts)
 * @return        Instantly returns the RETURN_VALUE specified in the
 *                failpoint, if triggered.
 */
#define KFAIL_POINT_RETURN(parent, name) \
	KFAIL_POINT_CODE(parent, name, return RETURN_VALUE)

/**
 * Instantiate a failpoint which returns (void) from the function when
 * triggered.
 * @param parent  The parent sysctl under which to locate the sysctl
 * @param name    The name of the failpoint in the sysctl tree (and printouts)
 * @return        Instantly returns void, if triggered in the failpoint.
 */
#define KFAIL_POINT_RETURN_VOID(parent, name) \
	KFAIL_POINT_CODE(parent, name, return)

/**
 * Instantiate a failpoint which sets an error when triggered.
 * @param parent     The parent sysctl under which to locate the sysctl
 * @param name       The name of the failpoint in the sysctl tree (and
 *                   printouts)
 * @param error_var  A variable to set to the failpoint's specified
 *                   return-value when triggered
 */
#define KFAIL_POINT_ERROR(parent, name, error_var) \
	KFAIL_POINT_CODE(parent, name, (error_var) = RETURN_VALUE)

/**
 * Instantiate a failpoint which sets an error and then goes to a
 * specified label in the function when triggered.
 * @param parent     The parent sysctl under which to locate the sysctl
 * @param name       The name of the failpoint in the sysctl tree (and
 *                   printouts)
 * @param error_var  A variable to set to the failpoint's specified
 *                   return-value when triggered
 * @param label      The location to goto when triggered.
 */
#define KFAIL_POINT_GOTO(parent, name, error_var, label) \
	KFAIL_POINT_CODE(parent, name, (error_var) = RETURN_VALUE; goto label)

/**
 * Instantiate a failpoint which sets its pre- and post-sleep callback
 * mechanisms.
 * @param parent     The parent sysctl under which to locate the sysctl
 * @param name       The name of the failpoint in the sysctl tree (and
 *                   printouts)
 * @param pre_func   Function pointer to the pre-sleep function, which will be
 *                   called directly before going to sleep.
 * @param pre_arg    Argument to the pre-sleep function
 * @param post_func  Function pointer to the pot-sleep function, which will be
 *                   called directly before going to sleep.
 * @param post_arg   Argument to the post-sleep function
 */
#define KFAIL_POINT_SLEEP_CALLBACKS(parent, name, pre_func, pre_arg, \
        post_func, post_arg) \
	KFAIL_POINT_CODE_SLEEP_CALLBACKS(parent, name, pre_func, \
	    pre_arg, post_func, post_arg, return RETURN_VALUE)

/**
 * Instantiate a failpoint which runs arbitrary code when triggered, and sets
 * its pre- and post-sleep callback mechanisms
 * @param parent     The parent sysctl under which to locate the sysctl
 * @param name       The name of the failpoint in the sysctl tree (and
 *                   printouts)
 * @param pre_func   Function pointer to the pre-sleep function, which will be
 *                   called directly before going to sleep.
 * @param pre_arg    Argument to the pre-sleep function
 * @param post_func  Function pointer to the pot-sleep function, which will be
 *                   called directly before going to sleep.
 * @param post_arg   Argument to the post-sleep function
 * @param code       The arbitrary code to run when triggered.  Can reference
 *                   "RETURN_VALUE" if desired to extract the specified
 *                   user return-value when triggered.  Note that this is
 *                   implemented with a do-while loop so be careful of
 *                   break and continue statements.
 */
#define KFAIL_POINT_CODE_SLEEP_CALLBACKS(parent, name, pre_func, pre_arg, \
        post_func, post_arg, code...) \
	do { \
		_FAIL_POINT_INIT(parent, name) \
		_FAIL_POINT_NAME(name).fp_pre_sleep_fn = pre_func; \
		_FAIL_POINT_NAME(name).fp_pre_sleep_arg = pre_arg; \
		_FAIL_POINT_NAME(name).fp_post_sleep_fn = post_func; \
		_FAIL_POINT_NAME(name).fp_post_sleep_arg = post_arg; \
		_FAIL_POINT_EVAL(name, true, code) \
	} while (0)


/**
 * Instantiate a failpoint which runs arbitrary code when triggered.
 * @param parent     The parent sysctl under which to locate the sysctl
 * @param name       The name of the failpoint in the sysctl tree
 *                   (and printouts)
 * @param code       The arbitrary code to run when triggered.  Can reference
 *                   "RETURN_VALUE" if desired to extract the specified
 *                   user return-value when triggered.  Note that this is
 *                   implemented with a do-while loop so be careful of
 *                   break and continue statements.
 */
#define KFAIL_POINT_CODE(parent, name, code...) \
	do { \
		_FAIL_POINT_INIT(parent, name, 0) \
		_FAIL_POINT_EVAL(name, true, code) \
	} while (0)

#define KFAIL_POINT_CODE_FLAGS(parent, name, flags, code...) \
	do { \
		_FAIL_POINT_INIT(parent, name, flags) \
		_FAIL_POINT_EVAL(name, true, code) \
	} while (0)

#define KFAIL_POINT_CODE_COND(parent, name, cond, flags, code...) \
	do { \
		_FAIL_POINT_INIT(parent, name, flags) \
		_FAIL_POINT_EVAL(name, cond, code) \
	} while (0)

/**
 * @}
 * (end group failpoint)
 */

#ifdef _KERNEL
int fail_point_sysctl(SYSCTL_HANDLER_ARGS);
int fail_point_sysctl_status(SYSCTL_HANDLER_ARGS);

/* The fail point sysctl tree. */
SYSCTL_DECL(_debug_fail_point);
#define	DEBUG_FP	_debug_fail_point
#endif

#endif /* _SYS_FAIL_H_ */
