// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for ext4 directory hash computation.
 */

#include <kunit/test.h>
#include <kunit/resource.h>
#include <linux/fs.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unicode.h>
#include "ext4.h"

static void ext4_hash_init_fake_dir(struct inode *dir, struct super_block *sb)
{
	memset(sb, 0, sizeof(*sb));
	memset(dir, 0, sizeof(*dir));
	dir->i_sb = sb;
	strscpy(sb->s_id, "kunit-ext4", sizeof(sb->s_id));
}

static void ext4_hash_init_fake_dir_with_sbi(struct inode *dir,
					     struct super_block *sb,
					     struct ext4_sb_info *sbi)
{
	ext4_hash_init_fake_dir(dir, sb);
	memset(sbi, 0, sizeof(*sbi));
	sb->s_fs_info = sbi;
	sbi->s_sb = sb;
}

#ifdef CONFIG_FS_ENCRYPTION
static const struct fscrypt_operations ext4_hash_test_cryptops = {
	.inode_info_offs =
		(int)offsetof(struct ext4_inode_info, i_crypt_info) -
		(int)offsetof(struct ext4_inode_info, vfs_inode),
};
#endif

static void ext4_hash_init_fake_ext4_dir(struct ext4_inode_info *ei,
					 struct super_block *sb,
					 struct ext4_sb_info *sbi)
{
	struct inode *dir = &ei->vfs_inode;

	memset(sb, 0, sizeof(*sb));
	memset(ei, 0, sizeof(*ei));
	memset(sbi, 0, sizeof(*sbi));

	strscpy(sb->s_id, "kunit-ext4", sizeof(sb->s_id));
	sb->s_fs_info = sbi;
	sbi->s_sb = sb;

	dir->i_sb = sb;
	dir->i_mode = S_IFDIR;

#ifdef CONFIG_FS_ENCRYPTION
	fscrypt_set_ops(sb, &ext4_hash_test_cryptops);
#endif
}

struct ext4_dirhash_test_case {
	const char *name;
	u32 hash_version;
	const char *input;
	int len;
	u32 seed[4];
	bool use_seed;
	u32 expected_hash;
	u32 expected_minor_hash;
};

