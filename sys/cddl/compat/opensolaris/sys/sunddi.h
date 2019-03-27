/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_SUNDDI_H_
#define	_OPENSOLARIS_SYS_SUNDDI_H_

#ifdef _KERNEL

#include <sys/kmem.h>
#include <sys/libkern.h>
#include <sys/sysevent.h>

#define	ddi_driver_major(zfs_dip)		(0)
#define	ddi_copyin(from, to, size, flag)				\
	(copyin((from), (to), (size)), 0)
#define	ddi_copyout(from, to, size, flag)				\
	(copyout((from), (to), (size)), 0)
int ddi_strtol(const char *str, char **nptr, int base, long *result);
int ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result);
int ddi_strtoll(const char *str, char **nptr, int base, long long *result);
int ddi_strtoull(const char *str, char **nptr, int base,
    unsigned long long *result);

#define	DDI_SUCCESS	(0)
#define	DDI_FAILURE	(-1)
#define	DDI_SLEEP	0x666

int ddi_soft_state_init(void **statep, size_t size, size_t nitems);
void ddi_soft_state_fini(void **statep);

void *ddi_get_soft_state(void *state, int item);
int ddi_soft_state_zalloc(void *state, int item);
void ddi_soft_state_free(void *state, int item);

int _ddi_log_sysevent(char *vendor, char *class_name, char *subclass_name,
    nvlist_t *attr_list, sysevent_id_t *eidp, int flag);
#define	ddi_log_sysevent(dip, vendor, class_name, subclass_name,	\
	    attr_list, eidp, flag)					\
	_ddi_log_sysevent((vendor), (class_name), (subclass_name),	\
	    (attr_list), (eidp), (flag))

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_SUNDDI_H_ */
