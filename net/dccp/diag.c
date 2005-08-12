/*
 *  net/dccp/diag.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/config.h>

#include <linux/module.h>
#include <linux/inet_diag.h>

#include "dccp.h"

static void dccp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			       void *_info)
{
	r->idiag_rqueue = r->idiag_wqueue = 0;
}

static struct inet_diag_handler dccp_diag_handler = {
	.idiag_hashinfo	 = &dccp_hashinfo,
	.idiag_get_info	 = dccp_diag_get_info,
	.idiag_type	 = DCCPDIAG_GETSOCK,
	.idiag_info_size = 0,
};

static int __init dccp_diag_init(void)
{
	return inet_diag_register(&dccp_diag_handler);
}

static void __exit dccp_diag_fini(void)
{
	inet_diag_unregister(&dccp_diag_handler);
}

module_init(dccp_diag_init);
module_exit(dccp_diag_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@mandriva.com>");
MODULE_DESCRIPTION("DCCP inet_diag handler");