static const struct ext4_dirhash_test_case ext4_dirhash_test_cases[] = {
	{
		.name = "legacy_abc",
		.hash_version = DX_HASH_LEGACY,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0x75afd992,
		.expected_minor_hash = 0x00000000,
	},
	{
		.name = "legacy_unsigned_abc",
		.hash_version = DX_HASH_LEGACY_UNSIGNED,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0x75afd992,
		.expected_minor_hash = 0x00000000,
	},
	{
		.name = "half_md4_abc",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0xd196a868,
		.expected_minor_hash = 0xc420eb28,
	},
	{
		.name = "half_md4_unsigned_abc",
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0xd196a868,
		.expected_minor_hash = 0xc420eb28,
	},
	{
		.name = "tea_abc",
		.hash_version = DX_HASH_TEA,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0xb1435ec4,
		.expected_minor_hash = 0x3f7eaa0e,
	},
	{
		.name = "tea_unsigned_abc",
		.hash_version = DX_HASH_TEA_UNSIGNED,
		.input = "abc",
		.len = 3,
		.use_seed = false,
		.expected_hash = 0xb1435ec4,
		.expected_minor_hash = 0x3f7eaa0e,
	},
	{
		.name = "empty_half_md4",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "",
		.len = 0,
		.use_seed = false,
		.expected_hash = 0xefcdab88,
		.expected_minor_hash = 0x98badcfe,
	},
	{
		.name = "half_md4_31bytes",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "1234567890123456789012345678901",
		.len = 31,
		.use_seed = false,
		.expected_hash = 0xc4db1f78,
		.expected_minor_hash = 0xea23921b,
	},
	{
		.name = "half_md4_32bytes",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "12345678901234567890123456789012",
		.len = 32,
		.use_seed = false,
		.expected_hash = 0xfa6cc63e,
		.expected_minor_hash = 0x2f77bd1c,
	},
	{
		.name = "half_md4_33bytes",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "123456789012345678901234567890123",
		.len = 33,
		.use_seed = false,
		.expected_hash = 0xdc0c2dec,
		.expected_minor_hash = 0x5ca23365,
	},
	{
		.name = "half_md4_unsigned_31bytes",
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
		.input = "1234567890123456789012345678901",
		.len = 31,
		.use_seed = false,
		.expected_hash = 0xc4db1f78,
		.expected_minor_hash = 0xea23921b,
	},
	{
		.name = "half_md4_unsigned_32bytes",
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
		.input = "12345678901234567890123456789012",
		.len = 32,
		.use_seed = false,
		.expected_hash = 0xfa6cc63e,
		.expected_minor_hash = 0x2f77bd1c,
	},
	{
		.name = "half_md4_unsigned_33bytes",
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
		.input = "123456789012345678901234567890123",
		.len = 33,
		.use_seed = false,
		.expected_hash = 0xdc0c2dec,
		.expected_minor_hash = 0x5ca23365,
	},
	{
		.name = "tea_15bytes",
		.hash_version = DX_HASH_TEA,
		.input = "123456789abcdef",
		.len = 15,
		.use_seed = false,
		.expected_hash = 0xa562903a,
		.expected_minor_hash = 0x6174a00f,
	},
	{
		.name = "tea_16bytes",
		.hash_version = DX_HASH_TEA,
		.input = "1234567890abcdef",
		.len = 16,
		.use_seed = false,
		.expected_hash = 0x8449f258,
		.expected_minor_hash = 0x49a16d46,
	},
	{
		.name = "tea_17bytes",
		.hash_version = DX_HASH_TEA,
		.input = "123456789abcdefgh",
		.len = 17,
		.use_seed = false,
		.expected_hash = 0xf32ec10c,
		.expected_minor_hash = 0x58ceae61,
	},
	{
		.name = "half_md4_seeded",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "same-name",
		.len = 9,
		.seed = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 },
		.use_seed = true,
		.expected_hash = 0x8aebf604,
		.expected_minor_hash = 0x66ce48fe,
	},
	{
		.name = "half_md4_non_ascii_signed",
		.hash_version = DX_HASH_HALF_MD4,
		.input = "\x80\x81\x82\x83\x84",
		.len = 5,
		.use_seed = false,
		.expected_hash = 0x8bab0498,
		.expected_minor_hash = 0xc326632d,
	},
	{
		.name = "half_md4_non_ascii_unsigned",
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
		.input = "\x80\x81\x82\x83\x84",
		.len = 5,
		.use_seed = false,
		.expected_hash = 0xbc48596e,
		.expected_minor_hash = 0xde0fad41,
	},
	{
		.name = "tea_non_ascii_signed",
		.hash_version = DX_HASH_TEA,
		.input = "\x80\x81\x82\x83\x84",
		.len = 5,
		.use_seed = false,
		.expected_hash = 0x21e3a154,
		.expected_minor_hash = 0x90112c3d,
	},
	{
		.name = "tea_non_ascii_unsigned",
		.hash_version = DX_HASH_TEA_UNSIGNED,
		.input = "\x80\x81\x82\x83\x84",
		.len = 5,
		.use_seed = false,
		.expected_hash = 0x9b648616,
		.expected_minor_hash = 0x011dd507,
	},
};

static void test_ext4fs_dirhash_vectors(struct kunit *test)
{
	struct super_block *sb;
	struct inode *dir;
	int i;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	dir = kunit_kzalloc(test, sizeof(*dir), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, dir);

	ext4_hash_init_fake_dir(dir, sb);

	for (i = 0; i < ARRAY_SIZE(ext4_dirhash_test_cases); i++) {
		const struct ext4_dirhash_test_case *tc =
			&ext4_dirhash_test_cases[i];
		struct dx_hash_info hinfo;
		int ret;

		memset(&hinfo, 0, sizeof(hinfo));
		hinfo.hash_version = tc->hash_version;
		hinfo.seed = tc->use_seed ? (u32 *)tc->seed : NULL;

		ret = ext4fs_dirhash(dir, tc->input, tc->len, &hinfo);

		KUNIT_ASSERT_EQ_MSG(test, ret, 0, "case=%s", tc->name);
		KUNIT_EXPECT_EQ_MSG(test, hinfo.hash, tc->expected_hash,
				    "case=%s", tc->name);
		KUNIT_EXPECT_EQ_MSG(test, hinfo.minor_hash,
				    tc->expected_minor_hash,
				    "case=%s", tc->name);
	}
}

