/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2010, 2012 Konstantin Belousov <kib@FreeBSD.ORG>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <elf.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <string.h>
#include <sys/auxv.h>
#include "un-namespace.h"
#include "libc_private.h"

extern char **environ;
extern int _DYNAMIC;
#pragma weak _DYNAMIC

void *__elf_aux_vector;
static pthread_once_t aux_vector_once = PTHREAD_ONCE_INIT;

static void
init_aux_vector_once(void)
{
	Elf_Addr *sp;

	sp = (Elf_Addr *)environ;
	while (*sp++ != 0)
		;
	__elf_aux_vector = (Elf_Auxinfo *)sp;
}

void
__init_elf_aux_vector(void)
{

	if (&_DYNAMIC != NULL)
		return;
	_once(&aux_vector_once, init_aux_vector_once);
}

static pthread_once_t aux_once = PTHREAD_ONCE_INIT;
static int pagesize, osreldate, canary_len, ncpus, pagesizes_len;
static int hwcap_present, hwcap2_present;
static char *canary, *pagesizes;
static void *timekeep;
static u_long hwcap, hwcap2;

static void
init_aux(void)
{
	Elf_Auxinfo *aux;

	for (aux = __elf_aux_vector; aux->a_type != AT_NULL; aux++) {
		switch (aux->a_type) {
		case AT_CANARY:
			canary = (char *)(aux->a_un.a_ptr);
			break;

		case AT_CANARYLEN:
			canary_len = aux->a_un.a_val;
			break;

		case AT_HWCAP:
			hwcap_present = 1;
			hwcap = (u_long)(aux->a_un.a_val);
			break;

		case AT_HWCAP2:
			hwcap2_present = 1;
			hwcap2 = (u_long)(aux->a_un.a_val);
			break;

		case AT_PAGESIZES:
			pagesizes = (char *)(aux->a_un.a_ptr);
			break;

		case AT_PAGESIZESLEN:
			pagesizes_len = aux->a_un.a_val;
			break;

		case AT_PAGESZ:
			pagesize = aux->a_un.a_val;
			break;

		case AT_OSRELDATE:
			osreldate = aux->a_un.a_val;
			break;

		case AT_NCPUS:
			ncpus = aux->a_un.a_val;
			break;

		case AT_TIMEKEEP:
			timekeep = aux->a_un.a_ptr;
			break;
		}
	}
}

__weak_reference(_elf_aux_info, elf_aux_info);

int
_elf_aux_info(int aux, void *buf, int buflen)
{
	int res;

	__init_elf_aux_vector();
	if (__elf_aux_vector == NULL)
		return (ENOSYS);
	_once(&aux_once, init_aux);

	switch (aux) {
	case AT_CANARY:
		if (canary != NULL && canary_len >= buflen) {
			memcpy(buf, canary, buflen);
			memset(canary, 0, canary_len);
			canary = NULL;
			res = 0;
		} else
			res = ENOENT;
		break;
	case AT_HWCAP:
		if (hwcap_present && buflen == sizeof(u_long)) {
			*(u_long *)buf = hwcap;
			res = 0;
		} else
			res = ENOENT;
		break;
	case AT_HWCAP2:
		if (hwcap2_present && buflen == sizeof(u_long)) {
			*(u_long *)buf = hwcap2;
			res = 0;
		} else
			res = ENOENT;
		break;
	case AT_PAGESIZES:
		if (pagesizes != NULL && pagesizes_len >= buflen) {
			memcpy(buf, pagesizes, buflen);
			res = 0;
		} else
			res = ENOENT;
		break;
	case AT_PAGESZ:
		if (buflen == sizeof(int)) {
			if (pagesize != 0) {
				*(int *)buf = pagesize;
				res = 0;
			} else
				res = ENOENT;
		} else
			res = EINVAL;
		break;
	case AT_OSRELDATE:
		if (buflen == sizeof(int)) {
			if (osreldate != 0) {
				*(int *)buf = osreldate;
				res = 0;
			} else
				res = ENOENT;
		} else
			res = EINVAL;
		break;
	case AT_NCPUS:
		if (buflen == sizeof(int)) {
			if (ncpus != 0) {
				*(int *)buf = ncpus;
				res = 0;
			} else
				res = ENOENT;
		} else
			res = EINVAL;
		break;
	case AT_TIMEKEEP:
		if (buflen == sizeof(void *)) {
			if (timekeep != NULL) {
				*(void **)buf = timekeep;
				res = 0;
			} else
				res = ENOENT;
		} else
			res = EINVAL;
		break;
	default:
		res = ENOENT;
		break;
	}
	return (res);
}
