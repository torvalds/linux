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

/* RPC server stuff */
EXPORT_SYMBOL(svc_create);
EXPORT_SYMBOL(svc_create_thread);
EXPORT_SYMBOL(svc_create_pooled);
EXPORT_SYMBOL(svc_set_num_threads);
EXPORT_SYMBOL(svc_exit_thread);
EXPORT_SYMBOL(svc_destroy);
EXPORT_SYMBOL(svc_drop);
EXPORT_SYMBOL(svc_process);
EXPORT_SYMBOL(svc_recv);
EXPORT_SYMBOL(svc_wake_up);
EXPORT_SYMBOL(svc_makesock);
EXPORT_SYMBOL(svc_reserve);
EXPORT_SYMBOL(svc_auth_register);
EXPORT_SYMBOL(auth_domain_lookup);
EXPORT_SYMBOL(svc_authenticate);
EXPORT_SYMBOL(svc_set_client);

/* RPC statistics */
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(svc_proc_register);
EXPORT_SYMBOL(svc_proc_unregister);
EXPORT_SYMBOL(svc_seq_show);
#endif

/* caching... */
EXPORT_SYMBOL(auth_domain_find);
EXPORT_SYMBOL(auth_domain_put);
EXPORT_SYMBOL(auth_unix_add_addr);
EXPORT_SYMBOL(auth_unix_forget_old);
EXPORT_SYMBOL(auth_unix_lookup);
EXPORT_SYMBOL(cache_check);
EXPORT_SYMBOL(cache_flush);
EXPORT_SYMBOL(cache_purge);
EXPORT_SYMBOL(cache_register);
EXPORT_SYMBOL(cache_unregister);
EXPORT_SYMBOL(qword_add);
EXPORT_SYMBOL(qword_addhex);
EXPORT_SYMBOL(qword_get);
EXPORT_SYMBOL(svcauth_unix_purge);
EXPORT_SYMBOL(unix_domain_find);

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
	init_socket_xprt();
	rpcauth_init_module();
out:
	return err;
}

static void __exit
cleanup_sunrpc(void)
{
	rpcauth_remove_module();
	cleanup_socket_xprt();
	unregister_rpc_pipefs();
	rpc_destroy_mempool();
	if (cache_unregister(&ip_map_cache))
		printk(KERN_ERR "sunrpc: failed to unregister ip_map cache\n");
	if (cache_unregister(&unix_gid_cache))
	      printk(KERN_ERR "sunrpc: failed to unregister unix_gid cache\n");
#ifdef RPC_DEBUG
	rpc_unregister_sysctl();
#endif
#ifdef CONFIG_PROC_FS
	rpc_proc_exit();
#endif
}
MODULE_LICENSE("GPL");
module_init(init_sunrpc);
module_exit(cleanup_sunrpc);
