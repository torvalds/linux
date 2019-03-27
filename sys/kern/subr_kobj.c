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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#ifndef TEST
#include <sys/systm.h>
#endif

#ifdef TEST
#include "usertest.h"
#endif

static MALLOC_DEFINE(M_KOBJ, "kobj", "Kernel object structures");

#ifdef KOBJ_STATS

u_int kobj_lookup_hits;
u_int kobj_lookup_misses;

SYSCTL_UINT(_kern, OID_AUTO, kobj_hits, CTLFLAG_RD,
	   &kobj_lookup_hits, 0, "");
SYSCTL_UINT(_kern, OID_AUTO, kobj_misses, CTLFLAG_RD,
	   &kobj_lookup_misses, 0, "");

#endif

static struct mtx kobj_mtx;
static int kobj_mutex_inited;
static int kobj_next_id = 1;

#define	KOBJ_LOCK()		mtx_lock(&kobj_mtx)
#define	KOBJ_UNLOCK()		mtx_unlock(&kobj_mtx)
#define	KOBJ_ASSERT(what)	mtx_assert(&kobj_mtx, what);

SYSCTL_INT(_kern, OID_AUTO, kobj_methodcount, CTLFLAG_RD,
	   &kobj_next_id, 0, "");

static void
kobj_init_mutex(void *arg)
{
	if (!kobj_mutex_inited) {
		mtx_init(&kobj_mtx, "kobj", NULL, MTX_DEF);
		kobj_mutex_inited = 1;
	}
}

SYSINIT(kobj, SI_SUB_LOCK, SI_ORDER_ANY, kobj_init_mutex, NULL);

/*
 * This method structure is used to initialise new caches. Since the
 * desc pointer is NULL, it is guaranteed never to match any read
 * descriptors.
 */
static const struct kobj_method null_method = {
	0, 0,
};

int
kobj_error_method(void)
{

	return ENXIO;
}

static void
kobj_class_compile_common(kobj_class_t cls, kobj_ops_t ops)
{
	kobj_method_t *m;
	int i;

	/*
	 * Don't do anything if we are already compiled.
	 */
	if (cls->ops)
		return;

	/*
	 * First register any methods which need it.
	 */
	for (i = 0, m = cls->methods; m->desc; i++, m++) {
		if (m->desc->id == 0)
			m->desc->id = kobj_next_id++;
	}

	/*
	 * Then initialise the ops table.
	 */
	for (i = 0; i < KOBJ_CACHE_SIZE; i++)
		ops->cache[i] = &null_method;
	ops->cls = cls;
	cls->ops = ops;
}

static int
kobj_class_compile1(kobj_class_t cls, int mflags)
{
	kobj_ops_t ops;

	KOBJ_ASSERT(MA_NOTOWNED);

	ops = malloc(sizeof(struct kobj_ops), M_KOBJ, mflags);
	if (ops == NULL)
		return (ENOMEM);

	/*
	 * We may have lost a race for kobj_class_compile here - check
	 * to make sure someone else hasn't already compiled this
	 * class.
	 */
	KOBJ_LOCK();
	if (cls->ops) {
		KOBJ_UNLOCK();
		free(ops, M_KOBJ);
		return (0);
	}
	kobj_class_compile_common(cls, ops);
	KOBJ_UNLOCK();
	return (0);
}

void
kobj_class_compile(kobj_class_t cls)
{
	int error;

	error = kobj_class_compile1(cls, M_WAITOK);
	KASSERT(error == 0, ("kobj_class_compile1 returned %d", error));
}

void
kobj_class_compile_static(kobj_class_t cls, kobj_ops_t ops)
{

	KASSERT(kobj_mutex_inited == 0,
	    ("%s: only supported during early cycles", __func__));

	/*
	 * Increment refs to make sure that the ops table is not freed.
	 */
	cls->refs++;
	kobj_class_compile_common(cls, ops);
}

