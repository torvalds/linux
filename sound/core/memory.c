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
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_to_user(dst, (const void __force*)src, count) ? -EFAULT : 0;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		memcpy_fromio(buf, (void __iomem *)src, c);
		if (copy_to_user(dst, buf, c))
			return -EFAULT;
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}
EXPORT_SYMBOL(copy_to_user_fromio);

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
#if defined(__i386__) || defined(CONFIG_SPARC32)
	return copy_from_user((void __force *)dst, src, count) ? -EFAULT : 0;
#else
	char buf[256];
	while (count) {
		size_t c = count;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (copy_from_user(buf, src, c))
			return -EFAULT;
		memcpy_toio(dst, buf, c);
		count -= c;
		dst += c;
		src += c;
	}
	return 0;
#endif
}
EXPORT_SYMBOL(copy_from_user_toio);
