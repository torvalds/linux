/*	$OpenBSD: proc.h,v 1.1.1.1 2018/04/27 16:14:37 eric Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

struct imsgproc;

struct imsgproc *proc_bypid(pid_t);
struct imsgproc *proc_exec(int, char **);
struct imsgproc *proc_attach(int, int);
void	         proc_enable(struct imsgproc *);
void	         proc_free(struct imsgproc *);
pid_t		 proc_getpid(struct imsgproc *);
int		 proc_gettype(struct imsgproc *);
int		 proc_getinstance(struct imsgproc *);
const char 	*proc_gettitle(struct imsgproc *);
void		 proc_setpid(struct imsgproc *, pid_t);
void		 proc_settitle(struct imsgproc *, const char *);
void		 proc_setinstance(struct imsgproc *, int);
void		 proc_setcallback(struct imsgproc *,
    void(*)(struct imsgproc *, struct imsg *, void *), void *);

void m_compose(struct imsgproc *, uint32_t, uint32_t, pid_t, int, const void *,
    size_t);
void m_create(struct imsgproc *, uint32_t, uint32_t, pid_t, int);
void m_close(struct imsgproc *);
void m_add(struct imsgproc *, const void *, size_t);
void m_add_int(struct imsgproc *, int);
void m_add_u32(struct imsgproc *, uint32_t);
void m_add_u64(struct imsgproc *, uint64_t);
void m_add_size(struct imsgproc *, size_t);
void m_add_time(struct imsgproc *, time_t);
void m_add_string(struct imsgproc *, const char *);
void m_add_sockaddr(struct imsgproc *, const struct sockaddr *);
void m_end(struct imsgproc *);
int  m_is_eom(struct imsgproc *);
void m_get(struct imsgproc *, void *, size_t);
void m_get_int(struct imsgproc *, int *);
void m_get_u32(struct imsgproc *, uint32_t *);
void m_get_u64(struct imsgproc *, uint64_t *);
void m_get_size(struct imsgproc *, size_t *);
void m_get_time(struct imsgproc *, time_t *);
void m_get_string(struct imsgproc *, const char **);
void m_get_sockaddr(struct imsgproc *, struct sockaddr *);
