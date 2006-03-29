/* main.c: Rx RPC interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/krxiod.h>
#include <rxrpc/krxsecd.h>
#include <rxrpc/krxtimod.h>
#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/message.h>
#include "internal.h"

MODULE_DESCRIPTION("Rx RPC implementation");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

__be32 rxrpc_epoch;

/*****************************************************************************/
/*
 * initialise the Rx module
 */
static int __init rxrpc_initialise(void)
{
	int ret;

	/* my epoch value */
	rxrpc_epoch = htonl(xtime.tv_sec);

	/* register the /proc interface */
#ifdef CONFIG_PROC_FS
	ret = rxrpc_proc_init();
	if (ret<0)
		return ret;
#endif

	/* register the sysctl files */
#ifdef CONFIG_SYSCTL
	ret = rxrpc_sysctl_init();
	if (ret<0)
		goto error_proc;
#endif

	/* start the krxtimod daemon */
	ret = rxrpc_krxtimod_start();
	if (ret<0)
		goto error_sysctl;

	/* start the krxiod daemon */
	ret = rxrpc_krxiod_init();
	if (ret<0)
		goto error_krxtimod;

	/* start the krxsecd daemon */
	ret = rxrpc_krxsecd_init();
	if (ret<0)
		goto error_krxiod;

	kdebug("\n\n");

	return 0;

 error_krxiod:
	rxrpc_krxiod_kill();
 error_krxtimod:
	rxrpc_krxtimod_kill();
 error_sysctl:
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl_cleanup();
 error_proc:
#endif
#ifdef CONFIG_PROC_FS
	rxrpc_proc_cleanup();
#endif
	return ret;
} /* end rxrpc_initialise() */

module_init(rxrpc_initialise);

/*****************************************************************************/
/*
 * clean up the Rx module
 */
static void __exit rxrpc_cleanup(void)
{
	kenter("");

	__RXACCT(printk("Outstanding Messages   : %d\n",
			atomic_read(&rxrpc_message_count)));
	__RXACCT(printk("Outstanding Calls      : %d\n",
			atomic_read(&rxrpc_call_count)));
	__RXACCT(printk("Outstanding Connections: %d\n",
			atomic_read(&rxrpc_connection_count)));
	__RXACCT(printk("Outstanding Peers      : %d\n",
			atomic_read(&rxrpc_peer_count)));
	__RXACCT(printk("Outstanding Transports : %d\n",
			atomic_read(&rxrpc_transport_count)));

	rxrpc_krxsecd_kill();
	rxrpc_krxiod_kill();
	rxrpc_krxtimod_kill();
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl_cleanup();
#endif
#ifdef CONFIG_PROC_FS
	rxrpc_proc_cleanup();
#endif

	__RXACCT(printk("Outstanding Messages   : %d\n",
			atomic_read(&rxrpc_message_count)));
	__RXACCT(printk("Outstanding Calls      : %d\n",
			atomic_read(&rxrpc_call_count)));
	__RXACCT(printk("Outstanding Connections: %d\n",
			atomic_read(&rxrpc_connection_count)));
	__RXACCT(printk("Outstanding Peers      : %d\n",
			atomic_read(&rxrpc_peer_count)));
	__RXACCT(printk("Outstanding Transports : %d\n",
			atomic_read(&rxrpc_transport_count)));

	kleave("");
} /* end rxrpc_cleanup() */

module_exit(rxrpc_cleanup);

/*****************************************************************************/
/*
 * clear the dead space between task_struct and kernel stack
 * - called by supplying -finstrument-functions to gcc
 */
#if 0
void __cyg_profile_func_enter (void *this_fn, void *call_site)
__attribute__((no_instrument_function));

void __cyg_profile_func_enter (void *this_fn, void *call_site)
{
       asm volatile("  movl    %%esp,%%edi     \n"
                    "  andl    %0,%%edi        \n"
                    "  addl    %1,%%edi        \n"
                    "  movl    %%esp,%%ecx     \n"
                    "  subl    %%edi,%%ecx     \n"
                    "  shrl    $2,%%ecx        \n"
                    "  movl    $0xedededed,%%eax     \n"
                    "  rep stosl               \n"
                    :
                    : "i"(~(THREAD_SIZE-1)), "i"(sizeof(struct thread_info))
                    : "eax", "ecx", "edi", "memory", "cc"
                    );
}

void __cyg_profile_func_exit(void *this_fn, void *call_site)
__attribute__((no_instrument_function));

void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
       asm volatile("  movl    %%esp,%%edi     \n"
                    "  andl    %0,%%edi        \n"
                    "  addl    %1,%%edi        \n"
                    "  movl    %%esp,%%ecx     \n"
                    "  subl    %%edi,%%ecx     \n"
                    "  shrl    $2,%%ecx        \n"
                    "  movl    $0xdadadada,%%eax     \n"
                    "  rep stosl               \n"
                    :
                    : "i"(~(THREAD_SIZE-1)), "i"(sizeof(struct thread_info))
                    : "eax", "ecx", "edi", "memory", "cc"
                    );
}
#endif
