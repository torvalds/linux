/*
 * linux/net/sunrpc/sunrpc_syms.c
 *
 * Symbols exported by the sunrpc module.
 *
 * Copyright (C) 1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/uio.h>
#include <linux/unistd.h>
#include <linux/init.h>

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/auth.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/sunrpc/xprtsock.h>

extern struct cache_detail ip_map_cache, unix_gid_cache;

static int __init
init_sunrpc(void)
{
	int err = register_rpc_pipefs();
	if (err)
		goto out;
	err = rpc_init_mempool();
	if (err) {
		unregister_rpc_pipefs();
		goto out;
	}
#ifdef RPC_DEBUG
	rpc_register_sysctl();
#endif
#ifdef CONFIG_PROC_FS
	rpc_proc_init();
#endif
	cache_register(&ip_map_cache);
	cache_register(&unix_gid_cache);
	svc_init_xprt_sock();	/* svc sock transport */
	init_socket_xprt();	/* clnt sock transport */
	rpcauth_init_module();
out:
	return err;
}

static void __exit
cleanup_sunrpc(void)
{
	rpcauth_remove_module();
	cleanup_socket_xprt();
	svc_cleanup_xprt_sock();
	unregister_rpc_pipefs();
	rpc_destroy_mempool();
	cache_unregister(&ip_map_cache);
	cache_unregister(&unix_gid_cache);
#ifdef RPC_DEBUG
	rpc_unregister_sysctl();
#endif
#ifdef CONFIG_PROC_FS
	rpc_proc_exit();
#endif
	rcu_barrier(); /* Wait for completion of call_rcu()'s */
}
MODULE_LICENSE("GPL");
fs_initcall(init_sunrpc); /* Ensure we're initialised before nfs */
module_exit(cleanup_sunrpc);
