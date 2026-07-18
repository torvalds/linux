/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FS_FUSE_DEV_H
#define _FS_FUSE_DEV_H

#include <linux/cleanup.h>

/** Maximum number of outstanding background requests */
#define FUSE_DEFAULT_MAX_BACKGROUND 12

struct fuse_conn;
struct fuse_chan;
struct fuse_dev;
struct fuse_args;
struct fuse_copy_state;
struct fuse_backing_map;
struct file;
struct folio;
enum fuse_notify_code;

struct fuse_chan_param {
	unsigned int minor;
	unsigned int max_write;
	unsigned int max_pages;
};

struct fuse_chan *fuse_chan_new(void);
struct fuse_chan *fuse_dev_chan_new(void);
void fuse_chan_release(struct fuse_chan *fch);
void fuse_chan_free(struct fuse_chan *fch);
unsigned int fuse_chan_num_background(struct fuse_chan *fch);
unsigned int fuse_chan_max_background(struct fuse_chan *fch);
void fuse_chan_max_background_set(struct fuse_chan *fch, unsigned int val);
unsigned int fuse_chan_num_waiting(struct fuse_chan *fch);
void fuse_chan_set_fc(struct fuse_chan *fch, struct fuse_conn *fc);
void fuse_chan_set_initialized(struct fuse_chan *fch, struct fuse_chan_param *param);
void fuse_chan_io_uring_enable(struct fuse_chan *fch);
ssize_t fuse_chan_send(struct fuse_chan *fch, struct fuse_args *args);
int fuse_chan_send_bg(struct fuse_chan *fch, struct fuse_args *args, gfp_t gfp_flags);
int fuse_chan_send_notify_reply(struct fuse_chan *fch, struct fuse_args *args, u64 unique);
void fuse_chan_resend(struct fuse_chan *fch);

struct fuse_forget_link *fuse_alloc_forget(void);
void fuse_chan_queue_forget(struct fuse_chan *fch, struct fuse_forget_link *forget,
			    u64 nodeid, u64 nlookup);

DEFINE_FREE(fuse_chan_free, struct fuse_chan *, if (_T) fuse_chan_free(_T))

/**
 * Initialize the client device
 */
int fuse_dev_init(void);

/**
 * Cleanup the client device
 */
void fuse_dev_cleanup(void);

void fuse_dev_install(struct fuse_dev *fud, struct fuse_chan *fch);
bool fuse_dev_verify(struct fuse_dev *fud, struct fuse_chan *fch);
void fuse_dev_put(struct fuse_dev *fud);
bool fuse_dev_is_installed(struct fuse_dev *fud);
bool fuse_dev_is_sync_init(struct fuse_dev *fud);
struct fuse_dev *fuse_dev_grab(struct file *file);

void fuse_init_server_timeout(struct fuse_chan *fch, unsigned int timeout);

/* Abort all requests */
void fuse_chan_abort(struct fuse_chan *fch, bool abort_with_err);
void fuse_chan_wait_aborted(struct fuse_chan *fch);

/**
 * Acquire reference to fuse_conn
 */
struct fuse_conn *fuse_conn_get(struct fuse_conn *fc);

/**
 * Release reference to fuse_conn
 */
void fuse_conn_put(struct fuse_conn *fc);

dev_t fuse_conn_get_id(struct fuse_conn *fc);

void fuse_end_polls(struct fuse_conn *fc);
int fuse_notify(struct fuse_conn *fc, enum fuse_notify_code code,
		unsigned int size, struct fuse_copy_state *cs);

int fuse_backing_open(struct fuse_conn *fc, struct fuse_backing_map *map);
int fuse_backing_close(struct fuse_conn *fc, int backing_id);

int fuse_copy_one(struct fuse_copy_state *cs, void *val, unsigned size);
int fuse_copy_folio(struct fuse_copy_state *cs, struct folio **foliop,
		    unsigned offset, unsigned count, int zeroing);
void fuse_copy_finish(struct fuse_copy_state *cs);

#ifdef CONFIG_FUSE_IO_URING
bool fuse_uring_enabled(void);
void fuse_uring_destruct(struct fuse_chan *fch);
#else /* CONFIG_FUSE_IO_URING */
static inline bool fuse_uring_enabled(void)
{
	return false;
}

static inline void fuse_uring_destruct(struct fuse_chan *fch)
{
}
#endif /* CONFIG_FUSE_IO_URING */

#endif /* _FS_FUSE_DEV_H */
