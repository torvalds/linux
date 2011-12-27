/*
 *   fs/cifs/smb2proto.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
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
#ifndef _SMB2PROTO_H
#define _SMB2PROTO_H
#include <linux/nls.h>
#include <linux/key-type.h>

struct statfs;

/*
 *****************************************************************
 * All Prototypes
 *****************************************************************
 */
extern int map_smb2_to_linux_error(char *buf, bool log_err);
extern int smb2_check_message(char *buf, unsigned int length);
extern unsigned int smb2_calc_size(struct smb2_hdr *hdr);
extern char *smb2_get_data_area_len(int *off, int *len, struct smb2_hdr *hdr);

extern int smb2_check_receive(struct mid_q_entry *mid,
			      struct TCP_Server_Info *server, bool log_error);
extern int smb2_setup_request(struct cifs_ses *ses, struct kvec *iov,
			      unsigned int nvec, struct mid_q_entry **ret_mid);

/*
 * SMB2 Worker functions - most of protocol specific implementation details
 * are contained within these calls.
 */
extern int SMB2_negotiate(const unsigned int xid, struct cifs_ses *ses);
extern int SMB2_sess_setup(const unsigned int xid, struct cifs_ses *ses,
			   const struct nls_table *nls_cp);
extern int SMB2_logoff(const unsigned int xid, struct cifs_ses *ses);
extern int SMB2_tcon(const unsigned int xid, struct cifs_ses *ses,
		     const char *tree, struct cifs_tcon *tcon,
		     const struct nls_table *);
extern int SMB2_tdis(const unsigned int xid, struct cifs_tcon *tcon);

#endif			/* _SMB2PROTO_H */
