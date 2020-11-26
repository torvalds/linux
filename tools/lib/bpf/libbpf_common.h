/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Common user-facing libbpf helpers.
 *
 * Copyright (c) 2019 Facebook
 */

#ifndef __LIBBPF_LIBBPF_COMMON_H
#define __LIBBPF_LIBBPF_COMMON_H

#include <string.h>

#ifndef LIBBPF_API
#define LIBBPF_API __attribute__((visibility("default")))
#endif

#define LIBBPF_DEPRECATED(msg) __attribute__((deprecated(msg)))

/* Helper macro to declare and initialize libbpf options struct
 *
 * This dance with uninitialized declaration, followed by memset to zero,
 * followed by assignment using compound literal syntax is done to preserve
 * ability to use a nice struct field initialization syntax and **hopefully**
 * have all the padding bytes initialized to zero. It's not guaranteed though,
 * when copying literal, that compiler won't copy garbage in literal's padding
 * bytes, but that's the best way I've found and it seems to work in practice.
 *
 * Macro declares opts struct of given type and name, zero-initializes,
 * including any extra padding, it with memset() and then assigns initial
 * values provided by users in struct initializer-syntax as varargs.
 */
#define DECLARE_LIBBPF_OPTS(TYPE, NAME, ...)				    \
	struct TYPE NAME = ({ 						    \
		memset(&NAME, 0, sizeof(struct TYPE));			    \
		(struct TYPE) {						    \
			.sz = sizeof(struct TYPE),			    \
			__VA_ARGS__					    \
		};							    \
	})

#endif /* __LIBBPF_LIBBPF_COMMON_H */
