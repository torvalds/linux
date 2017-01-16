/*
 * AppArmor security module
 *
 * This file contains AppArmor basic path manipulation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_PATH_H
#define __AA_PATH_H


enum path_flags {
	PATH_IS_DIR = 0x1,		/* path is a directory */
	PATH_CONNECT_PATH = 0x4,	/* connect disconnected paths to / */
	PATH_CHROOT_REL = 0x8,		/* do path lookup relative to chroot */
	PATH_CHROOT_NSCONNECT = 0x10,	/* connect paths that are at ns root */

	PATH_DELEGATE_DELETED = 0x08000, /* delegate deleted files */
	PATH_MEDIATE_DELETED = 0x10000,	/* mediate deleted paths */
};

int aa_path_name(const struct path *path, int flags, char **buffer,
		 const char **name, const char **info);

#define MAX_PATH_BUFFERS 2

/* Per cpu buffers used during mediation */
/* preallocated buffers to use during path lookups */
struct aa_buffers {
	char *buf[MAX_PATH_BUFFERS];
};

#include <linux/percpu.h>
#include <linux/preempt.h>

DECLARE_PER_CPU(struct aa_buffers, aa_buffers);

#define COUNT_ARGS(X...) COUNT_ARGS_HELPER(, ##X, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define COUNT_ARGS_HELPER(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, n, X...) n
#define CONCAT(X, Y) X ## Y
#define CONCAT_AFTER(X, Y) CONCAT(X, Y)

#define ASSIGN(FN, X, N) ((X) = FN(N))
#define EVAL1(FN, X) ASSIGN(FN, X, 0) /*X = FN(0)*/
#define EVAL2(FN, X, Y...) do { ASSIGN(FN, X, 1);  EVAL1(FN, Y); } while (0)
#define EVAL(FN, X...) CONCAT_AFTER(EVAL, COUNT_ARGS(X))(FN, X)

#define for_each_cpu_buffer(I) for ((I) = 0; (I) < MAX_PATH_BUFFERS; (I)++)

#ifdef CONFIG_DEBUG_PREEMPT
#define AA_BUG_PREEMPT_ENABLED(X) AA_BUG(preempt_count() <= 0, X)
#else
#define AA_BUG_PREEMPT_ENABLED(X) /* nop */
#endif

#define __get_buffer(N) ({					\
	struct aa_buffers *__cpu_var; \
	AA_BUG_PREEMPT_ENABLED("__get_buffer without preempt disabled");  \
	__cpu_var = this_cpu_ptr(&aa_buffers);			\
	__cpu_var->buf[(N)]; })

#define __get_buffers(X...)    EVAL(__get_buffer, X)

#define __put_buffers(X, Y...) ((void)&(X))

#define get_buffers(X...)	\
do {				\
	preempt_disable();	\
	__get_buffers(X);	\
} while (0)

#define put_buffers(X, Y...)	\
do {				\
	__put_buffers(X, Y);	\
	preempt_enable();	\
} while (0)

#endif /* __AA_PATH_H */
