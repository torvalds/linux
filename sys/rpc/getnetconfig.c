/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_inet6.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <rpc/types.h>

/*
 * For in-kernel use, we use a simple compiled-in configuration.
 */

static struct netconfig netconfigs[] = {
#ifdef INET6
	{
		.nc_netid =	"udp6",
		.nc_semantics =	NC_TPI_CLTS,
		.nc_flag =	NC_VISIBLE,
		.nc_protofmly =	"inet6",
		.nc_proto =	"udp",
	},
	{
		.nc_netid =	"tcp6",
		.nc_semantics =	NC_TPI_COTS_ORD,
		.nc_flag =	NC_VISIBLE,
		.nc_protofmly =	"inet6",
		.nc_proto =	"tcp",
	},
#endif	
	{
		.nc_netid =	"udp",
		.nc_semantics =	NC_TPI_CLTS,
		.nc_flag =	NC_VISIBLE,
		.nc_protofmly =	"inet",
		.nc_proto =	"udp",
	},
	{
		.nc_netid =	"tcp",
		.nc_semantics =	NC_TPI_COTS_ORD,
		.nc_flag =	NC_VISIBLE,
		.nc_protofmly =	"inet",
		.nc_proto =	"tcp",
	},
	{
		.nc_netid =	"local",
		.nc_semantics =	NC_TPI_COTS_ORD,
		.nc_flag =	0,
		.nc_protofmly =	"loopback",
		.nc_proto =	"",
	},
	{
		.nc_netid =	NULL,
	}
};

void *
setnetconfig(void)
{
	struct netconfig **nconfp;

	nconfp = malloc(sizeof(struct netconfig *), M_RPC, M_WAITOK);
	*nconfp = netconfigs;

	return ((void *) nconfp);
}

struct netconfig *
getnetconfig(void *handle)
{
	struct netconfig **nconfp = (struct netconfig **) handle;
	struct netconfig *nconf;

	nconf = *nconfp;
	if (nconf->nc_netid == NULL)
		return (NULL);

	(*nconfp)++;

	return (nconf);
}

struct netconfig *
getnetconfigent(const char *netid)
{
	struct netconfig *nconf;

	for (nconf = netconfigs; nconf->nc_netid; nconf++) {
		if (!strcmp(netid, nconf->nc_netid))
			return (nconf);
	}

	return (NULL);
}

void
freenetconfigent(struct netconfig *nconf)
{

}

int
endnetconfig(void * handle)
{
	struct netconfig **nconfp = (struct netconfig **) handle;

	free(nconfp, M_RPC);
	return (0);
}
