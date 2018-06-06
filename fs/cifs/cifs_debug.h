/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2002
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
*/

#ifndef _H_CIFS_DEBUG
#define _H_CIFS_DEBUG

void cifs_dump_mem(char *label, void *data, int length);
void cifs_dump_detail(void *);
void cifs_dump_mids(struct TCP_Server_Info *);
extern bool traceSMB;		/* flag which enables the function below */
void dump_smb(void *, int);
#define CIFS_INFO	0x01
#define CIFS_RC		0x02
#define CIFS_TIMER	0x04

#define VFS 1
#define FYI 2
extern int cifsFYI;
#ifdef CONFIG_CIFS_DEBUG2
#define NOISY 4
#else
#define NOISY 0
#endif
#define ONCE 8

/*
 *	debug ON
 *	--------
 */
#ifdef CONFIG_CIFS_DEBUG

/* information message: e.g., configuration, major event */
#define cifs_dbg_func(ratefunc, type, fmt, ...)			\
do {								\
	if ((type) & FYI && cifsFYI & CIFS_INFO) {		\
		pr_debug_ ## ratefunc("%s: "			\
				fmt, __FILE__, ##__VA_ARGS__);	\
	} else if ((type) & VFS) {				\
		pr_err_ ## ratefunc("CIFS VFS: "		\
				 fmt, ##__VA_ARGS__);		\
	} else if ((type) & NOISY && (NOISY != 0)) {		\
		pr_debug_ ## ratefunc(fmt, ##__VA_ARGS__);	\
	}							\
} while (0)

#define cifs_dbg(type, fmt, ...) \
do {							\
	if ((type) & ONCE)				\
		cifs_dbg_func(once,			\
			 type, fmt, ##__VA_ARGS__);	\
	else						\
		cifs_dbg_func(ratelimited,		\
			type, fmt, ##__VA_ARGS__);	\
} while (0)

/*
 *	debug OFF
 *	---------
 */
#else		/* _CIFS_DEBUG */
#define cifs_dbg(type, fmt, ...)					\
do {									\
	if (0)								\
		pr_debug(fmt, ##__VA_ARGS__);				\
} while (0)
#endif

#endif				/* _H_CIFS_DEBUG */
