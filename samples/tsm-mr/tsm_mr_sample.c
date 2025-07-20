// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024-2005 Intel Corporation. All rights reserved. */

#define pr_fmt(x) KBUILD_MODNAME ": " x

#include <linux/module.h>
#include <linux/tsm-mr.h>
#include <linux/miscdevice.h>
#include <crypto/hash.h>

static struct {
	u8 static_mr[SHA384_DIGEST_SIZE];
	u8 config_mr[SHA512_DIGEST_SIZE];
	u8 rtmr0[SHA256_DIGEST_SIZE];
	u8 rtmr1[SHA384_DIGEST_SIZE];
	u8 report_digest[SHA512_DIGEST_SIZE];
} sample_report = {
	.static_mr = "static_mr",
	.config_mr = "config_mr",
	.rtmr0 = "rtmr0",
	.rtmr1 = "rtmr1",
};

static int sample_report_refresh(const struct tsm_measurements *tm)
{
	struct crypto_shash *tfm;
	int rc;

	tfm = crypto_alloc_shash(hash_algo_name[HASH_ALGO_SHA512], 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("crypto_alloc_shash failed: %ld\n", PTR_ERR(tfm));
		return PTR_ERR(tfm);
	}

	rc = crypto_shash_tfm_digest(tfm, (u8 *)&sample_report,
				     offsetof(typeof(sample_report),
					      report_digest),
				     sample_report.report_digest);
	crypto_free_shash(tfm);
	if (rc)
		pr_err("crypto_shash_tfm_digest failed: %d\n", rc);
	return rc;
}

static int sample_report_extend_mr(const struct tsm_measurements *tm,
				   const struct tsm_measurement_register *mr,
				   const u8 *data)
{
	SHASH_DESC_ON_STACK(desc, 0);
	int rc;

	desc->tfm = crypto_alloc_shash(hash_algo_name[mr->mr_hash], 0, 0);
	if (IS_ERR(desc->tfm)) {
		pr_err("crypto_alloc_shash failed: %ld\n", PTR_ERR(desc->tfm));
		return PTR_ERR(desc->tfm);
	}

	rc = crypto_shash_init(desc);
	if (!rc)
		rc = crypto_shash_update(desc, mr->mr_value, mr->mr_size);
	if (!rc)
		rc = crypto_shash_finup(desc, data, mr->mr_size, mr->mr_value);
	crypto_free_shash(desc->tfm);
	if (rc)
		pr_err("SHA calculation failed: %d\n", rc);
	return rc;
}

#define MR_(mr, hash) .mr_value = &sample_report.mr, TSM_MR_(mr, hash)
static const struct tsm_measurement_register sample_mrs[] = {
	/* static MR, read-only */
	{ MR_(static_mr, SHA384) },
	/* config MR, read-only */
	{ MR_(config_mr, SHA512) | TSM_MR_F_NOHASH },
	/* RTMR, direct extension prohibited */
	{ MR_(rtmr0, SHA256) | TSM_MR_F_LIVE },
	/* RTMR, direct extension allowed */
	{ MR_(rtmr1, SHA384) | TSM_MR_F_RTMR },
	/* RTMR, crypto agile, alaised to rtmr0 and rtmr1, respectively */
	{ .mr_value = &sample_report.rtmr0,
	  TSM_MR_(rtmr_crypto_agile, SHA256) | TSM_MR_F_RTMR },
	{ .mr_value = &sample_report.rtmr1,
	  TSM_MR_(rtmr_crypto_agile, SHA384) | TSM_MR_F_RTMR },
	/* sha512 digest of the whole structure */
	{ MR_(report_digest, SHA512) | TSM_MR_F_LIVE },
};
#undef MR_

static struct tsm_measurements sample_tm = {
	.mrs = sample_mrs,
	.nr_mrs = ARRAY_SIZE(sample_mrs),
	.refresh = sample_report_refresh,
	.write = sample_report_extend_mr,
};

static const struct attribute_group *sample_groups[] = {
	NULL,
	NULL,
};

static struct miscdevice sample_misc_dev = {
	.name = KBUILD_MODNAME,
	.minor = MISC_DYNAMIC_MINOR,
	.groups = sample_groups,
};

static int __init tsm_mr_sample_init(void)
{
	int rc;

	sample_groups[0] = tsm_mr_create_attribute_group(&sample_tm);
	if (IS_ERR(sample_groups[0]))
		return PTR_ERR(sample_groups[0]);

	rc = misc_register(&sample_misc_dev);
	if (rc)
		tsm_mr_free_attribute_group(sample_groups[0]);
	return rc;
}

static void __exit tsm_mr_sample_exit(void)
{
	misc_deregister(&sample_misc_dev);
	tsm_mr_free_attribute_group(sample_groups[0]);
}

module_init(tsm_mr_sample_init);
module_exit(tsm_mr_sample_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sample module using tsm-mr to expose emulated MRs");
