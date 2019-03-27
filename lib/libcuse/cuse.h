/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky. All rights reserved.
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

#ifndef _CUSE_H_
#define	_CUSE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <fs/cuse/cuse_defs.h>

struct cuse_dev;

typedef int (cuse_open_t)(struct cuse_dev *, int fflags);
typedef int (cuse_close_t)(struct cuse_dev *, int fflags);
typedef int (cuse_read_t)(struct cuse_dev *, int fflags, void *user_ptr, int len);
typedef int (cuse_write_t)(struct cuse_dev *, int fflags, const void *user_ptr, int len);
typedef int (cuse_ioctl_t)(struct cuse_dev *, int fflags, unsigned long cmd, void *user_data);
typedef int (cuse_poll_t)(struct cuse_dev *, int fflags, int events);

struct cuse_methods {
	cuse_open_t *cm_open;
	cuse_close_t *cm_close;
	cuse_read_t *cm_read;
	cuse_write_t *cm_write;
	cuse_ioctl_t *cm_ioctl;
	cuse_poll_t *cm_poll;
};

int	cuse_init(void);
int	cuse_uninit(void);

void   *cuse_vmalloc(int);
int	cuse_is_vmalloc_addr(void *);
void	cuse_vmfree(void *);
unsigned long cuse_vmoffset(void *ptr);

int	cuse_alloc_unit_number_by_id(int *, int);
int	cuse_free_unit_number_by_id(int, int);
int	cuse_alloc_unit_number(int *);
int	cuse_free_unit_number(int);

struct cuse_dev *cuse_dev_create(const struct cuse_methods *, void *, void *, uid_t, gid_t, int, const char *,...);
void	cuse_dev_destroy(struct cuse_dev *);

void   *cuse_dev_get_priv0(struct cuse_dev *);
void   *cuse_dev_get_priv1(struct cuse_dev *);

void	cuse_dev_set_priv0(struct cuse_dev *, void *);
void	cuse_dev_set_priv1(struct cuse_dev *, void *);

void	cuse_set_local(int);
int	cuse_get_local(void);

int	cuse_wait_and_process(void);

void	cuse_dev_set_per_file_handle(struct cuse_dev *, void *);
void   *cuse_dev_get_per_file_handle(struct cuse_dev *);

int	cuse_copy_out(const void *src, void *user_dst, int len);
int	cuse_copy_in(const void *user_src, void *dst, int len);
int	cuse_got_peer_signal(void);
void	cuse_poll_wakeup(void);

struct cuse_dev *cuse_dev_get_current(int *);

extern int cuse_debug_level;

#ifdef __cplusplus
}
#endif

#endif			/* _CUSE_H_ */
