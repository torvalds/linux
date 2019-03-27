/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
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
 * Interface to the Trace buffer hardware.
 *
 * <hr>$Revision: 30644 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-tra.h>
#include <asm/octeon/cvmx-l2c.h>
#else
#include "cvmx.h"
#include "cvmx-tra.h"
#include "cvmx-l2c.h"
#endif

static const char *TYPE_ARRAY[] = {
    "DWB - Don't write back",
    "PL2 - Prefetch into L2",
    "PSL1 - Dcache fill, skip L2",
    "LDD - Dcache fill",
    "LDI - Icache/IO fill",
    "LDT - Icache/IO fill, skip L2",
    "STF - Store full",
    "STC - Store conditional",
    "STP - Store partial",
    "STT - Store full, skip L2",
    "IOBLD8 - IOB 8bit load",
    "IOBLD16 - IOB 16bit load",
    "IOBLD32 - IOB 32bit load",
    "IOBLD64 - IOB 64bit load",
    "IOBST - IOB store",
    "IOBDMA - Async IOB",
    "SAA - Store atomic add",
    "RSVD17",
    "RSVD18",
    "RSVD19",
    "RSVD20",
    "RSVD21",
    "RSVD22",
    "RSVD23",
    "RSVD24",
    "RSVD25",
    "RSVD26",
    "RSVD27",
    "RSVD28",
    "RSVD29",
    "RSVD30",
    "RSVD31"
};

static const char *TYPE_ARRAY2[] = {
    "NOP - None",
    "LDT - Icache/IO fill, skip L2",
    "LDI - Icache/IO fill",
    "PL2 - Prefetch into L2",
    "RPL2 - Mark for replacement in L2",
    "DWB - Don't write back",
    "RSVD6",
    "RSVD7",
    "LDD - Dcache fill",
    "PSL1 - Prefetch L1, skip L2",
    "RSVD10",
    "RSVD11",
    "RSVD12",
    "RSVD13",
    "RSVD14",
    "IOBDMA - Async IOB",
    "STF - Store full",
    "STT - Store full, skip L2",
    "STP - Store partial",
    "STC - Store conditional",
    "STFIL1 - Store full, invalidate L1",
    "STTIL1 - Store full, skip L2, invalidate L1",
    "FAS32 - Atomic 32bit swap",
    "FAS64 - Atomic 64bit swap",
    "WBIL2i - Writeback, invalidate, by index/way",
    "LTGL2i - Read tag@index/way",
    "STGL2i - Write tag@index/way",
    "RSVD27",
    "INVL2 - Invalidate, by address",
    "WBIL2 - Writeback, invalidate, by address",
    "WBL2 - Writeback, by address",
    "LCKL2 - Allocate, lock, by address",
    "IOBLD8 - IOB 8bit load",
    "IOBLD16 - IOB 16bit load",
    "IOBLD32 - IOB 32bit load",
    "IOBLD64 - IOB 64bit load",
    "IOBST8 - IOB 8bit store",
    "IOBST16 - IOB 16bit store",
    "IOBST32 - IOB 32bit store",
    "IOBST64 - IOB 64bit store",
    "SET8 - 8bit Atomic swap with 1's",
    "SET16 - 16bit Atomic swap with 1's",
    "SET32 - 32bit Atomic swap with 1's",
    "SET64 - 64bit Atomic swap with 1's",
    "CLR8 - 8bit Atomic swap with 0's",
    "CLR16 - 16bit Atomic swap with 0's",
    "CLR32 - 32bit Atomic swap with 0's",
    "CLR64 - 64bit Atomic swap with 0's",
    "INCR8 - 8bit Atomic fetch & add by 1",
    "INCR16 - 16bit Atomic fetch & add by 1",
    "INCR32 - 32bit Atomic fetch & add by 1",
    "INCR64 - 64bit Atomic fetch & add by 1",
    "DECR8 - 8bit Atomic fetch & add by -1",
    "DECR16 - 16bit Atomic fetch & add by -1",
    "DECR32 - 32bit Atomic fetch & add by -1",
    "DECR64 - 64bit Atomic fetch & add by -1",
    "RSVD56",
    "RSVD57",
    "FAA32 - 32bit Atomic fetch and add",
    "FAA64 - 64bit Atomic fetch and add",
    "RSVD60",
    "RSVD61",
    "SAA32 - 32bit Atomic add",
    "SAA64 - 64bit Atomic add"
};

