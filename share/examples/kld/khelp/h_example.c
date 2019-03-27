/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, Melbourne, Australia by
 * Lawrence Stewart under sponsorship from the FreeBSD Foundation.
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
 */

/*
 * This example Khelp module uses the helper hook points available in the TCP
 * stack to calculate a per-connection count of inbound and outbound packets
 * when the connection is in the established state. The code is verbosely
 * documented in an attempt to explain how everything fits together.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/hhook.h>
#include <sys/khelp.h>
#include <sys/module.h>
#include <sys/module_khelp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>

#include <netinet/tcp_var.h>

#include <vm/uma.h>

/*
 * Function prototype for our helper hook (man 9 hhook) compatible hook
 * function.
 */
static int example_hook(int hhook_type, int hhook_id, void *udata,
    void *ctx_data, void *hdata, struct osd *hosd);

/*
 * Our per-connection persistent data storage struct.
 */
struct example {
	uint32_t est_in_count;
	uint32_t est_out_count;
};

/*
 * Fill in the required bits of our module's struct helper (defined in
 * <sys/module_khelp.h>).
 *
 * - Our helper will be storing persistent state for each TCP connection, so we
 * request the use the Object Specific Data (OSD) feature from the framework by
 * setting the HELPER_NEEDS_OSD flag.
 *
 * - Our helper is related to the TCP subsystem, so tell the Khelp framework
 * this by setting an appropriate class for the module. When a new TCP
 * connection is created, the Khelp framework takes care of associating helper
 * modules of the appropriate class with the new connection.
 */
struct helper example_helper = {
	.h_flags = HELPER_NEEDS_OSD,
	.h_classes = HELPER_CLASS_TCP
};

/*
 * Set which helper hook points our module wants to hook by creating an array of
 * hookinfo structs (defined in <sys/hhook.h>). We hook the TCP established
 * inbound/outbound hook points (TCP hhook points are defined in
 * <netinet/tcp_var.h>) with our example_hook() function. We don't require a user
 * data pointer to be passed to our hook function when called, so we set it to
 * NULL.
 */
struct hookinfo example_hooks[] = {
	{
		.hook_type = HHOOK_TYPE_TCP,
		.hook_id = HHOOK_TCP_EST_IN,
		.hook_udata = NULL,
		.hook_func = &example_hook
	},
	{
		.hook_type = HHOOK_TYPE_TCP,
		.hook_id = HHOOK_TCP_EST_OUT,
		.hook_udata = NULL,
		.hook_func = &example_hook
	}
};

/*
 * Very simple helper hook function. Here's a quick run through the arguments:
 *
 * - hhook_type and hhook_id are useful if you use a single function with many
 * hook points and want to know which hook point called the function.
 *
 * - udata will be NULL, because we didn't elect to pass a pointer in either of
 * the hookinfo structs we instantiated above in the example_hooks array.
 *
 * - ctx_data contains context specific data from the hook point call site. The
 * data type passed is subsystem dependent. In the case of TCP, the hook points
 * pass a pointer to a "struct tcp_hhook_data" (defined in <netinet/tcp_var.h>).
 *
 * - hdata is a pointer to the persistent per-object storage for our module. The
 * pointer is allocated automagically by the Khelp framework when the connection
 * is created, and comes from a dedicated UMA zone. It will never be NULL.
 *
 * - hosd can be used with the Khelp framework's khelp_get_osd() function to
 * access data belonging to a different Khelp module.
 */
static int
example_hook(int hhook_type, int hhook_id, void *udata, void *ctx_data,
    void *hdata, struct osd *hosd)
{
	struct example *data;

	data = hdata;

	if (hhook_id == HHOOK_TCP_EST_IN)
		data->est_in_count++;
	else if (hhook_id == HHOOK_TCP_EST_OUT)
		data->est_out_count++;

	return (0);
}

/*
 * We use a convenient macro which handles registering our module with the Khelp
 * framework. Note that Khelp modules which set the HELPER_NEEDS_OSD flag (i.e.
 * require persistent per-object storage) must use the KHELP_DECLARE_MOD_UMA()
 * macro. If you don't require per-object storage, use the KHELP_DECLARE_MOD()
 * macro instead.
 */
KHELP_DECLARE_MOD_UMA(example, &example_helper, example_hooks, 1,
    sizeof(struct example), NULL, NULL);
