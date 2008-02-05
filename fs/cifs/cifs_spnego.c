/*
 *   fs/cifs/cifs_spnego.c -- SPNEGO upcall management for CIFS
 *
 *   Copyright (c) 2007 Red Hat, Inc.
 *   Author(s): Jeff Layton (jlayton@redhat.com)
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

#include <linux/list.h>
#include <linux/string.h>
#include <keys/user-type.h>
#include <linux/key-type.h>
#include "cifsglob.h"
#include "cifs_spnego.h"
#include "cifs_debug.h"

/* create a new cifs key */
static int
cifs_spnego_key_instantiate(struct key *key, const void *data, size_t datalen)
{
	char *payload;
	int ret;

	ret = -ENOMEM;
	payload = kmalloc(datalen, GFP_KERNEL);
	if (!payload)
		goto error;

	/* attach the data */
	memcpy(payload, data, datalen);
	rcu_assign_pointer(key->payload.data, payload);
	ret = 0;

error:
	return ret;
}

static void
cifs_spnego_key_destroy(struct key *key)
{
	kfree(key->payload.data);
}


/*
 * keytype for CIFS spnego keys
 */
struct key_type cifs_spnego_key_type = {
	.name		= "cifs.spnego",
	.instantiate	= cifs_spnego_key_instantiate,
	.match		= user_match,
	.destroy	= cifs_spnego_key_destroy,
	.describe	= user_describe,
};

#define MAX_VER_STR_LEN   9 /* length of longest version string e.g.
				strlen(";ver=0xFF") */
#define MAX_MECH_STR_LEN 13 /* length of longest security mechanism name, eg
			       in future could have strlen(";sec=ntlmsspi") */
#define MAX_IPV6_ADDR_LEN 42 /* eg FEDC:BA98:7654:3210:FEDC:BA98:7654:3210/60 */
/* get a key struct with a SPNEGO security blob, suitable for session setup */
struct key *
cifs_get_spnego_key(struct cifsSesInfo *sesInfo)
{
	struct TCP_Server_Info *server = sesInfo->server;
	char *description, *dp;
	size_t desc_len;
	struct key *spnego_key;
	const char *hostname = server->hostname;

	/* BB: come up with better scheme for determining length */
	/* length of fields (with semicolons): ver=0xyz ipv4= ipaddress host=
	   hostname sec=mechanism uid=0x uid */
	desc_len = MAX_VER_STR_LEN + 5 + MAX_IPV6_ADDR_LEN + 1 + 6 +
		  strlen(hostname) + MAX_MECH_STR_LEN + 8 + (sizeof(uid_t) * 2);
	spnego_key = ERR_PTR(-ENOMEM);
	description = kzalloc(desc_len, GFP_KERNEL);
	if (description == NULL)
		goto out;

	dp = description;
	/* start with version and hostname portion of UNC string */
	spnego_key = ERR_PTR(-EINVAL);
	sprintf(dp, "ver=0x%x;host=%s;", CIFS_SPNEGO_UPCALL_VERSION,
		hostname);
	dp = description + strlen(description);

	/* add the server address */
	if (server->addr.sockAddr.sin_family == AF_INET)
		sprintf(dp, "ip4=" NIPQUAD_FMT,
			NIPQUAD(server->addr.sockAddr.sin_addr));
	else if (server->addr.sockAddr.sin_family == AF_INET6)
		sprintf(dp, "ip6=" NIP6_SEQFMT,
			NIP6(server->addr.sockAddr6.sin6_addr));
	else
		goto out;

	dp = description + strlen(description);

	/* for now, only sec=krb5 is valid */
	if (server->secType == Kerberos)
		sprintf(dp, ";sec=krb5");
	else
		goto out;

	dp = description + strlen(description);
	sprintf(dp, ";uid=0x%x", sesInfo->linux_uid);

	cFYI(1, ("key description = %s", description));
	spnego_key = request_key(&cifs_spnego_key_type, description, "");

#ifdef CONFIG_CIFS_DEBUG2
	if (cifsFYI && !IS_ERR(spnego_key)) {
		struct cifs_spnego_msg *msg = spnego_key->payload.data;
		cifs_dump_mem("SPNEGO reply blob:", msg->data, min(1024U,
				msg->secblob_len + msg->sesskey_len));
	}
#endif /* CONFIG_CIFS_DEBUG2 */

out:
	kfree(description);
	return spnego_key;
}
