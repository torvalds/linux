/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 ARM Limited */

#ifndef __TEST_SIGNALS_H__
#define __TEST_SIGNALS_H__

#include <signal.h>
#include <stdbool.h>
#include <ucontext.h>

/*
 * Using ARCH specific and sanitized Kernel headers from the tree.
 */
#include <asm/ptrace.h>
#include <asm/hwcap.h>

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define get_regval(regname, out)			\
{							\
	asm volatile("mrs %0, " __stringify(regname)	\
	: "=r" (out)					\
	:						\
	: "memory");					\
}

/*
 * Feature flags used in tdescr.feats_required to specify
 * any feature by the test
 */
enum {
	FSSBS_BIT,
	FSVE_BIT,
	FSME_BIT,
	FSME_FA64_BIT,
	FMAX_END
};

#define FEAT_SSBS		(1UL << FSSBS_BIT)
#define FEAT_SVE		(1UL << FSVE_BIT)
#define FEAT_SME		(1UL << FSME_BIT)
#define FEAT_SME_FA64		(1UL << FSME_FA64_BIT)

/*
 * A descriptor used to describe and configure a test case.
 * Fields with a non-trivial meaning are described inline in the following.
 */
struct tdescr {
	/* KEEP THIS FIELD FIRST for easier lookup from assembly */
	void			*token;
	/* when disabled token based sanity checking is skipped in handler */
	bool			sanity_disabled;
	/* just a name for the test-case; manadatory field */
	char			*name;
	char			*descr;
	unsigned long		feats_required;
	unsigned long		feats_incompatible;
	/* bitmask of effectively supported feats: populated at run-time */
	unsigned long		feats_supported;
	bool			initialized;
	unsigned int		minsigstksz;
	/* signum used as a test trigger. Zero if no trigger-signal is used */
	int			sig_trig;
	/*
	 * signum considered as a successful test completion.
	 * Zero when no signal is expected on success
	 */
	int			sig_ok;
	/* signum expected on unsupported CPU features. */
	int			sig_unsupp;
	/* a timeout in second for test completion */
	unsigned int		timeout;
	bool			triggered;
	bool			pass;
	unsigned int		result;
	/* optional sa_flags for the installed handler */
	int			sa_flags;
	ucontext_t		saved_uc;
	/* used by get_current_ctx() */
	size_t			live_sz;
	ucontext_t		*live_uc;
	volatile sig_atomic_t	live_uc_valid;
	/* optional test private data */
	void			*priv;

	/* a custom setup: called alternatively to default_setup */
	int (*setup)(struct tdescr *td);
	/* a custom init: called by default test init after test_setup */
	bool (*init)(struct tdescr *td);
	/* a custom cleanup function called before test exits */
	void (*cleanup)(struct tdescr *td);
	/* an optional function to be used as a trigger for starting test */
	int (*trigger)(struct tdescr *td);
	/*
	 * the actual test-core: invoked differently depending on the
	 * presence of the trigger function above; this is mandatory
	 */
	int (*run)(struct tdescr *td, siginfo_t *si, ucontext_t *uc);
	/* an optional function for custom results' processing */
	void (*check_result)(struct tdescr *td);
};

extern struct tdescr tde;
#endif
