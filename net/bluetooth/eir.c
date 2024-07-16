// SPDX-License-Identifier: GPL-2.0
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "eir.h"

#define PNP_INFO_SVCLASS_ID		0x1200

static u8 eir_append_name(u8 *eir, u16 eir_len, u8 type, u8 *data, u8 data_len)
{
	u8 name[HCI_MAX_SHORT_NAME_LENGTH + 1];

	/* If data is already NULL terminated just pass it directly */
	if (data[data_len - 1] == '\0')
		return eir_append_data(eir, eir_len, type, data, data_len);

	memcpy(name, data, HCI_MAX_SHORT_NAME_LENGTH);
	name[HCI_MAX_SHORT_NAME_LENGTH] = '\0';

	return eir_append_data(eir, eir_len, type, name, sizeof(name));
}

u8 eir_append_local_name(struct hci_dev *hdev, u8 *ptr, u8 ad_len)
{
	size_t short_len;
	size_t complete_len;

	/* no space left for name (+ NULL + type + len) */
	if ((HCI_MAX_AD_LENGTH - ad_len) < HCI_MAX_SHORT_NAME_LENGTH + 3)
		return ad_len;

	/* use complete name if present and fits */
	complete_len = strnlen(hdev->dev_name, sizeof(hdev->dev_name));
	if (complete_len && complete_len <= HCI_MAX_SHORT_NAME_LENGTH)
		return eir_append_name(ptr, ad_len, EIR_NAME_COMPLETE,
				       hdev->dev_name, complete_len + 1);

	/* use short name if present */
	short_len = strnlen(hdev->short_name, sizeof(hdev->short_name));
	if (short_len)
		return eir_append_name(ptr, ad_len, EIR_NAME_SHORT,
				       hdev->short_name,
				       short_len == HCI_MAX_SHORT_NAME_LENGTH ?
				       short_len : short_len + 1);

	/* use shortened full name if present, we already know that name
	 * is longer then HCI_MAX_SHORT_NAME_LENGTH
	 */
	if (complete_len)
		return eir_append_name(ptr, ad_len, EIR_NAME_SHORT,
				       hdev->dev_name,
				       HCI_MAX_SHORT_NAME_LENGTH);

	return ad_len;
}

u8 eir_append_appearance(struct hci_dev *hdev, u8 *ptr, u8 ad_len)
{
	return eir_append_le16(ptr, ad_len, EIR_APPEARANCE, hdev->appearance);
}

u8 eir_append_service_data(u8 *eir, u16 eir_len, u16 uuid, u8 *data,
			   u8 data_len)
{
	eir[eir_len++] = sizeof(u8) + sizeof(uuid) + data_len;
	eir[eir_len++] = EIR_SERVICE_DATA;
	put_unaligned_le16(uuid, &eir[eir_len]);
	eir_len += sizeof(uuid);
	memcpy(&eir[eir_len], data, data_len);
	eir_len += data_len;

	return eir_len;
}

static u8 *create_uuid16_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 4)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		u16 uuid16;

		if (uuid->size != 16)
			continue;

		uuid16 = get_unaligned_le16(&uuid->uuid[12]);
		if (uuid16 < 0x1100)
			continue;

		if (uuid16 == PNP_INFO_SVCLASS_ID)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID16_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + sizeof(u16) > len) {
			uuids_start[1] = EIR_UUID16_SOME;
			break;
		}

		*ptr++ = (uuid16 & 0x00ff);
		*ptr++ = (uuid16 & 0xff00) >> 8;
		uuids_start[0] += sizeof(uuid16);
	}

	return ptr;
}

static u8 *create_uuid32_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 6)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		if (uuid->size != 32)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID32_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + sizeof(u32) > len) {
			uuids_start[1] = EIR_UUID32_SOME;
			break;
		}

		memcpy(ptr, &uuid->uuid[12], sizeof(u32));
		ptr += sizeof(u32);
		uuids_start[0] += sizeof(u32);
	}

	return ptr;
}

static u8 *create_uuid128_list(struct hci_dev *hdev, u8 *data, ptrdiff_t len)
{
	u8 *ptr = data, *uuids_start = NULL;
	struct bt_uuid *uuid;

	if (len < 18)
		return ptr;

	list_for_each_entry(uuid, &hdev->uuids, list) {
		if (uuid->size != 128)
			continue;

		if (!uuids_start) {
			uuids_start = ptr;
			uuids_start[0] = 1;
			uuids_start[1] = EIR_UUID128_ALL;
			ptr += 2;
		}

		/* Stop if not enough space to put next UUID */
		if ((ptr - data) + 16 > len) {
			uuids_start[1] = EIR_UUID128_SOME;
			break;
		}

		memcpy(ptr, uuid->uuid, 16);
		ptr += 16;
		uuids_start[0] += 16;
	}

	return ptr;
}

