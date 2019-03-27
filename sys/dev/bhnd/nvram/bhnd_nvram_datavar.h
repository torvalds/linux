/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_NVRAM_BHND_NVRAM_DATAVAR_H_
#define _BHND_NVRAM_BHND_NVRAM_DATAVAR_H_

#include <sys/param.h>
#include <sys/linker_set.h>
#include <sys/refcount.h>

#include "bhnd_nvram_io.h"

#include "bhnd_nvram_data.h"

/** Registered NVRAM parser class instances. */
SET_DECLARE(bhnd_nvram_data_class_set, bhnd_nvram_data_class);

void			*bhnd_nvram_data_generic_find(
			     struct bhnd_nvram_data *nv, const char *name);
int			 bhnd_nvram_data_generic_rp_getvar(
			     struct bhnd_nvram_data *nv, void *cookiep,
			     void *outp, size_t *olen, bhnd_nvram_type otype);
int			 bhnd_nvram_data_generic_rp_copy_val(
			     struct bhnd_nvram_data *nv, void *cookiep,
			     bhnd_nvram_val **val);

/** @see bhnd_nvram_data_probe() */
typedef int		 (bhnd_nvram_data_op_probe)(struct bhnd_nvram_io *io);

/** @see bhnd_nvram_data_probe() */
typedef int		 (bhnd_nvram_data_op_getvar_direct)(
			      struct bhnd_nvram_io *io, const char *name,
			      void *outp, size_t *olen, bhnd_nvram_type otype);

/** @see bhnd_nvram_data_serialize() */
typedef int		 (bhnd_nvram_data_op_serialize)(
			      bhnd_nvram_data_class *cls,
			      bhnd_nvram_plist *props,
			      bhnd_nvram_plist *options, void *outp,
			      size_t *olen);

/** @see bhnd_nvram_data_new() */
typedef int		 (bhnd_nvram_data_op_new)(struct bhnd_nvram_data *nv,
			      struct bhnd_nvram_io *io);

/** Free all resources associated with @p nv. Called by
 *  bhnd_nvram_data_release() when the reference count reaches zero. */
typedef void		 (bhnd_nvram_data_op_free)(struct bhnd_nvram_data *nv);

/** @see bhnd_nvram_data_count() */
typedef size_t		 (bhnd_nvram_data_op_count)(struct bhnd_nvram_data *nv);

/** @see bhnd_nvram_data_options() */
typedef bhnd_nvram_plist*(bhnd_nvram_data_op_options)(
			      struct bhnd_nvram_data *nv);

/** @see bhnd_nvram_data_caps() */
typedef uint32_t	 (bhnd_nvram_data_op_caps)(struct bhnd_nvram_data *nv);

/** @see bhnd_nvram_data_next() */
typedef const char	*(bhnd_nvram_data_op_next)(struct bhnd_nvram_data *nv,
			      void **cookiep);

/** @see bhnd_nvram_data_find() */
typedef void		*(bhnd_nvram_data_op_find)(struct bhnd_nvram_data *nv,
			      const char *name);

/** @see bhnd_nvram_data_copy_val() */
typedef int		 (bhnd_nvram_data_op_copy_val)(
			      struct bhnd_nvram_data *nv, void *cookiep,
			      bhnd_nvram_val **value);

/** @see bhnd_nvram_data_getvar_order() */
typedef int		 (bhnd_nvram_data_op_getvar_order)(
			      struct bhnd_nvram_data *nv, void *cookiep1,
			      void *cookiep2);

/** @see bhnd_nvram_data_getvar_name() */
typedef const char	*(bhnd_nvram_data_op_getvar_name)(
			      struct bhnd_nvram_data *nv,
			      void *cookiep);

/** @see bhnd_nvram_data_getvar() */
typedef int		 (bhnd_nvram_data_op_getvar)(struct bhnd_nvram_data *nv,
			      void *cookiep, void *buf, size_t *len,
			      bhnd_nvram_type type);

/** @see bhnd_nvram_data_getvar_ptr() */
typedef const void	*(bhnd_nvram_data_op_getvar_ptr)(
			      struct bhnd_nvram_data *nv, void *cookiep,
			      size_t *len, bhnd_nvram_type *type);

/** @see bhnd_nvram_data_filter_setvar() */
typedef int		 (bhnd_nvram_data_op_filter_setvar)(
			      struct bhnd_nvram_data *nv, const char *name,
			      bhnd_nvram_val *value, bhnd_nvram_val **result);

/** @see bhnd_nvram_data_filter_unsetvar() */
typedef int		 (bhnd_nvram_data_op_filter_unsetvar)(
			      struct bhnd_nvram_data *nv, const char *name);