static const char *SOURCE_ARRAY[] = {
    "PP0",
    "PP1",
    "PP2",
    "PP3",
    "PP4",
    "PP5",
    "PP6",
    "PP7",
    "PP8",
    "PP9",
    "PP10",
    "PP11",
    "PP12",
    "PP13",
    "PP14",
    "PP15",
    "PIP/IPD",
    "PKO-R",
    "FPA/TIM/DFA/PCI/ZIP/POW/PKO-W",
    "DWB",
    "RSVD20",
    "RSVD21",
    "RSVD22",
    "RSVD23",
    "RSVD24",
    "RSVD25",
    "RSVD26",
    "RSVD27",
    "RSVD28",
    "RSVD29",
    "RSVD30",
    "RSVD31",
    "PP16",
    "PP17",
    "PP18",
    "PP19",
    "PP20",
    "PP21",
    "PP22",
    "PP23",
    "PP24",
    "PP25",
    "PP26",
    "PP27",
    "PP28",
    "PP29",
    "PP30",
    "PP31"
};

static const char *DEST_ARRAY[] = {
    "CIU/GPIO",
    "RSVD1",
    "RSVD2",
    "PCI/PCIe/SLI",
    "KEY",
    "FPA",
    "DFA",
    "ZIP",
    "RNG",
    "IPD",
    "PKO",
    "RSVD11",
    "POW",
    "USB0",
    "RAD",
    "RSVD15",
    "RSVD16",
    "RSVD17",
    "RSVD18",
    "RSVD19",
    "RSVD20",
    "RSVD21",
    "RSVD22",
    "RSVD23",
    "RSVD24",
    "RSVD25",
    "RSVD26",
    "DPI",
    "RSVD28",
    "RSVD29",
    "FAU",
    "RSVD31"
};

int _cvmx_tra_unit = 0;

#define CVMX_TRA_SOURCE_MASK       (OCTEON_IS_MODEL(OCTEON_CN63XX) ? 0xf00ff : 0xfffff)
#define CVMX_TRA_DESTINATION_MASK  0xfffffffful

/**
 * @INTERNAL
 * Setup the trace buffer filter command mask. The bit position of filter commands
 * are different for each Octeon model.
 *
 * @param filter    Which event to log
 * @return          Bitmask of filter command based on the event.
 */
static uint64_t __cvmx_tra_set_filter_cmd_mask(cvmx_tra_filt_t filter)
{
    cvmx_tra_filt_cmd_t filter_command;

    if (OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX))
    {
        /* Bit positions of filter commands are different, map it accordingly */
        uint64_t cmd = 0;
        if ((filter & CVMX_TRA_FILT_ALL) == -1ull)
        {
            if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
                cmd = 0x1ffff;
            else
                cmd = 0xffff;
        }
        if (filter & CVMX_TRA_FILT_DWB)
            cmd |= 1ull<<0;
        if (filter & CVMX_TRA_FILT_PL2)
            cmd |= 1ull<<1;
        if (filter & CVMX_TRA_FILT_PSL1)
            cmd |= 1ull<<2;
        if (filter & CVMX_TRA_FILT_LDD)
            cmd |= 1ull<<3;
        if (filter & CVMX_TRA_FILT_LDI)
            cmd |= 1ull<<4;
        if (filter & CVMX_TRA_FILT_LDT)
            cmd |= 1ull<<5;
        if (filter & CVMX_TRA_FILT_STF)
            cmd |= 1ull<<6;
        if (filter & CVMX_TRA_FILT_STC)
            cmd |= 1ull<<7;
        if (filter & CVMX_TRA_FILT_STP)
            cmd |= 1ull<<8;
        if (filter & CVMX_TRA_FILT_STT)
            cmd |= 1ull<<9;
        if (filter & CVMX_TRA_FILT_IOBLD8)
            cmd |= 1ull<<10;
        if (filter & CVMX_TRA_FILT_IOBLD16)
            cmd |= 1ull<<11;
        if (filter & CVMX_TRA_FILT_IOBLD32)
            cmd |= 1ull<<12;
        if (filter & CVMX_TRA_FILT_IOBLD64)
            cmd |= 1ull<<13;
        if (filter & CVMX_TRA_FILT_IOBST)
            cmd |= 1ull<<14;
        if (filter & CVMX_TRA_FILT_IOBDMA)
            cmd |= 1ull<<15;
        if (OCTEON_IS_MODEL(OCTEON_CN5XXX) && (filter & CVMX_TRA_FILT_SAA))
            cmd |= 1ull<<16;

        filter_command.u64 = cmd;
    }
    else
    {
        if ((filter & CVMX_TRA_FILT_ALL) == -1ull)
            filter_command.u64 = CVMX_TRA_FILT_ALL;
        else
            filter_command.u64 = filter;

        filter_command.cn63xx.reserved_60_61 = 0;
        filter_command.cn63xx.reserved_56_57 = 0;
        filter_command.cn63xx.reserved_27_27 = 0;
        filter_command.cn63xx.reserved_10_14 = 0;
        filter_command.cn63xx.reserved_6_7 = 0;
    }
    return filter_command.u64;
}


