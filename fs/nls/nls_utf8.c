/*
 * Module for handling utf8 just like any other charset.
 * By Urban Widmark 2000
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/errno.h>

static unsigned char identity[256];

static int uni2char(wchar_t uni, unsigned char *out, int boundlen)
{
	int n;

	if (boundlen <= 0)
		return -ENAMETOOLONG;

	n = utf32_to_utf8(uni, out, boundlen);
	if (n < 0) {
		*out = '?';
		return -EINVAL;
	}
	return n;
}

static int char2uni(const unsigned char *rawstring, int boundlen, wchar_t *uni)
{
	int n;
	unicode_t u;

	n = utf8_to_utf32(rawstring, boundlen, &u);
	if (n < 0 || u > MAX_WCHAR_T) {
		*uni = 0x003f;	/* ? */
		return -EINVAL;
	}
	*uni = (wchar_t) u;
	return n;
}

static struct nls_table table = {
	.charset	= "utf8",
	.uni2char	= uni2char,
	.char2uni	= char2uni,
	.charset2lower	= identity,	/* no conversion */
	.charset2upper	= identity,
};

static int __init init_nls_utf8(void)
{
	int i;
	for (i=0; i<256; i++)
		identity[i] = i;

        return register_nls(&table);
}

static void __exit exit_nls_utf8(void)
{
        unregister_nls(&table);
}

module_init(init_nls_utf8)
module_exit(exit_nls_utf8)
MODULE_DESCRIPTION("NLS UTF-8");
MODULE_LICENSE("Dual BSD/GPL");
