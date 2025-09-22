/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ARRAY_H
#define ARRAY_H

#include "inlinedefs.h" // XXX
#include "utils.h"

#define ARRAYS_CHECKED

#ifdef ARRAYS_CHECKED
#include <assert.h>
#define arrayassert assert
#else
#define arrayassert(x) ((void)(x))
#endif

#ifndef ARRAYINLINE
#define ARRAYINLINE C99INLINE
#endif

////////////////////////////////////////////////////////////
// type and base operations

struct array {
	void **v;
	unsigned num, max;
};

struct array *array_create(void);
void array_destroy(struct array *);
void array_init(struct array *);
void array_cleanup(struct array *);
ARRAYINLINE unsigned array_num(const struct array *);
ARRAYINLINE void *array_get(const struct array *, unsigned index_);
ARRAYINLINE void array_set(const struct array *, unsigned index_, void *val);
void array_setsize(struct array *, unsigned num);
ARRAYINLINE void array_add(struct array *, void *val, unsigned *index_ret);
void array_insert(struct array *a, unsigned index_);
void array_remove(struct array *a, unsigned index_);

////////////////////////////////////////////////////////////
// inlining for base operations

ARRAYINLINE unsigned
array_num(const struct array *a)
{
	return a->num;
}

ARRAYINLINE void *
array_get(const struct array *a, unsigned index_)
{
	arrayassert(index_ < a->num);
	return a->v[index_];
}

ARRAYINLINE void
array_set(const struct array *a, unsigned index_, void *val)
{
	arrayassert(index_ < a->num);
	a->v[index_] = val;
}

ARRAYINLINE void
array_add(struct array *a, void *val, unsigned *index_ret)
{
	unsigned index_ = a->num;
	array_setsize(a, index_+1);
	a->v[index_] = val;
	if (index_ret != NULL) {
		*index_ret = index_;
	}
}

////////////////////////////////////////////////////////////
// bits for declaring and defining typed arrays

/*
 * Usage:
 *
 * DECLARRAY_BYTYPE(foo, bar, INLINE) declares "struct foo", which is
 * an array of pointers to "bar", plus the operations on it.
 *
 * DECLARRAY(foo, INLINE) is equivalent to
 * DECLARRAY_BYTYPE(fooarray, struct foo, INLINE).
 *
 * DEFARRAY_BYTYPE and DEFARRAY are the same as DECLARRAY except that
 * they define the operations.
 *
 * The argument INLINE can be used as follows:
 *
 * 1. For no inlining:
 *    In foo.h:
 *           DECLARRAY(foo, );
 *    In foo.c:
 *           DEFARRAY(foo, );
 *
 * 2. To be file-static:
 *    In foo.c:
 *           DECLARRAY(foo, static);
 *           DEFARRAY(foo, static);
 *
 * 3. To inline using C99:
 *    In foo.h:
 *           DECLARRAY(foo, inline);
 *           DEFARRAY(foo, inline);
 *
 * 4. To inline with old gcc:
 *    In foo.h:
 *           #ifndef FOO_INLINE
 *           #define FOO_INLINE extern inline
 *           #endif
 *           DECLARRAY(foo, );
 *           DEFARRAY(foo, FOO_INLINE);
 *    In foo.c:
 *           #define FOO_INLINE
 *           #include "foo.h"
 *
 * 5. To inline such that it works both with old gcc and C99:
 *    In foo.h:
 *           #ifndef FOO_INLINE
 *           #define FOO_INLINE extern inline
 *           #endif
 *           DECLARRAY(foo, FOO_INLINE);
 *           DEFARRAY(foo, FOO_INLINE);
 *    In foo.c:
 *           #define FOO_INLINE
 *           #include "foo.h"
 *
 * The mechanism in case (4) ensures that an externally linkable
 * definition exists.
 */

