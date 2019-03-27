/***********************license start***************
 * Copyright (c) 2011  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * @file
 *
 * Helper utilities for qlm.
 *
 * <hr>$Revision: 70129 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-bootmem.h>
#include <asm/octeon/cvmx-helper-jtag.h>
#include <asm/octeon/cvmx-qlm.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-sriox-defs.h>
#include <asm/octeon/cvmx-sriomaintx-defs.h>
#include <asm/octeon/cvmx-pciercx-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-bootmem.h"
#include "cvmx-helper-jtag.h"
#include "cvmx-qlm.h"
#else
#include "cvmx.h"
#include "cvmx-bootmem.h"
#include "cvmx-helper-jtag.h"
#include "cvmx-qlm.h"
#endif

#endif

/**
 * The JTAG chain for CN52XX and CN56XX is 4 * 268 bits long, or 1072.
 * CN5XXX full chain shift is:
 *     new data => lane 3 => lane 2 => lane 1 => lane 0 => data out
 * The JTAG chain for CN63XX is 4 * 300 bits long, or 1200.
 * The JTAG chain for CN68XX is 4 * 304 bits long, or 1216.
 * The JTAG chain for CN66XX/CN61XX/CNF71XX is 4 * 304 bits long, or 1216.
 * CN6XXX full chain shift is:
 *     new data => lane 0 => lane 1 => lane 2 => lane 3 => data out
 * Shift LSB first, get LSB out
 */
extern const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn52xx[];
extern const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn56xx[];
extern const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn63xx[];
extern const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn66xx[];
extern const __cvmx_qlm_jtag_field_t __cvmx_qlm_jtag_field_cn68xx[];

#define CVMX_QLM_JTAG_UINT32 40
#ifdef CVMX_BUILD_FOR_LINUX_HOST
extern void octeon_remote_read_mem(void *buffer, uint64_t physical_address, int length);
extern void octeon_remote_write_mem(uint64_t physical_address, const void *buffer, int length);
uint32_t __cvmx_qlm_jtag_xor_ref[5][CVMX_QLM_JTAG_UINT32];
#else
typedef uint32_t qlm_jtag_uint32_t[CVMX_QLM_JTAG_UINT32];
CVMX_SHARED qlm_jtag_uint32_t *__cvmx_qlm_jtag_xor_ref;
#endif


/**
 * Return the number of QLMs supported by the chip
 * 
 * @return  Number of QLMs
 */
int cvmx_qlm_get_num(void)
{
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        return 5;
    else if (OCTEON_IS_MODEL(OCTEON_CN66XX))
        return 3;
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX))
        return 3;
    else if (OCTEON_IS_MODEL(OCTEON_CN61XX))
        return 3;
    else if (OCTEON_IS_MODEL(OCTEON_CN56XX))
        return 4;
    else if (OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX))
        return 2;

    //cvmx_dprintf("Warning: cvmx_qlm_get_num: This chip does not have QLMs\n");
    return 0;
}

/**
 * Return the qlm number based on the interface
 *
 * @param interface  Interface to look up
 */
int cvmx_qlm_interface(int interface)
{
	if (OCTEON_IS_MODEL(OCTEON_CN61XX)) {
		return (interface == 0) ? 2 : 0;
	} else if (OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)) {
		return 2 - interface;
	} else {
		/* Must be cn68XX */
		switch(interface) {
		case 1:
			return 0;
		default:
			return interface;
		}
	}
}

/**
 * Return number of lanes for a given qlm
 * 
 * @return  Number of lanes
 */
int cvmx_qlm_get_lanes(int qlm)
{
    if (OCTEON_IS_MODEL(OCTEON_CN61XX) && qlm == 1)
        return 2;
    else if (OCTEON_IS_MODEL(OCTEON_CNF71XX))
        return 2;
    
    return 4;
}

/**
 * Get the QLM JTAG fields based on Octeon model on the supported chips. 
 *
 * @return  qlm_jtag_field_t structure
 */
