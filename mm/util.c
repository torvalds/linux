#include <linux/slab.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/err.h>
#include <asm/uaccess.h>

/**
 * __kzalloc - allocate memory. The memory is set to zero.
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 */
void *__kzalloc(size_t size, gfp_t flags)
{
	void *ret = ____kmalloc(size, flags);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
EXPORT_SYMBOL(__kzalloc);

/*
 * kstrdup - allocate space for and copy an existing string
 *
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 */
char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	buf = ____kmalloc(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}
EXPORT_SYMBOL(kstrdup);

/*
 * strndup_user - duplicate an existing string from user space
 *
 * @s: The string to duplicate
 * @n: Maximum number of bytes to copy, including the trailing NUL.
 */
char *strndup_user(const char __user *s, long n)
{
	char *p;
	long length;

	length = strnlen_user(s, n);

	if (!length)
		return ERR_PTR(-EFAULT);

	if (length > n)
		return ERR_PTR(-EINVAL);

	p = kmalloc(length, GFP_KERNEL);

	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, s, length)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	p[length - 1] = '\0';

	return p;
}
EXPORT_SYMBOL(strndup_user);
