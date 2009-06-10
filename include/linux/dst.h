/*
 * 2007+ Copyright (c) Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DST_H
#define __DST_H

#include <linux/types.h>
#include <linux/connector.h>

#define DST_NAMELEN		32
#define DST_NAME		"dst"

enum {
	/* Remove node with given id from storage */
	DST_DEL_NODE	= 0,
	/* Add remote node with given id to the storage */
	DST_ADD_REMOTE,
	/* Add local node with given id to the storage to be exported and used by remote peers */
	DST_ADD_EXPORT,
	/* Crypto initialization command (hash/cipher used to protect the connection) */
	DST_CRYPTO,
	/* Security attributes for given connection (permissions for example) */
	DST_SECURITY,
	/* Register given node in the block layer subsystem */
	DST_START,
	DST_CMD_MAX
};

struct dst_ctl
{
	/* Storage name */
	char			name[DST_NAMELEN];
	/* Command flags */
	__u32			flags;
	/* Command itself (see above) */
	__u32			cmd;
	/* Maximum number of pages per single request in this device */
	__u32			max_pages;
	/* Stale/error transaction scanning timeout in milliseconds */
	__u32			trans_scan_timeout;
	/* Maximum number of retry sends before completing transaction as broken */
	__u32			trans_max_retries;
	/* Storage size */
	__u64			size;
};

/* Reply command carries completion status */
struct dst_ctl_ack
{
	struct cn_msg		msg;
	int			error;
	int			unused[3];
};

/*
 * Unfortunaltely socket address structure is not exported to userspace
 * and is redefined there.
 */
#define SADDR_MAX_DATA	128

struct saddr {
	/* address family, AF_xxx	*/
	unsigned short		sa_family;
	/* 14 bytes of protocol address	*/
	char			sa_data[SADDR_MAX_DATA];
	/* Number of bytes used in sa_data */
	unsigned short		sa_data_len;
};

/* Address structure */
struct dst_network_ctl
{
	/* Socket type: datagram, stream...*/
	unsigned int		type;
	/* Let me guess, is it a Jupiter diameter? */
	unsigned int		proto;
	/* Peer's address */
	struct saddr		addr;
};

struct dst_crypto_ctl
{
	/* Cipher and hash names */
	char			cipher_algo[DST_NAMELEN];
	char			hash_algo[DST_NAMELEN];

	/* Key sizes. Can be zero for digest for example */
	unsigned int		cipher_keysize, hash_keysize;
	/* Alignment. Calculated by the DST itself. */
	unsigned int		crypto_attached_size;
	/* Number of threads to perform crypto operations */
	int			thread_num;
};

/* Export security attributes have this bits checked in when client connects */
#define DST_PERM_READ		(1<<0)
#define DST_PERM_WRITE		(1<<1)

/*
 * Right now it is simple model, where each remote address
 * is assigned to set of permissions it is allowed to perform.
 * In real world block device does not know anything but
 * reading and writing, so it should be more than enough.
 */
struct dst_secure_user
{
	unsigned int		permissions;
	struct saddr		addr;
};

/*
 * Export control command: device to export and network address to accept
 * clients to work with given device
 */
struct dst_export_ctl
{
	char			device[DST_NAMELEN];
	struct dst_network_ctl	ctl;
};

enum {
	DST_CFG	= 1, 		/* Request remote configuration */
	DST_IO,			/* IO command */
	DST_IO_RESPONSE,	/* IO response */
	DST_PING,		/* Keepalive message */
	DST_NCMD_MAX,
};

struct dst_cmd
{
	/* Network command itself, see above */
	__u32			cmd;
	/*
	 * Size of the attached data
	 * (in most cases, for READ command it means how many bytes were requested)
	 */
	__u32			size;
	/* Crypto size: number of attached bytes with digest/hmac */
	__u32			csize;
	/* Here we can carry secret data */
	__u32			reserved;
	/* Read/write bits, see how they are encoded in bio structure */
	__u64			rw;
	/* BIO flags */
	__u64			flags;
	/* Unique command id (like transaction ID) */
	__u64			id;
	/* Sector to start IO from */
	__u64			sector;
	/* Hash data is placed after this header */
	__u8			hash[0];
};

