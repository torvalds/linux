// SPDX-License-Identifier: 0BSD

/*
 * XZ decoder tester
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/crc32.h>
#include <linux/xz.h>

/* Maximum supported dictionary size */
#define DICT_MAX (1 << 20)

/* Device name to pass to register_chrdev(). */
#define DEVICE_NAME "xz_dec_test"

/* Dynamically allocated device major number */
static int device_major;

/*
 * We reuse the same decoder state, and thus can decode only one
 * file at a time.
 */
static bool device_is_open;

/* XZ decoder state */
static struct xz_dec *state;

/*
 * Return value of xz_dec_run(). We need to avoid calling xz_dec_run() after
 * it has returned XZ_STREAM_END, so we make this static.
 */
static enum xz_ret ret;

/*
 * Input and output buffers. The input buffer is used as a temporary safe
 * place for the data coming from the userspace.
 */
static uint8_t buffer_in[1024];
static uint8_t buffer_out[1024];

/*
 * Structure to pass the input and output buffers to the XZ decoder.
 * A few of the fields are never modified so we initialize them here.
 */
static struct xz_buf buffers = {
	.in = buffer_in,
	.out = buffer_out,
	.out_size = sizeof(buffer_out)
};

/*
 * CRC32 of uncompressed data. This is used to give the user a simple way
 * to check that the decoder produces correct output.
 */
static uint32_t crc;

static int xz_dec_test_open(struct inode *i, struct file *f)
{
	if (device_is_open)
		return -EBUSY;

	device_is_open = true;

	xz_dec_reset(state);
	ret = XZ_OK;
	crc = 0xFFFFFFFF;

	buffers.in_pos = 0;
	buffers.in_size = 0;
	buffers.out_pos = 0;

	printk(KERN_INFO DEVICE_NAME ": opened\n");
	return 0;
}

static int xz_dec_test_release(struct inode *i, struct file *f)
{
	device_is_open = false;

	if (ret == XZ_OK)
		printk(KERN_INFO DEVICE_NAME ": input was truncated\n");

	printk(KERN_INFO DEVICE_NAME ": closed\n");
	return 0;
}

/*
 * Decode the data given to us from the userspace. CRC32 of the uncompressed
 * data is calculated and is printed at the end of successful decoding. The
 * uncompressed data isn't stored anywhere for further use.
 *
 * The .xz file must have exactly one Stream and no Stream Padding. The data
 * after the first Stream is considered to be garbage.
 */
static ssize_t xz_dec_test_write(struct file *file, const char __user *buf,
				 size_t size, loff_t *pos)
{
	size_t remaining;

	if (ret != XZ_OK) {
		if (size > 0)
			printk(KERN_INFO DEVICE_NAME ": %zu bytes of "
					"garbage at the end of the file\n",
					size);

		return -ENOSPC;
	}

	printk(KERN_INFO DEVICE_NAME ": decoding %zu bytes of input\n",
			size);

	remaining = size;
	while ((remaining > 0 || buffers.out_pos == buffers.out_size)
			&& ret == XZ_OK) {
		if (buffers.in_pos == buffers.in_size) {
			buffers.in_pos = 0;
			buffers.in_size = min(remaining, sizeof(buffer_in));
			if (copy_from_user(buffer_in, buf, buffers.in_size))
				return -EFAULT;

			buf += buffers.in_size;
			remaining -= buffers.in_size;
		}

		buffers.out_pos = 0;
		ret = xz_dec_run(state, &buffers);
		crc = crc32(crc, buffer_out, buffers.out_pos);
	}

	switch (ret) {
	case XZ_OK:
		printk(KERN_INFO DEVICE_NAME ": XZ_OK\n");
		return size;

	case XZ_STREAM_END:
		printk(KERN_INFO DEVICE_NAME ": XZ_STREAM_END, "
				"CRC32 = 0x%08X\n", ~crc);
		return size - remaining - (buffers.in_size - buffers.in_pos);

	case XZ_MEMLIMIT_ERROR:
		printk(KERN_INFO DEVICE_NAME ": XZ_MEMLIMIT_ERROR\n");
		break;

	case XZ_FORMAT_ERROR:
		printk(KERN_INFO DEVICE_NAME ": XZ_FORMAT_ERROR\n");
		break;

	case XZ_OPTIONS_ERROR:
		printk(KERN_INFO DEVICE_NAME ": XZ_OPTIONS_ERROR\n");
		break;

	case XZ_DATA_ERROR:
		printk(KERN_INFO DEVICE_NAME ": XZ_DATA_ERROR\n");
		break;

	case XZ_BUF_ERROR:
		printk(KERN_INFO DEVICE_NAME ": XZ_BUF_ERROR\n");
		break;

	default:
		printk(KERN_INFO DEVICE_NAME ": Bug detected!\n");
		break;
	}

	return -EIO;
}

/* Allocate the XZ decoder state and register the character device. */
static int __init xz_dec_test_init(void)
{
	static const struct file_operations fileops = {
		.owner = THIS_MODULE,
		.open = &xz_dec_test_open,
		.release = &xz_dec_test_release,
		.write = &xz_dec_test_write
	};

	state = xz_dec_init(XZ_PREALLOC, DICT_MAX);
	if (state == NULL)
		return -ENOMEM;

	device_major = register_chrdev(0, DEVICE_NAME, &fileops);
	if (device_major < 0) {
		xz_dec_end(state);
		return device_major;
	}

	printk(KERN_INFO DEVICE_NAME ": module loaded\n");
	printk(KERN_INFO DEVICE_NAME ": Create a device node with "
			"'mknod " DEVICE_NAME " c %d 0' and write .xz files "
			"to it.\n", device_major);
	return 0;
}

static void __exit xz_dec_test_exit(void)
{
	unregister_chrdev(device_major, DEVICE_NAME);
	xz_dec_end(state);
	printk(KERN_INFO DEVICE_NAME ": module unloaded\n");
}

module_init(xz_dec_test_init);
module_exit(xz_dec_test_exit);

MODULE_DESCRIPTION("XZ decompressor tester");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Lasse Collin <lasse.collin@tukaani.org>");
MODULE_LICENSE("Dual BSD/GPL");
