/*	$OpenBSD: common.h,v 1.1 2020/09/16 14:02:23 mpi Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
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

#define SIGIO_REGRESS_USER	"nobody"

/*
 * Wait until signal signum arrives, or fail if a timeout occurs first.
 */
#define expect_signal(signum) \
	expect_signal_impl(signum, #signum, __FILE__, __LINE__)

/*
 * Fail if signal signum is pending.
 */
#define reject_signal(signum) \
	reject_signal_impl(signum, #signum, __FILE__, __LINE__)

void	expect_signal_impl(int, const char *, const char *, int);
void	reject_signal_impl(int, const char *, const char *, int);

int	test_common_badpgid(int);
int	test_common_badsession(int);
int	test_common_cansigio(int *);
int	test_common_getown(int);
int	test_common_read(int *);
int	test_common_write(int *);
int	test_pipe_badpgid(void);
int	test_pipe_badsession(void);
int	test_pipe_cansigio(void);
int	test_pipe_getown(void);
int	test_pipe_read(void);
int	test_pipe_write(void);
int	test_socket_badpgid(void);
int	test_socket_badsession(void);
int	test_socket_cansigio(void);
int	test_socket_getown(void);
int	test_socket_inherit(void);
int	test_socket_read(void);
int	test_socket_write(void);

void	test_init(void);
void	test_barrier(int);
int	test_fork(pid_t *, int *);
int	test_wait(pid_t, int);
#define PARENT	1
#define CHILD	0
