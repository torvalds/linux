// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/oid_registry.h>
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifsproto.h"
#include "cifs_spnego_negtokeninit.asn1.h"

int
decode_negTokenInit(unsigned char *security_blob, int length,
		    struct TCP_Server_Info *server)
{
	if (asn1_ber_decoder(&cifs_spnego_negtokeninit_decoder, server,
			     security_blob, length) == 0)
		return 1;
	else
		return 0;
}

int cifs_gssapi_this_mech(void *context, size_t hdrlen,
			  unsigned char tag, const void *value, size_t vlen)
{
	enum OID oid;

	oid = look_up_OID(value, vlen);
	if (oid != OID_spnego) {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		cifs_dbg(FYI, "Error decoding negTokenInit header: unexpected OID %s\n",
			 buf);
		return -EBADMSG;
	}
	return 0;
}

int cifs_neg_token_init_mech_type(void *context, size_t hdrlen,
				  unsigned char tag,
				  const void *value, size_t vlen)
{
	struct TCP_Server_Info *server = context;
	enum OID oid;

	oid = look_up_OID(value, vlen);
	if (oid == OID_mskrb5)
		server->sec_mskerberos = true;
	else if (oid == OID_krb5u2u)
		server->sec_kerberosu2u = true;
	else if (oid == OID_krb5)
		server->sec_kerberos = true;
	else if (oid == OID_ntlmssp)
		server->sec_ntlmssp = true;
	else if (oid == OID_IAKerb)
		server->sec_iakerb = true;
	else {
		char buf[50];

		sprint_oid(value, vlen, buf, sizeof(buf));
		cifs_dbg(FYI, "Decoding negTokenInit: unsupported OID %s\n",
			 buf);
	}
	return 0;
}
