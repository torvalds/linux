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

	if ( (n = utf8_wctomb(out, uni, boundlen)) == -1) {
		*out = '?';
		return -EINVAL;
	}
	return n;
}

static int char2uni(const unsigned char *rawstring, int boundlen, wchar_t *uni)
{
	int n;

	if ( (n = utf8_mbtowc(uni, rawstring, boundlen)) == -1) {
		*uni = 0x003f;	/* ? */
		n = -EINVAL;
	}
	return n;
}

static struct nls_table table = {
	.charset	= "utf8",
	.uni2char	= uni2char,
	.char2uni	= char2uni,
	.charset2lower	= identity,	/* no conversion */
	.charset2upper	= identity,
	.owner		= THIS_MODULE,
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
MODULE_LICENSE("Dual BSD/GPL");
