/*	$OpenBSD: radius_msgauth.c,v 1.5 2024/08/14 04:50:31 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE"AUTHOR" AND CONTRIBUTORS AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/hmac.h>

#include "radius.h"

#include "radius_local.h"

static int
radius_calc_message_authenticator(RADIUS_PACKET * packet, const char *secret,
    void *ma)
{
	const RADIUS_ATTRIBUTE	*attr;
	const RADIUS_ATTRIBUTE	*end;
	u_char			 zero16[16];
	HMAC_CTX		*ctx;
	int			 mdlen;
	int			 ret = -1;

	memset(zero16, 0, sizeof(zero16));

	if ((ctx = HMAC_CTX_new()) == NULL)
		goto err;

	if (!HMAC_Init_ex(ctx, secret, strlen(secret), EVP_md5(), NULL))
		goto err;

	/*
	 * Traverse the radius packet.
	 */
	if (packet->request != NULL) {
		if (!HMAC_Update(ctx, (const u_char *)packet->pdata, 4))
			goto err;
		if (!HMAC_Update(ctx, (unsigned char *)packet->request->pdata
		    ->authenticator, 16))
			goto err;
	} else {
		if (!HMAC_Update(ctx, (const u_char *)packet->pdata,
		    sizeof(RADIUS_PACKET_DATA)))
			goto err;
	}

	attr = ATTRS_BEGIN(packet->pdata);
	end = ATTRS_END(packet->pdata);

	for (; attr < end; ATTRS_ADVANCE(attr)) {
		if (attr->type == RADIUS_TYPE_MESSAGE_AUTHENTICATOR) {
			if (!HMAC_Update(ctx, (u_char *)attr, 2))
				goto err;
			if (!HMAC_Update(ctx, (u_char *)zero16, sizeof(zero16)))
				goto err;
		} else {
			if (!HMAC_Update(ctx, (u_char *)attr,
			    (int)attr->length))
				goto err;
		}
	}

	if (!HMAC_Final(ctx, (u_char *)ma, &mdlen))
		goto err;

	ret = 0;

 err:
	HMAC_CTX_free(ctx);

	return (ret);
}

int
radius_put_message_authenticator(RADIUS_PACKET * packet, const char *secret)
{
	u_char	 ma[16];

	/*
	 * It is not required to initialize ma
	 * because content of Message-Authenticator attribute is assumed zero
	 * during calculation.
	 */
	if (radius_unshift_raw_attr(packet, RADIUS_TYPE_MESSAGE_AUTHENTICATOR,
	    ma, sizeof(ma)) != 0)
		return (-1);

	return (radius_set_message_authenticator(packet, secret));
}

int
radius_set_message_authenticator(RADIUS_PACKET * packet, const char *secret)
{
	u_char	 ma[16];

	if (radius_calc_message_authenticator(packet, secret, ma) != 0)
		return (-1);

	return (radius_set_raw_attr(packet, RADIUS_TYPE_MESSAGE_AUTHENTICATOR,
	    ma, sizeof(ma)));
}

int
radius_check_message_authenticator(RADIUS_PACKET * packet, const char *secret)
{
	int	 rval;
	size_t	 len;
	u_char	 ma0[16], ma1[16];

	if (radius_calc_message_authenticator(packet, secret, ma0) != 0)
		return (-1);

	len = sizeof(ma1);
	if ((rval = radius_get_raw_attr(packet,
		    RADIUS_TYPE_MESSAGE_AUTHENTICATOR, ma1, &len)) != 0)
		return (rval);

	if (len != sizeof(ma1))
		return (-1);

	return (timingsafe_bcmp(ma0, ma1, sizeof(ma1)));
}