void eir_create(struct hci_dev *hdev, u8 *data)
{
	u8 *ptr = data;
	size_t name_len;

	name_len = strnlen(hdev->dev_name, sizeof(hdev->dev_name));

	if (name_len > 0) {
		/* EIR Data type */
		if (name_len > 48) {
			name_len = 48;
			ptr[1] = EIR_NAME_SHORT;
		} else {
			ptr[1] = EIR_NAME_COMPLETE;
		}

		/* EIR Data length */
		ptr[0] = name_len + 1;

		memcpy(ptr + 2, hdev->dev_name, name_len);

		ptr += (name_len + 2);
	}

	if (hdev->inq_tx_power != HCI_TX_POWER_INVALID) {
		ptr[0] = 2;
		ptr[1] = EIR_TX_POWER;
		ptr[2] = (u8)hdev->inq_tx_power;

		ptr += 3;
	}

	if (hdev->devid_source > 0) {
		ptr[0] = 9;
		ptr[1] = EIR_DEVICE_ID;

		put_unaligned_le16(hdev->devid_source, ptr + 2);
		put_unaligned_le16(hdev->devid_vendor, ptr + 4);
		put_unaligned_le16(hdev->devid_product, ptr + 6);
		put_unaligned_le16(hdev->devid_version, ptr + 8);

		ptr += 10;
	}

	ptr = create_uuid16_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
	ptr = create_uuid32_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
	ptr = create_uuid128_list(hdev, ptr, HCI_MAX_EIR_LENGTH - (ptr - data));
}

u8 eir_create_per_adv_data(struct hci_dev *hdev, u8 instance, u8 *ptr)
{
	struct adv_info *adv = NULL;
	u8 ad_len = 0;

	/* Return 0 when the current instance identifier is invalid. */
	if (instance) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return 0;
	}

	if (adv) {
		memcpy(ptr, adv->per_adv_data, adv->per_adv_data_len);
		ad_len += adv->per_adv_data_len;
		ptr += adv->per_adv_data_len;
	}

	return ad_len;
}

u8 eir_create_adv_data(struct hci_dev *hdev, u8 instance, u8 *ptr)
{
	struct adv_info *adv = NULL;
	u8 ad_len = 0, flags = 0;
	u32 instance_flags;

	/* Return 0 when the current instance identifier is invalid. */
	if (instance) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return 0;
	}

	instance_flags = hci_adv_instance_flags(hdev, instance);

	/* If instance already has the flags set skip adding it once
	 * again.
	 */
	if (adv && eir_get_data(adv->adv_data, adv->adv_data_len, EIR_FLAGS,
				NULL))
		goto skip_flags;

	/* The Add Advertising command allows userspace to set both the general
	 * and limited discoverable flags.
	 */
	if (instance_flags & MGMT_ADV_FLAG_DISCOV)
		flags |= LE_AD_GENERAL;

	if (instance_flags & MGMT_ADV_FLAG_LIMITED_DISCOV)
		flags |= LE_AD_LIMITED;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		flags |= LE_AD_NO_BREDR;

	if (flags || (instance_flags & MGMT_ADV_FLAG_MANAGED_FLAGS)) {
		/* If a discovery flag wasn't provided, simply use the global
		 * settings.
		 */
		if (!flags)
			flags |= mgmt_get_adv_discov_flags(hdev);

		/* If flags would still be empty, then there is no need to
		 * include the "Flags" AD field".
		 */
		if (flags) {
			ptr[0] = 0x02;
			ptr[1] = EIR_FLAGS;
			ptr[2] = flags;

			ad_len += 3;
			ptr += 3;
		}
	}

skip_flags:
	if (adv) {
		memcpy(ptr, adv->adv_data, adv->adv_data_len);
		ad_len += adv->adv_data_len;
		ptr += adv->adv_data_len;
	}

	if (instance_flags & MGMT_ADV_FLAG_TX_POWER) {
		s8 adv_tx_power;

		if (ext_adv_capable(hdev)) {
			if (adv)
				adv_tx_power = adv->tx_power;
			else
				adv_tx_power = hdev->adv_tx_power;
		} else {
			adv_tx_power = hdev->adv_tx_power;
		}

		/* Provide Tx Power only if we can provide a valid value for it */
		if (adv_tx_power != HCI_TX_POWER_INVALID) {
			ptr[0] = 0x02;
			ptr[1] = EIR_TX_POWER;
			ptr[2] = (u8)adv_tx_power;

			ad_len += 3;
			ptr += 3;
		}
	}

	return ad_len;
}

static u8 create_default_scan_rsp(struct hci_dev *hdev, u8 *ptr)
{
	u8 scan_rsp_len = 0;

	if (hdev->appearance)
		scan_rsp_len = eir_append_appearance(hdev, ptr, scan_rsp_len);

	return eir_append_local_name(hdev, ptr, scan_rsp_len);
}

u8 eir_create_scan_rsp(struct hci_dev *hdev, u8 instance, u8 *ptr)
{
	struct adv_info *adv;
	u8 scan_rsp_len = 0;

	if (!instance)
		return create_default_scan_rsp(hdev, ptr);

	adv = hci_find_adv_instance(hdev, instance);
	if (!adv)
		return 0;

	if ((adv->flags & MGMT_ADV_FLAG_APPEARANCE) && hdev->appearance)
		scan_rsp_len = eir_append_appearance(hdev, ptr, scan_rsp_len);

	memcpy(&ptr[scan_rsp_len], adv->scan_rsp_data, adv->scan_rsp_len);

	scan_rsp_len += adv->scan_rsp_len;

	if (adv->flags & MGMT_ADV_FLAG_LOCAL_NAME)
		scan_rsp_len = eir_append_local_name(hdev, ptr, scan_rsp_len);

	return scan_rsp_len;
}

void *eir_get_service_data(u8 *eir, size_t eir_len, u16 uuid, size_t *len)
{
	while ((eir = eir_get_data(eir, eir_len, EIR_SERVICE_DATA, len))) {
		u16 value = get_unaligned_le16(eir);

		if (uuid == value) {
			if (len)
				*len -= 2;
			return &eir[2];
		}

		eir += *len;
		eir_len -= *len;
	}

	return NULL;
}
