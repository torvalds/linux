/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Paolo Pisati <piso@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Alias_dummy is just an empty skeleton used to demostrate how to write
 * a module for libalias, that will run unalterated in userland or in
 * kernel land.
 */

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias_local.h"
#include "alias_mod.h"
#endif

static void
AliasHandleDummy(struct libalias *la, struct ip *ip, struct alias_data *ah);

static int
fingerprint(struct libalias *la, struct alias_data *ah)
{

	/*
	 * Check here all the data that will be used later, if any field
	 * is empy/NULL, return a -1 value.
	 */
	if (ah->dport == NULL || ah->sport == NULL || ah->lnk == NULL ||
		ah->maxpktsize == 0)
		return (-1);
	/*
	 * Fingerprint the incoming packet, if it matches any conditions
	 * return an OK value.
	 */
	if (ntohs(*ah->dport) == 123
	    || ntohs(*ah->sport) == 456)
		return (0); /* I know how to handle it. */
	return (-1); /* I don't recognize this packet. */
}

/*
 * Wrap in this general purpose function, the real function used to alias the
 * packets.
 */

static int
protohandler(struct libalias *la, struct ip *pip, struct alias_data *ah)
{
	
	AliasHandleDummy(la, pip, ah);
	return (0);
}

/*
 * NOTA BENE: the next variable MUST NOT be renamed in any case if you want
 * your module to work in userland, cause it's used to find and use all
 * the protocol handlers present in every module.
 * So WATCH OUT, your module needs this variables and it needs it with
 * ITS EXACT NAME: handlers.
 */

struct proto_handler handlers [] = {
	{
	  .pri = 666,
	  .dir = IN|OUT,
	  .proto = UDP|TCP,
	  .fingerprint = &fingerprint,
	  .protohandler = &protohandler
	},
	{ EOH }
};

static int
mod_handler(module_t mod, int type, void *data)
{
	int error;

	switch (type) {	
	case MOD_LOAD:
		error = 0;
		LibAliasAttachHandlers(handlers);
		break;
	case MOD_UNLOAD:
		error = 0;
		LibAliasDetachHandlers(handlers);
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

#ifdef _KERNEL
static
#endif
moduledata_t alias_mod = {
       "alias_dummy", mod_handler, NULL
};

#ifdef	_KERNEL
DECLARE_MODULE(alias_dummy, alias_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_VERSION(alias_dummy, 1);
MODULE_DEPEND(alias_dummy, libalias, 1, 1, 1);
#endif

static void
AliasHandleDummy(struct libalias *la, struct ip *ip, struct alias_data *ah)
{
	; /* Dummy. */
}

