/* $OpenBSD: app.c,v 1.14 2017/02/03 08:23:46 guenther Exp $	 */
/* $EOM: app.c,v 1.6 1999/05/01 20:21:06 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
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

/*
 * XXX This is just a wrapper module for now.  Later we might handle many
 * applications simultaneously but right now, we assume one system-dependent
 * one only.
 */

#include <netinet/in.h>

#include "app.h"
#include "log.h"
#include "monitor.h"
#include "pf_key_v2.h"

int app_socket;

/* Set this to not get any applications setup.  */
int app_none = 0;

/* Initialize applications.  */
void
app_init(void)
{
	if (app_none)
		return;
	app_socket = monitor_pf_key_v2_open();
	if (app_socket == -1)
		log_fatal("app_init: cannot open connection to application");
}

void
app_handler(void)
{
	pf_key_v2_handler(app_socket);
}
