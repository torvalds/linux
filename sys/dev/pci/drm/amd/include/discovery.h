/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef _DISCOVERY_H_
#define _DISCOVERY_H_

#define PSP_HEADER_SIZE                 256
#define BINARY_SIGNATURE                0x28211407
#define DISCOVERY_TABLE_SIGNATURE       0x53445049
#define GC_TABLE_ID                     0x4347
#define HARVEST_TABLE_SIGNATURE         0x56524148
#define VCN_INFO_TABLE_ID               0x004E4356
#define MALL_INFO_TABLE_ID              0x4C4C414D
#define NPS_INFO_TABLE_ID 0x0053504E

typedef enum {
	IP_DISCOVERY = 0,
	GC,
	HARVEST_INFO,
	VCN_INFO,
	MALL_INFO,
	NPS_INFO,
	TOTAL_TABLES = 6
} table;

#pragma pack(1)

typedef struct table_info
{
	uint16_t offset;   /* Byte offset */
	uint16_t checksum; /* Byte sum of the table */
	uint16_t size;     /* Table size */
	uint16_t padding;
} table_info;

typedef struct binary_header
{
	/* psp structure should go at the top of this structure */
	uint32_t binary_signature; /* 0x7, 0x14, 0x21, 0x28 */
	uint16_t version_major;
	uint16_t version_minor;
	uint16_t binary_checksum;  /* Byte sum of the binary after this field */
	uint16_t binary_size;      /* Binary Size*/
	table_info table_list[TOTAL_TABLES];
} binary_header;

typedef struct die_info
{
	uint16_t die_id;
	uint16_t die_offset; /* Points to the corresponding die_header structure */
} die_info;


typedef struct ip_discovery_header
{
	uint32_t signature;    /* Table Signature */
	uint16_t version;      /* Table Version */
	uint16_t size;         /* Table Size */
	uint32_t id;           /* Table ID */
	uint16_t num_dies;     /* Number of Dies */
	die_info die_info[16]; /* list die information for up to 16 dies */
	union {
		uint16_t padding[1];	/* version <= 3 */
		struct {		/* version == 4 */
			uint8_t base_addr_64_bit : 1; /* ip structures are using 64 bit base address */
			uint8_t reserved : 7;
			uint8_t reserved2;
		};
	};
} ip_discovery_header;

typedef struct ip
{
	uint16_t hw_id;           /* Hardware ID */
	uint8_t number_instance;  /* instance of the IP */
	uint8_t num_base_address; /* Number of Base Addresses */
	uint8_t major;            /* HCID Major */
	uint8_t minor;            /* HCID Minor */
	uint8_t revision;         /* HCID Revision */
#if defined(__BIG_ENDIAN)
	uint8_t reserved : 4;     /* Placeholder field */
	uint8_t harvest : 4;      /* Harvest */
#else
	uint8_t harvest : 4;      /* Harvest */
	uint8_t reserved : 4;     /* Placeholder field */
#endif
	uint32_t base_address[]; /* variable number of Addresses */
} ip;

typedef struct ip_v3
{
	uint16_t hw_id;                         /* Hardware ID */
	uint8_t instance_number;                /* Instance number for the IP */
	uint8_t num_base_address;               /* Number of base addresses*/
	uint8_t major;                          /* Hardware ID.major version */
	uint8_t minor;                          /* Hardware ID.minor version */
	uint8_t revision;                       /* Hardware ID.revision version */
#if defined(__BIG_ENDIAN)
	uint8_t variant : 4;                    /* HW variant */
	uint8_t sub_revision : 4;               /* HCID Sub-Revision */
#else
	uint8_t sub_revision : 4;               /* HCID Sub-Revision */
	uint8_t variant : 4;                    /* HW variant */
#endif
	uint32_t base_address[];		/* Base Address list. Corresponds to the num_base_address field*/
} ip_v3;

