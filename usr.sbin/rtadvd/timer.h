/*	$FreeBSD$	*/
/*	$KAME: timer.h,v 1.5 2002/05/31 13:30:38 jinmei Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

extern TAILQ_HEAD(rtadvd_timer_head_t, rtadvd_timer) ra_timer;
struct rtadvd_timer {
	TAILQ_ENTRY(rtadvd_timer)	rat_next;

	struct rainfo	*rat_rai;
	struct timespec	rat_tm;
	struct rtadvd_timer *(*rat_expire)(void *);
	void	*rat_expire_data;
	void	(*rat_update)(void *, struct timespec *);
	void	*rat_update_data;
};

void			rtadvd_timer_init(void);
void			rtadvd_update_timeout_handler(void);
struct rtadvd_timer	*rtadvd_add_timer(struct rtadvd_timer *(*)(void *),
			    void (*)(void *, struct timespec *), void *, void *);
void			rtadvd_set_timer(struct timespec *,
			    struct rtadvd_timer *);
void			rtadvd_remove_timer(struct rtadvd_timer *);
struct timespec		*rtadvd_check_timer(void);
