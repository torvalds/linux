/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV MMR definitions
 *
 * Copyright (C) 2007-2008 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef __ASM_X86_UV_MMRS__
#define __ASM_X86_UV_MMRS__

/*
 *       AUTO GENERATED - Do not edit
 */

 #define UV_MMR_ENABLE		(1UL << 63)

/* ========================================================================= */
/*                               UVH_IPI_INT                                 */
/* ========================================================================= */
#define UVH_IPI_INT 0x60500UL
#define UVH_IPI_INT_32 0x0360

#define UVH_IPI_INT_VECTOR_SHFT 0
#define UVH_IPI_INT_VECTOR_MASK 0x00000000000000ffUL
#define UVH_IPI_INT_DELIVERY_MODE_SHFT 8
#define UVH_IPI_INT_DELIVERY_MODE_MASK 0x0000000000000700UL
#define UVH_IPI_INT_DESTMODE_SHFT 11
#define UVH_IPI_INT_DESTMODE_MASK 0x0000000000000800UL
#define UVH_IPI_INT_APIC_ID_SHFT 16
#define UVH_IPI_INT_APIC_ID_MASK 0x0000ffffffff0000UL
#define UVH_IPI_INT_SEND_SHFT 63
#define UVH_IPI_INT_SEND_MASK 0x8000000000000000UL

union uvh_ipi_int_u {
    unsigned long	v;
    struct uvh_ipi_int_s {
	unsigned long	vector_       :  8;  /* RW */
	unsigned long	delivery_mode :  3;  /* RW */
	unsigned long	destmode      :  1;  /* RW */
	unsigned long	rsvd_12_15    :  4;  /*    */
	unsigned long	apic_id       : 32;  /* RW */
	unsigned long	rsvd_48_62    : 15;  /*    */
	unsigned long	send          :  1;  /* WP */
    } s;
};

/* ========================================================================= */
/*                   UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST 0x320050UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_32 0x009f0

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_ADDRESS_SHFT 4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_ADDRESS_MASK 0x000007fffffffff0UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_NODE_ID_SHFT 49
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST_NODE_ID_MASK 0x7ffe000000000000UL

union uvh_lb_bau_intd_payload_queue_first_u {
    unsigned long	v;
    struct uvh_lb_bau_intd_payload_queue_first_s {
	unsigned long	rsvd_0_3:  4;  /*    */
	unsigned long	address : 39;  /* RW */
	unsigned long	rsvd_43_48:  6;  /*    */
	unsigned long	node_id : 14;  /* RW */
	unsigned long	rsvd_63 :  1;  /*    */
    } s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST 0x320060UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_32 0x009f8

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_ADDRESS_SHFT 4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST_ADDRESS_MASK 0x000007fffffffff0UL