/*
 * Convert command to/from network byte order.
 * We do not use hton*() functions, since there is
 * no 64-bit implementation.
 */
static inline void dst_convert_cmd(struct dst_cmd *c)
{
	c->cmd = __cpu_to_be32(c->cmd);
	c->csize = __cpu_to_be32(c->csize);
	c->size = __cpu_to_be32(c->size);
	c->sector = __cpu_to_be64(c->sector);
	c->id = __cpu_to_be64(c->id);
	c->flags = __cpu_to_be64(c->flags);
	c->rw = __cpu_to_be64(c->rw);
}

/* Transaction id */
typedef __u64 dst_gen_t;

#ifdef __KERNEL__

#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/device.h>
#include <linux/mempool.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/rbtree.h>

#ifdef CONFIG_DST_DEBUG
#define dprintk(f, a...) printk(KERN_NOTICE f, ##a)
#else
static inline void __attribute__ ((format (printf, 1, 2)))
	dprintk(const char *fmt, ...) {}
#endif

struct dst_node;

struct dst_trans
{
	/* DST node we are working with */
	struct dst_node		*n;

	/* Entry inside transaction tree */
	struct rb_node		trans_entry;

	/* Merlin kills this transaction when this memory cell equals zero */
	atomic_t		refcnt;

	/* How this transaction should be processed by crypto engine */
	short			enc;
	/* How many times this transaction was resent */
	short			retries;
	/* Completion status */
	int			error;

	/* When did we send it to the remote peer */
	long			send_time;

	/* My name is...
	 * Well, computers does not speak, they have unique id instead */
	dst_gen_t		gen;

	/* Block IO we are working with */
	struct bio		*bio;

	/* Network command for above block IO request */
	struct dst_cmd		cmd;
};

struct dst_crypto_engine
{
	/* What should we do with all block requests */
	struct crypto_hash	*hash;
	struct crypto_ablkcipher	*cipher;

	/* Pool of pages used to encrypt data into before sending */
	int			page_num;
	struct page		**pages;

	/* What to do with current request */
	int			enc;
	/* Who we are and where do we go */
	struct scatterlist	*src, *dst;

	/* Maximum timeout waiting for encryption to be completed */
	long			timeout;
	/* IV is a 64-bit sequential counter */
	u64			iv;

	/* Secret data */
	void			*private;

	/* Cached temporary data lives here */
	int			size;
	void			*data;
};

struct dst_state
{
	/* The main state protection */
	struct mutex		state_lock;

	/* Polling machinery for sockets */
	wait_queue_t 		wait;
	wait_queue_head_t 	*whead;
	/* Most of events are being waited here */
	wait_queue_head_t 	thread_wait;

	/* Who owns this? */
	struct dst_node		*node;

	/* Network address for this state */
	struct dst_network_ctl	ctl;

	/* Permissions to work with: read-only or rw connection */
	u32			permissions;

	/* Called when we need to clean private data */
	void			(* cleanup)(struct dst_state *st);

	/* Used by the server: BIO completion queues BIOs here */
	struct list_head	request_list;
	spinlock_t		request_lock;

	/* Guess what? No, it is not number of planets */
	atomic_t		refcnt;

	/* This flags is set when connection should be dropped */
	int			need_exit;

	/*
	 * Socket to work with. Second pointer is used for
	 * lockless check if socket was changed before performing
	 * next action (like working with cached polling result)
	 */
	struct socket		*socket, *read_socket;

	/* Cached preallocated data */
	void			*data;
	unsigned int		size;

	/* Currently processed command */
	struct dst_cmd		cmd;
};

struct dst_info
{
	/* Device size */
	u64			size;