static kobj_method_t*
kobj_lookup_method_class(kobj_class_t cls, kobjop_desc_t desc)
{
	kobj_method_t *methods = cls->methods;
	kobj_method_t *ce;

	for (ce = methods; ce && ce->desc; ce++) {
		if (ce->desc == desc) {
			return ce;
		}
	}

	return NULL;
}

static kobj_method_t*
kobj_lookup_method_mi(kobj_class_t cls,
		      kobjop_desc_t desc)
{
	kobj_method_t *ce;
	kobj_class_t *basep;

	ce = kobj_lookup_method_class(cls, desc);
	if (ce)
		return ce;

	basep = cls->baseclasses;
	if (basep) {
		for (; *basep; basep++) {
			ce = kobj_lookup_method_mi(*basep, desc);
			if (ce)
				return ce;
		}
	}

	return NULL;
}

kobj_method_t*
kobj_lookup_method(kobj_class_t cls,
		   kobj_method_t **cep,
		   kobjop_desc_t desc)
{
	kobj_method_t *ce;

	ce = kobj_lookup_method_mi(cls, desc);
	if (!ce)
		ce = &desc->deflt;
	if (cep)
		*cep = ce;
	return ce;
}

void
kobj_class_free(kobj_class_t cls)
{
	void* ops = NULL;

	KOBJ_ASSERT(MA_NOTOWNED);
	KOBJ_LOCK();

	/*
	 * Protect against a race between kobj_create and
	 * kobj_delete.
	 */
	if (cls->refs == 0) {
		/*
		 * For now we don't do anything to unregister any methods
		 * which are no longer used.
		 */

		/*
		 * Free memory and clean up.
		 */
		ops = cls->ops;
		cls->ops = NULL;
	}
	
	KOBJ_UNLOCK();

	if (ops)
		free(ops, M_KOBJ);
}

static void
kobj_init_common(kobj_t obj, kobj_class_t cls)
{

	obj->ops = cls->ops;
	cls->refs++;
}

static int
kobj_init1(kobj_t obj, kobj_class_t cls, int mflags)
{
	int error;

	KOBJ_LOCK();
	while (cls->ops == NULL) {
		/*
		 * kobj_class_compile doesn't want the lock held
		 * because of the call to malloc - we drop the lock
		 * and re-try.
		 */
		KOBJ_UNLOCK();
		error = kobj_class_compile1(cls, mflags);
		if (error != 0)
			return (error);
		KOBJ_LOCK();
	}
	kobj_init_common(obj, cls);
	KOBJ_UNLOCK();
	return (0);
}

kobj_t
kobj_create(kobj_class_t cls, struct malloc_type *mtype, int mflags)
{
	kobj_t obj;

	obj = malloc(cls->size, mtype, mflags | M_ZERO);
	if (obj == NULL)
		return (NULL);
	if (kobj_init1(obj, cls, mflags) != 0) {
		free(obj, mtype);
		return (NULL);
	}
	return (obj);
}

void
kobj_init(kobj_t obj, kobj_class_t cls)
{
	int error;

	error = kobj_init1(obj, cls, M_NOWAIT);
	if (error != 0)
		panic("kobj_init1 failed: error %d", error);
}

void
kobj_init_static(kobj_t obj, kobj_class_t cls)
{

	KASSERT(kobj_mutex_inited == 0,
	    ("%s: only supported during early cycles", __func__));

	kobj_init_common(obj, cls);
}

void
kobj_delete(kobj_t obj, struct malloc_type *mtype)
{
	kobj_class_t cls = obj->ops->cls;
	int refs;

	/*
	 * Consider freeing the compiled method table for the class
	 * after its last instance is deleted. As an optimisation, we
	 * should defer this for a short while to avoid thrashing.
	 */
	KOBJ_ASSERT(MA_NOTOWNED);
	KOBJ_LOCK();
	cls->refs--;
	refs = cls->refs;
	KOBJ_UNLOCK();

	if (!refs)
		kobj_class_free(cls);

	obj->ops = NULL;
	if (mtype)
		free(obj, mtype);
}
