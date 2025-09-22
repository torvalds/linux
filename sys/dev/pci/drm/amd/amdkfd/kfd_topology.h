/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __KFD_TOPOLOGY_H__
#define __KFD_TOPOLOGY_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/kfd_sysfs.h>
#include "kfd_crat.h"

#define KFD_TOPOLOGY_PUBLIC_NAME_SIZE 32

#define HSA_DBG_WATCH_ADDR_MASK_LO_BIT_GFX9	6
#define HSA_DBG_WATCH_ADDR_MASK_LO_BIT_GFX9_4_3 7
#define HSA_DBG_WATCH_ADDR_MASK_LO_BIT_GFX10	7
#define HSA_DBG_WATCH_ADDR_MASK_HI_BIT  \
			(29 << HSA_DBG_WATCH_ADDR_MASK_HI_BIT_SHIFT)
#define HSA_DBG_WATCH_ADDR_MASK_HI_BIT_GFX9_4_3 \
			(30 << HSA_DBG_WATCH_ADDR_MASK_HI_BIT_SHIFT)

struct kfd_node_properties {
	uint64_t hive_id;
	uint32_t cpu_cores_count;
	uint32_t simd_count;
	uint32_t mem_banks_count;
	uint32_t caches_count;
	uint32_t io_links_count;
	uint32_t p2p_links_count;
	uint32_t cpu_core_id_base;
	uint32_t simd_id_base;
	uint32_t capability;
	uint64_t debug_prop;
	uint32_t max_waves_per_simd;
	uint32_t lds_size_in_kb;
	uint32_t gds_size_in_kb;
	uint32_t num_gws;
	uint32_t wave_front_size;
	uint32_t array_count;
	uint32_t simd_arrays_per_engine;
	uint32_t cu_per_simd_array;
	uint32_t simd_per_cu;
	uint32_t max_slots_scratch_cu;
	uint32_t engine_id;
	uint32_t gfx_target_version;
	uint32_t vendor_id;
	uint32_t device_id;
	uint32_t location_id;
	uint32_t domain;
	uint32_t max_engine_clk_fcompute;
	uint32_t max_engine_clk_ccompute;
	int32_t  drm_render_minor;
	uint32_t num_sdma_engines;
	uint32_t num_sdma_xgmi_engines;
	uint32_t num_sdma_queues_per_engine;
	uint32_t num_cp_queues;
	uint32_t cwsr_size;
	uint32_t ctl_stack_size;
	uint32_t eop_buffer_size;
	uint32_t debug_memory_size;
	char name[KFD_TOPOLOGY_PUBLIC_NAME_SIZE];
};

struct kfd_mem_properties {
	struct list_head	list;
	uint32_t		heap_type;
	uint64_t		size_in_bytes;
	uint32_t		flags;
	uint32_t		width;
	uint32_t		mem_clk_max;
	struct kfd_node		*gpu;
	struct kobject		*kobj;
	struct attribute	attr;
};

#define CACHE_SIBLINGMAP_SIZE 128

struct kfd_cache_properties {
	struct list_head	list;
	uint32_t		processor_id_low;
	uint32_t		cache_level;
	uint32_t		cache_size;
	uint32_t		cacheline_size;
	uint32_t		cachelines_per_tag;
	uint32_t		cache_assoc;
	uint32_t		cache_latency;
	uint32_t		cache_type;
	uint8_t			sibling_map[CACHE_SIBLINGMAP_SIZE];
	struct kfd_node		*gpu;
	struct kobject		*kobj;
	struct attribute	attr;
	uint32_t		sibling_map_size;
};

struct kfd_iolink_properties {
	struct list_head	list;
	uint32_t		iolink_type;
	uint32_t		ver_maj;
	uint32_t		ver_min;
	uint32_t		node_from;
	uint32_t		node_to;
	uint32_t		weight;
	uint32_t		min_latency;
	uint32_t		max_latency;
	uint32_t		min_bandwidth;
	uint32_t		max_bandwidth;
	uint32_t		rec_transfer_size;
	uint32_t		rec_sdma_eng_id_mask;
	uint32_t		flags;
	struct kfd_node		*gpu;
	struct kobject		*kobj;
	struct attribute	attr;
};

struct kfd_perf_properties {
	struct list_head	list;
	char			block_name[16];
	uint32_t		max_concurrent;
	struct attribute_group	*attr_group;
};

struct kfd_topology_device {
	struct list_head		list;
	uint32_t			gpu_id;
	uint32_t			proximity_domain;
	struct kfd_node_properties	node_props;
	struct list_head		mem_props;
	struct list_head		cache_props;
	struct list_head		io_link_props;
	struct list_head		p2p_link_props;
	struct list_head		perf_props;
	struct kfd_node			*gpu;
	struct kobject			*kobj_node;
	struct kobject			*kobj_mem;
	struct kobject			*kobj_cache;
	struct kobject			*kobj_iolink;
	struct kobject			*kobj_p2plink;
	struct kobject			*kobj_perf;
	struct attribute		attr_gpuid;
	struct attribute		attr_name;
	struct attribute		attr_props;
	union {
		uint8_t				oem_id[CRAT_OEMID_LENGTH];
		uint64_t			oem_id64;
	};
	uint8_t				oem_table_id[CRAT_OEMTABLEID_LENGTH];
	uint32_t			oem_revision;
};

struct kfd_system_properties {
	uint32_t		num_devices;     /* Number of H-NUMA nodes */
	uint32_t		generation_count;
	uint64_t		platform_oem;
	uint64_t		platform_id;
	uint64_t		platform_rev;
	struct kobject		*kobj_topology;
	struct kobject		*kobj_nodes;
	struct attribute	attr_genid;
	struct attribute	attr_props;
};

struct kfd_topology_device *kfd_create_topology_device(
		struct list_head *device_list);
void kfd_release_topology_device_list(struct list_head *device_list);

#endif /* __KFD_TOPOLOGY_H__ */
