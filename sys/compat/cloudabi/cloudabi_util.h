/*-
 * Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
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
 * $FreeBSD$
 */

#ifndef _CLOUDABI_UTIL_H_
#define	_CLOUDABI_UTIL_H_

#include <sys/socket.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

struct file;
struct sysentvec;
struct thread;
struct timespec;

/* Fetches the time value of a clock. */
int cloudabi_clock_time_get(struct thread *, cloudabi_clockid_t,
    cloudabi_timestamp_t *);

/* Converts a FreeBSD errno to a CloudABI errno. */
cloudabi_errno_t cloudabi_convert_errno(int);

/* Converts a file descriptor to a CloudABI file descriptor type. */
cloudabi_filetype_t cloudabi_convert_filetype(const struct file *);

/* Converts CloudABI rights to a set of Capsicum capabilities. */
int cloudabi_convert_rights(cloudabi_rights_t, cap_rights_t *);

/* Removes rights that conflict with the file descriptor type. */
void cloudabi_remove_conflicting_rights(cloudabi_filetype_t,
    cloudabi_rights_t *, cloudabi_rights_t *);

/* Converts a struct timespec to a CloudABI timestamp. */
int cloudabi_convert_timespec(const struct timespec *, cloudabi_timestamp_t *);

/*
 * Blocking futex functions.
 *
 * These functions are called by CloudABI's polling system calls to
 * sleep on a lock or condition variable.
 */
int cloudabi_futex_condvar_wait(struct thread *, cloudabi_condvar_t *,
    cloudabi_scope_t, cloudabi_lock_t *, cloudabi_scope_t, cloudabi_clockid_t,
    cloudabi_timestamp_t, cloudabi_timestamp_t, bool);
int cloudabi_futex_lock_rdlock(struct thread *, cloudabi_lock_t *,
    cloudabi_scope_t, cloudabi_clockid_t, cloudabi_timestamp_t,
    cloudabi_timestamp_t, bool);
int cloudabi_futex_lock_wrlock(struct thread *, cloudabi_lock_t *,
    cloudabi_scope_t, cloudabi_clockid_t, cloudabi_timestamp_t,
    cloudabi_timestamp_t, bool);

/* Socket operations. */
int cloudabi_sock_recv(struct thread *, cloudabi_fd_t, struct iovec *, size_t,
    cloudabi_fd_t *, size_t, cloudabi_riflags_t, size_t *, size_t *,
    cloudabi_roflags_t *);
int cloudabi_sock_send(struct thread *, cloudabi_fd_t, struct iovec *, size_t,
    const cloudabi_fd_t *, size_t, size_t *);

/* vDSO setup and teardown. */
void cloudabi_vdso_init(struct sysentvec *, char *, char *);
void cloudabi_vdso_destroy(struct sysentvec *);

#endif