/**
 * NVRAM data class.
 */
struct bhnd_nvram_data_class {
	const char			*desc;		/**< description */
	uint32_t			 caps;		/**< capabilities (BHND_NVRAM_DATA_CAP_*) */
	size_t				 size;		/**< instance size */

	bhnd_nvram_data_op_probe		*op_probe;
	bhnd_nvram_data_op_getvar_direct	*op_getvar_direct;
	bhnd_nvram_data_op_serialize		*op_serialize;
	bhnd_nvram_data_op_new			*op_new;
	bhnd_nvram_data_op_free			*op_free;
	bhnd_nvram_data_op_count		*op_count;
	bhnd_nvram_data_op_options		*op_options;
	bhnd_nvram_data_op_caps			*op_caps;
	bhnd_nvram_data_op_next			*op_next;
	bhnd_nvram_data_op_find			*op_find;
	bhnd_nvram_data_op_copy_val		*op_copy_val;
	bhnd_nvram_data_op_getvar_order		*op_getvar_order;
	bhnd_nvram_data_op_getvar		*op_getvar;
	bhnd_nvram_data_op_getvar_ptr		*op_getvar_ptr;
	bhnd_nvram_data_op_getvar_name		*op_getvar_name;
	bhnd_nvram_data_op_filter_setvar	*op_filter_setvar;
	bhnd_nvram_data_op_filter_unsetvar	*op_filter_unsetvar;
};

/**
 * NVRAM data instance.
 */
struct bhnd_nvram_data {
	struct bhnd_nvram_data_class	*cls;
	volatile u_int			 refs;
};

/*
 * Helper macro for BHND_NVRAM_DATA_CLASS_DEFN().
 *
 * Declares a bhnd_nvram_data_class method implementation with class name
 * _cname and method name _mname
 */
#define	BHND_NVRAM_DATA_CLASS_DECL_METHOD(_cname, _mname)		\
	static bhnd_nvram_data_op_ ## _mname				\
	    bhnd_nvram_ ## _cname ## _ ## _mname;			\

/*
 * Helper macro for BHND_NVRAM_DATA_CLASS_DEFN().
 *
 * Assign a bhnd_nvram_data_class method implementation with class name
 * @p _cname and method name @p _mname
 */
#define	BHND_NVRAM_DATA_CLASS_ASSIGN_METHOD(_cname, _mname)		\
	.op_ ## _mname = bhnd_nvram_ ## _cname ## _ ## _mname,

/*
 * Helper macro for BHND_NVRAM_DATA_CLASS_DEFN().
 *
 * Iterate over all bhnd_nvram_data_class method names, calling
 * _macro with the class name _cname as the first argument, and
 * a bhnd_nvram_data_class method name as the second.
 */
#define	BHND_NVRAM_DATA_CLASS_ITER_METHODS(_cname, _macro)	\
	_macro(_cname, probe)					\
	_macro(_cname, getvar_direct)				\
	_macro(_cname, serialize)				\
	_macro(_cname, new)					\
	_macro(_cname, free)					\
	_macro(_cname, count)					\
	_macro(_cname, options)					\
	_macro(_cname, caps)					\
	_macro(_cname, next)					\
	_macro(_cname, find)					\
	_macro(_cname, copy_val)				\
	_macro(_cname, getvar_order)				\
	_macro(_cname, getvar)					\
	_macro(_cname, getvar_ptr)				\
	_macro(_cname, getvar_name)				\
	_macro(_cname, filter_setvar)				\
	_macro(_cname, filter_unsetvar)

/**
 * Define a bhnd_nvram_data_class with class name @p _n and description
 * @p _desc, and register with bhnd_nvram_data_class_set.
 */
#define	BHND_NVRAM_DATA_CLASS_DEFN(_cname, _desc, _caps, _size)		\
	BHND_NVRAM_DATA_CLASS_ITER_METHODS(_cname,			\
	    BHND_NVRAM_DATA_CLASS_DECL_METHOD)				\
									\
	struct bhnd_nvram_data_class bhnd_nvram_## _cname ## _class = {	\
		.desc		= (_desc),				\
		.caps		= (_caps),				\
		.size		= (_size),				\
		BHND_NVRAM_DATA_CLASS_ITER_METHODS(_cname,		\
		    BHND_NVRAM_DATA_CLASS_ASSIGN_METHOD)		\
	};								\
									\
	DATA_SET(bhnd_nvram_data_class_set,				\
	    bhnd_nvram_## _cname ## _class);

#endif /* _BHND_NVRAM_BHND_NVRAM_DATAVAR_H_ */
