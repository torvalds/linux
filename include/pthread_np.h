/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996-98 John Birrell <jb@cimlogic.com.au>.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
#ifndef _PTHREAD_NP_H_
#define _PTHREAD_NP_H_

#include <sys/param.h>
#include <sys/cpuset.h>

/*
 * Non-POSIX type definitions:
 */
typedef void	(*pthread_switch_routine_t)(pthread_t, pthread_t);

/*
 * Non-POSIX thread function prototype definitions:
 */
__BEGIN_DECLS
int pthread_attr_setcreatesuspend_np(pthread_attr_t *);
int pthread_attr_get_np(pthread_t, pthread_attr_t *);
int pthread_attr_getaffinity_np(const pthread_attr_t *, size_t, cpuset_t *);
int pthread_attr_setaffinity_np(pthread_attr_t *, size_t, const cpuset_t *);
void pthread_get_name_np(pthread_t, char *, size_t);
int pthread_getaffinity_np(pthread_t, size_t, cpuset_t *);
int pthread_getthreadid_np(void);
int pthread_main_np(void);
int pthread_multi_np(void);
int pthread_mutexattr_getkind_np(pthread_mutexattr_t);
int pthread_mutexattr_setkind_np(pthread_mutexattr_t *, int);
int pthread_mutex_getspinloops_np(pthread_mutex_t *mutex, int *count);
int pthread_mutex_setspinloops_np(pthread_mutex_t *mutex, int count);
int pthread_mutex_getyieldloops_np(pthread_mutex_t *mutex, int *count);
int pthread_mutex_setyieldloops_np(pthread_mutex_t *mutex, int count);
int pthread_mutex_isowned_np(pthread_mutex_t *mutex);
void pthread_resume_all_np(void);
int pthread_resume_np(pthread_t);
void pthread_set_name_np(pthread_t, const char *);
int pthread_setaffinity_np(pthread_t, size_t, const cpuset_t *);
int pthread_single_np(void);
void pthread_suspend_all_np(void);
int pthread_suspend_np(pthread_t);
int pthread_switch_add_np(pthread_switch_routine_t);
int pthread_switch_delete_np(pthread_switch_routine_t);
int pthread_timedjoin_np(pthread_t, void **, const struct timespec *);
__END_DECLS

#endif