#define DECLARRAY_BYTYPE(ARRAY, T, INLINE) \
	struct ARRAY {							\
		struct array arr;					\
	};								\
									\
	INLINE struct ARRAY *ARRAY##_create(void);			\
	INLINE void ARRAY##_destroy(struct ARRAY *a);			\
	INLINE void ARRAY##_init(struct ARRAY *a);			\
	INLINE void ARRAY##_cleanup(struct ARRAY *a);			\
	INLINE unsigned ARRAY##_num(const struct ARRAY *a);		\
	INLINE T *ARRAY##_get(const struct ARRAY *a, unsigned index_);	\
	INLINE void ARRAY##_set(struct ARRAY *a, unsigned index_, T *val); \
	INLINE void ARRAY##_setsize(struct ARRAY *a, unsigned num);	\
	INLINE void ARRAY##_add(struct ARRAY *a, T *val, unsigned *index_ret);\
	INLINE void ARRAY##_insert(struct ARRAY *a, unsigned index_);	\
	INLINE void ARRAY##_remove(struct ARRAY *a, unsigned index_)


#define DEFARRAY_BYTYPE(ARRAY, T, INLINE) \
	INLINE void						\
	ARRAY##_init(struct ARRAY *a)				\
	{							\
		array_init(&a->arr);				\
	}							\
								\
	INLINE void						\
	ARRAY##_cleanup(struct ARRAY *a)			\
	{							\
		array_cleanup(&a->arr);				\
	}							\
								\
	INLINE struct						\
	ARRAY *ARRAY##_create(void)				\
	{							\
		struct ARRAY *a;				\
								\
		a = domalloc(sizeof(*a));			\
		ARRAY##_init(a);				\
		return a;					\
	}							\
								\
	INLINE void						\
	ARRAY##_destroy(struct ARRAY *a)			\
	{							\
		ARRAY##_cleanup(a);				\
		dofree(a, sizeof(*a));				\
	}							\
								\
	INLINE unsigned						\
	ARRAY##_num(const struct ARRAY *a)			\
	{							\
		return array_num(&a->arr);			\
	}							\
								\
	INLINE T *						\
	ARRAY##_get(const struct ARRAY *a, unsigned index_)	\
	{				 			\
		return (T *)array_get(&a->arr, index_);		\
	}							\
								\
	INLINE void						\
	ARRAY##_set(struct ARRAY *a, unsigned index_, T *val)	\
	{				 			\
		array_set(&a->arr, index_, (void *)val);	\
	}							\
								\
	INLINE void						\
	ARRAY##_setsize(struct ARRAY *a, unsigned num)		\
	{				 			\
		array_setsize(&a->arr, num);			\
	}							\
								\
	INLINE void						\
	ARRAY##_add(struct ARRAY *a, T *val, unsigned *ret)	\
	{				 			\
		array_add(&a->arr, (void *)val, ret);		\
	}							\
								\
	INLINE void						\
	ARRAY##_insert(struct ARRAY *a, unsigned index_)	\
	{				 			\
		array_insert(&a->arr, index_);			\
	}							\
								\
	INLINE void						\
	ARRAY##_remove(struct ARRAY *a, unsigned index_)	\
	{				 			\
		array_remove(&a->arr, index_);			\
	}

#define DECLARRAY(T, INLINE) DECLARRAY_BYTYPE(T##array, struct T, INLINE)
#define DEFARRAY(T, INLINE) DEFARRAY_BYTYPE(T##array, struct T, INLINE)

#define DESTROYALL_ARRAY(T, INLINE) \
	INLINE void T##array_destroyall(struct T##array *arr);	\
							\
	INLINE void					\
	T##array_destroyall(struct T##array *arr)	\
	{						\
		unsigned i, num;			\
		struct T *t;				\
							\
		num = T##array_num(arr);		\
		for (i=0; i<num; i++) {			\
			t = T##array_get(arr, i);	\
			T##_destroy(t);			\
		}					\
		T##array_setsize(arr, 0);		\
	}


////////////////////////////////////////////////////////////
// basic array types

DECLARRAY_BYTYPE(stringarray, char, ARRAYINLINE);
DEFARRAY_BYTYPE(stringarray, char, ARRAYINLINE);

#endif /* ARRAY_H */
