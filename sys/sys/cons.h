/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	from: @(#)cons.h	7.2 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _MACHINE_CONS_H_
#define	_MACHINE_CONS_H_

struct consdev;
struct tty;

typedef	void	cn_probe_t(struct consdev *);
typedef	void	cn_init_t(struct consdev *);
typedef	void	cn_term_t(struct consdev *);
typedef	void	cn_grab_t(struct consdev *);
typedef	void	cn_ungrab_t(struct consdev *);
typedef	int	cn_getc_t(struct consdev *);
typedef	void	cn_putc_t(struct consdev *, int);

struct consdev_ops {
	cn_probe_t	*cn_probe;
				/* probe hardware and fill in consdev info */
	cn_init_t	*cn_init;
				/* turn on as console */
	cn_term_t	*cn_term;
				/* turn off as console */
	cn_getc_t	*cn_getc;
				/* kernel getchar interface */
	cn_putc_t	*cn_putc;
				/* kernel putchar interface */
	cn_grab_t	*cn_grab;
				/* grab console for exclusive kernel use */
	cn_ungrab_t	*cn_ungrab;
				/* ungrab console */
	cn_init_t	*cn_resume;
				/* set up console after sleep, optional */
};

struct consdev {
	const struct consdev_ops *cn_ops;
				/* console device operations. */
	short	cn_pri;		/* pecking order; the higher the better */
	void	*cn_arg;	/* drivers method argument */
	int	cn_flags;	/* capabilities of this console */
	char	cn_name[SPECNAMELEN + 1];	/* console (device) name */
};

/* values for cn_pri - reflect our policy for console selection */
#define	CN_DEAD		0	/* device doesn't exist */
#define CN_LOW		1	/* device is a last restort only */
#define CN_NORMAL	2	/* device exists but is nothing special */
#define CN_INTERNAL	3	/* "internal" bit-mapped display */
#define CN_REMOTE	4	/* serial interface with remote bit set */

/* Values for cn_flags. */
#define	CN_FLAG_NODEBUG	0x00000001	/* Not supported with debugger. */
#define	CN_FLAG_NOAVAIL	0x00000002	/* Temporarily not available. */

/* Visibility of characters in cngets() */
#define	GETS_NOECHO	0	/* Disable echoing of characters. */
#define	GETS_ECHO	1	/* Enable echoing of characters. */
#define	GETS_ECHOPASS	2	/* Print a * for every character. */

#ifdef _KERNEL

extern	struct msgbuf consmsgbuf; /* Message buffer for constty. */
extern	struct tty *constty;	/* Temporary virtual console. */

#define	CONSOLE_DEVICE(name, ops, arg)					\
	static struct consdev name = {					\
		.cn_ops = &ops,						\
		.cn_arg = (arg),					\
	};								\
	DATA_SET(cons_set, name)

#define	CONSOLE_DRIVER(name, ...)					\
	static const struct consdev_ops name##_consdev_ops = {		\
		/* Mandatory methods. */				\
		.cn_probe = name##_cnprobe,				\
		.cn_init = name##_cninit,				\
		.cn_term = name##_cnterm,				\
		.cn_getc = name##_cngetc,				\
		.cn_putc = name##_cnputc,				\
		.cn_grab = name##_cngrab,				\
		.cn_ungrab = name##_cnungrab,				\
		/* Optional fields. */					\
		__VA_ARGS__						\
	};								\
	CONSOLE_DEVICE(name##_consdev, name##_consdev_ops, NULL)

/* Other kernel entry points. */
void	cninit(void);
void	cninit_finish(void);
int	cnadd(struct consdev *);
void	cnavailable(struct consdev *, int);
void	cnremove(struct consdev *);
void	cnselect(struct consdev *);
void	cngrab(void);
void	cnungrab(void);
void	cnresume(void);
int	cncheckc(void);
int	cngetc(void);
void	cngets(char *, size_t, int);
void	cnputc(int);
void	cnputs(char *);
void	cnputsn(const char *, size_t);
int	cnunavailable(void);
void	constty_set(struct tty *tp);
void	constty_clear(void);

/* sc(4) / vt(4) coexistence shim */
#define	VTY_SC 0x01
#define	VTY_VT 0x02
int	vty_enabled(unsigned int);
void	vty_set_preferred(unsigned int);

#endif /* _KERNEL */

#endif /* !_MACHINE_CONS_H_ */
