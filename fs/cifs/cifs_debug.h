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
#ifdef CONFIG_CIFS_DEBUG2
#define DBG2 2
#else
#define DBG2 0
#endif
extern int traceSMB;		/* flag which enables the function below */
void dump_smb(void *, int);
#define CIFS_INFO	0x01
#define CIFS_RC		0x02
#define CIFS_TIMER	0x04

extern int cifsFYI;

/*
 *	debug ON
 *	--------
 */
#ifdef CONFIG_CIFS_DEBUG

/* information message: e.g., configuration, major event */
#define cifsfyi(fmt, ...)						\
do {									\
	if (cifsFYI & CIFS_INFO)					\
		printk(KERN_DEBUG "%s: " fmt "\n",			\
		       __FILE__, ##__VA_ARGS__);			\
} while (0)

#define cFYI(set, fmt, ...)						\
do {									\
	if (set)							\
		cifsfyi(fmt, ##__VA_ARGS__);				\
} while (0)

#define cifswarn(fmt, ...)						\
	printk(KERN_WARNING fmt "\n", ##__VA_ARGS__)

/* error event message: e.g., i/o error */
#define cifserror(fmt, ...)						\
	printk(KERN_ERR "CIFS VFS: " fmt "\n", ##__VA_ARGS__);		\

#define cERROR(set, fmt, ...)						\
do {									\
	if (set)							\
		cifserror(fmt, ##__VA_ARGS__);				\
} while (0)

/*
 *	debug OFF
 *	---------
 */
#else		/* _CIFS_DEBUG */
#define cifsfyi(fmt, ...)						\
do {									\
	if (0)								\
		printk(KERN_DEBUG "%s: " fmt "\n",			\
		       __FILE__, ##__VA_ARGS__);			\
} while (0)
#define cFYI(set, fmt, ...)						\
do {									\
	if (0 && set)							\
		cifsfyi(fmt, ##__VA_ARGS__);				\
} while (0)
#define cifserror(fmt, ...)						\
do {									\
	if (0)								\
		printk(KERN_ERR "CIFS VFS: " fmt "\n", ##__VA_ARGS__);	\
} while (0)
#define cERROR(set, fmt, ...)						\
do {									\
	if (0 && set)							\
		cifserror(fmt, ##__VA_ARGS__);				\
} while (0)
#endif		/* _CIFS_DEBUG */

#endif				/* _H_CIFS_DEBUG */
