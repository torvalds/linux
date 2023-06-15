/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __LIBPERF_INTERNAL_RC_CHECK_H
#define __LIBPERF_INTERNAL_RC_CHECK_H

#include <stdlib.h>
#include <linux/zalloc.h>

/*
 * Enable reference count checking implicitly with leak checking, which is
 * integrated into address sanitizer.
 */
#if defined(LEAK_SANITIZER) || defined(ADDRESS_SANITIZER)
#define REFCNT_CHECKING 1
#endif

/*
 * Shared reference count checking macros.
 *
 * Reference count checking is an approach to sanitizing the use of reference
 * counted structs. It leverages address and leak sanitizers to make sure gets
 * are paired with a put. Reference count checking adds a malloc-ed layer of
 * indirection on a get, and frees it on a put. A missed put will be reported as
 * a memory leak. A double put will be reported as a double free. Accessing
 * after a put will cause a use-after-free and/or a segfault.
 */

#ifndef REFCNT_CHECKING
/* Replaces "struct foo" so that the pointer may be interposed. */
#define DECLARE_RC_STRUCT(struct_name)		\
	struct struct_name

/* Declare a reference counted struct variable. */
#define RC_STRUCT(struct_name) struct struct_name

/*
 * Interpose the indirection. Result will hold the indirection and object is the
 * reference counted struct.
 */
#define ADD_RC_CHK(result, object) (result = object, object)

/* Strip the indirection layer. */
#define RC_CHK_ACCESS(object) object

/* Frees the object and the indirection layer. */
#define RC_CHK_FREE(object) free(object)

/* A get operation adding the indirection layer. */
#define RC_CHK_GET(result, object) ADD_RC_CHK(result, object)

/* A put operation removing the indirection layer. */
#define RC_CHK_PUT(object) {}

#else

/* Replaces "struct foo" so that the pointer may be interposed. */
#define DECLARE_RC_STRUCT(struct_name)			\
	struct original_##struct_name;			\
	struct struct_name {				\
		struct original_##struct_name *orig;	\
	};						\
	struct original_##struct_name

/* Declare a reference counted struct variable. */
#define RC_STRUCT(struct_name) struct original_##struct_name

/*
 * Interpose the indirection. Result will hold the indirection and object is the
 * reference counted struct.
 */
#define ADD_RC_CHK(result, object)					\
	(								\
		object ? (result = malloc(sizeof(*result)),		\
			result ? (result->orig = object, result)	\
			: (result = NULL, NULL))			\
		: (result = NULL, NULL)					\
		)

/* Strip the indirection layer. */
#define RC_CHK_ACCESS(object) object->orig

/* Frees the object and the indirection layer. */
#define RC_CHK_FREE(object)			\
	do {					\
		zfree(&object->orig);		\
		free(object);			\
	} while(0)

/* A get operation adding the indirection layer. */
#define RC_CHK_GET(result, object) ADD_RC_CHK(result, (object ? object->orig : NULL))

/* A put operation removing the indirection layer. */
#define RC_CHK_PUT(object)			\
	do {					\
		if (object) {			\
			object->orig = NULL;	\
			free(object);		\
		}				\
	} while(0)

#endif

#endif /* __LIBPERF_INTERNAL_RC_CHECK_H */
