/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   SPNEGO upcall management for CIFS
 *
 *   Copyright (c) 2007 Red Hat, Inc.
 *   Author(s): Jeff Layton (jlayton@redhat.com)
 *              Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _CIFS_SPNEGO_H
#define _CIFS_SPNEGO_H

#define CIFS_SPNEGO_UPCALL_VERSION 2

/*
 * The version field should always be set to CIFS_SPNEGO_UPCALL_VERSION.
 * The flags field is for future use. The request-key callout should set
 * sesskey_len and secblob_len, and then concatenate the SessKey+SecBlob
 * and stuff it in the data field.
 */
struct cifs_spnego_msg {
	uint32_t	version;
	uint32_t	flags;
	uint32_t	sesskey_len;
	uint32_t	secblob_len;
	uint8_t		data[];
};

#ifdef __KERNEL__
extern struct key_type cifs_spnego_key_type;
extern struct key *cifs_get_spnego_key(struct cifs_ses *sesInfo,
				       struct TCP_Server_Info *server);
#endif /* KERNEL */

#endif /* _CIFS_SPNEGO_H */