typedef struct ip_v4 {
	uint16_t hw_id;                         /* Hardware ID */
	uint8_t instance_number;                /* Instance number for the IP */
	uint8_t num_base_address;               /* Number of base addresses*/
	uint8_t major;                          /* Hardware ID.major version */
	uint8_t minor;                          /* Hardware ID.minor version */
	uint8_t revision;                       /* Hardware ID.revision version */
#if defined(LITTLEENDIAN_CPU)
	uint8_t sub_revision : 4;               /* HCID Sub-Revision */
	uint8_t variant : 4;                    /* HW variant */
#elif defined(BIGENDIAN_CPU)
	uint8_t variant : 4;                    /* HW variant */
	uint8_t sub_revision : 4;               /* HCID Sub-Revision */
#endif
	union {
		DECLARE_FLEX_ARRAY(uint32_t, base_address);	/* 32-bit Base Address list. Corresponds to the num_base_address field*/
		DECLARE_FLEX_ARRAY(uint64_t, base_address_64);	/* 64-bit Base Address list. Corresponds to the num_base_address field*/
	} __packed;
} ip_v4;

typedef struct die_header
{
	uint16_t die_id;
	uint16_t num_ips;
} die_header;

typedef struct ip_structure
{
	ip_discovery_header* header;
	struct die
	{
		die_header *die_header;
		union
		{
			ip *ip_list;
			ip_v3 *ip_v3_list;
			ip_v4 *ip_v4_list;
		};                                  /* IP list. Variable size*/
	} die;
} ip_structure;

struct gpu_info_header {
	uint32_t table_id;      /* table ID */
	uint16_t version_major; /* table version */
	uint16_t version_minor; /* table version */
	uint32_t size;          /* size of the entire header+data in bytes */
};

struct gc_info_v1_0 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_wgp0_per_sa;
	uint32_t gc_num_wgp1_per_sa;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_gl2c;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_sa_per_se;
	uint32_t gc_num_packer_per_sc;
	uint32_t gc_num_gl2a;
};

struct gc_info_v1_1 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_wgp0_per_sa;
	uint32_t gc_num_wgp1_per_sa;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_gl2c;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_sa_per_se;
	uint32_t gc_num_packer_per_sc;
	uint32_t gc_num_gl2a;
	uint32_t gc_num_tcp_per_sa;
	uint32_t gc_num_sdp_interface;
	uint32_t gc_num_tcps;
};

struct gc_info_v1_2 {
	struct gpu_info_header header;
	uint32_t gc_num_se;
	uint32_t gc_num_wgp0_per_sa;
	uint32_t gc_num_wgp1_per_sa;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_gl2c;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_sa_per_se;
	uint32_t gc_num_packer_per_sc;
	uint32_t gc_num_gl2a;
	uint32_t gc_num_tcp_per_sa;
	uint32_t gc_num_sdp_interface;
	uint32_t gc_num_tcps;
	uint32_t gc_num_tcp_per_wpg;
	uint32_t gc_tcp_l1_size;
	uint32_t gc_num_sqc_per_wgp;
	uint32_t gc_l1_instruction_cache_size_per_sqc;
	uint32_t gc_l1_data_cache_size_per_sqc;
	uint32_t gc_gl1c_per_sa;
	uint32_t gc_gl1c_size_per_instance;
	uint32_t gc_gl2c_per_gpu;
};

struct gc_info_v1_3 {
    struct gpu_info_header header;
    uint32_t gc_num_se;
    uint32_t gc_num_wgp0_per_sa;
    uint32_t gc_num_wgp1_per_sa;
    uint32_t gc_num_rb_per_se;
    uint32_t gc_num_gl2c;
    uint32_t gc_num_gprs;
    uint32_t gc_num_max_gs_thds;
    uint32_t gc_gs_table_depth;
    uint32_t gc_gsprim_buff_depth;
    uint32_t gc_parameter_cache_depth;
    uint32_t gc_double_offchip_lds_buffer;
    uint32_t gc_wave_size;
    uint32_t gc_max_waves_per_simd;
    uint32_t gc_max_scratch_slots_per_cu;
    uint32_t gc_lds_size;
    uint32_t gc_num_sc_per_se;
    uint32_t gc_num_sa_per_se;
    uint32_t gc_num_packer_per_sc;
    uint32_t gc_num_gl2a;
    uint32_t gc_num_tcp_per_sa;
    uint32_t gc_num_sdp_interface;
    uint32_t gc_num_tcps;
    uint32_t gc_num_tcp_per_wpg;
    uint32_t gc_tcp_l1_size;
    uint32_t gc_num_sqc_per_wgp;
    uint32_t gc_l1_instruction_cache_size_per_sqc;
    uint32_t gc_l1_data_cache_size_per_sqc;
    uint32_t gc_gl1c_per_sa;
    uint32_t gc_gl1c_size_per_instance;
    uint32_t gc_gl2c_per_gpu;
    uint32_t gc_tcp_size_per_cu;
    uint32_t gc_tcp_cache_line_size;
    uint32_t gc_instruction_cache_size_per_sqc;
    uint32_t gc_instruction_cache_line_size;
    uint32_t gc_scalar_data_cache_size_per_sqc;
    uint32_t gc_scalar_data_cache_line_size;
    uint32_t gc_tcc_size;
    uint32_t gc_tcc_cache_line_size;
};

