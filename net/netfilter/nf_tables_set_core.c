/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <net/netfilter/nf_tables_core.h>

static int __init nf_tables_set_module_init(void)
{
	nft_register_set(&nft_set_hash_fast_type);
	nft_register_set(&nft_set_hash_type);
	nft_register_set(&nft_set_rhash_type);
	nft_register_set(&nft_set_bitmap_type);
	nft_register_set(&nft_set_rbtree_type);
	nft_register_set(&nft_set_pipapo_type);

	return 0;
}

static void __exit nf_tables_set_module_exit(void)
{
	nft_unregister_set(&nft_set_pipapo_type);
	nft_unregister_set(&nft_set_rbtree_type);
	nft_unregister_set(&nft_set_bitmap_type);
	nft_unregister_set(&nft_set_rhash_type);
	nft_unregister_set(&nft_set_hash_type);
	nft_unregister_set(&nft_set_hash_fast_type);
}

module_init(nf_tables_set_module_init);
module_exit(nf_tables_set_module_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NFT_SET();
