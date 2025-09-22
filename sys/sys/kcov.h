/*	$OpenBSD: kcov.h,v 1.9 2023/07/29 06:52:08 anton Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_KCOV_H_
#define _SYS_KCOV_H_

#include <sys/ioccom.h>

#define KIOSETBUFSIZE	_IOW('K', 1, unsigned long)
#define KIOENABLE	_IOW('K', 2, int)
#define KIODISABLE	_IO('K', 3)
#define KIOREMOTEATTACH	_IOW('K', 4, struct kio_remote_attach *)

#define KCOV_MODE_NONE		0
#define KCOV_MODE_TRACE_PC	1
#define KCOV_MODE_TRACE_CMP	2

#define KCOV_REMOTE_COMMON	0

struct kio_remote_attach {
	int subsystem;
	int id;
};

#ifdef _KERNEL

struct proc;

extern int kcov_cold;

void kcov_exit(struct proc *);
int kcov_vnode(struct vnode *);
void kcov_remote_register(int, void *);
void kcov_remote_unregister(int, void *);
void kcov_remote_enter(int, void *);
void kcov_remote_leave(int, void *);

#endif /* _KERNEL */

#endif /* !_SYS_KCOV_H_ */