static void test_ext4fs_dirhash_seed_changes_result(struct kunit *test)
{
	struct super_block *sb;
	struct inode *dir;
	u32 seed[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
	struct dx_hash_info plain = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	struct dx_hash_info seeded = {
		.hash_version = DX_HASH_HALF_MD4,
		.seed = seed,
	};
	int ret_plain, ret_seeded;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	dir = kunit_kzalloc(test, sizeof(*dir), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, dir);

	ext4_hash_init_fake_dir(dir, sb);

	ret_plain = ext4fs_dirhash(dir, "same-name", 9, &plain);
	ret_seeded = ext4fs_dirhash(dir, "same-name", 9, &seeded);

	KUNIT_ASSERT_EQ(test, ret_plain, 0);
	KUNIT_ASSERT_EQ(test, ret_seeded, 0);

	KUNIT_EXPECT_TRUE(test,
			  plain.hash != seeded.hash ||
			  plain.minor_hash != seeded.minor_hash);
}

static void test_ext4fs_dirhash_invalid_version_returns_einval(struct kunit *test)
{
	struct super_block *sb;
	struct inode *dir;
	struct ext4_sb_info *sbi;
	struct dx_hash_info hinfo = {
		.hash = 0xdeadbeef,
		.minor_hash = 0xcafebabe,
		.hash_version = DX_HASH_LAST + 1,
	};
	int ret;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	dir = kunit_kzalloc(test, sizeof(*dir), GFP_KERNEL);
	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, dir);
	KUNIT_ASSERT_NOT_NULL(test, sbi);

	ext4_hash_init_fake_dir_with_sbi(dir, sb, sbi);

	ret = ext4fs_dirhash(dir, "abc", 3, &hinfo);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_EQ(test, hinfo.hash, 0);
	KUNIT_EXPECT_EQ(test, hinfo.minor_hash, 0);
}

static void test_ext4fs_dirhash_siphash_without_key_returns_einval(struct kunit *test)
{
	struct super_block *sb;
	struct ext4_inode_info *ei;
	struct inode *dir;
	struct ext4_sb_info *sbi;
	struct dx_hash_info hinfo = {
		.hash_version = DX_HASH_SIPHASH,
	};
	int ret;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	ei = kunit_kzalloc(test, sizeof(*ei), GFP_KERNEL);
	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, ei);
	KUNIT_ASSERT_NOT_NULL(test, sbi);

	ext4_hash_init_fake_ext4_dir(ei, sb, sbi);
	dir = &ei->vfs_inode;

	ret = ext4fs_dirhash(dir, "name", strlen("name"), &hinfo);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void test_ext4fs_dirhash_signed_unsigned_differ_on_nonascii(struct kunit *test)
{
	struct super_block *sb;
	struct inode *dir;
	static const char input[] = "\x80\xff\x81\xfe\101bc";
	struct dx_hash_info legacy_signed = {
		.hash_version = DX_HASH_LEGACY,
	};
	struct dx_hash_info legacy_unsigned = {
		.hash_version = DX_HASH_LEGACY_UNSIGNED,
	};
	struct dx_hash_info md4_signed = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	struct dx_hash_info md4_unsigned = {
		.hash_version = DX_HASH_HALF_MD4_UNSIGNED,
	};
	struct dx_hash_info tea_signed = {
		.hash_version = DX_HASH_TEA,
	};
	struct dx_hash_info tea_unsigned = {
		.hash_version = DX_HASH_TEA_UNSIGNED,
	};
	int ret;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	dir = kunit_kzalloc(test, sizeof(*dir), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, dir);

	ext4_hash_init_fake_dir(dir, sb);

	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &legacy_signed);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &legacy_unsigned);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_NE(test, legacy_signed.hash, legacy_unsigned.hash);

	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &md4_signed);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &md4_unsigned);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test,
			  md4_signed.hash != md4_unsigned.hash ||
			  md4_signed.minor_hash != md4_unsigned.minor_hash);

	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &tea_signed);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = ext4fs_dirhash(dir, input, sizeof(input) - 1, &tea_unsigned);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test,
			  tea_signed.hash != tea_unsigned.hash ||
			  tea_signed.minor_hash != tea_unsigned.minor_hash);
}

#if IS_ENABLED(CONFIG_UNICODE)
KUNIT_DEFINE_ACTION_WRAPPER(utf8_unload_action, utf8_unload,
			    struct unicode_map *);
