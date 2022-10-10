/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

#include <asm/unaligned.h>

void eir_create(struct hci_dev *hdev, u8 *data);

u8 eir_create_adv_data(struct hci_dev *hdev, u8 instance, u8 *ptr);
u8 eir_create_scan_rsp(struct hci_dev *hdev, u8 instance, u8 *ptr);
u8 eir_create_per_adv_data(struct hci_dev *hdev, u8 instance, u8 *ptr);

u8 eir_append_local_name(struct hci_dev *hdev, u8 *eir, u8 ad_len);
u8 eir_append_appearance(struct hci_dev *hdev, u8 *ptr, u8 ad_len);
u8 eir_append_service_data(u8 *eir, u16 eir_len, u16 uuid, u8 *data,
			   u8 data_len);

static inline u16 eir_precalc_len(u8 data_len)
{
	return sizeof(u8) * 2 + data_len;
}

static inline u16 eir_append_data(u8 *eir, u16 eir_len, u8 type,
				  u8 *data, u8 data_len)
{
	eir[eir_len++] = sizeof(type) + data_len;
	eir[eir_len++] = type;
	memcpy(&eir[eir_len], data, data_len);
	eir_len += data_len;

	return eir_len;
}

static inline u16 eir_append_le16(u8 *eir, u16 eir_len, u8 type, u16 data)
{
	eir[eir_len++] = sizeof(type) + sizeof(data);
	eir[eir_len++] = type;
	put_unaligned_le16(data, &eir[eir_len]);
	eir_len += sizeof(data);

	return eir_len;
}

static inline u16 eir_skb_put_data(struct sk_buff *skb, u8 type, u8 *data, u8 data_len)
{
	u8 *eir;
	u16 eir_len;

	eir_len	= eir_precalc_len(data_len);
	eir = skb_put(skb, eir_len);
	WARN_ON(sizeof(type) + data_len > U8_MAX);
	eir[0] = sizeof(type) + data_len;
	eir[1] = type;
	memcpy(&eir[2], data, data_len);

	return eir_len;
}

static inline void *eir_get_data(u8 *eir, size_t eir_len, u8 type,
				 size_t *data_len)
{
	size_t parsed = 0;

	if (eir_len < 2)
		return NULL;

	while (parsed < eir_len - 1) {
		u8 field_len = eir[0];

		if (field_len == 0)
			break;

		parsed += field_len + 1;

		if (parsed > eir_len)
			break;

		if (eir[1] != type) {
			eir += field_len + 1;
			continue;
		}

		/* Zero length data */
		if (field_len == 1)
			return NULL;

		if (data_len)
			*data_len = field_len - 1;

		return &eir[2];
	}

	return NULL;
}

void *eir_get_service_data(u8 *eir, size_t eir_len, u16 uuid, size_t *len);
