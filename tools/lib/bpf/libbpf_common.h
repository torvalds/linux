/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Common user-facing libbpf helpers.
 *
 * Copyright (c) 2019 Facebook
 */

#ifndef __LIBBPF_LIBBPF_COMMON_H
#define __LIBBPF_LIBBPF_COMMON_H

#include <string.h>
#include "libbpf_version.h"

#ifndef LIBBPF_API
#define LIBBPF_API __attribute__((visibility("default")))
#endif

#define LIBBPF_DEPRECATED(msg) __attribute__((deprecated(msg)))

/* Mark a symbol as deprecated when libbpf version is >= {major}.{minor} */
#define LIBBPF_DEPRECATED_SINCE(major, minor, msg)			    \
	__LIBBPF_MARK_DEPRECATED_ ## major ## _ ## minor		    \
		(LIBBPF_DEPRECATED("libbpf v" # major "." # minor "+: " msg))

#define __LIBBPF_CURRENT_VERSION_GEQ(major, minor)			    \
	(LIBBPF_MAJOR_VERSION > (major) ||				    \
	 (LIBBPF_MAJOR_VERSION == (major) && LIBBPF_MINOR_VERSION >= (minor)))

/* Add checks for other versions below when planning deprecation of API symbols
 * with the LIBBPF_DEPRECATED_SINCE macro.
 */
#if __LIBBPF_CURRENT_VERSION_GEQ(0, 6)
#define __LIBBPF_MARK_DEPRECATED_0_6(X) X
#else
#define __LIBBPF_MARK_DEPRECATED_0_6(X)
#endif

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