	/* Local device name for export devices */
	char			local[DST_NAMELEN];

	/* Network setup */
	struct dst_network_ctl	net;

	/* Sysfs bits use this */
	struct device		device;
};

struct dst_node
{
	struct list_head	node_entry;

	/* Hi, my name is stored here */
	char			name[DST_NAMELEN];
	/* My cache name is stored here */
	char			cache_name[DST_NAMELEN];

	/* Block device attached to given node.
	 * Only valid for exporting nodes */
	struct block_device 	*bdev;
	/* Network state machine for given peer */
	struct dst_state	*state;

	/* Block IO machinery */
	struct request_queue	*queue;
	struct gendisk		*disk;

	/* Number of threads in processing pool */
	int			thread_num;
	/* Maximum number of pages in single IO */
	int			max_pages;

	/* I'm that big in bytes */
	loff_t			size;

	/* Exported to userspace node information */
	struct dst_info		*info;

	/*
	 * Security attribute list.
	 * Used only by exporting node currently.
	 */
	struct list_head	security_list;
	struct mutex		security_lock;

	/*
	 * When this unerflows below zero, university collapses.
	 * But this will not happen, since node will be freed,
	 * when reference counter reaches zero.
	 */
	atomic_t		refcnt;

	/* How precisely should I be started? */
	int 			(*start)(struct dst_node *);

	/* Crypto capabilities */
	struct dst_crypto_ctl	crypto;
	u8			*hash_key;
	u8			*cipher_key;

	/* Pool of processing thread */
	struct thread_pool	*pool;

	/* Transaction IDs live here */
	atomic_long_t		gen;

	/*
	 * How frequently and how many times transaction
	 * tree should be scanned to drop stale objects.
	 */
	long			trans_scan_timeout;
	int			trans_max_retries;

	/* Small gnomes live here */
	struct rb_root		trans_root;
	struct mutex		trans_lock;

	/*
	 * Transaction cache/memory pool.
	 * It is big enough to contain not only transaction
	 * itself, but additional crypto data (digest/hmac).
	 */
	struct kmem_cache	*trans_cache;
	mempool_t		*trans_pool;

	/* This entity scans transaction tree */
	struct delayed_work 	trans_work;

	wait_queue_head_t	wait;
};

/* Kernel representation of the security attribute */
struct dst_secure
{
	struct list_head	sec_entry;
	struct dst_secure_user	sec;
};

int dst_process_bio(struct dst_node *n, struct bio *bio);

int dst_node_init_connected(struct dst_node *n, struct dst_network_ctl *r);
int dst_node_init_listened(struct dst_node *n, struct dst_export_ctl *le);

static inline struct dst_state *dst_state_get(struct dst_state *st)
{
	BUG_ON(atomic_read(&st->refcnt) == 0);
	atomic_inc(&st->refcnt);
	return st;
}

void dst_state_put(struct dst_state *st);

struct dst_state *dst_state_alloc(struct dst_node *n);
int dst_state_socket_create(struct dst_state *st);
void dst_state_socket_release(struct dst_state *st);

void dst_state_exit_connected(struct dst_state *st);

int dst_state_schedule_receiver(struct dst_state *st);

void dst_dump_addr(struct socket *sk, struct sockaddr *sa, char *str);

static inline void dst_state_lock(struct dst_state *st)
{
	mutex_lock(&st->state_lock);
}

static inline void dst_state_unlock(struct dst_state *st)
{
	mutex_unlock(&st->state_lock);
}

void dst_poll_exit(struct dst_state *st);
int dst_poll_init(struct dst_state *st);

static inline unsigned int dst_state_poll(struct dst_state *st)
{
	unsigned int revents = POLLHUP | POLLERR;

	dst_state_lock(st);
	if (st->socket)
		revents = st->socket->ops->poll(NULL, st->socket, NULL);
	dst_state_unlock(st);

	return revents;
}

static inline int dst_thread_setup(void *private, void *data)
{
	return 0;
}