const __cvmx_qlm_jtag_field_t *cvmx_qlm_jtag_get_field(void)
{
    /* Figure out which JTAG chain description we're using */
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        return __cvmx_qlm_jtag_field_cn68xx;
    else if (OCTEON_IS_MODEL(OCTEON_CN66XX) 
             || OCTEON_IS_MODEL(OCTEON_CN61XX)
             || OCTEON_IS_MODEL(OCTEON_CNF71XX))
        return __cvmx_qlm_jtag_field_cn66xx;
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX))
        return __cvmx_qlm_jtag_field_cn63xx;
    else if (OCTEON_IS_MODEL(OCTEON_CN56XX))
        return __cvmx_qlm_jtag_field_cn56xx;
    else if (OCTEON_IS_MODEL(OCTEON_CN52XX))
        return __cvmx_qlm_jtag_field_cn52xx;
    else
    {
        //cvmx_dprintf("cvmx_qlm_jtag_get_field: Needs update for this chip\n");
        return NULL;
    }
}

/**
 * Get the QLM JTAG length by going through qlm_jtag_field for each
 * Octeon model that is supported
 *
 * @return return the length.
 */
int cvmx_qlm_jtag_get_length(void)
{
    const __cvmx_qlm_jtag_field_t *qlm_ptr = cvmx_qlm_jtag_get_field();
    int length = 0;

    /* Figure out how many bits are in the JTAG chain */
    while (qlm_ptr != NULL && qlm_ptr->name)
    {
        if (qlm_ptr->stop_bit > length)
            length = qlm_ptr->stop_bit + 1;
        qlm_ptr++;
    }
    return length;
}

/**
 * Initialize the QLM layer
 */
void cvmx_qlm_init(void)
{
    int qlm;
    int qlm_jtag_length;
    char *qlm_jtag_name = "cvmx_qlm_jtag";
    int qlm_jtag_size = CVMX_QLM_JTAG_UINT32 * 8 * 4;
    static uint64_t qlm_base = 0;
    const cvmx_bootmem_named_block_desc_t *desc;
    
#ifndef CVMX_BUILD_FOR_LINUX_HOST
    /* Skip actual JTAG accesses on simulator */
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
        return;
#endif

    qlm_jtag_length = cvmx_qlm_jtag_get_length();

    if (4 * qlm_jtag_length > (int)sizeof(__cvmx_qlm_jtag_xor_ref[0]) * 8)
    {
        cvmx_dprintf("ERROR: cvmx_qlm_init: JTAG chain larger than XOR ref size\n");
        return;
    }

    /* No need to initialize the initial JTAG state if cvmx_qlm_jtag
       named block is already created. */
    if ((desc = cvmx_bootmem_find_named_block(qlm_jtag_name)) != NULL)
    {
#ifdef CVMX_BUILD_FOR_LINUX_HOST
        char buffer[qlm_jtag_size];

        octeon_remote_read_mem(buffer, desc->base_addr, qlm_jtag_size);
        memcpy(__cvmx_qlm_jtag_xor_ref, buffer, qlm_jtag_size);
#else
        __cvmx_qlm_jtag_xor_ref = cvmx_phys_to_ptr(desc->base_addr);
#endif
        /* Initialize the internal JTAG */
        cvmx_helper_qlm_jtag_init();
        return;
    }

    /* Create named block to store the initial JTAG state. */
    qlm_base = cvmx_bootmem_phy_named_block_alloc(qlm_jtag_size, 0, 0, 128, qlm_jtag_name, CVMX_BOOTMEM_FLAG_END_ALLOC);

    if (qlm_base == -1ull)
    {
        cvmx_dprintf("ERROR: cvmx_qlm_init: Error in creating %s named block\n", qlm_jtag_name);
        return;
    }

#ifndef CVMX_BUILD_FOR_LINUX_HOST
    __cvmx_qlm_jtag_xor_ref = cvmx_phys_to_ptr(qlm_base);
#endif
    memset(__cvmx_qlm_jtag_xor_ref, 0, qlm_jtag_size);

    /* Initialize the internal JTAG */
    cvmx_helper_qlm_jtag_init();

    /* Read the XOR defaults for the JTAG chain */
    for (qlm=0; qlm<cvmx_qlm_get_num(); qlm++)
    {
        int i;
        /* Capture the reset defaults */
        cvmx_helper_qlm_jtag_capture(qlm);
        /* Save the reset defaults. This will shift out too much data, but
           the extra zeros don't hurt anything */
        for (i=0; i<CVMX_QLM_JTAG_UINT32; i++)
            __cvmx_qlm_jtag_xor_ref[qlm][i] = cvmx_helper_qlm_jtag_shift(qlm, 32, 0);
    }

#ifdef CVMX_BUILD_FOR_LINUX_HOST
    /* Update the initial state for oct-remote utils. */
    {
        char buffer[qlm_jtag_size];

        memcpy(buffer, &__cvmx_qlm_jtag_xor_ref, qlm_jtag_size);
        octeon_remote_write_mem(qlm_base, buffer, qlm_jtag_size);
    }
#endif

    /* Apply speed tweak as a workaround for errata G-16094. */
    __cvmx_qlm_speed_tweak();
    __cvmx_qlm_pcie_idle_dac_tweak();
}

