/*
 *   fs/cifs/cn_cifs.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _CN_CIFS_H
#define _CN_CIFS_H
#ifdef CONFIG_CIFS_UPCALL
#include <linux/types.h>
#include <linux/connector.h>

struct cifs_upcall {
	char signature[4]; /* CIFS */
	enum command {
		CIFS_GET_IP = 0x00000001,   /* get ip address for hostname */
		CIFS_GET_SECBLOB = 0x00000002, /* get SPNEGO wrapped blob */
	} command;
	/* union cifs upcall data follows */
};
#endif /* CIFS_UPCALL */
#endif /* _CN_CIFS_H */