void dst_node_put(struct dst_node *n);

static inline struct dst_node *dst_node_get(struct dst_node *n)
{
	atomic_inc(&n->refcnt);
	return n;
}

int dst_data_recv(struct dst_state *st, void *data, unsigned int size);
int dst_recv_cdata(struct dst_state *st, void *cdata);
int dst_data_send_header(struct socket *sock,
		void *data, unsigned int size, int more);

int dst_send_bio(struct dst_state *st, struct dst_cmd *cmd, struct bio *bio);

int dst_process_io(struct dst_state *st);
int dst_export_crypto(struct dst_node *n, struct bio *bio);
int dst_export_send_bio(struct bio *bio);
int dst_start_export(struct dst_node *n);

int __init dst_export_init(void);
void dst_export_exit(void);

/* Private structure for export block IO requests */
struct dst_export_priv
{
	struct list_head		request_entry;
	struct dst_state		*state;
	struct bio			*bio;
	struct dst_cmd			cmd;
};

static inline void dst_trans_get(struct dst_trans *t)
{
	atomic_inc(&t->refcnt);
}

struct dst_trans *dst_trans_search(struct dst_node *node, dst_gen_t gen);
int dst_trans_remove(struct dst_trans *t);
int dst_trans_remove_nolock(struct dst_trans *t);
void dst_trans_put(struct dst_trans *t);

/*
 * Convert bio into network command.
 */
static inline void dst_bio_to_cmd(struct bio *bio, struct dst_cmd *cmd,
		u32 command, u64 id)
{
	cmd->cmd = command;
	cmd->flags = (bio->bi_flags << BIO_POOL_BITS) >> BIO_POOL_BITS;
	cmd->rw = bio->bi_rw;
	cmd->size = bio->bi_size;
	cmd->csize = 0;
	cmd->id = id;
	cmd->sector = bio->bi_sector;
};

int dst_trans_send(struct dst_trans *t);
int dst_trans_crypto(struct dst_trans *t);

int dst_node_crypto_init(struct dst_node *n, struct dst_crypto_ctl *ctl);
void dst_node_crypto_exit(struct dst_node *n);

static inline int dst_need_crypto(struct dst_node *n)
{
	struct dst_crypto_ctl *c = &n->crypto;
	/*
	 * Logical OR is appropriate here, but boolean one produces
	 * more optimal code, so it is used instead.
	 */
	return (c->hash_algo[0] | c->cipher_algo[0]);
}

int dst_node_trans_init(struct dst_node *n, unsigned int size);
void dst_node_trans_exit(struct dst_node *n);

/*
 * Pool of threads.
 * Ready list contains threads currently free to be used,
 * active one contains threads with some work scheduled for them.
 * Caller can wait in given queue when thread is ready.
 */
struct thread_pool
{
	int			thread_num;
	struct mutex		thread_lock;
	struct list_head	ready_list, active_list;

	wait_queue_head_t	wait;
};

void thread_pool_del_worker(struct thread_pool *p);
void thread_pool_del_worker_id(struct thread_pool *p, unsigned int id);
int thread_pool_add_worker(struct thread_pool *p,
		char *name,
		unsigned int id,
		void *(* init)(void *data),
		void (* cleanup)(void *data),
		void *data);

void thread_pool_destroy(struct thread_pool *p);
struct thread_pool *thread_pool_create(int num, char *name,
		void *(* init)(void *data),
		void (* cleanup)(void *data),
		void *data);

int thread_pool_schedule(struct thread_pool *p,
		int (* setup)(void *stored_private, void *setup_data),
		int (* action)(void *stored_private, void *setup_data),
		void *setup_data, long timeout);
int thread_pool_schedule_private(struct thread_pool *p,
		int (* setup)(void *private, void *data),
		int (* action)(void *private, void *data),
		void *data, long timeout, void *id);

#endif /* __KERNEL__ */
#endif /* __DST_H */