union uvh_lb_bau_intd_payload_queue_last_u {
    unsigned long	v;
    struct uvh_lb_bau_intd_payload_queue_last_s {
	unsigned long	rsvd_0_3:  4;  /*    */
	unsigned long	address : 39;  /* RW */
	unsigned long	rsvd_43_63: 21;  /*    */
    } s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL                     */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL 0x320070UL
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_32 0x00a00

#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_ADDRESS_SHFT 4
#define UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL_ADDRESS_MASK 0x000007fffffffff0UL

union uvh_lb_bau_intd_payload_queue_tail_u {
    unsigned long	v;
    struct uvh_lb_bau_intd_payload_queue_tail_s {
	unsigned long	rsvd_0_3:  4;  /*    */
	unsigned long	address : 39;  /* RW */
	unsigned long	rsvd_43_63: 21;  /*    */
    } s;
};

/* ========================================================================= */
/*                   UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE                    */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE 0x320080UL

#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_0_SHFT 0
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_0_MASK 0x0000000000000001UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_1_SHFT 1
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_1_MASK 0x0000000000000002UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_2_SHFT 2
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_2_MASK 0x0000000000000004UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_3_SHFT 3
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_3_MASK 0x0000000000000008UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_4_SHFT 4
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_4_MASK 0x0000000000000010UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_5_SHFT 5
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_5_MASK 0x0000000000000020UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_6_SHFT 6
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_6_MASK 0x0000000000000040UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_7_SHFT 7
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_PENDING_7_MASK 0x0000000000000080UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_0_SHFT 8
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_0_MASK 0x0000000000000100UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_1_SHFT 9
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_1_MASK 0x0000000000000200UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_2_SHFT 10
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_2_MASK 0x0000000000000400UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_3_SHFT 11
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_3_MASK 0x0000000000000800UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_4_SHFT 12
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_4_MASK 0x0000000000001000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_5_SHFT 13
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_5_MASK 0x0000000000002000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_6_SHFT 14
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_6_MASK 0x0000000000004000UL
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_7_SHFT 15
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_TIMEOUT_7_MASK 0x0000000000008000UL
union uvh_lb_bau_intd_software_acknowledge_u {
    unsigned long	v;
    struct uvh_lb_bau_intd_software_acknowledge_s {
	unsigned long	pending_0 :  1;  /* RW, W1C */
	unsigned long	pending_1 :  1;  /* RW, W1C */
	unsigned long	pending_2 :  1;  /* RW, W1C */
	unsigned long	pending_3 :  1;  /* RW, W1C */
	unsigned long	pending_4 :  1;  /* RW, W1C */
	unsigned long	pending_5 :  1;  /* RW, W1C */
	unsigned long	pending_6 :  1;  /* RW, W1C */
	unsigned long	pending_7 :  1;  /* RW, W1C */
	unsigned long	timeout_0 :  1;  /* RW, W1C */
	unsigned long	timeout_1 :  1;  /* RW, W1C */
	unsigned long	timeout_2 :  1;  /* RW, W1C */
	unsigned long	timeout_3 :  1;  /* RW, W1C */
	unsigned long	timeout_4 :  1;  /* RW, W1C */
	unsigned long	timeout_5 :  1;  /* RW, W1C */
	unsigned long	timeout_6 :  1;  /* RW, W1C */
	unsigned long	timeout_7 :  1;  /* RW, W1C */
	unsigned long	rsvd_16_63: 48;  /*    */
    } s;
};

/* ========================================================================= */
/*                UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS                 */
/* ========================================================================= */
#define UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS 0x0000000000320088UL

/* ========================================================================= */
/*                     UVH_LB_BAU_SB_ACTIVATION_CONTROL                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL 0x320020UL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_32 0x009d8

#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INDEX_SHFT 0
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INDEX_MASK 0x000000000000003fUL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_SHFT 62
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_MASK 0x4000000000000000UL
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INIT_SHFT 63
#define UVH_LB_BAU_SB_ACTIVATION_CONTROL_INIT_MASK 0x8000000000000000UL

union uvh_lb_bau_sb_activation_control_u {
    unsigned long	v;
    struct uvh_lb_bau_sb_activation_control_s {
	unsigned long	index :  6;  /* RW */
	unsigned long	rsvd_6_61: 56;  /*    */
	unsigned long	push  :  1;  /* WP */
	unsigned long	init  :  1;  /* WP */
    } s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_SB_ACTIVATION_STATUS_0                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0 0x320030UL
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_32 0x009e0

#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_STATUS_SHFT 0
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_0_STATUS_MASK 0xffffffffffffffffUL

union uvh_lb_bau_sb_activation_status_0_u {
    unsigned long	v;
    struct uvh_lb_bau_sb_activation_status_0_s {
	unsigned long	status : 64;  /* RW */
    } s;
};

/* ========================================================================= */
/*                    UVH_LB_BAU_SB_ACTIVATION_STATUS_1                      */
/* ========================================================================= */
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1 0x320040UL
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_32 0x009e8

#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_STATUS_SHFT 0
#define UVH_LB_BAU_SB_ACTIVATION_STATUS_1_STATUS_MASK 0xffffffffffffffffUL

union uvh_lb_bau_sb_activation_status_1_u {
    unsigned long	v;
    struct uvh_lb_bau_sb_activation_status_1_s {
	unsigned long	status : 64;  /* RW */
    } s;
};

/* ========================================================================= */
/*                      UVH_LB_BAU_SB_DESCRIPTOR_BASE                        */
/* ========================================================================= */
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE 0x320010UL
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_32 0x009d0

#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_PAGE_ADDRESS_SHFT 12
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_PAGE_ADDRESS_MASK 0x000007fffffff000UL
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_NODE_ID_SHFT 49
#define UVH_LB_BAU_SB_DESCRIPTOR_BASE_NODE_ID_MASK 0x7ffe000000000000UL

union uvh_lb_bau_sb_descriptor_base_u {
    unsigned long	v;
    struct uvh_lb_bau_sb_descriptor_base_s {
	unsigned long	rsvd_0_11    : 12;  /*    */
	unsigned long	page_address : 31;  /* RW */
	unsigned long	rsvd_43_48   :  6;  /*    */
	unsigned long	node_id      : 14;  /* RW */
	unsigned long	rsvd_63      :  1;  /*    */
    } s;
};

/* ========================================================================= */
/*                               UVH_NODE_ID                                 */
/* ========================================================================= */
#define UVH_NODE_ID 0x0UL

