/*-
 * provider.c
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
 * $Id: provider.c,v 1.5 2004/01/13 01:54:39 max Exp $
 * $FreeBSD$
 */

#include <sys/queue.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <string.h>
#include <stdlib.h>
#include "profile.h"
#include "provider.h"

static TAILQ_HEAD(, provider)	providers = TAILQ_HEAD_INITIALIZER(providers);
static uint32_t			change_state = 0;		
static uint32_t			handle = 0;

/*
 * Register Service Discovery provider.
 * Should not be called more the once.
 */

int32_t
provider_register_sd(int32_t fd)
{
	extern profile_t	sd_profile_descriptor;
	extern profile_t	bgd_profile_descriptor;

	provider_p		sd = calloc(1, sizeof(*sd));
	provider_p		bgd = calloc(1, sizeof(*bgd));

	if (sd == NULL || bgd == NULL) {
		if (sd != NULL)
			free(sd);

		if (bgd != NULL)
			free(bgd);

		return (-1);
	}

	sd->profile = &sd_profile_descriptor;
	bgd->handle = 0;
	sd->fd = fd;
	TAILQ_INSERT_HEAD(&providers, sd, provider_next);

	bgd->profile = &bgd_profile_descriptor;
	bgd->handle = 1;
	sd->fd = fd;
	TAILQ_INSERT_AFTER(&providers, sd, bgd, provider_next);
	
	change_state ++;

	return (0);
}

/*
 * Register new provider for a given profile, bdaddr and session.
 */

provider_p
provider_register(profile_p const profile, bdaddr_p const bdaddr, int32_t fd,
	uint8_t const *data, uint32_t datalen)
{
	provider_p	provider = calloc(1, sizeof(*provider));

	if (provider != NULL) {
		provider->data = malloc(datalen);
		if (provider->data != NULL) {
			provider->profile = profile;
			memcpy(provider->data, data, datalen);

			/*
			 * Record handles 0x0 and 0x1 are reserved
			 * for SDP itself
			 */

			if (++ handle <= 1)
				handle = 2;

			provider->handle = handle;

			memcpy(&provider->bdaddr, bdaddr,
				sizeof(provider->bdaddr));
			provider->fd = fd;

			TAILQ_INSERT_TAIL(&providers, provider, provider_next);
			change_state ++;
		} else {
			free(provider);
			provider = NULL;
		}
	}

	return (provider);
}

/*
 * Unregister provider
 */

void
provider_unregister(provider_p provider)
{
	TAILQ_REMOVE(&providers, provider, provider_next);
	if (provider->data != NULL)
		free(provider->data);
	free(provider);
	change_state ++;
}

/*
 * Update provider data
 */

int32_t
provider_update(provider_p provider, uint8_t const *data, uint32_t datalen)
{
	uint8_t	*new_data = (uint8_t *) realloc(provider->data, datalen);

	if (new_data == NULL)
		return (-1);

	memcpy(new_data, data, datalen);
	provider->data = new_data;

	return (0);
}

/*
 * Get a provider for given record handle
 */

provider_p
provider_by_handle(uint32_t handle)
{
	provider_p	provider = NULL;

	TAILQ_FOREACH(provider, &providers, provider_next)
		if (provider->handle == handle)
			break;

	return (provider);
}

/*
 * Cursor access
 */

provider_p
provider_get_first(void)
{
	return (TAILQ_FIRST(&providers));
}

provider_p
provider_get_next(provider_p provider)
{
	return (TAILQ_NEXT(provider, provider_next));
}

/*
 * Return change state
 */

uint32_t
provider_get_change_state(void)
{
	return (change_state);
}

