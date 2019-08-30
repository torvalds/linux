/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


Public declarations for DSP Proramming Interface to TI C6701

Shared between hpi6000.c and DSP code

(C) Copyright AudioScience Inc. 1998-2003
******************************************************************************/

#ifndef _HPI6000_H_
#define _HPI6000_H_

#define HPI_NMIXER_CONTROLS 200

/*
 * Control caching is always supported in the HPI code.
 * The DSP should make sure that dwControlCacheSizeInBytes is initialized to 0
 * during boot to make it in-active.
 */
struct hpi_hif_6000 {
	u32 host_cmd;
	u32 dsp_ack;
	u32 address;
	u32 length;
	u32 message_buffer_address;
	u32 response_buffer_address;
	u32 dsp_number;
	u32 adapter_info;
	u32 control_cache_is_dirty;
	u32 control_cache_address;
	u32 control_cache_size_in_bytes;
	u32 control_cache_count;
};

#define HPI_HIF_PACK_ADAPTER_INFO(adapter, version_major, version_minor) \
		((adapter << 16) | (version_major << 8) | version_minor)
#define HPI_HIF_ADAPTER_INFO_EXTRACT_ADAPTER(adapterinfo) \
		((adapterinfo >> 16) & 0xffff)
#define HPI_HIF_ADAPTER_INFO_EXTRACT_HWVERSION_MAJOR(adapterinfo) \
		((adapterinfo >> 8) & 0xff)
#define HPI_HIF_ADAPTER_INFO_EXTRACT_HWVERSION_MINOR(adapterinfo) \
		(adapterinfo & 0xff)

/* Command/status exchanged between host and DSP */
#define HPI_HIF_IDLE            0
#define HPI_HIF_SEND_MSG        1
#define HPI_HIF_GET_RESP        2
#define HPI_HIF_DATA_MASK       0x10
#define HPI_HIF_SEND_DATA       0x13
#define HPI_HIF_GET_DATA        0x14
#define HPI_HIF_SEND_DONE       5
#define HPI_HIF_RESET           9

#endif				/* _HPI6000_H_ */
