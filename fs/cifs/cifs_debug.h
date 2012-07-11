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
#define CIFS_DEBUG		/* BB temporary */

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

/*
 *	debug ON
 *	--------
 */
#ifdef CIFS_DEBUG

/* information message: e.g., configuration, major event */
extern int cifsFYI;
#define cifsfyi(fmt, arg...)						\
do {									\
	if (cifsFYI & CIFS_INFO)					\
		printk(KERN_DEBUG "%s: " fmt "\n", __FILE__, ##arg);	\
} while (0)

#define cFYI(set, fmt, arg...)			\
do {						\
	if (set)				\
		cifsfyi(fmt, ##arg);		\
} while (0)

#define cifswarn(fmt, arg...)			\
	printk(KERN_WARNING fmt "\n", ##arg)

/* debug event message: */
extern int cifsERROR;

#define cEVENT(fmt, arg...)						\
do {									\
	if (cifsERROR)							\
		printk(KERN_EVENT "%s: " fmt "\n", __FILE__, ##arg);	\
} while (0)

/* error event message: e.g., i/o error */
#define cifserror(fmt, arg...)					\
do {								\
	if (cifsERROR)						\
		printk(KERN_ERR "CIFS VFS: " fmt "\n", ##arg);	\
} while (0)

#define cERROR(set, fmt, arg...)		\
do {						\
	if (set)				\
		cifserror(fmt, ##arg);		\
} while (0)

/*
 *	debug OFF
 *	---------
 */
#else		/* _CIFS_DEBUG */
#define cERROR(set, fmt, arg...)
#define cEVENT(fmt, arg...)
#define cFYI(set, fmt, arg...)
#define cifserror(fmt, arg...)
#endif		/* _CIFS_DEBUG */

#endif				/* _H_CIFS_DEBUG */
