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
 * Configuration functions for low latency memory.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-llm.h"
#include "cvmx-sysinfo.h"
#include "cvmx-csr-db.h"

#define	MIN(a,b) (((a)<(b))?(a):(b))

typedef struct
{
    uint32_t dfa_memcfg0_base;
    uint32_t dfa_memcfg1_base;
    uint32_t mrs_dat_p0bunk0;
    uint32_t mrs_dat_p0bunk1;
    uint32_t mrs_dat_p1bunk0;
    uint32_t mrs_dat_p1bunk1;
    uint8_t  p0_ena;
    uint8_t  p1_ena;
    uint8_t  bunkport;
} rldram_csr_config_t;





int rld_csr_config_generate(llm_descriptor_t *llm_desc_ptr, rldram_csr_config_t *cfg_ptr);


void print_rld_cfg(rldram_csr_config_t *cfg_ptr);
void write_rld_cfg(rldram_csr_config_t *cfg_ptr);
static void cn31xx_dfa_memory_init(void);

static uint32_t process_address_map_str(uint32_t mrs_dat, char *addr_str);



#ifndef CVMX_LLM_NUM_PORTS
#warning WARNING: default CVMX_LLM_NUM_PORTS used.  Defaults deprecated, please set in executive-config.h
#define CVMX_LLM_NUM_PORTS 1
#endif


#if (CVMX_LLM_NUM_PORTS != 1) && (CVMX_LLM_NUM_PORTS != 2)
#error "Invalid CVMX_LLM_NUM_PORTS value: must be 1 or 2\n"
#endif

int cvmx_llm_initialize()
{
    if (cvmx_llm_initialize_desc(NULL) < 0)
        return -1;

    return 0;
}


