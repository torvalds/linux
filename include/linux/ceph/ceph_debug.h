/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_DEBUG_H
#define _FS_CEPH_DEBUG_H

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/string.h>

#ifdef CONFIG_CEPH_LIB_PRETTYDEBUG

/*
 * wrap pr_debug to include a filename:lineno prefix on each line.
 * this incurs some overhead (kernel size and execution time) due to
 * the extra function call at each call site.
 */

# if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
#  define dout(fmt, ...)						\
	pr_debug("%.*s %12.12s:%-4d : " fmt,				\
		 8 - (int)sizeof(KBUILD_MODNAME), "    ",		\
		 kbasename(__FILE__), __LINE__, ##__VA_ARGS__)
#  define doutc(client, fmt, ...)					\
	pr_debug("%.*s %12.12s:%-4d : [%pU %llu] " fmt,			\
		 8 - (int)sizeof(KBUILD_MODNAME), "    ",		\
		 kbasename(__FILE__), __LINE__,				\
		 &client->fsid, client->monc.auth->global_id,		\
		 ##__VA_ARGS__)
# else
/* faux printk call just to see any compiler warnings. */
#  define dout(fmt, ...)	do {				\
		if (0)						\
			printk(KERN_DEBUG fmt, ##__VA_ARGS__);	\
	} while (0)
#  define doutc(client, fmt, ...)	do {			\
		if (0)						\
			printk(KERN_DEBUG "[%pU %llu] " fmt,	\
			&client->fsid,				\
			client->monc.auth->global_id,		\
			##__VA_ARGS__);				\
		} while (0)
# endif

#else

/*
 * or, just wrap pr_debug
 */
# define dout(fmt, ...)	pr_debug(" " fmt, ##__VA_ARGS__)
# define doutc(client, fmt, ...)					\
	pr_debug(" [%pU %llu] %s: " fmt, &client->fsid,			\
		 client->monc.auth->global_id, __func__, ##__VA_ARGS__)

#endif

#define pr_notice_client(client, fmt, ...)				\
	pr_notice("[%pU %llu]: " fmt, &client->fsid,			\
		  client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_info_client(client, fmt, ...)				\
	pr_info("[%pU %llu]: " fmt, &client->fsid,			\
		client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_warn_client(client, fmt, ...)				\
	pr_warn("[%pU %llu]: " fmt, &client->fsid,			\
		client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_warn_once_client(client, fmt, ...)				\
	pr_warn_once("[%pU %llu]: " fmt, &client->fsid,			\
		     client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_err_client(client, fmt, ...)					\
	pr_err("[%pU %llu]: " fmt, &client->fsid,			\
	       client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_warn_ratelimited_client(client, fmt, ...)			\
	pr_warn_ratelimited("[%pU %llu]: " fmt, &client->fsid,		\
			    client->monc.auth->global_id, ##__VA_ARGS__)
#define pr_err_ratelimited_client(client, fmt, ...)			\
	pr_err_ratelimited("[%pU %llu]: " fmt, &client->fsid,		\
			   client->monc.auth->global_id, ##__VA_ARGS__)

#endif
