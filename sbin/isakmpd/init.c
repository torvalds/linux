/* $OpenBSD: init.c,v 1.44 2021/10/13 16:56:30 tb Exp $	 */
/* $EOM: init.c,v 1.25 2000/03/30 14:27:24 ho Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2000 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2003, 2004 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

/* XXX This file could easily be built dynamically instead.  */

#include <stdlib.h>

#include "app.h"
#include "cert.h"
#include "conf.h"
#include "connection.h"
#include "doi.h"
#include "exchange.h"
#include "init.h"
#include "ipsec.h"
#include "isakmp_doi.h"
#include "libcrypto.h"
#include "log.h"
#include "dh.h"
#include "monitor.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "virtual.h"
#include "udp.h"
#include "ui.h"
#include "util.h"
#include "vendor.h"

#include "policy.h"

#include "nat_traversal.h"
#include "udp_encap.h"

void
init(void)
{
	app_init();
	doi_init();
	exchange_init();
	group_init();
	ipsec_init();
	isakmp_doi_init();

	timer_init();

	/* The following group are depending on timer_init having run.  */
	conf_init();
	connection_init();

	/* This depends on conf_init, thus check as soon as possible. */
	log_reinit();

	/* policy_init depends on conf_init having run.  */
	policy_init();

	/* Depends on conf_init and policy_init having run */
	cert_init();
	crl_init();

	sa_init();
	transport_init();
	virtual_init();
	udp_init();
	nat_t_init();
	udp_encap_init();
	vendor_init();
}

/* Reinitialize, either after a SIGHUP reception or by FIFO UI cmd.  */
void
reinit(void)
{
	log_print("isakmpd: reinitializing daemon");

	/*
	 * XXX Remove all(/some?) pending exchange timers? - they may not be
	 *     possible to complete after we've re-read the config file.
	 *     User-initiated SIGHUP's maybe "authorizes" a wait until
	 *     next connection-check.
	 * XXX This means we discard exchange->last_msg, is this really ok?
	 */

	/* Reread config file.  */
	conf_reinit();

	log_reinit();

	/* Reread the policies.  */
	policy_init();

	/* Reinitialize certificates */
	cert_init();
	crl_init();

	/* Reinitialize our connection list.  */
	connection_reinit();

	/*
	 * Rescan interfaces (call reinit() in all transports).
	 */
	transport_reinit();

	/*
	 * XXX "These" (non-existent) reinitializations should not be done.
	 * cookie_reinit ();
	 * ui_reinit ();
	 */

	sa_reinit();
}