/**
 * Lookup the bit information for a JTAG field name
 *
 * @param name   Name to lookup
 *
 * @return Field info, or NULL on failure
 */
static const __cvmx_qlm_jtag_field_t *__cvmx_qlm_lookup_field(const char *name)
{
    const __cvmx_qlm_jtag_field_t *ptr = cvmx_qlm_jtag_get_field();
    while (ptr->name)
    {
        if (strcmp(name, ptr->name) == 0)
            return ptr;
        ptr++;
    }
    cvmx_dprintf("__cvmx_qlm_lookup_field: Illegal field name %s\n", name);
    return NULL;
}

/**
 * Get a field in a QLM JTAG chain
 *
 * @param qlm    QLM to get
 * @param lane   Lane in QLM to get
 * @param name   String name of field
 *
 * @return JTAG field value
 */
uint64_t cvmx_qlm_jtag_get(int qlm, int lane, const char *name)
{
    const __cvmx_qlm_jtag_field_t *field = __cvmx_qlm_lookup_field(name);
    int qlm_jtag_length = cvmx_qlm_jtag_get_length();
    int num_lanes = cvmx_qlm_get_lanes(qlm);

    if (!field)
        return 0;

    /* Capture the current settings */
    cvmx_helper_qlm_jtag_capture(qlm);
    /* Shift past lanes we don't care about. CN6XXX shifts lane 3 first */
    cvmx_helper_qlm_jtag_shift_zeros(qlm, qlm_jtag_length * (num_lanes-1-lane));    /* Shift to the start of the field */
    cvmx_helper_qlm_jtag_shift_zeros(qlm, field->start_bit);
    /* Shift out the value and return it */
    return cvmx_helper_qlm_jtag_shift(qlm, field->stop_bit - field->start_bit + 1, 0);
}

/**
 * Set a field in a QLM JTAG chain
 *
 * @param qlm    QLM to set
 * @param lane   Lane in QLM to set, or -1 for all lanes
 * @param name   String name of field
 * @param value  Value of the field
 */
void cvmx_qlm_jtag_set(int qlm, int lane, const char *name, uint64_t value)
{
    int i, l;
    uint32_t shift_values[CVMX_QLM_JTAG_UINT32];
    int num_lanes = cvmx_qlm_get_lanes(qlm);
    const __cvmx_qlm_jtag_field_t *field = __cvmx_qlm_lookup_field(name);
    int qlm_jtag_length = cvmx_qlm_jtag_get_length();
    int total_length = qlm_jtag_length * num_lanes;
    int bits = 0;

    if (!field)
        return;

    /* Get the current state */
    cvmx_helper_qlm_jtag_capture(qlm);
    for (i=0; i<CVMX_QLM_JTAG_UINT32; i++)
        shift_values[i] = cvmx_helper_qlm_jtag_shift(qlm, 32, 0);

    /* Put new data in our local array */
    for (l=0; l<num_lanes; l++)
    {
        uint64_t new_value = value;
        int bits;
        if ((l != lane) && (lane != -1))
            continue;
        for (bits = field->start_bit + (num_lanes-1-l)*qlm_jtag_length;
             bits <= field->stop_bit + (num_lanes-1-l)*qlm_jtag_length;
             bits++)
        {
            if (new_value & 1)
                shift_values[bits/32] |= 1<<(bits&31);
            else
                shift_values[bits/32] &= ~(1<<(bits&31));
            new_value>>=1;
        }
    }

    /* Shift out data and xor with reference */
    while (bits < total_length)
    {
        uint32_t shift = shift_values[bits/32] ^ __cvmx_qlm_jtag_xor_ref[qlm][bits/32];
        int width = total_length - bits;
        if (width > 32)
            width = 32;
        cvmx_helper_qlm_jtag_shift(qlm, width, shift);
        bits += 32;
    }

    /* Update the new data */
    cvmx_helper_qlm_jtag_update(qlm);
    /* Always give the QLM 1ms to settle after every update. This may not
       always be needed, but some of the options make significant
       electrical changes */
    cvmx_wait_usec(1000);
}
                                                                                