/**
 * Setup the TRA buffer for use
 *
 * @param control TRA control setup
 * @param filter  Which events to log
 * @param source_filter
 *                Source match
 * @param dest_filter
 *                Destination match
 * @param address Address compare
 * @param address_mask
 *                Address mask
 */
void cvmx_tra_setup(cvmx_tra_ctl_t control, cvmx_tra_filt_t filter,
                    cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                    uint64_t address, uint64_t address_mask)
{
    cvmx_tra_filt_cmd_t filt_cmd;
    cvmx_tra_filt_sid_t filt_sid;
    cvmx_tra_filt_did_t filt_did;
    int tad;

    filt_cmd.u64 = __cvmx_tra_set_filter_cmd_mask(filter);
    filt_sid.u64 = source_filter & CVMX_TRA_SOURCE_MASK;
    filt_did.u64 = dest_filter & CVMX_TRA_DESTINATION_MASK;

    /* Address filtering does not work when IOBDMA filter command is enabled
       because of some caveats.  Disable the IOBDMA filter command. */
    if ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) 
        && ((filt_cmd.u64 & CVMX_TRA_FILT_IOBDMA) == CVMX_TRA_FILT_IOBDMA)
        && address_mask != 0)
    {
        cvmx_dprintf("The address-based filtering does not work with IOBDMAs, disabling the filter command.\n");
        filt_cmd.u64 &= ~(CVMX_TRA_FILT_IOBDMA);
    } 

    /* In OcteonII pass2, the mode bit is added to enable reading the trace 
       buffer data from different registers for lower and upper 64-bit value.
       This bit is reserved in other Octeon models. */
    control.s.rdat_md = 1;

    for (tad = 0; tad < CVMX_L2C_TADS; tad++)
    {
        cvmx_write_csr(CVMX_TRAX_CTL(tad),            control.u64);
        cvmx_write_csr(CVMX_TRAX_FILT_CMD(tad),       filt_cmd.u64);
        cvmx_write_csr(CVMX_TRAX_FILT_SID(tad),       filt_sid.u64);
        cvmx_write_csr(CVMX_TRAX_FILT_DID(tad),       filt_did.u64);
        cvmx_write_csr(CVMX_TRAX_FILT_ADR_ADR(tad),   address);
        cvmx_write_csr(CVMX_TRAX_FILT_ADR_MSK(tad),   address_mask);
    }
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_setup);
#endif

/**
 * Setup each TRA buffer for use
 *
 * @param tra     Which TRA buffer to use (0-3)
 * @param control TRA control setup
 * @param filter  Which events to log
 * @param source_filter
 *                Source match
 * @param dest_filter
 *                Destination match
 * @param address Address compare
 * @param address_mask
 *                Address mask
 */
