/*
 * LIRC base driver
 *
 * by Artur Lipowski <alipowski@interia.pl>
 *        This code is licensed under GNU GPL
 *
 */

#ifndef _LINUX_LIRC_DEV_H
#define _LINUX_LIRC_DEV_H

#define BUFLEN            16

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <media/lirc.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct lirc_buffer {
	wait_queue_head_t wait_poll;
	spinlock_t fifo_lock;
	unsigned int chunk_size;
	unsigned int size; /* in chunks */
	/* Using chunks instead of bytes pretends to simplify boundary checking
	 * And should allow for some performance fine tunning later */
	struct kfifo fifo;
};

static inline void lirc_buffer_clear(struct lirc_buffer *buf)
{
	unsigned long flags;

	if (kfifo_initialized(&buf->fifo)) {
		spin_lock_irqsave(&buf->fifo_lock, flags);
		kfifo_reset(&buf->fifo);
		spin_unlock_irqrestore(&buf->fifo_lock, flags);
	} else
		WARN(1, "calling %s on an uninitialized lirc_buffer\n",
		     __func__);
}

static inline int lirc_buffer_init(struct lirc_buffer *buf,
				    unsigned int chunk_size,
				    unsigned int size)
{
	int ret;

	init_waitqueue_head(&buf->wait_poll);
	spin_lock_init(&buf->fifo_lock);
	buf->chunk_size = chunk_size;
	buf->size = size;
	ret = kfifo_alloc(&buf->fifo, size * chunk_size, GFP_KERNEL);

	return ret;
}

static inline void lirc_buffer_free(struct lirc_buffer *buf)
{
	if (kfifo_initialized(&buf->fifo)) {
		kfifo_free(&buf->fifo);
	} else
		WARN(1, "calling %s on an uninitialized lirc_buffer\n",
		     __func__);
}

static inline int lirc_buffer_len(struct lirc_buffer *buf)
{
	int len;
	unsigned long flags;

	spin_lock_irqsave(&buf->fifo_lock, flags);
	len = kfifo_len(&buf->fifo);
	spin_unlock_irqrestore(&buf->fifo_lock, flags);

	return len;
}

static inline int lirc_buffer_full(struct lirc_buffer *buf)
{
	return lirc_buffer_len(buf) == buf->size * buf->chunk_size;
}

static inline int lirc_buffer_empty(struct lirc_buffer *buf)
{
	return !lirc_buffer_len(buf);
}

static inline unsigned int lirc_buffer_read(struct lirc_buffer *buf,
					    unsigned char *dest)
{
	unsigned int ret = 0;

	if (lirc_buffer_len(buf) >= buf->chunk_size)
		ret = kfifo_out_locked(&buf->fifo, dest, buf->chunk_size,
				       &buf->fifo_lock);
	return ret;

}

static inline unsigned int lirc_buffer_write(struct lirc_buffer *buf,
					     unsigned char *orig)
{
	unsigned int ret;

	ret = kfifo_in_locked(&buf->fifo, orig, buf->chunk_size,
			      &buf->fifo_lock);

	return ret;
}

/**
 * struct lirc_dev - represents a LIRC device
 *
 * @name:		used for logging
 * @minor:		the minor device (/dev/lircX) number for the device
 * @code_length:	length of a remote control key code expressed in bits
 * @features:		lirc compatible hardware features, like LIRC_MODE_RAW,
 *			LIRC_CAN\_\*, as defined at include/media/lirc.h.
 * @buffer_size:	Number of FIFO buffers with @chunk_size size.
 *			Only used if @rbuf is NULL.
 * @chunk_size:		Size of each FIFO buffer.
 *			Only used if @rbuf is NULL.
 * @data:		private per-driver data
 * @buf:		if %NULL, lirc_dev will allocate and manage the buffer,
 *			otherwise allocated by the caller which will
 *			have to write to the buffer by other means, like irq's
 *			(see also lirc_serial.c).
 * @buf_internal:	whether lirc_dev has allocated the read buffer or not
 * @rdev:		&struct rc_dev associated with the device
 * @fops:		&struct file_operations for the device
 * @owner:		the module owning this struct
 * @attached:		if the device is still live
 * @open:		open count for the device's chardev
 * @mutex:		serialises file_operations calls
 * @dev:		&struct device assigned to the device
 * @cdev:		&struct cdev assigned to the device
 */
struct lirc_dev {
	char name[40];
	unsigned int minor;
	__u32 code_length;
	__u32 features;

	unsigned int buffer_size; /* in chunks holding one code each */
	unsigned int chunk_size;
	struct lirc_buffer *buf;
	bool buf_internal;

	void *data;
	struct rc_dev *rdev;
	const struct file_operations *fops;
	struct module *owner;

	bool attached;
	int open;

	struct mutex mutex; /* protect from simultaneous accesses */

	struct device dev;
	struct cdev cdev;
};

struct lirc_dev *lirc_allocate_device(void);

void lirc_free_device(struct lirc_dev *d);

int lirc_register_device(struct lirc_dev *d);

void lirc_unregister_device(struct lirc_dev *d);

/* Must be called in the open fop before lirc_get_pdata() can be used */
void lirc_init_pdata(struct inode *inode, struct file *file);

/* Returns the private data stored in the lirc_dev
 * associated with the given device file pointer.
 */
void *lirc_get_pdata(struct file *file);

/* default file operations
 * used by drivers if they override only some operations
 */
int lirc_dev_fop_open(struct inode *inode, struct file *file);
int lirc_dev_fop_close(struct inode *inode, struct file *file);
__poll_t lirc_dev_fop_poll(struct file *file, poll_table *wait);
long lirc_dev_fop_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
ssize_t lirc_dev_fop_read(struct file *file, char __user *buffer, size_t length,
			  loff_t *ppos);
#endif
