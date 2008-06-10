/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV MMR definitions
 *
 * Copyright (C) 2007-2008 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef __ASM_IA64_UV_MMRS__
#define __ASM_IA64_UV_MMRS__

/*
 *       AUTO GENERATED - Do not edit
 */

 #define UV_MMR_ENABLE		(1UL << 63)

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
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR 0x16000d0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_0_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_alias210_redirect_config_0_mmr_s {
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
    } s;
};

/* ========================================================================= */
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR 0x16000e0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_1_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_alias210_redirect_config_1_mmr_s {
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
    } s;
};

/* ========================================================================= */
/*                UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR                  */
/* ========================================================================= */
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR 0x16000f0UL

#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR_DEST_BASE_SHFT 24
#define UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR_DEST_BASE_MASK 0x00003fffff000000UL

union uvh_rh_gam_alias210_redirect_config_2_mmr_u {
    unsigned long	v;
    struct uvh_rh_gam_alias210_redirect_config_2_mmr_s {
	unsigned long	rsvd_0_23 : 24;  /*    */
	unsigned long	dest_base : 22;  /* RW */
	unsigned long	rsvd_46_63: 18;  /*    */
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

/* ========================================================================= */
/*                       UVH_SI_ALIAS0_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS0_OVERLAY_CONFIG 0xc80008UL

#define UVH_SI_ALIAS0_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS0_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias0_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias0_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                       UVH_SI_ALIAS1_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS1_OVERLAY_CONFIG 0xc80010UL

#define UVH_SI_ALIAS1_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS1_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias1_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias1_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};

/* ========================================================================= */
/*                       UVH_SI_ALIAS2_OVERLAY_CONFIG                        */
/* ========================================================================= */
#define UVH_SI_ALIAS2_OVERLAY_CONFIG 0xc80018UL

#define UVH_SI_ALIAS2_OVERLAY_CONFIG_BASE_SHFT 24
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_BASE_MASK 0x00000000ff000000UL
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_M_ALIAS_SHFT 48
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_M_ALIAS_MASK 0x001f000000000000UL
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_ENABLE_SHFT 63
#define UVH_SI_ALIAS2_OVERLAY_CONFIG_ENABLE_MASK 0x8000000000000000UL

union uvh_si_alias2_overlay_config_u {
    unsigned long	v;
    struct uvh_si_alias2_overlay_config_s {
	unsigned long	rsvd_0_23: 24;  /*    */
	unsigned long	base    :  8;  /* RW */
	unsigned long	rsvd_32_47: 16;  /*    */
	unsigned long	m_alias :  5;  /* RW */
	unsigned long	rsvd_53_62: 10;  /*    */
	unsigned long	enable  :  1;  /* RW */
    } s;
};


#endif /* __ASM_IA64_UV_MMRS__ */
