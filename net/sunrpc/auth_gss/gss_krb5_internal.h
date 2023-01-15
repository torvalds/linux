/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * SunRPC GSS Kerberos 5 mechanism internal definitions
 *
 * Copyright (c) 2022 Oracle and/or its affiliates.
 */

#ifndef _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H
#define _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H

void krb5_make_confounder(u8 *p, int conflen);

u32 gss_krb5_checksum(struct crypto_ahash *tfm, char *header, int hdrlen,
		      const struct xdr_buf *body, int body_offset,
		      struct xdr_netobj *cksumout);

#endif /* _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H */
