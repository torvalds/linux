#include <linux/slab.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/err.h>
#include <asm/uaccess.h>

/**
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
	buf = kmalloc_track_caller(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}
EXPORT_SYMBOL(kstrdup);

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 */
void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc_track_caller(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}
EXPORT_SYMBOL(kmemdup);

/**
 * krealloc - reallocate memory. The contents will remain unchanged.
 * @p: object to reallocate memory for.
 * @new_size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * The contents of the object pointed to are preserved up to the
 * lesser of the new and old sizes.  If @p is %NULL, krealloc()
 * behaves exactly like kmalloc().  If @size is 0 and @p is not a
 * %NULL pointer, the object pointed to is freed.
 */
void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	void *ret;
	size_t ks;

	if (unlikely(!new_size)) {
		kfree(p);
		return ZERO_SIZE_PTR;
	}

	ks = ksize(p);
	if (ks >= new_size)
		return (void *)p;

	ret = kmalloc_track_caller(new_size, flags);
	if (ret) {
		memcpy(ret, p, min(new_size, ks));
		kfree(p);
	}
	return ret;
}
EXPORT_SYMBOL(krealloc);

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
