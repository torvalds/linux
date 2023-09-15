// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 * 
 *  Misc memory accessors
 */

#include <linux/export.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <sound/core.h>
#include <sound/pcm.h>

/**
 * copy_to_user_fromio - copy data from mmio-space to user-space
 * @dst: the destination pointer on user-space
 * @src: the source pointer on mmio
 * @count: the data size to copy in bytes
 *
 * Copies the data from mmio-space to user-space.
 *
 * Return: Zero if successful, or non-zero on failure.
 */
int copy_to_user_fromio(void __user *dst, const volatile void __iomem *src, size_t count)
{
	struct iov_iter iter;

	if (import_ubuf(ITER_DEST, dst, count, &iter))
		return -EFAULT;
	return copy_to_iter_fromio(&iter, (const void __iomem *)src, count);
}
EXPORT_SYMBOL(copy_to_user_fromio);

/**
 * copy_to_iter_fromio - copy data from mmio-space to iov_iter
 * @dst: the destination iov_iter
 * @src: the source pointer on mmio
 * @count: the data size to copy in bytes
 *
 * Copies the data from mmio-space to iov_iter.
 *
 * Return: Zero if successful, or non-zero on failure.
 */
int copy_to_iter_fromio(struct iov_iter *dst, const void __iomem *src,
			size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_to_iter((const void __force *)src, count, dst) == count ? 0 : -EFAULT;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		memcpy_fromio(buf, (void __iomem *)src, c);
		if (copy_to_iter(buf, c, dst) != c)
			return -EFAULT;
		count -= c;
		src += c;
	}
	return 0;
#endif
}
EXPORT_SYMBOL(copy_to_iter_fromio);

/**
 * copy_from_user_toio - copy data from user-space to mmio-space
 * @dst: the destination pointer on mmio-space
 * @src: the source pointer on user-space
 * @count: the data size to copy in bytes
 *
 * Copies the data from user-space to mmio-space.
 *
 * Return: Zero if successful, or non-zero on failure.
 */
int copy_from_user_toio(volatile void __iomem *dst, const void __user *src, size_t count)
{
	struct iov_iter iter;

	if (import_ubuf(ITER_SOURCE, (void __user *)src, count, &iter))
		return -EFAULT;
	return copy_from_iter_toio((void __iomem *)dst, &iter, count);
}
EXPORT_SYMBOL(copy_from_user_toio);

/**
 * copy_from_iter_toio - copy data from iov_iter to mmio-space
 * @dst: the destination pointer on mmio-space
 * @src: the source iov_iter
 * @count: the data size to copy in bytes
 *
 * Copies the data from iov_iter to mmio-space.
 *
 * Return: Zero if successful, or non-zero on failure.
 */
int copy_from_iter_toio(void __iomem *dst, struct iov_iter *src, size_t count)
{
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_from_iter((void __force *)dst, count, src) == count ? 0 : -EFAULT;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (copy_from_iter(buf, c, src) != c)
			return -EFAULT;
		memcpy_toio(dst, buf, c);
		count -= c;
		dst += c;
	}
	return 0;
#endif
}
EXPORT_SYMBOL(copy_from_iter_toio);