void cvmx_tra_setup_v2(int tra, cvmx_tra_ctl_t control, cvmx_tra_filt_t filter,
                    cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                    uint64_t address, uint64_t address_mask)
{
    cvmx_tra_filt_cmd_t filt_cmd;
    cvmx_tra_filt_sid_t filt_sid;
    cvmx_tra_filt_did_t filt_did;

    if ((tra + 1) > CVMX_L2C_TADS)
    {
        cvmx_dprintf("cvmx_tra_setup_per_tra: Invalid tra(%d), max allowed (%d)\n", tra, CVMX_L2C_TADS - 1);
        tra = 0;
    }

    filt_cmd.u64 = __cvmx_tra_set_filter_cmd_mask(filter);
    filt_sid.u64 = source_filter & CVMX_TRA_SOURCE_MASK;
    filt_did.u64 = dest_filter & CVMX_TRA_DESTINATION_MASK;

    /* Address filtering does not work when IOBDMA filter command is enabled
       because of some caveats.  Disable the IOBDMA filter command. */
    if ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) 
        && ((filt_cmd.u64 & CVMX_TRA_FILT_IOBDMA) == CVMX_TRA_FILT_IOBDMA)
        && address_mask != 0)
    {
        cvmx_dprintf("The address-based filtering does not work with IOBDMAs, disabling the filter command.\n");
        filt_cmd.u64 &= ~(CVMX_TRA_FILT_IOBDMA);
    } 

    /* In OcteonII pass2, the mode bit is added to enable reading the trace 
       buffer data from different registers for lower and upper 64-bit value.
       This bit is reserved in other Octeon models. */
    control.s.rdat_md = 1;

    cvmx_write_csr(CVMX_TRAX_CTL(tra),            control.u64);
    cvmx_write_csr(CVMX_TRAX_FILT_CMD(tra),       filt_cmd.u64);
    cvmx_write_csr(CVMX_TRAX_FILT_SID(tra),       filt_sid.u64);
    cvmx_write_csr(CVMX_TRAX_FILT_DID(tra),       filt_did.u64);
    cvmx_write_csr(CVMX_TRAX_FILT_ADR_ADR(tra),   address);
    cvmx_write_csr(CVMX_TRAX_FILT_ADR_MSK(tra),   address_mask);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_setup_v2);
#endif

/**
 * Setup a TRA trigger. How the triggers are used should be
 * setup using cvmx_tra_setup.
 *
 * @param trigger Trigger to setup (0 or 1)
 * @param filter  Which types of events to trigger on
 * @param source_filter
 *                Source trigger match
 * @param dest_filter
 *                Destination trigger match
 * @param address Trigger address compare
 * @param address_mask
 *                Trigger address mask
 */
void cvmx_tra_trig_setup(uint64_t trigger, cvmx_tra_filt_t filter,
                         cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                         uint64_t address, uint64_t address_mask)
{
    cvmx_tra_filt_cmd_t tra_filt_cmd;
    cvmx_tra_filt_sid_t tra_filt_sid;
    cvmx_tra_filt_did_t tra_filt_did;
    int tad;

    tra_filt_cmd.u64 = __cvmx_tra_set_filter_cmd_mask(filter);
    tra_filt_sid.u64 = source_filter & CVMX_TRA_SOURCE_MASK;
    tra_filt_did.u64 = dest_filter & CVMX_TRA_DESTINATION_MASK;

    /* Address filtering does not work when IOBDMA filter command is enabled
       because of some caveats.  Disable the IOBDMA filter command. */
    if ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) 
        && ((tra_filt_cmd.u64 & CVMX_TRA_FILT_IOBDMA) == CVMX_TRA_FILT_IOBDMA)
        && address_mask != 0)
    {
        cvmx_dprintf("The address-based filtering does not work with IOBDMAs, disabling the filter command.\n");
        tra_filt_cmd.u64 &= ~(CVMX_TRA_FILT_IOBDMA);
    }

    for (tad = 0; tad < CVMX_L2C_TADS; tad++)
    {
        cvmx_write_csr(CVMX_TRAX_TRIG0_CMD(tad) + trigger * 64,       tra_filt_cmd.u64);
        cvmx_write_csr(CVMX_TRAX_TRIG0_SID(tad) + trigger * 64,       tra_filt_sid.u64);
        cvmx_write_csr(CVMX_TRAX_TRIG0_DID(tad) + trigger * 64,       tra_filt_did.u64);
        cvmx_write_csr(CVMX_TRAX_TRIG0_ADR_ADR(tad) + trigger * 64,   address);
        cvmx_write_csr(CVMX_TRAX_TRIG0_ADR_MSK(tad) + trigger * 64,   address_mask);
    }
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_trig_setup);
#endif

