/* SPDX-License-Identifier: GPL-2.0-only */
/**

    AudioScience HPI driver
    Copyright (C) 1997-2014  AudioScience Inc. <support@audioscience.com>


*/

struct hpi_adapter_obj;

/* a function that takes an adapter obj and returns an int */
typedef int adapter_int_func(struct hpi_adapter_obj *pao, u32 message);

#define HPI_IRQ_NONE		(0)
#define HPI_IRQ_MESSAGE		(1)
#define HPI_IRQ_MIXER		(2)

struct hpi_adapter_obj {
	struct hpi_pci pci;	/* PCI info - bus#,dev#,address etc */
	u16 type;		/* 0x6644 == ASI6644 etc */
	u16 index;

	struct hpios_spinlock dsp_lock;

	u16 dsp_crashed;
	u16 has_control_cache;
	void *priv;
	adapter_int_func *irq_query_and_clear;
	struct hpi_hostbuffer_status *instream_host_buffer_status;
	struct hpi_hostbuffer_status *outstream_host_buffer_status;
};

struct hpi_control_cache {
	/** indicates whether the structures are initialized */
	u16 init;
	u16 adap_idx;
	u32 control_count;
	u32 cache_size_in_bytes;
	/** pointer to allocated memory of lookup pointers. */
	struct hpi_control_cache_info **p_info;
	/** pointer to DSP's control cache. */
	u8 *p_cache;
};

struct hpi_adapter_obj *hpi_find_adapter(u16 adapter_index);

u16 hpi_add_adapter(struct hpi_adapter_obj *pao);

void hpi_delete_adapter(struct hpi_adapter_obj *pao);

short hpi_check_control_cache(struct hpi_control_cache *pC,
	struct hpi_message *phm, struct hpi_response *phr);

short hpi_check_control_cache_single(struct hpi_control_cache_single *pC,
	struct hpi_message *phm, struct hpi_response *phr);

struct hpi_control_cache *hpi_alloc_control_cache(const u32
	number_of_controls, const u32 size_in_bytes, u8 *pDSP_control_buffer);

void hpi_free_control_cache(struct hpi_control_cache *p_cache);

void hpi_cmn_control_cache_sync_to_msg(struct hpi_control_cache *pC,
	struct hpi_message *phm, struct hpi_response *phr);

void hpi_cmn_control_cache_sync_to_msg_single(struct hpi_control_cache_single
	*pC, struct hpi_message *phm, struct hpi_response *phr);

u16 hpi_validate_response(struct hpi_message *phm, struct hpi_response *phr);

hpi_handler_func HPI_COMMON;