/**
 * Errata G-16094: QLM Gen2 Equalizer Default Setting Change.
 * CN68XX pass 1.x and CN66XX pass 1.x QLM tweak. This function tweaks the
 * JTAG setting for a QLMs to run better at 5 and 6.25Ghz.
 */
void __cvmx_qlm_speed_tweak(void)
{
    cvmx_mio_qlmx_cfg_t qlm_cfg;
    int num_qlms = 0;
    int qlm;

    if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_X))
        num_qlms = 5;
    else if (OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_X))
        num_qlms = 3;
    else
        return;

    /* Loop through the QLMs */
    for (qlm = 0; qlm < num_qlms; qlm++)
    {
        /* Read the QLM speed */
	qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));

        /* If the QLM is at 6.25Ghz or 5Ghz then program JTAG */
        if ((qlm_cfg.s.qlm_spd == 5) || (qlm_cfg.s.qlm_spd == 12) ||
            (qlm_cfg.s.qlm_spd == 0) || (qlm_cfg.s.qlm_spd == 6) ||
            (qlm_cfg.s.qlm_spd == 11))
        {
            cvmx_qlm_jtag_set(qlm, -1, "rx_cap_gen2", 0x1);
            cvmx_qlm_jtag_set(qlm, -1, "rx_eq_gen2", 0x8);
        }
    }
}

/**
 * Errata G-16174: QLM Gen2 PCIe IDLE DAC change.
 * CN68XX pass 1.x, CN66XX pass 1.x and CN63XX pass 1.0-2.2 QLM tweak.
 * This function tweaks the JTAG setting for a QLMs for PCIe to run better.
 */
void __cvmx_qlm_pcie_idle_dac_tweak(void)
{
    int num_qlms = 0;
    int qlm;

    if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_X))
        num_qlms = 5;
    else if (OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_X))
        num_qlms = 3;
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
             OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_X))
        num_qlms = 3;
    else
        return;

    /* Loop through the QLMs */
    for (qlm = 0; qlm < num_qlms; qlm++)
        cvmx_qlm_jtag_set(qlm, -1, "idle_dac", 0x2);
}

#ifndef CVMX_BUILD_FOR_LINUX_HOST
/**
 * Get the speed (Gbaud) of the QLM in Mhz.
 *
 * @param qlm    QLM to examine
 *
 * @return Speed in Mhz
 */