int cvmx_llm_get_default_descriptor(llm_descriptor_t *llm_desc_ptr)
{
    cvmx_sysinfo_t *sys_ptr;
    sys_ptr = cvmx_sysinfo_get();

    if (!llm_desc_ptr)
        return -1;

    memset(llm_desc_ptr, 0, sizeof(llm_descriptor_t));

    llm_desc_ptr->cpu_hz = cvmx_clock_get_rate(CVMX_CLOCK_CORE);

    if (sys_ptr->board_type == CVMX_BOARD_TYPE_EBT3000)
    { // N3K->RLD0 Address Swizzle
        strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 19 20 08 07 06 05 04 03 02 01 00 09 18 17 16 15 14 13 12 11 10");
        // N3K->RLD1 Address Swizzle
        strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 20 00 08 07 06 05 04 13 02 01 03 09 18 17 16 15 14 10 12 11 19");
        /* NOTE: The ebt3000 has a strange RLDRAM configuration for validation purposes.  It is not recommended to have
        ** different amounts of memory on different ports as that renders some memory unusable */
        llm_desc_ptr->rld0_bunks = 2;
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld0_mbytes = 128;          // RLD0: 4x 32Mx9
        llm_desc_ptr->rld1_mbytes = 64;           // RLD1: 2x 16Mx18
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_EBT5800)
    {
        strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 20 00 08 07 06 05 04 13 02 01 03 09 18 17 16 15 14 10 12 11 19");
        strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 20 00 08 07 06 05 04 13 02 01 03 09 18 17 16 15 14 10 12 11 19");
        llm_desc_ptr->rld0_bunks = 2;
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld0_mbytes = 128;
        llm_desc_ptr->rld1_mbytes = 128;
        llm_desc_ptr->max_rld_clock_mhz = 400;  /* CN58XX needs a max clock speed for selecting optimal divisor */
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_EBH3000)
    {
        strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 19 20 08 07 06 05 04 03 02 01 00 09 18 17 16 15 14 13 12 11 10");
        strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 19 20 08 07 06 05 04 03 02 01 00 09 18 17 16 15 14 13 12 11 10");
        llm_desc_ptr->rld0_bunks = 2;
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld0_mbytes = 128;
        llm_desc_ptr->rld1_mbytes = 128;
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_THUNDER)
    {

        if (sys_ptr->board_rev_major >= 4)
        {
            strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 13 11 01 02 07 19 03 18 10 12 20 06 04 08 17 05 14 16 00 09 15");
            strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 11 13 04 08 17 05 14 16 00 09 15 06 01 02 07 19 03 18 10 12 20");
            strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 02 19 18 17 16 09 14 13 20 11 10 01 08 03 06 15 04 07 05 12 00");
            strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 19 02 08 03 06 15 04 07 05 12 00 01 18 17 16 09 14 13 20 11 10");
        }
        else
        {
            strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
            strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
            strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
            strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        }

        llm_desc_ptr->rld0_bunks = 2;
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld0_mbytes = 128;
        llm_desc_ptr->rld1_mbytes = 128;
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_NICPRO2)
    {
        strcpy(llm_desc_ptr->addr_rld0_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld0_bb_str, "22 21 19 20 08 07 06 05 04 03 02 01 00 09 18 17 16 15 14 13 12 11 10");
        strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 19 20 08 07 06 05 04 03 02 01 00 09 18 17 16 15 14 13 12 11 10");
        llm_desc_ptr->rld0_bunks = 2;
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld0_mbytes = 256;
        llm_desc_ptr->rld1_mbytes = 256;
        llm_desc_ptr->max_rld_clock_mhz = 400;  /* CN58XX needs a max clock speed for selecting optimal divisor */
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_EBH3100)
    {
        /* CN31xx DFA memory is DDR based, so it is completely different from the CN38XX DFA memory */
        llm_desc_ptr->rld0_bunks = 1;
        llm_desc_ptr->rld0_mbytes = 256;
    }
    else if (sys_ptr->board_type == CVMX_BOARD_TYPE_KBP)
    {
        strcpy(llm_desc_ptr->addr_rld0_fb_str, "");
        strcpy(llm_desc_ptr->addr_rld0_bb_str, "");
        llm_desc_ptr->rld0_bunks = 0;
        llm_desc_ptr->rld0_mbytes = 0;
        strcpy(llm_desc_ptr->addr_rld1_fb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        strcpy(llm_desc_ptr->addr_rld1_bb_str, "22 21 20 19 18 17 16 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00");
        llm_desc_ptr->rld1_bunks = 2;
        llm_desc_ptr->rld1_mbytes = 64;
    }
    else
    {
        cvmx_dprintf("No default LLM configuration available for board %s (%d)\n", cvmx_board_type_to_string(sys_ptr->board_type),  sys_ptr->board_type);
        return -1;
    }

    return(0);
}

int cvmx_llm_initialize_desc(llm_descriptor_t *llm_desc_ptr)
{
    cvmx_sysinfo_t *sys_ptr;
    sys_ptr = cvmx_sysinfo_get();
    llm_descriptor_t default_llm_desc;

    memset(&default_llm_desc, 0, sizeof(default_llm_desc));
    if (sys_ptr->board_type == CVMX_BOARD_TYPE_SIM)
    {
        cvmx_dprintf("Skipping llm configuration for simulator.\n");
        return 0;
    }

    if (sys_ptr->board_type == CVMX_BOARD_TYPE_EBH3100)
    {
        /* CN31xx DFA memory is DDR based, so it is completely different from the CN38XX DFA memory
        ** config descriptors are not supported yet.*/
        cvmx_dprintf("Warning: preliminary DFA memory configuration\n");
        cn31xx_dfa_memory_init();
        return(256*1024*1024);
    }

    /* If no descriptor passed, generate default descriptor based on board type.
    ** Fail if no default available for given board type
    */
    if (!llm_desc_ptr)
    {
        /* Get default descriptor */
        if (0 > cvmx_llm_get_default_descriptor(&default_llm_desc))
            return -1;

        /* Disable second port depending on CVMX config */
        if (CVMX_LLM_NUM_PORTS == 1)
	  default_llm_desc.rld0_bunks = 0;        // For single port: Force RLD0(P1) to appear EMPTY

        cvmx_dprintf("Using default LLM configuration for board %s (%d)\n", cvmx_board_type_to_string(sys_ptr->board_type),  sys_ptr->board_type);

        llm_desc_ptr = &default_llm_desc;
    }



    rldram_csr_config_t ebt3000_rld_cfg;
    if (!rld_csr_config_generate(llm_desc_ptr, &ebt3000_rld_cfg))
    {
        cvmx_dprintf("Configuring %d llm port(s).\n", !!llm_desc_ptr->rld0_bunks + !!llm_desc_ptr->rld1_bunks);
        write_rld_cfg(&ebt3000_rld_cfg);
    }
    else
    {
        cvmx_dprintf("Error creating rldram configuration\n");
        return(-1);
    }

    /* Compute how much memory is configured
    ** Memory is interleaved, so if one port has more than the other some memory is not usable */

    /* If both ports are enabled, handle the case where one port has more than the other.
    ** This is an unusual and not recommended configuration that exists on the ebt3000 board */
    if (!!llm_desc_ptr->rld0_bunks && !!llm_desc_ptr->rld1_bunks)
        llm_desc_ptr->rld0_mbytes = llm_desc_ptr->rld1_mbytes = MIN(llm_desc_ptr->rld0_mbytes, llm_desc_ptr->rld1_mbytes);

    return(((!!llm_desc_ptr->rld0_bunks) * llm_desc_ptr->rld0_mbytes
          + (!!llm_desc_ptr->rld1_bunks) * llm_desc_ptr->rld1_mbytes) * 1024*1024);
}

//======================
// SUPPORT FUNCTIONS:
//======================
//======================================================================
// Extracts srcvec[srcbitpos] and places it in return int (bit[0])
int bit_extract ( int srcvec,         // source word (to extract)
                  int srcbitpos       // source bit position
                )
{
    return(((1 << srcbitpos) & srcvec) >> srcbitpos);
}
//======================================================================
// Inserts srcvec[0] into dstvec[dstbitpos] (without affecting other bits)
int bit_insert ( int srcvec,           // srcvec[0] = bit to be inserted
                 int dstbitpos,        // Bit position to insert into returned int
                 int dstvec            // dstvec (destination vector)
               )
{
    return((srcvec << dstbitpos) | dstvec);      // Shift bit to insert into bit position/OR with accumulated number
}
//======================================================================

int rld_csr_config_generate(llm_descriptor_t *llm_desc_ptr, rldram_csr_config_t *cfg_ptr)
{
    char *addr_rld0_fb_str;
    char *addr_rld0_bb_str;
    char *addr_rld1_fb_str;
    char *addr_rld1_bb_str;
    int eclk_ps;
    int mtype = 0;                           // MTYPE (0: RLDRAM/1: FCRAM
    int trcmin = 20;                         // tRC(min) - from RLDRAM data sheet
    int trc_cyc;                             // TRC(cyc)
    int trc_mod;
    int trl_cyc;                             // TRL(cyc)
    int twl_cyc;                             // TWL(cyc)
    int tmrsc_cyc = 6;                       // tMRSC(cyc)  [2-7]
    int mclk_ps;                             // DFA Memory Clock(in ps) = 2x eclk
    int rldcfg = 99;                         // RLDRAM-II CFG (1,2,3)
    int mrs_odt = 0;                         // RLDRAM MRS A[9]=ODT (default)
    int mrs_impmatch = 0;                    // RLDRAM MRS A[8]=Impedance Matching (default)
    int mrs_dllrst = 1;                      // RLDRAM MRS A[7]=DLL Reset (default)
    uint32_t mrs_dat;
    int mrs_dat_p0bunk0 = 0;                 // MRS Register Data After Address Map (for Port0 Bunk0)
    int mrs_dat_p0bunk1 = 0;                 // MRS Register Data After Address Map (for Port0 Bunk1)
    int mrs_dat_p1bunk0 = 0;                 // MRS Register Data After Address Map (for Port1 Bunk0)
    int mrs_dat_p1bunk1 = 0;                 // MRS Register Data After Address Map (for Port1 Bunk1)
    int p0_ena = 0;                          // DFA Port#0 Enabled
    int p1_ena = 0;                          // DFA Port#1 Enabled
    int memport = 0;                       // Memory(MB) per Port [MAX=512]
    int membunk;                             // Memory(MB) per Bunk
    int bunkport = 0;                        // Bunks/Port [1/2]
    int pbunk = 0;                               // Physical Bunk(or Rank) encoding for address bit
    int tref_ms = 32;                        // tREF(ms) (RLDRAM-II overall device refresh interval
    int trefi_ns;                            // tREFI(ns) = tREF(ns)/#rows/bank
    int rows = 8;                            // #rows/bank (K) typically 8K
    int ref512int;
    int ref512mod;
    int tskw_cyc = 0;
    int fprch = 1;
    int bprch = 0;
    int dfa_memcfg0_base = 0;
    int dfa_memcfg1_base = 0;
    int tbl = 1;                             // tBL (1: 2-burst /2: 4-burst)
    int rw_dly;
    int wr_dly;
    int r2r = 1;
    int sil_lat = 1;
    int clkdiv = 2;  /* CN38XX is fixed at 2, CN58XX supports 2,3,4 */
    int clkdiv_enc = 0x0;  /* Encoded clock divisor, only used for CN58XX */

    if (!llm_desc_ptr)
        return -1;

    /* Setup variables from descriptor */

    addr_rld0_fb_str = llm_desc_ptr->addr_rld0_fb_str;
    addr_rld0_bb_str = llm_desc_ptr->addr_rld0_bb_str;
    addr_rld1_fb_str = llm_desc_ptr->addr_rld1_fb_str;
    addr_rld1_bb_str = llm_desc_ptr->addr_rld1_bb_str;

    p0_ena = !!llm_desc_ptr->rld1_bunks;        // NOTE: P0 == RLD1
    p1_ena = !!llm_desc_ptr->rld0_bunks;        // NOTE: P1 == RLD0

    // Massage the code, so that if the user had imbalanced memory per-port (or imbalanced bunks/port), we
    // at least try to configure 'workable' memory.
    if (p0_ena && p1_ena)  // IF BOTH PORTS Enabled (imbalanced memory), select smaller of BOTH
    {
        memport = MIN(llm_desc_ptr->rld0_mbytes, llm_desc_ptr->rld1_mbytes);
        bunkport = MIN(llm_desc_ptr->rld0_bunks, llm_desc_ptr->rld1_bunks);
    }
    else if (p0_ena) // P0=RLD1 Enabled
    {
        memport = llm_desc_ptr->rld1_mbytes;
        bunkport = llm_desc_ptr->rld1_bunks;
    }
    else if (p1_ena) // P1=RLD0 Enabled
    {
        memport = llm_desc_ptr->rld0_mbytes;
        bunkport = llm_desc_ptr->rld0_bunks;
    }
    else
        return -1;

    uint32_t eclk_mhz = llm_desc_ptr->cpu_hz/1000000;



    /* Tweak skew based on cpu clock */
    if (eclk_mhz <= 367)
    {
        tskw_cyc = 0;
    }
    else
    {
        tskw_cyc = 1;
    }

    /* Determine clock divider ratio (only required for CN58XX) */
    if (OCTEON_IS_MODEL(OCTEON_CN58XX))
    {
        uint32_t max_llm_clock_mhz = llm_desc_ptr->max_rld_clock_mhz;
        if (!max_llm_clock_mhz)
        {
            max_llm_clock_mhz = 400;  /* Default to 400 MHz */
            cvmx_dprintf("Warning, using default max_rld_clock_mhz of: %lu MHz\n", (unsigned long)max_llm_clock_mhz);
        }

        /* Compute the divisor, and round up */
        clkdiv = eclk_mhz/max_llm_clock_mhz;
        if (clkdiv * max_llm_clock_mhz < eclk_mhz)
            clkdiv++;

        if (clkdiv > 4)
        {
            cvmx_dprintf("ERROR: CN58XX LLM clock divisor out of range\n");
            goto TERMINATE;
        }
        if (clkdiv < 2)
            clkdiv = 2;

        cvmx_dprintf("Using llm clock divisor: %d, llm clock is: %lu MHz\n", clkdiv, (unsigned long)eclk_mhz/clkdiv);
        /* Translate divisor into bit encoding for register */
        /* 0 -> div 2
        ** 1 -> reserved
        ** 2 -> div 3
        ** 3 -> div 4
        */
        if (clkdiv == 2)
            clkdiv_enc = 0;
        else
            clkdiv_enc = clkdiv - 1;

    /* Odd divisor needs sil_lat to be 2 */
        if (clkdiv == 0x3)
            sil_lat = 2;

        /* Increment tskw for high clock speeds */
        if ((unsigned long)eclk_mhz/clkdiv >= 375)
            tskw_cyc += 1;
    }

    eclk_ps = (1000000+(eclk_mhz-1)) / eclk_mhz;  // round up if nonzero remainder
    //=======================================================================

    //=======================================================================
    // Now, Query User for DFA Memory Type
    if (mtype != 0)
    {
        goto TERMINATE;         // Complete this code for FCRAM usage on N3K-P2
    }
    //=======================================================================
    // Query what the tRC(min) value is from the data sheets
    //=======================================================================
    // Now determine the Best CFG based on Memory clock(ps) and tRCmin(ns)
    mclk_ps = eclk_ps * clkdiv;
    trc_cyc = ((trcmin * 1000)/mclk_ps);
    trc_mod = ((trcmin * 1000) % mclk_ps);
    // If remainder exists, bump up to the next integer multiple
    if (trc_mod != 0)
    {
        trc_cyc = trc_cyc + 1;
    }
    // If tRC is now ODD, then bump it to the next EVEN integer (RLDRAM-II does not support odd tRC values at this time).
    if (trc_cyc & 1)
    {
        trc_cyc = trc_cyc + 1;           // Bump it to an even #
    }
    // RLDRAM CFG Range Check: If the computed trc_cyc is less than 4, then set it to min CFG1 [tRC=4]
    if (trc_cyc < 4)
    {
        trc_cyc = 4;             // If computed trc_cyc < 4 then clamp to 4
    }
    else if (trc_cyc > 8)
    {    // If the computed trc_cyc > 8, then report an error (because RLDRAM cannot support a tRC>8
        goto TERMINATE;
    }
    // Assuming all is ok(up to here)
    // At this point the tRC_cyc has been clamped  between 4 and 8 (and is even), So it can only be 4,6,8 which are
    // the RLDRAM valid CFG range values.
    trl_cyc = trc_cyc;                 // tRL = tRC (for RLDRAM=II)
    twl_cyc = trl_cyc + 1;             // tWL = tRL + 1 (for RLDRAM-II)
    // NOTE: RLDRAM-II (as of 4/25/05) only have 3 supported CFG encodings:
    if (trc_cyc == 4)
    {
        rldcfg = 1;           // CFG #1 (tRL=4/tRC=4/tWL=5)
    }
    else if (trc_cyc == 6)
    {
        rldcfg = 2;           // CFG #2 (tRL=6/tRC=6/tWL=7)
    }
    else if (trc_cyc == 8)
    {
        rldcfg = 3;           // CFG #3 (tRL=8/tRC=8/tWL=9)
    }
    else
    {
        goto TERMINATE;
    }
    //=======================================================================
    mrs_dat = ( (mrs_odt << 9) | (mrs_impmatch << 8) | (mrs_dllrst << 7) | rldcfg );
    //=======================================================================
    // If there is only a single bunk, then skip over address mapping queries (which are not required)
    if (bunkport == 1)
    {
        goto CALC_PBUNK;
    }

    /* Process the address mappings */
    /* Note that that RLD0 pins corresponds to Port#1, and
    **                RLD1 pins corresponds to Port#0.
    */
    mrs_dat_p1bunk0 = process_address_map_str(mrs_dat, addr_rld0_fb_str);
    mrs_dat_p1bunk1 = process_address_map_str(mrs_dat, addr_rld0_bb_str);
    mrs_dat_p0bunk0 = process_address_map_str(mrs_dat, addr_rld1_fb_str);
    mrs_dat_p0bunk1 = process_address_map_str(mrs_dat, addr_rld1_bb_str);


    //=======================================================================
    CALC_PBUNK:
    // Determine the PBUNK field (based on Memory/Bunk)
    // This determines the addr bit used to distinguish when crossing a bunk.
    // NOTE: For RLDRAM, the bunk bit is extracted from 'a' programmably selected high
    // order addr bit. [linear address per-bunk]
    if (bunkport == 2)
    {
        membunk = (memport / 2);
    }
    else
    {
        membunk = memport;
    }
    if (membunk == 16)
    {       // 16MB/bunk MA[19]
        pbunk = 0;
    }
    else if (membunk == 32)
    {  // 32MB/bunk MA[20]
        pbunk = 1;
    }
    else if (membunk == 64)
    {  // 64MB/bunk MA[21]
        pbunk = 2;
    }
    else if (membunk == 128)
    { // 128MB/bunk MA[22]
        pbunk = 3;
    }
    else if (membunk == 256)
    { // 256MB/bunk MA[23]
        pbunk = 4;
    }
    else if (membunk == 512)
    { // 512MB/bunk
    }
    //=======================================================================
    //=======================================================================
    //=======================================================================
    // Now determine N3K REFINT
    trefi_ns = (tref_ms * 1000 * 1000) / (rows * 1024);
    ref512int = ((trefi_ns * 1000) / (eclk_ps * 512));
    ref512mod = ((trefi_ns * 1000) % (eclk_ps * 512));
    //=======================================================================
    // Ask about tSKW
#if 0
    if (tskw_ps ==  0)
    {
        tskw_cyc = 0;
    }
    else
    { // CEILING function
        tskw_cyc = (tskw_ps / eclk_ps);
        tskw_mod = (tskw_ps % eclk_ps);
        if (tskw_mod != 0)
        {  // If there's a remainder - then bump to next (+1)
            tskw_cyc = tskw_cyc + 1;
        }
    }
#endif
    if (tskw_cyc > 3)
    {
        goto TERMINATE;
    }

    tbl = 1;        // BLEN=2 (ALWAYs for RLDRAM)
    //=======================================================================
    // RW_DLY = (ROUND_UP{[[(TRL+TBL)*2 + tSKW + BPRCH] + 1] / 2}) - tWL
    rw_dly = ((((trl_cyc + tbl) * 2 + tskw_cyc + bprch) + 1) / 2);
    if (rw_dly & 1)
    { // If it's ODD then round up
        rw_dly = rw_dly + 1;
    }
    rw_dly = rw_dly - twl_cyc +1 ;
    if (rw_dly < 0)
    { // range check - is it positive
        goto TERMINATE;
    }
    //=======================================================================
    // WR_DLY = (ROUND_UP[[(tWL + tBL)*2 - tSKW + FPRCH] / 2]) - tRL
    wr_dly = (((twl_cyc + tbl) * 2 - tskw_cyc + fprch) / 2);
    if (wr_dly & 1)
    { // If it's ODD then round up
        wr_dly = wr_dly + 1;
    }
    wr_dly = wr_dly - trl_cyc + 1;
    if (wr_dly < 0)
    { // range check - is it positive
        goto TERMINATE;
    }


    dfa_memcfg0_base = 0;
    dfa_memcfg0_base = ( p0_ena |
                         (p1_ena << 1) |
                         (mtype << 3) |
                         (sil_lat << 4) |
                         (rw_dly << 6) |
                         (wr_dly << 10) |
                         (fprch << 14) |
                         (bprch << 16) |
                         (0 << 18) |         // BLEN=0(2-burst for RLDRAM)
                         (pbunk << 19) |
                         (r2r << 22) |       // R2R=1
    			 (clkdiv_enc << 28 )
                       );


    dfa_memcfg1_base = 0;
    dfa_memcfg1_base = ( ref512int |
                         (tskw_cyc << 4) |
                         (trl_cyc << 8) |
                         (twl_cyc << 12) |
                         (trc_cyc << 16) |
                         (tmrsc_cyc << 20)
                       );




    cfg_ptr->dfa_memcfg0_base = dfa_memcfg0_base;
    cfg_ptr->dfa_memcfg1_base = dfa_memcfg1_base;
    cfg_ptr->mrs_dat_p0bunk0 =  mrs_dat_p0bunk0;
    cfg_ptr->mrs_dat_p1bunk0 =  mrs_dat_p1bunk0;
    cfg_ptr->mrs_dat_p0bunk1 =  mrs_dat_p0bunk1;
    cfg_ptr->mrs_dat_p1bunk1 =  mrs_dat_p1bunk1;
    cfg_ptr->p0_ena =           p0_ena;
    cfg_ptr->p1_ena =           p1_ena;
    cfg_ptr->bunkport =         bunkport;
    //=======================================================================

    return(0);
    TERMINATE:
    return(-1);

}



static uint32_t process_address_map_str(uint32_t mrs_dat, char *addr_str)
{
    int count = 0;
    int amap [23];
    uint32_t new_mrs_dat = 0;

//    cvmx_dprintf("mrs_dat: 0x%x, str: %x\n", mrs_dat, addr_str);
    char *charptr = strtok(addr_str," ");
    while ((charptr != NULL) & (count <= 22))
    {
        amap[22-count] = atoi(charptr);         // Assign the AMAP Array
        charptr = strtok(NULL," ");             // Get Next char string (which represents next addr bit mapping)
        count++;
    }
    // Now do the bit swap of MRSDAT (based on address mapping)
    uint32_t mrsdat_bit;
    for (count=0;count<=22;count++)
    {
        mrsdat_bit = bit_extract(mrs_dat, count);
        new_mrs_dat = bit_insert(mrsdat_bit, amap[count], new_mrs_dat);
    }

    return new_mrs_dat;
}


//#define PRINT_LLM_CONFIG
#ifdef PRINT_LLM_CONFIG
#define ll_printf printf
#else
#define ll_printf(...)
#define cvmx_csr_db_decode(...)
#endif

static void cn31xx_dfa_memory_init(void)
{
    if (OCTEON_IS_MODEL(OCTEON_CN31XX))
    {
        cvmx_dfa_ddr2_cfg_t  dfaCfg;
        cvmx_dfa_eclkcfg_t   dfaEcklCfg;
        cvmx_dfa_ddr2_addr_t dfaAddr;
        cvmx_dfa_ddr2_tmg_t  dfaTmg;
        cvmx_dfa_ddr2_pll_t  dfaPll;
        int mem_freq_hz = 533*1000000;
        int ref_freq_hz = cvmx_sysinfo_get()->dfa_ref_clock_hz;
        if (!ref_freq_hz)
            ref_freq_hz = 33*1000000;

        cvmx_dprintf ("Configuring DFA memory for %d MHz operation.\n",mem_freq_hz/1000000);

          /* Turn on the DFA memory port. */
        dfaCfg.u64 = cvmx_read_csr (CVMX_DFA_DDR2_CFG);
        dfaCfg.s.prtena = 1;
        cvmx_write_csr (CVMX_DFA_DDR2_CFG, dfaCfg.u64);

          /* Start the PLL alignment sequence */
        dfaPll.u64 = 0;
        dfaPll.s.pll_ratio  = mem_freq_hz/ref_freq_hz         /*400Mhz / 33MHz*/;
        dfaPll.s.pll_div2   = 1              /*400 - 1 */;
        dfaPll.s.pll_bypass = 0;
        cvmx_write_csr (CVMX_DFA_DDR2_PLL, dfaPll.u64);

        dfaPll.s.pll_init = 1;
        cvmx_write_csr (CVMX_DFA_DDR2_PLL, dfaPll.u64);

        cvmx_wait (RLD_INIT_DELAY); //want 150uS
        dfaPll.s.qdll_ena = 1;
        cvmx_write_csr (CVMX_DFA_DDR2_PLL, dfaPll.u64);

        cvmx_wait (RLD_INIT_DELAY); //want 10us
        dfaEcklCfg.u64 = 0;
        dfaEcklCfg.s.dfa_frstn = 1;
        cvmx_write_csr (CVMX_DFA_ECLKCFG, dfaEcklCfg.u64);

          /* Configure the DFA Memory */
        dfaCfg.s.silo_hc = 1 /*400 - 1 */;
        dfaCfg.s.silo_qc = 0 /*400 - 0 */;
        dfaCfg.s.tskw    = 1 /*400 - 1 */;
        dfaCfg.s.ref_int = 0x820 /*533 - 0x820  400 - 0x618*/;
        dfaCfg.s.trfc    = 0x1A  /*533 - 0x23   400 - 0x1A*/;
        dfaCfg.s.fprch   = 0; /* 1 more conservative*/
        dfaCfg.s.bprch   = 0; /* 1 */
        cvmx_write_csr (CVMX_DFA_DDR2_CFG, dfaCfg.u64);

        dfaEcklCfg.u64 = cvmx_read_csr (CVMX_DFA_ECLKCFG);
        dfaEcklCfg.s.maxbnk = 1;
        cvmx_write_csr (CVMX_DFA_ECLKCFG, dfaEcklCfg.u64);

        dfaAddr.u64 = cvmx_read_csr (CVMX_DFA_DDR2_ADDR);
        dfaAddr.s.num_cols    = 0x1;
        dfaAddr.s.num_colrows = 0x2;
        dfaAddr.s.num_rnks    = 0x1;
        cvmx_write_csr (CVMX_DFA_DDR2_ADDR, dfaAddr.u64);

        dfaTmg.u64 =  cvmx_read_csr (CVMX_DFA_DDR2_TMG);
        dfaTmg.s.ddr2t    = 0;
        dfaTmg.s.tmrd     = 0x2;
        dfaTmg.s.caslat   = 0x4 /*400 - 0x3, 500 - 0x4*/;
        dfaTmg.s.pocas    = 0;
        dfaTmg.s.addlat   = 0;
        dfaTmg.s.trcd     = 4   /*400 - 3, 500 - 4*/;
        dfaTmg.s.trrd     = 2;
        dfaTmg.s.tras     = 0xB /*400 - 8, 500 - 0xB*/;
        dfaTmg.s.trp      = 4   /*400 - 3, 500 - 4*/;
        dfaTmg.s.twr      = 4   /*400 - 3, 500 - 4*/;
        dfaTmg.s.twtr     = 2   /*400 - 2 */;
        dfaTmg.s.tfaw     = 0xE /*400 - 0xA, 500 - 0xE*/;
        dfaTmg.s.r2r_slot = 0;
        dfaTmg.s.dic      = 0;  /*400 - 0 */
        dfaTmg.s.dqsn_ena = 0;
        dfaTmg.s.odt_rtt  = 0;
        cvmx_write_csr (CVMX_DFA_DDR2_TMG, dfaTmg.u64);

          /* Turn on the DDR2 interface and wait a bit for the hardware to setup. */
        dfaCfg.s.init = 1;
        cvmx_write_csr (CVMX_DFA_DDR2_CFG, dfaCfg.u64);
        cvmx_wait(RLD_INIT_DELAY); // want at least 64K cycles
    }
}

void write_rld_cfg(rldram_csr_config_t *cfg_ptr)
{
    cvmx_dfa_memcfg0_t    memcfg0;
    cvmx_dfa_memcfg2_t    memcfg2;

    memcfg0.u64 = cfg_ptr->dfa_memcfg0_base;

    if ((OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
    {
        uint32_t dfa_memcfg0;

        if (OCTEON_IS_MODEL (OCTEON_CN58XX)) {
	      // Set RLDQK90_RST and RDLCK_RST to reset all three DLLs.
	    memcfg0.s.rldck_rst    = 1;
	    memcfg0.s.rldqck90_rst = 1;
            cvmx_write_csr(CVMX_DFA_MEMCFG0, memcfg0.u64);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  clk/qk90 reset\n", (uint32_t) memcfg0.u64);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), memcfg0.u64);

	      // Clear RDLCK_RST while asserting RLDQK90_RST to bring RLDCK DLL out of reset.
	    memcfg0.s.rldck_rst    = 0;
	    memcfg0.s.rldqck90_rst = 1;
            cvmx_write_csr(CVMX_DFA_MEMCFG0, memcfg0.u64);
            cvmx_wait(4000000);  /* Wait  */
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  qk90 reset\n", (uint32_t) memcfg0.u64);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), memcfg0.u64);

	      // Clear both RDLCK90_RST and RLDQK90_RST to bring the RLDQK90 DLL out of reset.
	    memcfg0.s.rldck_rst    = 0;
	    memcfg0.s.rldqck90_rst = 0;
	    cvmx_write_csr(CVMX_DFA_MEMCFG0, memcfg0.u64);
            cvmx_wait(4000000);  /* Wait  */
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  DLL out of reset\n", (uint32_t) memcfg0.u64);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), memcfg0.u64);
	}

        //=======================================================================
        // Now print out the sequence of events:
        cvmx_write_csr(CVMX_DFA_MEMCFG0, cfg_ptr->dfa_memcfg0_base);
        ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  port enables\n", cfg_ptr->dfa_memcfg0_base);
        cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), cfg_ptr->dfa_memcfg0_base);
        cvmx_wait(4000000);  /* Wait  */

        cvmx_write_csr(CVMX_DFA_MEMCFG1, cfg_ptr->dfa_memcfg1_base);
        ll_printf("CVMX_DFA_MEMCFG1: 0x%08x\n", cfg_ptr->dfa_memcfg1_base);
        cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG1 & ~(1ull<<63), cfg_ptr->dfa_memcfg1_base);

        if (cfg_ptr->p0_ena ==1)
        {
            cvmx_write_csr(CVMX_DFA_MEMRLD,  cfg_ptr->mrs_dat_p0bunk0);
            ll_printf("CVMX_DFA_MEMRLD : 0x%08x  p0_ena memrld\n", cfg_ptr->mrs_dat_p0bunk0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMRLD & ~(1ull<<63), cfg_ptr->mrs_dat_p0bunk0);

            dfa_memcfg0 = ( cfg_ptr->dfa_memcfg0_base |
                            (1 << 23) |   // P0_INIT
                            (1 << 25)     // BUNK_INIT[1:0]=Bunk#0
                          );

            cvmx_write_csr(CVMX_DFA_MEMCFG0, dfa_memcfg0);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  p0_init/bunk_init\n", dfa_memcfg0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), dfa_memcfg0);
            cvmx_wait(RLD_INIT_DELAY);
            ll_printf("Delay.....\n");
            cvmx_write_csr(CVMX_DFA_MEMCFG0, cfg_ptr->dfa_memcfg0_base);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  back to base\n", cfg_ptr->dfa_memcfg0_base);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), cfg_ptr->dfa_memcfg0_base);
        }

        if (cfg_ptr->p1_ena ==1)
        {
            cvmx_write_csr(CVMX_DFA_MEMRLD,  cfg_ptr->mrs_dat_p1bunk0);
            ll_printf("CVMX_DFA_MEMRLD : 0x%08x  p1_ena memrld\n", cfg_ptr->mrs_dat_p1bunk0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMRLD & ~(1ull<<63), cfg_ptr->mrs_dat_p1bunk0);

            dfa_memcfg0 = ( cfg_ptr->dfa_memcfg0_base |
                            (1 << 24) |   // P1_INIT
                            (1 << 25)     // BUNK_INIT[1:0]=Bunk#0
                          );
            cvmx_write_csr(CVMX_DFA_MEMCFG0, dfa_memcfg0);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  p1_init/bunk_init\n", dfa_memcfg0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), dfa_memcfg0);
            cvmx_wait(RLD_INIT_DELAY);
            ll_printf("Delay.....\n");
            cvmx_write_csr(CVMX_DFA_MEMCFG0, cfg_ptr->dfa_memcfg0_base);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  back to base\n", cfg_ptr->dfa_memcfg0_base);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), cfg_ptr->dfa_memcfg0_base);
	}

        // P0 Bunk#1
        if ((cfg_ptr->p0_ena ==1) && (cfg_ptr->bunkport == 2))
        {
            cvmx_write_csr(CVMX_DFA_MEMRLD,  cfg_ptr->mrs_dat_p0bunk1);
            ll_printf("CVMX_DFA_MEMRLD : 0x%08x  p0_ena memrld\n", cfg_ptr->mrs_dat_p0bunk1);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMRLD & ~(1ull<<63), cfg_ptr->mrs_dat_p0bunk1);

            dfa_memcfg0 = ( cfg_ptr->dfa_memcfg0_base |
                            (1 << 23) |   // P0_INIT
                            (2 << 25)     // BUNK_INIT[1:0]=Bunk#1
                          );
            cvmx_write_csr(CVMX_DFA_MEMCFG0, dfa_memcfg0);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  p0_init/bunk_init\n", dfa_memcfg0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), dfa_memcfg0);
            cvmx_wait(RLD_INIT_DELAY);
            ll_printf("Delay.....\n");

            if (cfg_ptr->p1_ena == 1)
            { // Re-arm Px_INIT if P1-B1 init is required
                cvmx_write_csr(CVMX_DFA_MEMCFG0, cfg_ptr->dfa_memcfg0_base);
                ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  px_init rearm\n", cfg_ptr->dfa_memcfg0_base);
                cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), cfg_ptr->dfa_memcfg0_base);
            }
        }

        if ((cfg_ptr->p1_ena == 1) && (cfg_ptr->bunkport == 2))
        {
            cvmx_write_csr(CVMX_DFA_MEMRLD,  cfg_ptr->mrs_dat_p1bunk1);
            ll_printf("CVMX_DFA_MEMRLD : 0x%08x  p1_ena memrld\n", cfg_ptr->mrs_dat_p1bunk1);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMRLD & ~(1ull<<63), cfg_ptr->mrs_dat_p1bunk1);

            dfa_memcfg0 = ( cfg_ptr->dfa_memcfg0_base |
                            (1 << 24) |   // P1_INIT
                            (2 << 25)     // BUNK_INIT[1:0]=10
                          );
            cvmx_write_csr(CVMX_DFA_MEMCFG0, dfa_memcfg0);
            ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  p1_init/bunk_init\n", dfa_memcfg0);
            cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), dfa_memcfg0);
        }
        cvmx_wait(4000000);  // 1/100S, 0.01S, 10mS
        ll_printf("Delay.....\n");

          /* Enable bunks */
        dfa_memcfg0 = cfg_ptr->dfa_memcfg0_base |((cfg_ptr->bunkport >= 1) << 25) | ((cfg_ptr->bunkport == 2) << 26);
        cvmx_write_csr(CVMX_DFA_MEMCFG0, dfa_memcfg0);
        ll_printf("CVMX_DFA_MEMCFG0: 0x%08x  enable bunks\n", dfa_memcfg0);
        cvmx_csr_db_decode(cvmx_get_proc_id(), CVMX_DFA_MEMCFG0 & ~(1ull<<63), dfa_memcfg0);
        cvmx_wait(RLD_INIT_DELAY);
        ll_printf("Delay.....\n");

          /* Issue a Silo reset by toggling SILRST in memcfg2. */
        memcfg2.u64 = cvmx_read_csr (CVMX_DFA_MEMCFG2);
        memcfg2.s.silrst = 1;
	cvmx_write_csr (CVMX_DFA_MEMCFG2, memcfg2.u64);
        ll_printf("CVMX_DFA_MEMCFG2: 0x%08x  silo reset start\n", (uint32_t) memcfg2.u64);
        memcfg2.s.silrst = 0;
	cvmx_write_csr (CVMX_DFA_MEMCFG2, memcfg2.u64);
        ll_printf("CVMX_DFA_MEMCFG2: 0x%08x  silo reset done\n", (uint32_t) memcfg2.u64);
    }
}

