/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SOC_QCOM_HIBERNATION_H__
#define __SOC_QCOM_HIBERNATION_H__

#include <linux/blk_types.h>
#include <linux/blkdev.h>

#define AES256_KEY_SIZE		32
#define NUM_KEYS		2
#define PAYLOAD_KEY_SIZE	(AES256_KEY_SIZE * NUM_KEYS)
#define RAND_INDEX_SIZE		8
#define NONCE_LENGTH		8
#define MAC_LENGTH		16
#define TIME_STRUCT_LENGTH	48
#define WRAP_PAYLOAD_LENGTH \
		(PAYLOAD_KEY_SIZE + RAND_INDEX_SIZE + TIME_STRUCT_LENGTH)
#define AAD_LENGTH		20
#define AAD_WITH_PAD_LENGTH	32
#define WRAPPED_KEY_SIZE \
		(AAD_WITH_PAD_LENGTH + WRAP_PAYLOAD_LENGTH + MAC_LENGTH + \
		NONCE_LENGTH)
#define IV_SIZE			12

struct qcom_crypto_params {
	unsigned int authsize;
	unsigned int authslot_count;
	unsigned char key_blob[WRAPPED_KEY_SIZE];
	unsigned char iv[IV_SIZE];
	unsigned char aad[12];
};

struct hib_bio_batch {
	atomic_t		count;
	wait_queue_head_t	wait;
	blk_status_t		error;
	struct blk_plug		plug;
};

extern struct block_device *hiber_bdev;

#endif /* __SOC_QCOM_HIBERNATION_H__ */