/**
 * Setup each TRA trigger. How the triggers are used should be
 * setup using cvmx_tra_setup.
 *
 * @param tra     Which TRA buffer to use (0-3)
 * @param trigger Trigger to setup (0 or 1)
 * @param filter  Which types of events to trigger on
 * @param source_filter
 *                Source trigger match
 * @param dest_filter
 *                Destination trigger match
 * @param address Trigger address compare
 * @param address_mask
 *                Trigger address mask
 */
void cvmx_tra_trig_setup_v2(int tra, uint64_t trigger, cvmx_tra_filt_t filter,
                         cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                         uint64_t address, uint64_t address_mask)
{
    cvmx_tra_filt_cmd_t tra_filt_cmd;
    cvmx_tra_filt_sid_t tra_filt_sid;
    cvmx_tra_filt_did_t tra_filt_did;

    tra_filt_cmd.u64 = __cvmx_tra_set_filter_cmd_mask(filter);
    tra_filt_sid.u64 = source_filter & CVMX_TRA_SOURCE_MASK;
    tra_filt_did.u64 = dest_filter & CVMX_TRA_DESTINATION_MASK;

    /* Address filtering does not work when IOBDMA filter command is enabled
       because of some caveats.  Disable the IOBDMA filter command. */
    if ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) 
        && ((tra_filt_cmd.u64 & CVMX_TRA_FILT_IOBDMA) == CVMX_TRA_FILT_IOBDMA)
        && address_mask != 0)
    {
        cvmx_dprintf("The address-based filtering does not work with IOBDMAs, disabling the filter command.\n");
        tra_filt_cmd.u64 &= ~(CVMX_TRA_FILT_IOBDMA);
    }

    cvmx_write_csr(CVMX_TRAX_TRIG0_CMD(tra) + trigger * 64,       tra_filt_cmd.u64);
    cvmx_write_csr(CVMX_TRAX_TRIG0_SID(tra) + trigger * 64,       tra_filt_sid.u64);
    cvmx_write_csr(CVMX_TRAX_TRIG0_DID(tra) + trigger * 64,       tra_filt_did.u64);
    cvmx_write_csr(CVMX_TRAX_TRIG0_ADR_ADR(tra) + trigger * 64,   address);
    cvmx_write_csr(CVMX_TRAX_TRIG0_ADR_MSK(tra) + trigger * 64,   address_mask);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_trig_setup_v2);
#endif

/**
 * Read an entry from the TRA buffer
 *
 * @return Value return. High bit will be zero if there wasn't any data
 */
cvmx_tra_data_t cvmx_tra_read(void)
{
    uint64_t address = CVMX_TRA_READ_DAT;
    cvmx_tra_data_t result;

    /* The trace buffer format is wider than 64-bits in OcteonII model,
       read the register again to get the second part of the data. */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X))
    {
        /* These reads need to be as close as possible to each other */
        result.u128.data = cvmx_read_csr(address);
        result.u128.datahi = cvmx_read_csr(address);
    }
    else if (!OCTEON_IS_MODEL(OCTEON_CN3XXX) && !OCTEON_IS_MODEL(OCTEON_CN5XXX))
    {
        /* OcteonII pass2 uses different trace buffer data register for reading
           lower and upper 64-bit values */
        result.u128.data = cvmx_read_csr(address);
        result.u128.datahi = cvmx_read_csr(CVMX_TRA_READ_DAT_HI);
    }
    else
    {
        result.u128.data = cvmx_read_csr(address);
        result.u128.datahi = 0;
    }

    return result;
}

/**
 * Read an entry from the TRA buffer from a given TRA unit.
 *
 * @param tra_unit  Trace buffer unit to read
 *
 * @return Value return. High bit will be zero if there wasn't any data
 */
cvmx_tra_data_t cvmx_tra_read_v2(int tra_unit)
{
    cvmx_tra_data_t result;

    result.u128.data = cvmx_read_csr(CVMX_TRAX_READ_DAT(tra_unit));
    result.u128.datahi = cvmx_read_csr(CVMX_TRAX_READ_DAT_HI(tra_unit));

    return result;
} 

/**
 * Decode a TRA entry into human readable output
 *
 * @param tra_ctl Trace control setup
 * @param data    Data to decode
 */