int cvmx_qlm_get_gbaud_mhz(int qlm)
{
    if (OCTEON_IS_MODEL(OCTEON_CN63XX))
    {
        if (qlm == 2)
        {
            cvmx_gmxx_inf_mode_t inf_mode;
            inf_mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(0));
            switch (inf_mode.s.speed)
            {
                case 0: return 5000;    /* 5     Gbaud */
                case 1: return 2500;    /* 2.5   Gbaud */
                case 2: return 2500;    /* 2.5   Gbaud */
                case 3: return 1250;    /* 1.25  Gbaud */
                case 4: return 1250;    /* 1.25  Gbaud */
                case 5: return 6250;    /* 6.25  Gbaud */
                case 6: return 5000;    /* 5     Gbaud */
                case 7: return 2500;    /* 2.5   Gbaud */
                case 8: return 3125;    /* 3.125 Gbaud */
                case 9: return 2500;    /* 2.5   Gbaud */
                case 10: return 1250;   /* 1.25  Gbaud */
                case 11: return 5000;   /* 5     Gbaud */
                case 12: return 6250;   /* 6.25  Gbaud */
                case 13: return 3750;   /* 3.75  Gbaud */
                case 14: return 3125;   /* 3.125 Gbaud */
                default: return 0;      /* Disabled */
            }
        }
        else
        {
            cvmx_sriox_status_reg_t status_reg;
            status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(qlm));
            if (status_reg.s.srio)
            {
                cvmx_sriomaintx_port_0_ctl2_t sriomaintx_port_0_ctl2;
                sriomaintx_port_0_ctl2.u32 = cvmx_read_csr(CVMX_SRIOMAINTX_PORT_0_CTL2(qlm));
                switch (sriomaintx_port_0_ctl2.s.sel_baud)
                {
                    case 1: return 1250;    /* 1.25  Gbaud */
                    case 2: return 2500;    /* 2.5   Gbaud */
                    case 3: return 3125;    /* 3.125 Gbaud */
                    case 4: return 5000;    /* 5     Gbaud */
                    case 5: return 6250;    /* 6.250 Gbaud */
                    default: return 0;      /* Disabled */
                }
            }
            else
            {
                cvmx_pciercx_cfg032_t pciercx_cfg032;
                pciercx_cfg032.u32 = cvmx_read_csr(CVMX_PCIERCX_CFG032(qlm));
                switch (pciercx_cfg032.s.ls)
                {
                    case 1:
                        return 2500;
                    case 2:
                        return 5000;
                    case 4:
                        return 8000;
                    default:
                    {
                        cvmx_mio_rst_boot_t mio_rst_boot;
                        mio_rst_boot.u64 = cvmx_read_csr(CVMX_MIO_RST_BOOT);
                        if ((qlm == 0) && mio_rst_boot.s.qlm0_spd == 0xf)
                            return 0;
                        if ((qlm == 1) && mio_rst_boot.s.qlm1_spd == 0xf)
                            return 0;
                        return 5000; /* Best guess I can make */
                    }
                }
            }
        }
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF71XX))
    {
        cvmx_mio_qlmx_cfg_t qlm_cfg;

        qlm_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));
        switch (qlm_cfg.s.qlm_spd)
        {
            case 0: return 5000;    /* 5     Gbaud */
            case 1: return 2500;    /* 2.5   Gbaud */
            case 2: return 2500;    /* 2.5   Gbaud */
            case 3: return 1250;    /* 1.25  Gbaud */
            case 4: return 1250;    /* 1.25  Gbaud */
            case 5: return 6250;    /* 6.25  Gbaud */
            case 6: return 5000;    /* 5     Gbaud */
            case 7: return 2500;    /* 2.5   Gbaud */
            case 8: return 3125;    /* 3.125 Gbaud */
            case 9: return 2500;    /* 2.5   Gbaud */
            case 10: return 1250;   /* 1.25  Gbaud */
            case 11: return 5000;   /* 5     Gbaud */
            case 12: return 6250;   /* 6.25  Gbaud */
            case 13: return 3750;   /* 3.75  Gbaud */
            case 14: return 3125;   /* 3.125 Gbaud */
            default: return 0;      /* Disabled */
        }
    }
    return 0;
}
#endif

/*
 * Read QLM and return status based on CN66XX.
 * @return  Return 1 if QLM is SGMII
 *                 2 if QLM is XAUI
 *                 3 if QLM is PCIe gen2 / gen1
 *                 4 if QLM is SRIO 1x4 short / long
 *                 5 if QLM is SRIO 2x2 short / long
 *                 6 if QLM is SRIO 4x1 short / long
 *                 7 if QLM is PCIe 1x2 gen2 / gen1
 *                 8 if QLM is PCIe 2x1 gen2 / gen1
 *                 9 if QLM is ILK
 *                 10 if QLM is RXAUI
 *                 -1 otherwise
 */
