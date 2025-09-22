/*	$OpenBSD: hibernate.h,v 1.10 2018/06/21 07:33:30 mlarkin Exp $	*/

/*
 * Copyright (c) 2011 Mike Larkin <mlarkin@openbsd.org>
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

#include <machine/hibernate_var.h>

/* i386 hibernate support structures and functions */

int	get_hibernate_info_md(union hibernate_info *);
void	hibernate_flush(void);
void	hibernate_enter_resume_mapping(vaddr_t, paddr_t, int);
int	hibernate_inflate_skip(union hibernate_info *, paddr_t);
int	hibernate_suspend(void);
void	hibernate_switch_stack_machdep(void);
void	hibernate_resume_machdep(vaddr_t);
void	hibernate_activate_resume_pt_machdep(void);
void	hibernate_enable_intr_machdep(void);
void	hibernate_disable_intr_machdep(void);
#ifdef MULTIPROCESSOR
void	hibernate_quiesce_cpus(void);
#endif /* MULTIPROCESSOR */
