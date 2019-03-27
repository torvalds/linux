/*-
 * provider.h
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: provider.h,v 1.6 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#ifndef _PROVIDER_H_
#define _PROVIDER_H_

/*
 * Provider of service
 */

struct profile;

struct provider
{
	struct profile 		*profile;		/* profile */
	void			*data;			/* profile data */
	uint32_t		 handle;		/* record handle */
	bdaddr_t		 bdaddr;		/* provider's BDADDR */
	int32_t			 fd;			/* session descriptor */
	TAILQ_ENTRY(provider)	 provider_next;		/* all providers */
};

typedef struct provider		provider_t;
typedef struct provider	*	provider_p;

#define		provider_match_bdaddr(p, b) \
	(memcmp(b, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0 || \
	 memcmp(&(p)->bdaddr, NG_HCI_BDADDR_ANY, sizeof(bdaddr_t)) == 0 || \
	 memcmp(&(p)->bdaddr, b, sizeof(bdaddr_t)) == 0)
	
int32_t		provider_register_sd		(int32_t fd);
provider_p	provider_register		(profile_p const profile,
						 bdaddr_p const bdaddr,
						 int32_t fd,
						 uint8_t const *data,
						 uint32_t datalen);

void		provider_unregister		(provider_p provider);
int32_t		provider_update			(provider_p provider,
						 uint8_t const *data,
						 uint32_t datalen);
provider_p	provider_by_handle		(uint32_t handle);
provider_p	provider_get_first		(void);
provider_p	provider_get_next		(provider_p provider);
uint32_t	provider_get_change_state	(void);

#endif /* ndef _PROVIDER_H_ */