#define UVH_NODE_ID_FORCE1_SHFT 0
#define UVH_NODE_ID_FORCE1_MASK 0x0000000000000001UL
#define UVH_NODE_ID_MANUFACTURER_SHFT 1
#define UVH_NODE_ID_MANUFACTURER_MASK 0x0000000000000ffeUL
#define UVH_NODE_ID_PART_NUMBER_SHFT 12
#define UVH_NODE_ID_PART_NUMBER_MASK 0x000000000ffff000UL
#define UVH_NODE_ID_REVISION_SHFT 28
#define UVH_NODE_ID_REVISION_MASK 0x00000000f0000000UL
#define UVH_NODE_ID_NODE_ID_SHFT 32
#define UVH_NODE_ID_NODE_ID_MASK 0x00007fff00000000UL
#define UVH_NODE_ID_NODES_PER_BIT_SHFT 48
#define UVH_NODE_ID_NODES_PER_BIT_MASK 0x007f000000000000UL
#define UVH_NODE_ID_NI_PORT_SHFT 56
#define UVH_NODE_ID_NI_PORT_MASK 0x0f00000000000000UL

union uvh_node_id_u {
    unsigned long	v;
    struct uvh_node_id_s {
	unsigned long	force1        :  1;  /* RO */
	unsigned long	manufacturer  : 11;  /* RO */
	unsigned long	part_number   : 16;  /* RO */
	unsigned long	revision      :  4;  /* RO */
	unsigned long	node_id       : 15;  /* RW */
	unsigned long	rsvd_47       :  1;  /*    */
	unsigned long	nodes_per_bit :  7;  /* RW */
	unsigned long	rsvd_55       :  1;  /*    */
	unsigned long	ni_port       :  4;  /* RO */
	unsigned long	rsvd_60_63    :  4;  /*    */
    } s;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR 0x1600010UL

#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_SHFT 28
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_BASE_MASK 0x00003ffff0000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_SHFT 46
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_GR4_MASK 0x0000400000000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_SHFT 52
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_N_GRU_MASK 0x00f0000000000000UL
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_GRU_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_gru_overlay_config_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_gru_overlay_config_mmr_s {
	unsigned long	rsvd_0_27: 28;  /*    */
	unsigned long	base   : 18;  /* RW */
	unsigned long	gr4    :  1;  /* RW */
	unsigned long	rsvd_47_51:  5;  /*    */
	unsigned long	n_gru  :  4;  /* RW */
	unsigned long	rsvd_56_62:  7;  /*    */
	unsigned long	enable :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                    UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR                      */
/* ========================================================================= */
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR 0x1600028UL

#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_SHFT 26
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_BASE_MASK 0x00003ffffc000000UL
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_SHFT 46
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_DUAL_HUB_MASK 0x0000400000000000UL
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_SHFT 63
#define UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR_ENABLE_MASK 0x8000000000000000UL

union uvh_rh_gam_mmr_overlay_config_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_mmr_overlay_config_mmr_s {
	unsigned long	rsvd_0_25: 26;  /*    */
	unsigned long	base     : 20;  /* RW */
	unsigned long	dual_hub :  1;  /* RW */
	unsigned long	rsvd_47_62: 16;  /*    */
	unsigned long	enable   :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                                 UVH_RTC                                   */
/* ========================================================================= */
#define UVH_RTC 0x28000UL

#define UVH_RTC_REAL_TIME_CLOCK_SHFT 0
#define UVH_RTC_REAL_TIME_CLOCK_MASK 0x00ffffffffffffffUL

union uvh_rtc_u {
    unsigned long	v;
    struct uvh_rtc_s {
	unsigned long	real_time_clock : 56;  /* RW */
	unsigned long	rsvd_56_63      :  8;  /*    */
    } s;
};

/* ========================================================================= */
/*                          UVH_SI_ADDR_MAP_CONFIG                           */
/* ========================================================================= */
#define UVH_SI_ADDR_MAP_CONFIG 0xc80000UL

#define UVH_SI_ADDR_MAP_CONFIG_M_SKT_SHFT 0
#define UVH_SI_ADDR_MAP_CONFIG_M_SKT_MASK 0x000000000000003fUL
#define UVH_SI_ADDR_MAP_CONFIG_N_SKT_SHFT 8
#define UVH_SI_ADDR_MAP_CONFIG_N_SKT_MASK 0x0000000000000f00UL

union uvh_si_addr_map_config_u {
    unsigned long	v;
    struct uvh_si_addr_map_config_s {
	unsigned long	m_skt :  6;  /* RW */
	unsigned long	rsvd_6_7:  2;  /*    */
	unsigned long	n_skt :  4;  /* RW */
	unsigned long	rsvd_12_63: 52;  /*    */
    } s;
};


#endif /* __ASM_X86_UV_MMRS__ */
