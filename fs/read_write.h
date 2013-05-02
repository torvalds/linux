/*
 * This file is only for sharing some helpers from read_write.c with compat.c.
 * Don't use anywhere else.
 */


typedef ssize_t (*io_fn_t)(struct file *, char __user *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct kiocb *, const struct iovec *,
		unsigned long, loff_t);
