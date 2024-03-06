/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Siemens SIMATIC IPC drivers
 *
 * Copyright (c) Siemens AG, 2018-2023
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 */

#ifndef __PLATFORM_DATA_X86_SIMATIC_IPC_H
#define __PLATFORM_DATA_X86_SIMATIC_IPC_H

#include <linux/dmi.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>

#define SIMATIC_IPC_DMI_ENTRY_OEM	129
/* binary type */
#define SIMATIC_IPC_DMI_TYPE		0xff
#define SIMATIC_IPC_DMI_GROUP		0x05
#define SIMATIC_IPC_DMI_ENTRY		0x02
#define SIMATIC_IPC_DMI_TID		0x02

enum simatic_ipc_station_ids {
	SIMATIC_IPC_INVALID_STATION_ID = 0,
	SIMATIC_IPC_IPC227D = 0x00000501,
	SIMATIC_IPC_IPC427D = 0x00000701,
	SIMATIC_IPC_IPC227E = 0x00000901,
	SIMATIC_IPC_IPC277E = 0x00000902,
	SIMATIC_IPC_IPC427E = 0x00000A01,
	SIMATIC_IPC_IPC477E = 0x00000A02,
	SIMATIC_IPC_IPC127E = 0x00000D01,
	SIMATIC_IPC_IPC227G = 0x00000F01,
	SIMATIC_IPC_IPC277G = 0x00000F02,
	SIMATIC_IPC_IPCBX_39A = 0x00001001,
	SIMATIC_IPC_IPCPX_39A = 0x00001002,
	SIMATIC_IPC_IPCBX_21A = 0x00001101,
	SIMATIC_IPC_IPCBX_56A = 0x00001201,
	SIMATIC_IPC_IPCBX_59A = 0x00001202,
};

static inline u32 simatic_ipc_get_station_id(u8 *data, int max_len)
{
	struct {
		u8	type;		/* type (0xff = binary) */
		u8	len;		/* len of data entry */
		u8	group;
		u8	entry;
		u8	tid;
		__le32	station_id;	/* station id (LE) */
	} __packed * data_entry = (void *)data + sizeof(struct dmi_header);

	while ((u8 *)data_entry < data + max_len) {
		if (data_entry->type == SIMATIC_IPC_DMI_TYPE &&
		    data_entry->len == sizeof(*data_entry) &&
		    data_entry->group == SIMATIC_IPC_DMI_GROUP &&
		    data_entry->entry == SIMATIC_IPC_DMI_ENTRY &&
		    data_entry->tid == SIMATIC_IPC_DMI_TID) {
			return le32_to_cpu(data_entry->station_id);
		}
		data_entry = (void *)((u8 *)(data_entry) + data_entry->len);
	}

	return SIMATIC_IPC_INVALID_STATION_ID;
}

static inline void
simatic_ipc_find_dmi_entry_helper(const struct dmi_header *dh, void *_data)
{
	u32 *id = _data;

	if (dh->type != SIMATIC_IPC_DMI_ENTRY_OEM)
		return;

	*id = simatic_ipc_get_station_id((u8 *)dh, dh->length);
}

#endif /* __PLATFORM_DATA_X86_SIMATIC_IPC_H */