struct gc_info_v2_0 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_cu_per_sh;
	uint32_t gc_num_sh_per_se;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_tccs;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_packer_per_sc;
};

struct gc_info_v2_1 {
	struct gpu_info_header header;

	uint32_t gc_num_se;
	uint32_t gc_num_cu_per_sh;
	uint32_t gc_num_sh_per_se;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_tccs;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
	uint32_t gc_num_sc_per_se;
	uint32_t gc_num_packer_per_sc;
	/* new for v2_1 */
	uint32_t gc_num_tcp_per_sh;
	uint32_t gc_tcp_size_per_cu;
	uint32_t gc_num_sdp_interface;
	uint32_t gc_num_cu_per_sqc;
	uint32_t gc_instruction_cache_size_per_sqc;
	uint32_t gc_scalar_data_cache_size_per_sqc;
	uint32_t gc_tcc_size;
};

typedef struct harvest_info_header {
	uint32_t signature; /* Table Signature */
	uint32_t version;   /* Table Version */
} harvest_info_header;

typedef struct harvest_info {
	uint16_t hw_id;          /* Hardware ID */
	uint8_t number_instance; /* Instance of the IP */
	uint8_t reserved;        /* Reserved for alignment */
} harvest_info;

typedef struct harvest_table {
	harvest_info_header header;
	harvest_info list[32];
} harvest_table;

struct mall_info_header {
	uint32_t table_id; /* table ID */
	uint16_t version_major; /* table version */
	uint16_t version_minor; /* table version */
	uint32_t size_bytes; /* size of the entire header+data in bytes */
};

struct mall_info_v1_0 {
	struct mall_info_header header;
	uint32_t mall_size_per_m;
	uint32_t m_s_present;
	uint32_t m_half_use;
	uint32_t m_mall_config;
	uint32_t reserved[5];
};

struct mall_info_v2_0 {
	struct mall_info_header header;
	uint32_t mall_size_per_umc;
	uint32_t reserved[8];
};

#define VCN_INFO_TABLE_MAX_NUM_INSTANCES 4

struct vcn_info_header {
    uint32_t table_id; /* table ID */
    uint16_t version_major; /* table version */
    uint16_t version_minor; /* table version */
    uint32_t size_bytes; /* size of the entire header+data in bytes */
};

struct vcn_instance_info_v1_0
{
	uint32_t instance_num; /* VCN IP instance number. 0 - VCN0; 1 - VCN1 etc*/
	union _fuse_data {
		struct {
			uint32_t av1_disabled : 1;
			uint32_t vp9_disabled : 1;
			uint32_t hevc_disabled : 1;
			uint32_t h264_disabled : 1;
			uint32_t reserved : 28;
		} bits;
		uint32_t all_bits;
	} fuse_data;
	uint32_t reserved[2];
};

struct vcn_info_v1_0 {
	struct vcn_info_header header;
	uint32_t num_of_instances; /* number of entries used in instance_info below*/
	struct vcn_instance_info_v1_0 instance_info[VCN_INFO_TABLE_MAX_NUM_INSTANCES];
	uint32_t reserved[4];
};

#define NPS_INFO_TABLE_MAX_NUM_INSTANCES 12

struct nps_info_header {
	uint32_t table_id; /* table ID */
	uint16_t version_major; /* table version */
	uint16_t version_minor; /* table version */
	uint32_t size_bytes; /* size of the entire header+data in bytes = 0x000000D4 (212) */
};

struct nps_instance_info_v1_0 {
	uint64_t base_address;
	uint64_t limit_address;
};

struct nps_info_v1_0 {
	struct nps_info_header header;
	uint32_t nps_type;
	uint32_t count;
	struct nps_instance_info_v1_0
		instance_info[NPS_INFO_TABLE_MAX_NUM_INSTANCES];
};

#pragma pack()

#endif