int cvmx_qlm_get_status(int qlm)
{
    cvmx_mio_qlmx_cfg_t qlmx_cfg;

    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
    {
        qlmx_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));
        /* QLM is disabled when QLM SPD is 15. */
        if (qlmx_cfg.s.qlm_spd == 15)
            return  -1;

        switch (qlmx_cfg.s.qlm_cfg)
        {
            case 0: /* PCIE */
                return 3;
            case 1: /* ILK */
                return 9;
            case 2: /* SGMII */
                return 1;
            case 3: /* XAUI */
                return 2;
            case 7: /* RXAUI */
                return 10;
            default: return -1;
        }
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN66XX))
    {
        qlmx_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));
        /* QLM is disabled when QLM SPD is 15. */
        if (qlmx_cfg.s.qlm_spd == 15)
            return  -1;

        switch (qlmx_cfg.s.qlm_cfg)
        {
            case 0x9: /* SGMII */
                return 1;
            case 0xb: /* XAUI */
                return 2;
            case 0x0: /* PCIE gen2 */
            case 0x8: /* PCIE gen2 (alias) */
            case 0x2: /* PCIE gen1 */
            case 0xa: /* PCIE gen1 (alias) */
                return 3;
            case 0x1: /* SRIO 1x4 short */
            case 0x3: /* SRIO 1x4 long */
                return 4;
            case 0x4: /* SRIO 2x2 short */
            case 0x6: /* SRIO 2x2 long */
                return 5;
            case 0x5: /* SRIO 4x1 short */
            case 0x7: /* SRIO 4x1 long */
                if (!OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_0))
                    return 6;
            default:
                return -1;
        }
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX))
    {
        cvmx_sriox_status_reg_t status_reg;
        /* For now skip qlm2 */
        if (qlm == 2)
        {
            cvmx_gmxx_inf_mode_t inf_mode;
            inf_mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(0));
            if (inf_mode.s.speed == 15) 
                return -1;
            else if(inf_mode.s.mode == 0)
                return 1;
            else
                return 2;
        }
        status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(qlm));
        if (status_reg.s.srio)
            return 4;
        else
            return 3;
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN61XX))
    {
        qlmx_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));
        /* QLM is disabled when QLM SPD is 15. */
        if (qlmx_cfg.s.qlm_spd == 15)
            return  -1;

        switch(qlm)
        {
            case 0:
                switch (qlmx_cfg.s.qlm_cfg)
                {
                    case 0: /* PCIe 1x4 gen2 / gen1 */
                        return 3;
                    case 2: /* SGMII */
                        return 1;
                    case 3: /* XAUI */
                        return 2;
                    default: return -1;
                }
                break;
            case 1:
                switch (qlmx_cfg.s.qlm_cfg)
                {
                    case 0: /* PCIe 1x2 gen2 / gen1 */
                        return 7;
                    case 1: /* PCIe 2x1 gen2 / gen1 */
                        return 8;
                    default: return -1;
                }
                break;
            case 2:
                switch (qlmx_cfg.s.qlm_cfg)
                {
                    case 2: /* SGMII */
                        return 1;
                    case 3: /* XAUI */
                        return 2;
                    default: return -1;
                }
                break;
        }
    }
    else if (OCTEON_IS_MODEL(OCTEON_CNF71XX))
    {
        qlmx_cfg.u64 = cvmx_read_csr(CVMX_MIO_QLMX_CFG(qlm));
        /* QLM is disabled when QLM SPD is 15. */
        if (qlmx_cfg.s.qlm_spd == 15)
            return  -1;

        switch(qlm)
        {
            case 0:
                if (qlmx_cfg.s.qlm_cfg == 2) /* SGMII */
                    return 1;
                break;
            case 1:
                switch (qlmx_cfg.s.qlm_cfg)
                {
                    case 0: /* PCIe 1x2 gen2 / gen1 */
                        return 7;
                    case 1: /* PCIe 2x1 gen2 / gen1 */
                        return 8;
                    default: return -1;
                }
                break;
        }
    }
    return -1;
}
