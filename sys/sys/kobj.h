/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2003 Doug Rabson
 * All rights reserved.
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
 *	$FreeBSD$
 */

#ifndef _SYS_KOBJ_H_
#define _SYS_KOBJ_H_

/*
 * Forward declarations
 */
typedef struct kobj		*kobj_t;
typedef struct kobj_class	*kobj_class_t;
typedef const struct kobj_method kobj_method_t;
typedef int			(*kobjop_t)(void);
typedef struct kobj_ops		*kobj_ops_t;
typedef struct kobjop_desc	*kobjop_desc_t;
struct malloc_type;

struct kobj_method {
	kobjop_desc_t	desc;
	kobjop_t	func;
};

/*
 * A class is simply a method table and a sizeof value. When the first
 * instance of the class is created, the method table will be compiled 
 * into a form more suited to efficient method dispatch. This compiled 
 * method table is always the first field of the object.
 */
#define KOBJ_CLASS_FIELDS						\
	const char	*name;		/* class name */		\
	kobj_method_t	*methods;	/* method table */		\
	size_t		size;		/* object size */		\
	kobj_class_t	*baseclasses;	/* base classes */		\
	u_int		refs;		/* reference count */		\
	kobj_ops_t	ops		/* compiled method table */

struct kobj_class {
	KOBJ_CLASS_FIELDS;
};

/*
 * Implementation of kobj.
 */
#define KOBJ_FIELDS				\
	kobj_ops_t	ops

struct kobj {
	KOBJ_FIELDS;
};

/*
 * The ops table is used as a cache of results from kobj_lookup_method().
 */

#define KOBJ_CACHE_SIZE	256

struct kobj_ops {
	kobj_method_t	*cache[KOBJ_CACHE_SIZE];
	kobj_class_t	cls;
};

struct kobjop_desc {
	unsigned int	id;		/* unique ID */
	kobj_method_t	deflt;		/* default implementation */
};

/*
 * Shorthand for constructing method tables.
 * The ternary operator is (ab)used to provoke a warning when FUNC
 * has a signature that is not compatible with kobj method signature.
 */
#define KOBJMETHOD(NAME, FUNC) \
	{ &NAME##_desc, (kobjop_t) (1 ? FUNC : (NAME##_t *)NULL) }

/*
 *
 */
#define KOBJMETHOD_END	{ NULL, NULL }

/*
 * Declare a class (which should be defined in another file.
 */
#define DECLARE_CLASS(name) extern struct kobj_class name

/*
 * Define a class with no base classes (api backward-compatible. with
 * FreeBSD-5.1 and earlier).
 */
#define DEFINE_CLASS(name, methods, size)     		\
DEFINE_CLASS_0(name, name ## _class, methods, size)

/*
 * Define a class with no base classes. Use like this:
 *
 * DEFINE_CLASS_0(foo, foo_class, foo_methods, sizeof(foo_softc));
 */
#define DEFINE_CLASS_0(name, classvar, methods, size)	\
							\
struct kobj_class classvar = {				\
	#name, methods, size, NULL			\
}

/*
 * Define a class inheriting a single base class. Use like this:
 *
 * DEFINE_CLASS_1(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar);
 */
#define DEFINE_CLASS_1(name, classvar, methods, size,	\
		       base1)				\
							\
static kobj_class_t name ## _baseclasses[] =		\
	{ &base1, NULL };				\
struct kobj_class classvar = {				\
	#name, methods, size, name ## _baseclasses	\
}

/*
 * Define a class inheriting two base classes. Use like this:
 *
 * DEFINE_CLASS_2(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar, baz);
 */
#define DEFINE_CLASS_2(name, classvar, methods, size,	\
	               base1, base2)			\
							\
static kobj_class_t name ## _baseclasses[] =		\
	{ &base1,					\
	  &base2, NULL };				\
struct kobj_class classvar = {				\
	#name, methods, size, name ## _baseclasses	\
}

/*
 * Define a class inheriting three base classes. Use like this:
 *
 * DEFINE_CLASS_3(foo, foo_class, foo_methods, sizeof(foo_softc),
 *			  bar, baz, foobar);
 */
#define DEFINE_CLASS_3(name, classvar, methods, size,	\
		       base1, base2, base3)		\
							\
static kobj_class_t name ## _baseclasses[] =		\
	{ &base1,					\
	  &base2,					\
	  &base3, NULL };				\
struct kobj_class classvar = {				\
	#name, methods, size, name ## _baseclasses	\
}


/*
 * Compile the method table in a class.
 */
void		kobj_class_compile(kobj_class_t cls);

/*
 * Compile the method table, with the caller providing the space for
 * the ops table.(for use before malloc is initialised).
 */
void		kobj_class_compile_static(kobj_class_t cls, kobj_ops_t ops);

/*
 * Free the compiled method table in a class.
 */
void		kobj_class_free(kobj_class_t cls);

/*
 * Allocate memory for and initialise a new object.
 */
kobj_t		kobj_create(kobj_class_t cls,
			    struct malloc_type *mtype,
			    int mflags);

/*
 * Initialise a pre-allocated object.
 */
void		kobj_init(kobj_t obj, kobj_class_t cls);
void		kobj_init_static(kobj_t obj, kobj_class_t cls);

/*
 * Delete an object. If mtype is non-zero, free the memory.
 */
void		kobj_delete(kobj_t obj, struct malloc_type *mtype);

/*
 * Maintain stats on hits/misses in lookup caches.
 */
#ifdef KOBJ_STATS
extern u_int kobj_lookup_hits;
extern u_int kobj_lookup_misses;
#endif

/*
 * Lookup the method in the cache and if it isn't there look it up the
 * slow way.
 */
#ifdef KOBJ_STATS
#define KOBJOPLOOKUP(OPS,OP) do {				\
	kobjop_desc_t _desc = &OP##_##desc;			\
	kobj_method_t **_cep =					\
	    &OPS->cache[_desc->id & (KOBJ_CACHE_SIZE-1)];	\
	kobj_method_t *_ce = *_cep;				\
	if (_ce->desc != _desc) {				\
		_ce = kobj_lookup_method(OPS->cls,		\
					 _cep, _desc);		\
		kobj_lookup_misses++;				\
	} else							\
		kobj_lookup_hits++;				\
	_m = _ce->func;						\
} while(0)
#else
#define KOBJOPLOOKUP(OPS,OP) do {				\
	kobjop_desc_t _desc = &OP##_##desc;			\
	kobj_method_t **_cep =					\
	    &OPS->cache[_desc->id & (KOBJ_CACHE_SIZE-1)];	\
	kobj_method_t *_ce = *_cep;				\
	if (_ce->desc != _desc)					\
		_ce = kobj_lookup_method(OPS->cls,		\
					 _cep, _desc);		\
	_m = _ce->func;						\
} while(0)
#endif

kobj_method_t* kobj_lookup_method(kobj_class_t cls,
				  kobj_method_t **cep,
				  kobjop_desc_t desc);


/*
 * Default method implementation. Returns ENXIO.
 */
int kobj_error_method(void);

#endif /* !_SYS_KOBJ_H_ */