void cvmx_tra_decode_text(cvmx_tra_ctl_t tra_ctl, cvmx_tra_data_t data)
{
    if (OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX))
    {
        /* The type is a five bit field for some entries and 4 for other. The four
           bit entries can be mis-typed if the top is set */
        int type = data.cmn.type;

        if (type >= 0x1a)
            type &= 0xf;

        switch (type)
        {
            case 0:  /* DWB */
            case 1:  /* PL2 */
            case 2:  /* PSL1 */
            case 3:  /* LDD */
            case 4:  /* LDI */
            case 5:  /* LDT */
                cvmx_dprintf("0x%016llx %c%+10d %s %s 0x%016llx\n",
                    (unsigned long long)data.u128.data,
                    (data.cmn.discontinuity) ? 'D' : ' ',
                    data.cmn.timestamp << (tra_ctl.s.time_grn*3),
                    TYPE_ARRAY[type],
                    SOURCE_ARRAY[data.cmn.source],
                    (unsigned long long)data.cmn.address);
                break;
            case 6:  /* STF */
            case 7:  /* STC */
            case 8:  /* STP */
            case 9:  /* STT */
            case 16: /* SAA */
                cvmx_dprintf("0x%016llx %c%+10d %s %s mask=0x%02x 0x%016llx\n",
                   (unsigned long long)data.u128.data,
                   (data.cmn.discontinuity) ? 'D' : ' ',
                   data.cmn.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY[type],
                   SOURCE_ARRAY[data.store.source],
                   (unsigned int)data.store.mask,
                   (unsigned long long)data.store.address << 3);
                break;
            case 10:  /* IOBLD8 */
            case 11:  /* IOBLD16 */
            case 12:  /* IOBLD32 */
            case 13:  /* IOBLD64 */
            case 14:  /* IOBST */
                cvmx_dprintf("0x%016llx %c%+10d %s %s->%s subdid=0x%x 0x%016llx\n",
                   (unsigned long long)data.u128.data,
                   (data.cmn.discontinuity) ? 'D' : ' ',
                   data.cmn.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY[type],
                   SOURCE_ARRAY[data.iobld.source],
                   DEST_ARRAY[data.iobld.dest],
                   (unsigned int)data.iobld.subid,
                   (unsigned long long)data.iobld.address);
                break;
            case 15:  /* IOBDMA */
                cvmx_dprintf("0x%016llx %c%+10d %s %s->%s len=0x%x 0x%016llx\n",
                   (unsigned long long)data.u128.data,
                   (data.cmn.discontinuity) ? 'D' : ' ',
                   data.cmn.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY[type],
                   SOURCE_ARRAY[data.iob.source],
                   DEST_ARRAY[data.iob.dest],
                   (unsigned int)data.iob.mask,
                   (unsigned long long)data.iob.address << 3);
                break;
            default:
                cvmx_dprintf("0x%016llx %c%+10d Unknown format\n",
                   (unsigned long long)data.u128.data,
                   (data.cmn.discontinuity) ? 'D' : ' ',
                   data.cmn.timestamp << (tra_ctl.s.time_grn*3));
                break;
        }
    }
    else
    {
        int type;
        int srcId;

        type = data.cmn2.type;

        switch (1ull<<type)
        {
            case CVMX_TRA_FILT_DECR64:
            case CVMX_TRA_FILT_DECR32:
            case CVMX_TRA_FILT_DECR16:
            case CVMX_TRA_FILT_DECR8:
            case CVMX_TRA_FILT_INCR64:
            case CVMX_TRA_FILT_INCR32:
            case CVMX_TRA_FILT_INCR16:
            case CVMX_TRA_FILT_INCR8:
            case CVMX_TRA_FILT_CLR64:
            case CVMX_TRA_FILT_CLR32:
            case CVMX_TRA_FILT_CLR16:
            case CVMX_TRA_FILT_CLR8:
            case CVMX_TRA_FILT_SET64:
            case CVMX_TRA_FILT_SET32:
            case CVMX_TRA_FILT_SET16:
            case CVMX_TRA_FILT_SET8:
            case CVMX_TRA_FILT_LCKL2:
            case CVMX_TRA_FILT_WBIL2:
            case CVMX_TRA_FILT_INVL2:
            case CVMX_TRA_FILT_STGL2I:
            case CVMX_TRA_FILT_LTGL2I:
            case CVMX_TRA_FILT_WBIL2I:
            case CVMX_TRA_FILT_WBL2:
            case CVMX_TRA_FILT_DWB:
            case CVMX_TRA_FILT_RPL2:
            case CVMX_TRA_FILT_PL2:
            case CVMX_TRA_FILT_LDI:
            case CVMX_TRA_FILT_LDT:
                /* CN68XX has 32 cores which are distributed to use different
                   trace buffers, decode the core that has data */
                if (OCTEON_IS_MODEL(OCTEON_CN68XX))
                {
                    if (data.cmn2.source <= 7)
                    {
                        srcId = _cvmx_tra_unit + (data.cmn2.source * 4);
                        if (srcId >= 16)
                            srcId += 16;
                    }
                    else
                        srcId = (data.cmn2.source);
                }
                else
                        srcId = (data.cmn2.source);
                
                cvmx_dprintf("0x%016llx%016llx %c%+10d %s %s 0x%016llx%llx\n",
                   (unsigned long long)data.u128.datahi, (unsigned long long)data.u128.data,
                   (data.cmn2.discontinuity) ? 'D' : ' ',
                   data.cmn2.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY2[type],
                   SOURCE_ARRAY[srcId],
                   (unsigned long long)data.cmn2.addresshi,
                   (unsigned long long)data.cmn2.addresslo);
                break;
            case CVMX_TRA_FILT_PSL1:
            case CVMX_TRA_FILT_LDD:
            case CVMX_TRA_FILT_FAS64:
            case CVMX_TRA_FILT_FAS32:
            case CVMX_TRA_FILT_FAA64:
            case CVMX_TRA_FILT_FAA32:
            case CVMX_TRA_FILT_SAA64:
            case CVMX_TRA_FILT_SAA32:
            case CVMX_TRA_FILT_STC:
            case CVMX_TRA_FILT_STF:
            case CVMX_TRA_FILT_STP:
            case CVMX_TRA_FILT_STT:
            case CVMX_TRA_FILT_STTIL1:
            case CVMX_TRA_FILT_STFIL1:
                /* CN68XX has 32 cores which are distributed to use different
                   trace buffers, decode the core that has data */
                if (OCTEON_IS_MODEL(OCTEON_CN68XX))
                {
                    if (data.store2.source <= 7)
                    {
                        srcId = _cvmx_tra_unit + (data.store2.source * 4);
                        if (srcId >= 16)
                            srcId += 16;
                    }
                    else
                        srcId = data.store2.source;
                }
                else
                        srcId = data.store2.source;

                cvmx_dprintf("0x%016llx%016llx %c%+10d %s %s mask=0x%02x 0x%016llx%llx\n",
                   (unsigned long long)data.u128.datahi, (unsigned long long)data.u128.data,
                   (data.cmn2.discontinuity) ? 'D' : ' ',
                   data.cmn2.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY2[type],
                   SOURCE_ARRAY[srcId],
                   (unsigned int)data.store2.mask,
                   (unsigned long long)data.store2.addresshi,
                   (unsigned long long)data.store2.addresslo);
                break;
            case CVMX_TRA_FILT_IOBST64:
            case CVMX_TRA_FILT_IOBST32:
            case CVMX_TRA_FILT_IOBST16:
            case CVMX_TRA_FILT_IOBST8:
            case CVMX_TRA_FILT_IOBLD64:
            case CVMX_TRA_FILT_IOBLD32:
            case CVMX_TRA_FILT_IOBLD16:
            case CVMX_TRA_FILT_IOBLD8:
                /* CN68XX has 32 cores which are distributed to use different
                   trace buffers, decode the core that has data */
                if (OCTEON_IS_MODEL(OCTEON_CN68XX))
                {
                    if (data.iobld2.source <= 7)
                    {
                        srcId = _cvmx_tra_unit + (data.iobld2.source * 4);
                        if (srcId >= 16)
                            srcId += 16;
                    }
                    else
                        srcId = data.iobld2.source;
                }
                else
                        srcId = data.iobld2.source;

                cvmx_dprintf("0x%016llx%016llx %c%+10d %s %s->%s subdid=0x%x 0x%016llx%llx\n",
                   (unsigned long long)data.u128.datahi, (unsigned long long)data.u128.data,
                   (data.cmn2.discontinuity) ? 'D' : ' ',
                   data.cmn2.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY2[type],
                   SOURCE_ARRAY[srcId],
                   DEST_ARRAY[data.iobld2.dest],
                   (unsigned int)data.iobld2.subid,
                   (unsigned long long)data.iobld2.addresshi,
                   (unsigned long long)data.iobld2.addresslo);
                break;
            case CVMX_TRA_FILT_IOBDMA:
                /* CN68XX has 32 cores which are distributed to use different
                   trace buffers, decode the core that has data */
                if (OCTEON_IS_MODEL(OCTEON_CN68XX))
                {
                    if (data.iob2.source <= 7)
                    {
                        srcId = _cvmx_tra_unit + (data.iob2.source * 4);
                        if (srcId >= 16)
                            srcId += 16;
                    }
                    else
                        srcId = data.iob2.source;
                }
                else
                        srcId = data.iob2.source;

                cvmx_dprintf("0x%016llx%016llx %c%+10d %s %s->%s len=0x%x 0x%016llx%llx\n",
                   (unsigned long long)data.u128.datahi, (unsigned long long)data.u128.data,
                   (data.iob2.discontinuity) ? 'D' : ' ',
                   data.iob2.timestamp << (tra_ctl.s.time_grn*3),
                   TYPE_ARRAY2[type],
                   SOURCE_ARRAY[srcId],
                   DEST_ARRAY[data.iob2.dest],
                   (unsigned int)data.iob2.mask,
                   (unsigned long long)data.iob2.addresshi << 3,
                   (unsigned long long)data.iob2.addresslo << 3);
                break;
            default:
                cvmx_dprintf("0x%016llx%016llx %c%+10d Unknown format\n",
                   (unsigned long long)data.u128.datahi, (unsigned long long)data.u128.data,
                   (data.cmn2.discontinuity) ? 'D' : ' ',
                   data.cmn2.timestamp << (tra_ctl.s.time_grn*3));
                break;
        }
    }
}