static void test_ext4fs_dirhash_casefolded_names_hash_consistently(struct kunit *test)
{
	struct super_block *sb;
	struct ext4_inode_info *ei;
	struct ext4_sb_info *sbi;
	struct unicode_map *um;
	struct dx_hash_info h1 = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	struct dx_hash_info h2 = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	int ret, ret1, ret2;

	sb = kunit_kzalloc(test, sizeof(*sb), GFP_KERNEL);
	ei = kunit_kzalloc(test, sizeof(*ei), GFP_KERNEL);
	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb);
	KUNIT_ASSERT_NOT_NULL(test, ei);
	KUNIT_ASSERT_NOT_NULL(test, sbi);

	um = utf8_load(UTF8_LATEST);
	if (IS_ERR(um)) {
		kunit_skip(test, "utf8_load(UTF8_LATEST) failed: %pe",
			   um);
		return;
	}

	ret = kunit_add_action_or_reset(test, utf8_unload_action, um);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ext4_hash_init_fake_ext4_dir(ei, sb, sbi);
	sb->s_encoding = um;
	ei->vfs_inode.i_flags |= S_CASEFOLD;

	KUNIT_ASSERT_TRUE(test, IS_CASEFOLDED(&ei->vfs_inode));

	ret1 = ext4fs_dirhash(&ei->vfs_inode, "Alpha", 5, &h1);
	ret2 = ext4fs_dirhash(&ei->vfs_inode, "aLPHa", 5, &h2);

	KUNIT_ASSERT_EQ(test, ret1, 0);
	KUNIT_ASSERT_EQ(test, ret2, 0);
	KUNIT_EXPECT_EQ(test, h1.hash, h2.hash);
	KUNIT_EXPECT_EQ(test, h1.minor_hash, h2.minor_hash);
}

static void test_ext4fs_dirhash_casefold_fallback(struct kunit *test)
{
	struct super_block *sb_cf, *sb_plain;
	struct ext4_inode_info *ei;
	struct ext4_sb_info *sbi;
	struct inode *plain_dir;
	struct unicode_map *um;
	static const char invalid_utf8[] = "\xc3\x28";
	struct dx_hash_info folded_dir = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	struct dx_hash_info plain = {
		.hash_version = DX_HASH_HALF_MD4,
	};
	int ret, ret_cf, ret_plain;

	sb_cf = kunit_kzalloc(test, sizeof(*sb_cf), GFP_KERNEL);
	sb_plain = kunit_kzalloc(test, sizeof(*sb_plain), GFP_KERNEL);
	ei = kunit_kzalloc(test, sizeof(*ei), GFP_KERNEL);
	sbi = kunit_kzalloc(test, sizeof(*sbi), GFP_KERNEL);
	plain_dir = kunit_kzalloc(test, sizeof(*plain_dir), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sb_cf);
	KUNIT_ASSERT_NOT_NULL(test, sb_plain);
	KUNIT_ASSERT_NOT_NULL(test, ei);
	KUNIT_ASSERT_NOT_NULL(test, sbi);
	KUNIT_ASSERT_NOT_NULL(test, plain_dir);

	um = utf8_load(UTF8_LATEST);
	if (IS_ERR(um)) {
		kunit_skip(test, "utf8_load(UTF8_LATEST) failed: %pe",
			   um);
		return;
	}

	ret = kunit_add_action_or_reset(test, utf8_unload_action, um);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ext4_hash_init_fake_ext4_dir(ei, sb_cf, sbi);
	sb_cf->s_encoding = um;
	ei->vfs_inode.i_flags |= S_CASEFOLD;

	KUNIT_ASSERT_TRUE(test, IS_CASEFOLDED(&ei->vfs_inode));

	ext4_hash_init_fake_dir(plain_dir, sb_plain);

	ret_cf = ext4fs_dirhash(&ei->vfs_inode, invalid_utf8,
				sizeof(invalid_utf8) - 1, &folded_dir);
	ret_plain = ext4fs_dirhash(plain_dir, invalid_utf8,
				   sizeof(invalid_utf8) - 1, &plain);

	KUNIT_ASSERT_EQ(test, ret_cf, 0);
	KUNIT_ASSERT_EQ(test, ret_plain, 0);
	KUNIT_EXPECT_EQ(test, folded_dir.hash, plain.hash);
	KUNIT_EXPECT_EQ(test, folded_dir.minor_hash, plain.minor_hash);
}
#endif

static struct kunit_case ext4_hash_test_cases[] = {
	KUNIT_CASE(test_ext4fs_dirhash_vectors),
	KUNIT_CASE(test_ext4fs_dirhash_seed_changes_result),
	KUNIT_CASE(test_ext4fs_dirhash_invalid_version_returns_einval),
	KUNIT_CASE(test_ext4fs_dirhash_siphash_without_key_returns_einval),
	KUNIT_CASE(test_ext4fs_dirhash_signed_unsigned_differ_on_nonascii),
#if IS_ENABLED(CONFIG_UNICODE)
	KUNIT_CASE(test_ext4fs_dirhash_casefolded_names_hash_consistently),
	KUNIT_CASE(test_ext4fs_dirhash_casefold_fallback),
#endif
	{}
};

static struct kunit_suite ext4_hash_test_suite = {
	.name = "ext4_hash",
	.test_cases = ext4_hash_test_cases,
};

kunit_test_suites(&ext4_hash_test_suite);

MODULE_LICENSE("GPL");
