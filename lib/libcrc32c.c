/* 
 * CRC32C
 *@Article{castagnoli-crc,
 * author =       { Guy Castagnoli and Stefan Braeuer and Martin Herrman},
 * title =        {{Optimization of Cyclic Redundancy-Check Codes with 24
 *                 and 32 Parity Bits}},
 * journal =      IEEE Transactions on Communication,
 * year =         {1993},
 * volume =       {41},
 * number =       {6},
 * pages =        {},
 * month =        {June},
 *}
 * Used by the iSCSI driver, possibly others, and derived from the
 * the iscsi-crc.c module of the linux-iscsi driver at
 * http://linux-iscsi.sourceforge.net.
 *
 * Following the example of lib/crc32, this function is intended to be
 * flexible and useful for all users.  Modules that currently have their
 * own crc32c, but hopefully may be able to use this one are:
 *  net/sctp (please add all your doco to here if you change to
 *            use this one!)
 *  <endoflist>
 *
 * Copyright (c) 2004 Cisco Systems, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static struct crypto_shash *tfm;

u32 crc32c(u32 crc, const void *address, unsigned int length)
{
	SHASH_DESC_ON_STACK(shash, tfm);
	u32 ret, *ctx = (u32 *)shash_desc_ctx(shash);
	int err;

	shash->tfm = tfm;
	shash->flags = 0;
	*ctx = crc;

	err = crypto_shash_update(shash, address, length);
	BUG_ON(err);

	ret = *ctx;
	barrier_data(ctx);
	return ret;
}

EXPORT_SYMBOL(crc32c);

static int __init libcrc32c_mod_init(void)
{
	tfm = crypto_alloc_shash("crc32c", 0, 0);
	return PTR_ERR_OR_ZERO(tfm);
}

static void __exit libcrc32c_mod_fini(void)
{
	crypto_free_shash(tfm);
}

module_init(libcrc32c_mod_init);
module_exit(libcrc32c_mod_fini);

MODULE_AUTHOR("Clay Haapala <chaapala@cisco.com>");
MODULE_DESCRIPTION("CRC32c (Castagnoli) calculations");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crc32c");