/**
 * Display the entire trace buffer. It is advised that you
 * disable the trace buffer before calling this routine
 * otherwise it could infinitely loop displaying trace data
 * that it created.
 */
void cvmx_tra_display(void)
{
    int valid = 0;

    /* Collect data from each TRA unit for decoding */
    if (CVMX_L2C_TADS > 1)
    {
        cvmx_trax_ctl_t tra_ctl;
        cvmx_tra_data_t data[4];
        int tad;
        do 
        {
            valid = 0;
            for (tad = 0; tad < CVMX_L2C_TADS; tad++)
                data[tad] = cvmx_tra_read_v2(tad);

            for (tad = 0; tad < CVMX_L2C_TADS; tad++)
            {
                tra_ctl.u64 = cvmx_read_csr(CVMX_TRAX_CTL(tad));

                if (data[tad].cmn2.valid)
                {
                    _cvmx_tra_unit = tad;
                    cvmx_tra_decode_text(tra_ctl, data[tad]);
                    valid = 1;
                }
            }
        } while (valid);
    }
    else
    {
        cvmx_tra_ctl_t tra_ctl;
        cvmx_tra_data_t data;

        tra_ctl.u64 = cvmx_read_csr(CVMX_TRA_CTL);

        do
        {
            data = cvmx_tra_read();
            if ((OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN5XXX)) && data.cmn.valid)
                valid = 1;
            else if (data.cmn2.valid)
                valid = 1;
            else
                valid = 0;
    
            if (valid)
                cvmx_tra_decode_text(tra_ctl, data);

        } while (valid);
    }
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_display);
#endif

/**
 * Display the entire trace buffer. It is advised that you
 * disable the trace buffer before calling this routine
 * otherwise it could infinitely loop displaying trace data
 * that it created.
 *
 * @param tra_unit   Which TRA buffer to use.
 */
void cvmx_tra_display_v2(int tra_unit)
{
    int valid = 0;

    cvmx_trax_ctl_t tra_ctl;
    cvmx_tra_data_t data;

    valid = 0;
    tra_ctl.u64 = cvmx_read_csr(CVMX_TRAX_CTL(tra_unit));

    do 
    {
        data = cvmx_tra_read_v2(tra_unit);
        if (data.cmn2.valid)
        {
            _cvmx_tra_unit = tra_unit; 
            cvmx_tra_decode_text(tra_ctl, data);
            valid = 1;
        }
    } while (valid);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_tra_display_v2);
#endif
