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
 * Interface to the NAND flash controller.
 * See cvmx-nand.h for usage documentation and notes.
 *
 * <hr>$Revision: 35726 $<hr>
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-nand.h>
#include <asm/octeon/cvmx-ndf-defs.h>
#include <asm/octeon/cvmx-swap.h>
#include <asm/octeon/cvmx-bootmem.h>
#else
#include "cvmx.h"
#include "cvmx-nand.h"
#include "cvmx-swap.h"
#include "cvmx-bootmem.h"
#endif
#if defined(__U_BOOT__) && defined(CONFIG_HW_WATCHDOG)
# include <watchdog.h>
#else
# define WATCHDOG_RESET()
#endif

#define NAND_COMMAND_READ_ID            0x90
#define NAND_COMMAND_READ_PARAM_PAGE    0xec
#define NAND_COMMAND_RESET              0xff
#define NAND_COMMAND_STATUS             0x70
#define NAND_COMMAND_READ               0x00
#define NAND_COMMAND_READ_FIN           0x30
#define NAND_COMMAND_ERASE              0x60
#define NAND_COMMAND_ERASE_FIN          0xd0
#define NAND_COMMAND_PROGRAM            0x80
#define NAND_COMMAND_PROGRAM_FIN        0x10
#define NAND_TIMEOUT_USECS_READ         100000
#define NAND_TIMEOUT_USECS_WRITE        1000000
#define NAND_TIMEOUT_USECS_BLOCK_ERASE  1000000

#define CVMX_NAND_ROUNDUP(_Dividend, _Divisor) (((_Dividend)+((_Divisor)-1))/(_Divisor))
#undef min
#define min(X, Y)                               \
        ({ typeof (X) __x = (X);                \
           typeof (Y) __y = (Y);                \
                (__x < __y) ? __x : __y; })

#undef max
#define max(X, Y)                               \
        ({ typeof (X) __x = (X);                \
           typeof (Y) __y = (Y);                \
                (__x > __y) ? __x : __y; })


/* Structure to store the parameters that we care about that
** describe the ONFI speed modes.  This is used to configure
** the flash timing to match what is reported in the
** parameter page of the ONFI flash chip. */
typedef struct
{
    int twp;
    int twh;
    int twc;
    int tclh;
    int tals;
} onfi_speed_mode_desc_t;
static const onfi_speed_mode_desc_t onfi_speed_modes[] =
{

    {50,30,100,20,50},  /* Mode 0 */
    {25,15, 45,10,25},  /* Mode 1 */
    {17,15, 35,10,15},  /* Mode 2 */
    {15,10, 30, 5,10},  /* Mode 3 */
    {12,10, 25, 5,10},  /* Mode 4, requires EDO timings */
    {10, 7, 20, 5,10},  /* Mode 5, requries EDO timings */
    {10,10, 25, 5,12},	/* Mode 6, requires EDO timings */
};



typedef enum
{
    CVMX_NAND_STATE_16BIT = 1<<0,
} cvmx_nand_state_flags_t;

/**
 * Structure used to store data about the NAND devices hooked
 * to the bootbus.
 */
typedef struct
{
    int page_size;
    int oob_size;
    int pages_per_block;
    int blocks;
    int tim_mult;
    int tim_par[8];
    int clen[4];
    int alen[4];
    int rdn[4];
    int wrn[2];
    int onfi_timing;
    cvmx_nand_state_flags_t flags;
} cvmx_nand_state_t;

/**
 * Array indexed by bootbus chip select with information
 * about NAND devices.
 */
#if defined(__U_BOOT__)
/* For u-boot nand boot we need to play some tricks to be able
** to use this early in boot.  We put them in a special section that is merged
** with the text segment.  (Using the text segment directly results in an assembler warning.)
*/
/*#define USE_DATA_IN_TEXT*/
#endif

#ifdef USE_DATA_IN_TEXT
static uint8_t cvmx_nand_buffer[CVMX_NAND_MAX_PAGE_AND_OOB_SIZE] __attribute__((aligned(8)))  __attribute__ ((section (".data_in_text")));
static cvmx_nand_state_t cvmx_nand_state[8] __attribute__ ((section (".data_in_text")));
static cvmx_nand_state_t cvmx_nand_default __attribute__ ((section (".data_in_text")));
static cvmx_nand_initialize_flags_t cvmx_nand_flags __attribute__ ((section (".data_in_text")));
static int debug_indent __attribute__ ((section (".data_in_text")));
#else
static CVMX_SHARED cvmx_nand_state_t cvmx_nand_state[8];
static CVMX_SHARED cvmx_nand_state_t cvmx_nand_default;
static CVMX_SHARED cvmx_nand_initialize_flags_t cvmx_nand_flags;
static CVMX_SHARED uint8_t *cvmx_nand_buffer = NULL;
static int debug_indent = 0;
#endif

static CVMX_SHARED const char *cvmx_nand_opcode_labels[] =
{
    "NOP",                      /* 0 */
    "Timing",                   /* 1 */
    "Wait",                     /* 2 */
    "Chip Enable / Disable",    /* 3 */
    "CLE",                      /* 4 */
    "ALE",                      /* 5 */
    "6 - Unknown",              /* 6 */
    "7 - Unknown",              /* 7 */
    "Write",                    /* 8 */
    "Read",                     /* 9 */
    "Read EDO",                 /* 10 */
    "Wait Status",              /* 11 */
    "12 - Unknown",             /* 12 */
    "13 - Unknown",             /* 13 */
    "14 - Unknown",             /* 14 */
    "Bus Aquire / Release"      /* 15 */
};

#define ULL unsigned long long
/* This macro logs out whenever a function is called if debugging is on */
#define CVMX_NAND_LOG_CALLED() \
    if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG)) \
        cvmx_dprintf("%*s%s: called\n", 2*debug_indent++, "", __FUNCTION__);

/* This macro logs out each function parameter if debugging is on */
#define CVMX_NAND_LOG_PARAM(format, param) \
    if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG)) \
        cvmx_dprintf("%*s%s: param %s = " format "\n", 2*debug_indent, "", __FUNCTION__, #param, param);

/* This macro logs out when a function returns a value */
#define CVMX_NAND_RETURN(v)                                              \
    do {                                                                \
        typeof(v) r = v;                                                \
        if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))         \
            cvmx_dprintf("%*s%s: returned %s(%d)\n", 2*--debug_indent, "", __FUNCTION__, #v, r); \
        return r;                                                       \
    } while (0);

/* This macro logs out when a function doesn't return a value */
#define CVMX_NAND_RETURN_NOTHING()                                      \
    do {                                                                \
        if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))         \
            cvmx_dprintf("%*s%s: returned\n", 2*--debug_indent, "", __FUNCTION__); \
        return;                                                         \
    } while (0);






/* Compute the CRC for the ONFI parameter page.  Adapted from sample code
** in the specification.
*/
static uint16_t __onfi_parameter_crc_compute(uint8_t *data)
{
    const int order = 16;                     // Order of the CRC-16
    unsigned long i, j, c, bit;
    unsigned long crc = 0x4F4E;              // Initialize the shift register with 0x4F4E
    unsigned long crcmask = ((((unsigned long)1<<(order-1))-1)<<1)|1;
    unsigned long crchighbit = (unsigned long)1<<(order-1);

    for (i = 0; i < 254; i++)
    {
        c = (unsigned long)data[i];
        for (j = 0x80; j; j >>= 1) {
              bit = crc & crchighbit;
              crc <<= 1;
              if (c & j)
                  bit ^= crchighbit;
              if (bit)
                   crc ^= 0x8005;
        }
        crc &= crcmask;
    }
    return(crc);
}


/**
 * Validate the ONFI parameter page and return a pointer to
 * the config values.
 *
 * @param param_page Pointer to the raw NAND data returned after a parameter page read. It will
 *                   contain at least 4 copies of the parameter structure.
 *
 * @return Pointer to a validated paramter page, or NULL if one couldn't be found.
 */
static cvmx_nand_onfi_param_page_t *__cvmx_nand_onfi_process(cvmx_nand_onfi_param_page_t param_page[4])
{
    int index;

    for (index=0; index<4; index++)
    {
        uint16_t crc = __onfi_parameter_crc_compute((void *)&param_page[index]);
        if (crc == cvmx_le16_to_cpu(param_page[index].crc))
            break;
        if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
            cvmx_dprintf("%s: Paramter page %d is corrupt. (Expected CRC: 0x%04x, computed: 0x%04x)\n",
                          __FUNCTION__, index, cvmx_le16_to_cpu(param_page[index].crc), crc);
    }

    if (index == 4)
    {
        if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
            cvmx_dprintf("%s: All parameter pages fail CRC check.  Checking to see if any look sane.\n", __FUNCTION__);

        if (!memcmp(param_page, param_page + 1, 256))
        {
            /* First and second copies match, now check some values */
            if (param_page[0].pages_per_block != 0 && param_page[0].pages_per_block != 0xFFFFFFFF
                && param_page[0].page_data_bytes != 0 && param_page[0].page_data_bytes != 0xFFFFFFFF
                && param_page[0].page_spare_bytes != 0 && param_page[0].page_spare_bytes != 0xFFFF
                && param_page[0].blocks_per_lun != 0 && param_page[0].blocks_per_lun != 0xFFFFFFFF
                && param_page[0].timing_mode != 0 && param_page[0].timing_mode != 0xFFFF)
            {
                /* Looks like we have enough values to use */
                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                    cvmx_dprintf("%s: Page 0 looks sane, using even though CRC fails.\n", __FUNCTION__);
                index = 0;
            }
        }
    }

    if (index == 4)
    {
        cvmx_dprintf("%s: WARNING: ONFI part but no valid ONFI parameter pages found.\n", __FUNCTION__);
        return NULL;
    }

    if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
    {
        cvmx_dprintf("%*sONFI Information (from copy %d in param page)\n", 2*debug_indent, "", index);
        debug_indent++;
        cvmx_dprintf("%*sonfi = %c%c%c%c\n", 2*debug_indent, "", param_page[index].onfi[0], param_page[index].onfi[1],
            param_page[index].onfi[2], param_page[index].onfi[3]);
        cvmx_dprintf("%*srevision_number = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].revision_number));
        cvmx_dprintf("%*sfeatures = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].features));
        cvmx_dprintf("%*soptional_commands = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].optional_commands));

        cvmx_dprintf("%*smanufacturer = %12.12s\n", 2*debug_indent, "", param_page[index].manufacturer);
        cvmx_dprintf("%*smodel = %20.20s\n", 2*debug_indent, "", param_page[index].model);
        cvmx_dprintf("%*sjedec_id = 0x%x\n", 2*debug_indent, "", param_page[index].jedec_id);
        cvmx_dprintf("%*sdate_code = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].date_code));

        cvmx_dprintf("%*spage_data_bytes = %u\n", 2*debug_indent, "", (int)cvmx_le32_to_cpu(param_page[index].page_data_bytes));
        cvmx_dprintf("%*spage_spare_bytes = %u\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].page_spare_bytes));
        cvmx_dprintf("%*spartial_page_data_bytes = %u\n", 2*debug_indent, "", (int)cvmx_le32_to_cpu(param_page[index].partial_page_data_bytes));
        cvmx_dprintf("%*spartial_page_spare_bytes = %u\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].partial_page_spare_bytes));
        cvmx_dprintf("%*spages_per_block = %u\n", 2*debug_indent, "", (int)cvmx_le32_to_cpu(param_page[index].pages_per_block));
        cvmx_dprintf("%*sblocks_per_lun = %u\n", 2*debug_indent, "", (int)cvmx_le32_to_cpu(param_page[index].blocks_per_lun));
        cvmx_dprintf("%*snumber_lun = %u\n", 2*debug_indent, "", param_page[index].number_lun);
        cvmx_dprintf("%*saddress_cycles = 0x%x\n", 2*debug_indent, "", param_page[index].address_cycles);
        cvmx_dprintf("%*sbits_per_cell = %u\n", 2*debug_indent, "", param_page[index].bits_per_cell);
        cvmx_dprintf("%*sbad_block_per_lun = %u\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].bad_block_per_lun));
        cvmx_dprintf("%*sblock_endurance = %u\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].block_endurance));
        cvmx_dprintf("%*sgood_blocks = %u\n", 2*debug_indent, "", param_page[index].good_blocks);
        cvmx_dprintf("%*sgood_block_endurance = %u\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].good_block_endurance));
        cvmx_dprintf("%*sprograms_per_page = %u\n", 2*debug_indent, "", param_page[index].programs_per_page);
        cvmx_dprintf("%*spartial_program_attrib = 0x%x\n", 2*debug_indent, "", param_page[index].partial_program_attrib);
        cvmx_dprintf("%*sbits_ecc = %u\n", 2*debug_indent, "", param_page[index].bits_ecc);
        cvmx_dprintf("%*sinterleaved_address_bits = 0x%x\n", 2*debug_indent, "", param_page[index].interleaved_address_bits);
        cvmx_dprintf("%*sinterleaved_attrib = 0x%x\n", 2*debug_indent, "", param_page[index].interleaved_attrib);

        cvmx_dprintf("%*spin_capacitance = %u\n", 2*debug_indent, "", param_page[index].pin_capacitance);
        cvmx_dprintf("%*stiming_mode = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].timing_mode));
        cvmx_dprintf("%*scache_timing_mode = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].cache_timing_mode));
        cvmx_dprintf("%*st_prog = %d us\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].t_prog));
        cvmx_dprintf("%*st_bers = %u us\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].t_bers));
        cvmx_dprintf("%*st_r = %u us\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].t_r));
        cvmx_dprintf("%*st_ccs = %u ns\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].t_ccs));
        cvmx_dprintf("%*svendor_revision = 0x%x\n", 2*debug_indent, "", cvmx_le16_to_cpu(param_page[index].vendor_revision));
        //uint8_t vendor_specific[88];    /**< Byte 166-253: Vendor specific */
        cvmx_dprintf("%*scrc = 0x%x\n", 2*debug_indent, "", param_page[index].crc);
        debug_indent--;
    }
    return param_page + index;
}

void __set_onfi_timing_mode(int *tim_par, int clocks_us, int mode)
{
    const onfi_speed_mode_desc_t *mp = &onfi_speed_modes[mode];  /* use shorter name to fill in timing array */
    int margin;
    int pulse_adjust;

    if (mode > 6)
    {
        cvmx_dprintf("%s: invalid ONFI timing mode: %d\n", __FUNCTION__, mode);
        return;
    }

    /* Adjust the read/write pulse duty cycle to make it more even.  The cycle time
    ** requirement is longer than the sum of the high low times, so we exend both the high
    ** and low times to meet the cycle time requirement.
    */
    pulse_adjust = ((mp->twc - mp->twh - mp->twp)/2 + 1) * clocks_us;

    /* Add a small margin to all timings. */
    margin = 2 * clocks_us;
    /* Update timing parameters based on supported mode */
    tim_par[1] = CVMX_NAND_ROUNDUP(mp->twp * clocks_us + margin + pulse_adjust, 1000); /* Twp, WE# pulse width */
    tim_par[2] = CVMX_NAND_ROUNDUP(max(mp->twh, mp->twc - mp->twp) * clocks_us + margin + pulse_adjust, 1000); /* Tw, WE# pulse width high */
    tim_par[3] = CVMX_NAND_ROUNDUP(mp->tclh * clocks_us + margin, 1000); /* Tclh, CLE hold time */
    tim_par[4] = CVMX_NAND_ROUNDUP(mp->tals * clocks_us + margin, 1000); /* Tals, ALE setup time */
    tim_par[5] = tim_par[3]; /* Talh, ALE hold time */
    tim_par[6] = tim_par[1]; /* Trp, RE# pulse width*/
    tim_par[7] = tim_par[2]; /* Treh, RE# high hold time */

}


/* Internal helper function to set chip configuration to use default values */
static void __set_chip_defaults(int chip, int clocks_us)
{
    if (!cvmx_nand_default.page_size)
        return;
    cvmx_nand_state[chip].page_size = cvmx_nand_default.page_size;  /* NAND page size in bytes */
    cvmx_nand_state[chip].oob_size = cvmx_nand_default.oob_size;     /* NAND OOB (spare) size in bytes (per page) */
    cvmx_nand_state[chip].pages_per_block = cvmx_nand_default.pages_per_block;
    cvmx_nand_state[chip].blocks = cvmx_nand_default.blocks;
    cvmx_nand_state[chip].onfi_timing = cvmx_nand_default.onfi_timing;
    __set_onfi_timing_mode(cvmx_nand_state[chip].tim_par, clocks_us, cvmx_nand_state[chip].onfi_timing);
    if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
    {

        cvmx_dprintf("%s: Using default NAND parameters.\n", __FUNCTION__);
        cvmx_dprintf("%s: Defaults: page size: %d, OOB size: %d, pages per block %d, blocks: %d, timing mode: %d\n",
                     __FUNCTION__, cvmx_nand_state[chip].page_size, cvmx_nand_state[chip].oob_size, cvmx_nand_state[chip].pages_per_block,
                     cvmx_nand_state[chip].blocks, cvmx_nand_state[chip].onfi_timing);
    }
}
/* Do the proper wait for the ready/busy signal.  First wait
** for busy to be valid, then wait for busy to de-assert.
*/
static int __wait_for_busy_done(int chip)
{
    cvmx_nand_cmd_t cmd;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    memset(&cmd,  0,  sizeof(cmd));
    cmd.wait.two = 2;
    cmd.wait.r_b=0;
    cmd.wait.n = 2;

    /* Wait for RB to be valied (tWB).
    ** Use 5 * tWC as proxy.  In some modes this is
    ** much longer than required, but does not affect performance
    ** since we will wait much longer for busy to de-assert.
    */
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    cmd.wait.r_b=1; /* Now wait for busy to be de-asserted */
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
/**
 * Called to initialize the NAND controller for use. Note that
 * you must be running out of L2 or memory and not NAND before
 * calling this function.
 * When probing for NAND chips, this function attempts to autoconfigure based on the NAND parts detected.
 * It currently supports autodetection for ONFI parts (with valid parameter pages), and some Samsung NAND
 * parts (decoding ID bits.)  If autoconfiguration fails, the defaults set with __set_chip_defaults()
 * prior to calling cvmx_nand_initialize() are used.
 * If defaults are set and the CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE flag is provided, the defaults are used
 * for all chips in the active_chips mask.
 *
 * @param flags  Optional initialization flags
 *               If the CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE flag is passed, chips are not probed,
 *               and the default parameters (if set with cvmx_nand_set_defaults) are used for all chips
 *               in the active_chips mask.
 * @param active_chips
 *               Each bit in this parameter represents a chip select that might
 *               contain NAND flash. Any chip select present in this bitmask may
 *               be connected to NAND. It is normally safe to pass 0xff here and
 *               let the API probe all 8 chip selects.
 *
 * @return Zero on success, a negative cvmx_nand_status error code on failure
 */
cvmx_nand_status_t cvmx_nand_initialize(cvmx_nand_initialize_flags_t flags, int active_chips)
{
    int chip;
    int start_chip;
    int stop_chip;
    uint64_t clocks_us;
    union cvmx_ndf_misc ndf_misc;
    uint8_t nand_id_buffer[16];

    if (!octeon_has_feature(OCTEON_FEATURE_NAND))
        CVMX_NAND_RETURN(CVMX_NAND_NO_DEVICE);

    cvmx_nand_flags = flags;
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("0x%x", flags);

    memset(&cvmx_nand_state,  0,  sizeof(cvmx_nand_state));

#ifndef USE_DATA_IN_TEXT
    /* cvmx_nand_buffer is statically allocated in the TEXT_IN_DATA case */
    if (!cvmx_nand_buffer)
    {
        cvmx_nand_buffer = cvmx_bootmem_alloc_named_flags(CVMX_NAND_MAX_PAGE_AND_OOB_SIZE, 128, "__nand_buffer", CVMX_BOOTMEM_FLAG_END_ALLOC);
    }
    if (!cvmx_nand_buffer) {
        const cvmx_bootmem_named_block_desc_t *block_desc = cvmx_bootmem_find_named_block("__nand_buffer");
        if (block_desc)
            cvmx_nand_buffer = cvmx_phys_to_ptr(block_desc->base_addr);
    }

    if (!cvmx_nand_buffer)
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
#endif

    /* Disable boot mode and reset the fifo */
    ndf_misc.u64 = cvmx_read_csr(CVMX_NDF_MISC);
    ndf_misc.s.rd_cmd = 0;
    ndf_misc.s.bt_dma = 0;
    ndf_misc.s.bt_dis = 1;
    ndf_misc.s.ex_dis = 0;
    ndf_misc.s.rst_ff = 1;
    cvmx_write_csr(CVMX_NDF_MISC, ndf_misc.u64);
    cvmx_read_csr(CVMX_NDF_MISC);

    /* Bring the fifo out of reset */
    cvmx_wait_usec(1);
    ndf_misc.s.rst_ff = 0;
    cvmx_write_csr(CVMX_NDF_MISC, ndf_misc.u64);
    cvmx_read_csr(CVMX_NDF_MISC);
    cvmx_wait_usec(1);

    /* Clear the ECC counter */
    //cvmx_write_csr(CVMX_NDF_ECC_CNT, cvmx_read_csr(CVMX_NDF_ECC_CNT));

    /* Clear the interrupt state */
    cvmx_write_csr(CVMX_NDF_INT, cvmx_read_csr(CVMX_NDF_INT));
    cvmx_write_csr(CVMX_NDF_INT_EN, 0);
    cvmx_write_csr(CVMX_MIO_NDF_DMA_INT, cvmx_read_csr(CVMX_MIO_NDF_DMA_INT));
    cvmx_write_csr(CVMX_MIO_NDF_DMA_INT_EN, 0);


    /* The simulator crashes if you access non existant devices. Assume
        only chip select 1 is connected to NAND */
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
    {
        start_chip = 1;
        stop_chip = 2;
    }
    else
    {
        start_chip = 0;
        stop_chip = 8;
    }

    /* Figure out how many clocks are in one microsecond, rounding up */
    clocks_us = CVMX_NAND_ROUNDUP(cvmx_clock_get_rate(CVMX_CLOCK_SCLK), 1000000);

    /* If the CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE flag is set, then
    ** use the supplied default values to configured the chips in the
    ** active_chips mask */
    if (cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE)
    {
        if (cvmx_nand_default.page_size)
        {
            for (chip=start_chip; chip<stop_chip; chip++)
            {
                /* Skip chip selects that the caller didn't supply in the active chip bits */
                if (((1<<chip) & active_chips) == 0)
                    continue;
                __set_chip_defaults(chip, clocks_us);
            }
        }
        CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
    }

    /* Probe and see what NAND flash we can find */
    for (chip=start_chip; chip<stop_chip; chip++)
    {
        union cvmx_mio_boot_reg_cfgx mio_boot_reg_cfg;
        cvmx_nand_onfi_param_page_t *onfi_param_page;
        int probe_failed;
        int width_16;

        /* Skip chip selects that the caller didn't supply in the active chip bits */
        if (((1<<chip) & active_chips) == 0)
            continue;

        mio_boot_reg_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(chip));
        /* Enabled regions can't be connected to NAND flash */
        if (mio_boot_reg_cfg.s.en)
            continue;

        /* Start out with some sane, but slow, defaults */
        cvmx_nand_state[chip].page_size = 0;
        cvmx_nand_state[chip].oob_size = 64;
        cvmx_nand_state[chip].pages_per_block = 64;
        cvmx_nand_state[chip].blocks = 100;


        /* Set timing mode to ONFI mode 0 for initial accesses */
        __set_onfi_timing_mode(cvmx_nand_state[chip].tim_par, clocks_us, 0);

        /* Put the index of which timing parameter to use.  The indexes are into the tim_par
        ** which match the indexes of the 8 timing parameters that the hardware supports.
        ** Index 0 is not software controlled, and is fixed by hardware. */
        cvmx_nand_state[chip].clen[0] = 0; /* Command doesn't need to be held before WE */
        cvmx_nand_state[chip].clen[1] = 1; /* Twp, WE# pulse width */
        cvmx_nand_state[chip].clen[2] = 3; /* Tclh, CLE hold time */
        cvmx_nand_state[chip].clen[3] = 1;

        cvmx_nand_state[chip].alen[0] = 4; /* Tals, ALE setup time */
        cvmx_nand_state[chip].alen[1] = 1; /* Twp, WE# pulse width */
        cvmx_nand_state[chip].alen[2] = 2; /* Twh, WE# pulse width high */
        cvmx_nand_state[chip].alen[3] = 5; /* Talh, ALE hold time */

        cvmx_nand_state[chip].rdn[0] = 0;
        cvmx_nand_state[chip].rdn[1] = 6; /* Trp, RE# pulse width*/
        cvmx_nand_state[chip].rdn[2] = 7; /* Treh, RE# high hold time */
        cvmx_nand_state[chip].rdn[3] = 0;

        cvmx_nand_state[chip].wrn[0] = 1; /* Twp, WE# pulse width */
        cvmx_nand_state[chip].wrn[1] = 2; /* Twh, WE# pulse width high */

        /* Probe and see if we get an answer.  Read more than required, as in
        ** 16 bit mode only every other byte is valid.
        ** Here we probe twice, once in 8 bit mode, and once in 16 bit mode to autodetect
        ** the width.
        */
        probe_failed = 1;
        for (width_16 = 0; width_16 <= 1 && probe_failed; width_16++)
        {
            probe_failed = 0;

            if (width_16)
                cvmx_nand_state[chip].flags |= CVMX_NAND_STATE_16BIT;
            memset(cvmx_nand_buffer, 0xff, 16);
            if (cvmx_nand_read_id(chip, 0x0, cvmx_ptr_to_phys(cvmx_nand_buffer), 16) < 16)
            {
                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                    cvmx_dprintf("%s: Failed to probe chip %d\n", __FUNCTION__, chip);
                probe_failed = 1;

            }
            if (*(uint32_t*)cvmx_nand_buffer == 0xffffffff || *(uint32_t*)cvmx_nand_buffer == 0x0)
            {
                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                    cvmx_dprintf("%s: Probe returned nothing for chip %d\n", __FUNCTION__, chip);
                probe_failed = 1;
            }
        }
        /* Neither 8 or 16 bit mode worked, so go on to next chip select */
        if (probe_failed)
            continue;

        /* Save copy of ID for later use */
        memcpy(nand_id_buffer, cvmx_nand_buffer, sizeof(nand_id_buffer));

        if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
            cvmx_dprintf("%s: NAND chip %d has ID 0x%08llx\n", __FUNCTION__, chip, (unsigned long long int)*(uint64_t*)cvmx_nand_buffer);
        /* Read more than required, as in 16 bit mode only every other byte is valid. */
        if (cvmx_nand_read_id(chip, 0x20, cvmx_ptr_to_phys(cvmx_nand_buffer), 8) < 8)
        {
            if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                cvmx_dprintf("%s: Failed to probe chip %d\n", __FUNCTION__, chip);
            continue;
        }

        if (((cvmx_nand_buffer[0] == 'O') && (cvmx_nand_buffer[1] == 'N') &&
            (cvmx_nand_buffer[2] == 'F') && (cvmx_nand_buffer[3] == 'I')))
        {
            /* We have an ONFI part, so read the parameter page */

            cvmx_nand_read_param_page(chip, cvmx_ptr_to_phys(cvmx_nand_buffer), 2048);
            onfi_param_page = __cvmx_nand_onfi_process((cvmx_nand_onfi_param_page_t *)cvmx_nand_buffer);
            if (onfi_param_page)
            {
                /* ONFI NAND parts are described by a parameter page.  Here we extract the configuration values
                ** from the parameter page that we need to access the chip. */
                cvmx_nand_state[chip].page_size = cvmx_le32_to_cpu(onfi_param_page->page_data_bytes);
                cvmx_nand_state[chip].oob_size = cvmx_le16_to_cpu(onfi_param_page->page_spare_bytes);
                cvmx_nand_state[chip].pages_per_block = cvmx_le32_to_cpu(onfi_param_page->pages_per_block);
                cvmx_nand_state[chip].blocks = cvmx_le32_to_cpu(onfi_param_page->blocks_per_lun) * onfi_param_page->number_lun;

                if (cvmx_le16_to_cpu(onfi_param_page->timing_mode) <= 0x3f)
                {
                    int mode_mask = cvmx_le16_to_cpu(onfi_param_page->timing_mode);
                    int mode = 0;
                    int i;
                    for (i = 0; i < 6;i++)
                    {
                        if (mode_mask & (1 << i))
                            mode = i;
                    }
                    cvmx_nand_state[chip].onfi_timing = mode;
                }
                else
                {
                    cvmx_dprintf("%s: Invalid timing mode (%d) in ONFI parameter page, ignoring\n", __FUNCTION__, cvmx_nand_state[chip].onfi_timing);
                    cvmx_nand_state[chip].onfi_timing = 0;

                }
                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                    cvmx_dprintf("%s: Using ONFI timing mode: %d\n", __FUNCTION__, cvmx_nand_state[chip].onfi_timing);
                __set_onfi_timing_mode(cvmx_nand_state[chip].tim_par, clocks_us, cvmx_nand_state[chip].onfi_timing);
                if (cvmx_nand_state[chip].page_size + cvmx_nand_state[chip].oob_size > CVMX_NAND_MAX_PAGE_AND_OOB_SIZE)
                {
                    cvmx_dprintf("%s: ERROR: Page size (%d) + OOB size (%d) is greater than max size (%d)\n",
                                 __FUNCTION__, cvmx_nand_state[chip].page_size, cvmx_nand_state[chip].oob_size, CVMX_NAND_MAX_PAGE_AND_OOB_SIZE);
                    return(CVMX_NAND_ERROR);
                }
                /* We have completed setup for this ONFI chip, so go on to next chip. */
                continue;
            }
            else
            {
                /* Parameter page is not valid */
                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                    cvmx_dprintf("%s: ONFI paramater page missing or invalid.\n", __FUNCTION__);

            }


        }
        else
        {
            /* We have a non-ONFI part. */
            if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                cvmx_dprintf("%s: Chip %d doesn't support ONFI.\n", __FUNCTION__, chip);


            if (nand_id_buffer[0] == 0xEC)
            {
                /* We have a Samsung part, so decode part info from ID bytes */
                uint64_t nand_size_bits = (64*1024*1024ULL) << ((nand_id_buffer[4] & 0x70) >> 4); /* Plane size */
                cvmx_nand_state[chip].page_size = 1024 << (nand_id_buffer[3] & 0x3);  /* NAND page size in bytes */
		/* NAND OOB (spare) size in bytes (per page) */
		cvmx_nand_state[chip].oob_size = (cvmx_nand_state[chip].page_size / 512) * ((nand_id_buffer[3] & 4) ? 16 : 8);
                cvmx_nand_state[chip].pages_per_block = (0x10000 << ((nand_id_buffer[3] & 0x30) >> 4))/cvmx_nand_state[chip].page_size;

                nand_size_bits *= 1 << ((nand_id_buffer[4] & 0xc) >> 2);

                cvmx_nand_state[chip].oob_size = cvmx_nand_state[chip].page_size/64;
                if (nand_id_buffer[3] & 0x4)
                    cvmx_nand_state[chip].oob_size *= 2;

                cvmx_nand_state[chip].blocks = nand_size_bits/(8ULL*cvmx_nand_state[chip].page_size*cvmx_nand_state[chip].pages_per_block);
                switch (nand_id_buffer[1]) {
                case 0xD3:      /* K9F8G08U0M */
                case 0xDC:      /* K9F4G08U0B */
                    cvmx_nand_state[chip].onfi_timing = 6;
                    break;
                default:
                    cvmx_nand_state[chip].onfi_timing = 2;
                    break;
                }

                if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                {
                    cvmx_dprintf("%s: Samsung NAND chip detected, using parameters decoded from ID bytes.\n", __FUNCTION__);
                    cvmx_dprintf("%s: Defaults: page size: %d, OOB size: %d, pages per block %d, part size: %d MBytes, timing mode: %d\n",
                                 __FUNCTION__, cvmx_nand_state[chip].page_size, cvmx_nand_state[chip].oob_size, cvmx_nand_state[chip].pages_per_block,
                                 (int)(nand_size_bits/(8*1024*1024)), cvmx_nand_state[chip].onfi_timing);
                }

                __set_onfi_timing_mode(cvmx_nand_state[chip].tim_par, clocks_us, cvmx_nand_state[chip].onfi_timing);
                if (cvmx_nand_state[chip].page_size + cvmx_nand_state[chip].oob_size > CVMX_NAND_MAX_PAGE_AND_OOB_SIZE)
                {
                    cvmx_dprintf("%s: ERROR: Page size (%d) + OOB size (%d) is greater than max size (%d)\n",
                                 __FUNCTION__, cvmx_nand_state[chip].page_size, cvmx_nand_state[chip].oob_size, CVMX_NAND_MAX_PAGE_AND_OOB_SIZE);
                    return(CVMX_NAND_ERROR);
                }

                /* We have completed setup for this Samsung chip, so go on to next chip. */
                continue;


            }

        }



        /*  We were not able to automatically identify the NAND chip parameters.  If default values were configured,
        ** use them. */
        if (cvmx_nand_default.page_size)
        {
            __set_chip_defaults(chip, clocks_us);
        }
        else
        {

            if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
                cvmx_dprintf("%s: Unable to determine NAND parameters, and no defaults supplied.\n", __FUNCTION__);
        }
    }
    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_initialize);
#endif


/**
 * Call to shutdown the NAND controller after all transactions
 * are done. In most setups this will never be called.
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_shutdown(void)
{
    CVMX_NAND_LOG_CALLED();
    memset(&cvmx_nand_state,  0,  sizeof(cvmx_nand_state));
    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}


/**
 * Returns a bitmask representing the chip selects that are
 * connected to NAND chips. This can be called after the
 * initialize to determine the actual number of NAND chips
 * found. Each bit in the response coresponds to a chip select.
 *
 * @return Zero if no NAND chips were found. Otherwise a bit is set for
 *         each chip select (1<<chip).
 */
int cvmx_nand_get_active_chips(void)
{
    int chip;
    int result = 0;
    for (chip=0; chip<8; chip++)
    {
        if (cvmx_nand_state[chip].page_size)
            result |= 1<<chip;
    }
    return result;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_get_active_chips);
#endif


/**
 * Override the timing parameters for a NAND chip
 *
 * @param chip     Chip select to override
 * @param tim_mult
 * @param tim_par
 * @param clen
 * @param alen
 * @param rdn
 * @param wrn
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_set_timing(int chip, int tim_mult, int tim_par[8], int clen[4], int alen[4], int rdn[4], int wrn[2])
{
    int i;
    CVMX_NAND_LOG_CALLED();

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!cvmx_nand_state[chip].page_size)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    cvmx_nand_state[chip].tim_mult = tim_mult;
    for (i=0;i<8;i++)
        cvmx_nand_state[chip].tim_par[i] = tim_par[i];
    for (i=0;i<4;i++)
        cvmx_nand_state[chip].clen[i] = clen[i];
    for (i=0;i<4;i++)
        cvmx_nand_state[chip].alen[i] = alen[i];
    for (i=0;i<4;i++)
        cvmx_nand_state[chip].rdn[i] = rdn[i];
    for (i=0;i<2;i++)
        cvmx_nand_state[chip].wrn[i] = wrn[i];

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}


/**
 * @INTERNAL
 * Get the number of free bytes in the NAND command queue
 *
 * @return Number of bytes in queue
 */
static inline int __cvmx_nand_get_free_cmd_bytes(void)
{
    union cvmx_ndf_misc ndf_misc;
    CVMX_NAND_LOG_CALLED();
    ndf_misc.u64 = cvmx_read_csr(CVMX_NDF_MISC);
    CVMX_NAND_RETURN((int)ndf_misc.s.fr_byt);
}


/**
 * Submit a command to the NAND command queue. Generally this
 * will not be used directly. Instead most programs will use the other
 * higher level NAND functions.
 *
 * @param cmd    Command to submit
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_submit(cvmx_nand_cmd_t cmd)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)cmd.u64[0]);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)cmd.u64[1]);
    CVMX_NAND_LOG_PARAM("%s", cvmx_nand_opcode_labels[cmd.s.op_code]);
    switch (cmd.s.op_code)
    {
        /* All these commands fit in one 64bit word */
        case 0: /* NOP */
        case 1: /* Timing */
        case 2: /* WAIT */
        case 3: /* Chip Enable/Disable */
        case 4: /* CLE */
        case 8: /* Write */
        case 9: /* Read */
        case 10: /* Read EDO */
        case 15: /* Bus Aquire/Release */
            if (__cvmx_nand_get_free_cmd_bytes() < 8)
                CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
            cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[1]);
            CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);

        case 5: /* ALE commands take either one or two 64bit words */
            if (cmd.ale.adr_byte_num < 5)
            {
                if (__cvmx_nand_get_free_cmd_bytes() < 8)
                    CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
                cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[1]);
                CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
            }
            else
            {
                if (__cvmx_nand_get_free_cmd_bytes() < 16)
                    CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
                cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[1]);
                cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[0]);
                CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
            }

        case 11: /* Wait status commands take two 64bit words */
            if (__cvmx_nand_get_free_cmd_bytes() < 16)
                CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
            cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[1]);
            cvmx_write_csr(CVMX_NDF_CMD, cmd.u64[0]);
            CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);

        default:
            CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    }
}


/**
 * @INTERNAL
 * Get the number of bits required to encode the column bits. This
 * does not include padding to align on a byte boundary.
 *
 * @param chip   NAND chip to get data for
 *
 * @return Number of column bits
 */
static inline int __cvmx_nand_get_column_bits(int chip)
{
    return cvmx_pop(cvmx_nand_state[chip].page_size - 1);
}


/**
 * @INTERNAL
 * Get the number of bits required to encode the row bits. This
 * does not include padding to align on a byte boundary.
 *
 * @param chip   NAND chip to get data for
 *
 * @return Number of row bits
 */
static inline int __cvmx_nand_get_row_bits(int chip)
{
    return cvmx_pop(cvmx_nand_state[chip].blocks-1) + cvmx_pop(cvmx_nand_state[chip].pages_per_block-1);
}


/**
 * @INTERNAL
 * Get the number of address cycles required for this NAND part.
 * This include column bits, padding, page bits, and block bits.
 *
 * @param chip   NAND chip to get data for
 *
 * @return Number of address cycles on the bus
 */
static inline int __cvmx_nand_get_address_cycles(int chip)
{
    int address_bits = ((__cvmx_nand_get_column_bits(chip) + 7) >> 3) << 3;
    address_bits += ((__cvmx_nand_get_row_bits(chip) + 7) >> 3) << 3;
    return (address_bits + 7) >> 3;
}


/**
 * @INTERNAL
 * Build the set of command common to most transactions
 * @param chip      NAND chip to program
 * @param cmd_data  NAND command for CLE cycle 1
 * @param num_address_cycles
 *                  Number of address cycles to put on the bus
 * @param nand_address
 *                  Data to be put on the bus. It is translated according to
 *                  the rules in the file information section.
 *
 * @param cmd_data2 If non zero, adds a second CLE cycle used by a number of NAND
 *                  transactions.
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
static inline cvmx_nand_status_t __cvmx_nand_build_pre_cmd(int chip, int cmd_data, int num_address_cycles, uint64_t nand_address, int cmd_data2)
{
    cvmx_nand_status_t result;
    cvmx_nand_cmd_t cmd;

    CVMX_NAND_LOG_CALLED();

    /* Send timing parameters */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.set_tm_par.one = 1;
    cmd.set_tm_par.tim_mult = cvmx_nand_state[chip].tim_mult;
    /* tim_par[0] unused */
    cmd.set_tm_par.tim_par1 = cvmx_nand_state[chip].tim_par[1];
    cmd.set_tm_par.tim_par2 = cvmx_nand_state[chip].tim_par[2];
    cmd.set_tm_par.tim_par3 = cvmx_nand_state[chip].tim_par[3];
    cmd.set_tm_par.tim_par4 = cvmx_nand_state[chip].tim_par[4];
    cmd.set_tm_par.tim_par5 = cvmx_nand_state[chip].tim_par[5];
    cmd.set_tm_par.tim_par6 = cvmx_nand_state[chip].tim_par[6];
    cmd.set_tm_par.tim_par7 = cvmx_nand_state[chip].tim_par[7];
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Send bus select */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.bus_acq.fifteen = 15;
    cmd.bus_acq.one = 1;
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Send chip select */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.chip_en.chip = chip;
    cmd.chip_en.one = 1;
    cmd.chip_en.three = 3;
    cmd.chip_en.width = (cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT) ? 2 : 1;
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Send wait, fixed time
    ** This meets chip enable to command latch enable timing.
    ** This is tCS - tCLS from the ONFI spec.
    ** Use tWP as a proxy, as this is adequate for
    ** all ONFI 1.0 timing modes. */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.wait.two = 2;
    cmd.wait.n = 1;
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Send CLE */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.cle.cmd_data = cmd_data;
    cmd.cle.clen1 = cvmx_nand_state[chip].clen[0];
    cmd.cle.clen2 = cvmx_nand_state[chip].clen[1];
    cmd.cle.clen3 = cvmx_nand_state[chip].clen[2];
    cmd.cle.four = 4;
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Send ALE */
    if (num_address_cycles)
    {
        memset(&cmd,  0,  sizeof(cmd));
        cmd.ale.adr_byte_num = num_address_cycles;
        if (num_address_cycles < __cvmx_nand_get_address_cycles(chip))
        {
            cmd.ale.adr_bytes_l = nand_address;
            cmd.ale.adr_bytes_h = nand_address >> 32;
        }
        else
        {
            int column_bits = __cvmx_nand_get_column_bits(chip);
            int column_shift = ((column_bits + 7) >> 3) << 3;
            int column = nand_address & (cvmx_nand_state[chip].page_size-1);
            int row = nand_address >> column_bits;
            cmd.ale.adr_bytes_l = column + (row << column_shift);
            cmd.ale.adr_bytes_h = row >> (32 - column_shift);
        }
        cmd.ale.alen1 = cvmx_nand_state[chip].alen[0];
        cmd.ale.alen2 = cvmx_nand_state[chip].alen[1];
        cmd.ale.alen3 = cvmx_nand_state[chip].alen[2];
        cmd.ale.alen4 = cvmx_nand_state[chip].alen[3];
        cmd.ale.five = 5;
        result = cvmx_nand_submit(cmd);
        if (result)
            CVMX_NAND_RETURN(result);
    }

    /* Send CLE 2 */
    if (cmd_data2)
    {
        memset(&cmd,  0,  sizeof(cmd));
        cmd.cle.cmd_data = cmd_data2;
        cmd.cle.clen1 = cvmx_nand_state[chip].clen[0];
        cmd.cle.clen2 = cvmx_nand_state[chip].clen[1];
        cmd.cle.clen3 = cvmx_nand_state[chip].clen[2];
        cmd.cle.four = 4;
        result = cvmx_nand_submit(cmd);
        if (result)
            CVMX_NAND_RETURN(result);
    }

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}


/**
 * @INTERNAL
 * Build the set of command common to most transactions
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
static inline cvmx_nand_status_t __cvmx_nand_build_post_cmd(void)
{
    cvmx_nand_status_t result;
    cvmx_nand_cmd_t cmd;

    CVMX_NAND_LOG_CALLED();

    /* Send chip deselect */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.chip_dis.three = 3;
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Send bus release */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.bus_rel.fifteen = 15;
    result = cvmx_nand_submit(cmd);
    if (result)
        CVMX_NAND_RETURN(result);

    /* Ring the doorbell */
    cvmx_write_csr(CVMX_NDF_DRBELL, 1);
    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}


/**
 * @INTERNAL
 * Setup the NAND DMA engine for a transfer
 *
 * @param chip     Chip select for NAND flash
 * @param is_write Non zero if this is a write
 * @param buffer_address
 *                 Physical memory address to DMA to/from
 * @param buffer_length
 *                 Length of the DMA in bytes
 */
static inline void __cvmx_nand_setup_dma(int chip, int is_write, uint64_t buffer_address, int buffer_length)
{
    union cvmx_mio_ndf_dma_cfg ndf_dma_cfg;
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("%d", is_write);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);
    CVMX_NAND_LOG_PARAM("%d", buffer_length);
    ndf_dma_cfg.u64 = 0;
    ndf_dma_cfg.s.en = 1;
    ndf_dma_cfg.s.rw = is_write; /* One means DMA reads from memory and writes to flash */
    ndf_dma_cfg.s.clr = 0;
    ndf_dma_cfg.s.size = ((buffer_length + 7) >> 3) - 1;
    ndf_dma_cfg.s.adr = buffer_address;
    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_MIO_NDF_DMA_CFG, ndf_dma_cfg.u64);
    CVMX_NAND_RETURN_NOTHING();
}


/**
 * Dump a buffer out in hex for debug
 *
 * @param buffer_address
 *               Starting physical address
 * @param buffer_length
 *               Number of bytes to display
 */
static void __cvmx_nand_hex_dump(uint64_t buffer_address, int buffer_length)
{
    uint8_t *buffer = cvmx_phys_to_ptr(buffer_address);
    int offset = 0;
    while (offset < buffer_length)
    {
        int i;
        cvmx_dprintf("%*s%04x:",  2*debug_indent, "", offset);
        for (i=0; i<32; i++)
        {
            if ((i&3) == 0)
                cvmx_dprintf(" ");
            if (offset+i < buffer_length)
                cvmx_dprintf("%02x", 0xff & buffer[offset+i]);
            else
                cvmx_dprintf("  ");
        }
        cvmx_dprintf("\n");
        offset += 32;
    }
}

/**
 * @INTERNAL
 * Perform a low level NAND read command
 *
 * @param chip   Chip to read from
 * @param nand_command1
 *               First command cycle value
 * @param address_cycles
 *               Number of address cycles after comand 1
 * @param nand_address
 *               NAND address to use for address cycles
 * @param nand_command2
 *               NAND command cycle 2 if not zero
 * @param buffer_address
 *               Physical address to DMA into
 * @param buffer_length
 *               Length of the transfer in bytes
 *
 * @return Number of bytes transfered or a negative error code
 */
static inline int __cvmx_nand_low_level_read(int chip, int nand_command1, int address_cycles, uint64_t nand_address, int nand_command2, uint64_t buffer_address, int buffer_length)
{
    cvmx_nand_cmd_t cmd;
    union cvmx_mio_ndf_dma_cfg ndf_dma_cfg;
    int bytes;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%x", nand_command1);
    CVMX_NAND_LOG_PARAM("%d", address_cycles);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)nand_address);
    CVMX_NAND_LOG_PARAM("0x%x", nand_command2);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);
    CVMX_NAND_LOG_PARAM("%d", buffer_length);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_address)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_address & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_length & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_length)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    /* Build the command and address cycles */
    if (__cvmx_nand_build_pre_cmd(chip, nand_command1, address_cycles, nand_address, nand_command2))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Send WAIT.  This waits for some time, then
    ** waits for busy to be de-asserted. */
    if (__wait_for_busy_done(chip))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Wait for tRR after busy de-asserts.
    ** Use 2* tALS as proxy.  This is overkill in
    ** the slow modes, but not bad in the faster ones. */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.wait.two = 2;
    cmd.wait.n=4;
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Send READ */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.rd.data_bytes = buffer_length;
    if (cvmx_nand_state[chip].onfi_timing >= 4)
        cmd.rd.nine = 10;  /* READ_EDO command is required for ONFI timing modes 4 and 5 */
    else
        cmd.rd.nine = 9;
    cmd.rd.rdn1 = cvmx_nand_state[chip].rdn[0];
    cmd.rd.rdn2 = cvmx_nand_state[chip].rdn[1];
    cmd.rd.rdn3 = cvmx_nand_state[chip].rdn[2];
    cmd.rd.rdn4 = cvmx_nand_state[chip].rdn[3];
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    __cvmx_nand_setup_dma(chip, 0, buffer_address, buffer_length);

    if (__cvmx_nand_build_post_cmd())
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);
    WATCHDOG_RESET();
    /* Wait for the DMA to complete */
    if (CVMX_WAIT_FOR_FIELD64(CVMX_MIO_NDF_DMA_CFG, cvmx_mio_ndf_dma_cfg_t, en, ==, 0, NAND_TIMEOUT_USECS_READ))
    {
        WATCHDOG_RESET();
        CVMX_NAND_RETURN(CVMX_NAND_TIMEOUT);
    }
    /* Return the number of bytes transfered */
    ndf_dma_cfg.u64 = cvmx_read_csr(CVMX_MIO_NDF_DMA_CFG);
    bytes = ndf_dma_cfg.s.adr - buffer_address;

    if (cvmx_unlikely(cvmx_nand_flags & CVMX_NAND_INITIALIZE_FLAGS_DEBUG))
        __cvmx_nand_hex_dump(buffer_address, bytes);

    CVMX_NAND_RETURN(bytes);
}


/**
 * Read a page from NAND. If the buffer has room, the out of band
 * data will be included.
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               Location in NAND to read. See description in file comment
 * @param buffer_address
 *               Physical address to store the result at
 * @param buffer_length
 *               Number of bytes to read
 *
 * @return Bytes read on success, a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_page_read(int chip, uint64_t nand_address, uint64_t buffer_address, int buffer_length)
{
    int bytes;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)nand_address);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);
    CVMX_NAND_LOG_PARAM("%d", buffer_length);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!cvmx_nand_state[chip].page_size)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_address)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_address & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_length & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_length)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    /* For 16 bit mode, addresses within a page are word address, rather than byte addresses */
    if (cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT)
            nand_address = (nand_address & ~(cvmx_nand_state[chip].page_size - 1)) |  ((nand_address & (cvmx_nand_state[chip].page_size - 1)) >> 1);

    bytes = __cvmx_nand_low_level_read(chip, NAND_COMMAND_READ, __cvmx_nand_get_address_cycles(chip), nand_address, NAND_COMMAND_READ_FIN, buffer_address, buffer_length);
    CVMX_NAND_RETURN(bytes);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_page_read);
#endif


/**
 * Write a page to NAND. The buffer must contain the entire page
 * including the out of band data.
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               Location in NAND to write. See description in file comment
 * @param buffer_address
 *               Physical address to read the data from
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_page_write(int chip, uint64_t nand_address, uint64_t buffer_address)
{
    cvmx_nand_cmd_t cmd;
    int buffer_length;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)nand_address);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!cvmx_nand_state[chip].page_size)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_address)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_address & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    /* For 16 bit mode, addresses within a page are word address, rather than byte addresses */
    if (cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT)
            nand_address = (nand_address & ~(cvmx_nand_state[chip].page_size - 1)) |  ((nand_address & (cvmx_nand_state[chip].page_size - 1)) >> 1);

    buffer_length = cvmx_nand_state[chip].page_size + cvmx_nand_state[chip].oob_size;

    /* The NAND DMA engine always does transfers in 8 byte blocks, so round the buffer size down
    ** to a multiple of 8, otherwise we will transfer too much data to the NAND chip.
    ** Note this prevents the last few bytes of the OOB being written.  If these bytes
    ** need to be written, then this check needs to be removed, but this will result in
    ** extra write cycles beyond the end of the OOB. */
    buffer_length &= ~0x7;

    /* Build the command and address cycles */
    if (__cvmx_nand_build_pre_cmd(chip, NAND_COMMAND_PROGRAM, __cvmx_nand_get_address_cycles(chip), nand_address, 0))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Send WRITE */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.wr.data_bytes = buffer_length;
    cmd.wr.eight = 8;
    cmd.wr.wrn1 = cvmx_nand_state[chip].wrn[0];
    cmd.wr.wrn2 = cvmx_nand_state[chip].wrn[1];
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Send WRITE command */
    memset(&cmd,  0,  sizeof(cmd));
    cmd.cle.cmd_data = NAND_COMMAND_PROGRAM_FIN;
    cmd.cle.clen1 = cvmx_nand_state[chip].clen[0];
    cmd.cle.clen2 = cvmx_nand_state[chip].clen[1];
    cmd.cle.clen3 = cvmx_nand_state[chip].clen[2];
    cmd.cle.four = 4;
    if (cvmx_nand_submit(cmd))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    __cvmx_nand_setup_dma(chip, 1, buffer_address, buffer_length);

    /* WAIT for R_B to signal program is complete  */
    if (__wait_for_busy_done(chip))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    if (__cvmx_nand_build_post_cmd())
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Wait for the DMA to complete */
    WATCHDOG_RESET();
    if (CVMX_WAIT_FOR_FIELD64(CVMX_MIO_NDF_DMA_CFG, cvmx_mio_ndf_dma_cfg_t, en, ==, 0, NAND_TIMEOUT_USECS_WRITE))
    {
        WATCHDOG_RESET();
        CVMX_NAND_RETURN(CVMX_NAND_TIMEOUT);
    }
    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_page_write);
#endif


/**
 * Erase a NAND block. A single block contains multiple pages.
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               Location in NAND to erase. See description in file comment
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_block_erase(int chip, uint64_t nand_address)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)nand_address);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!cvmx_nand_state[chip].page_size)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    /* Build the command and address cycles */
    if (__cvmx_nand_build_pre_cmd(chip, NAND_COMMAND_ERASE,
                                  (__cvmx_nand_get_row_bits(chip)+7) >> 3,
                                  nand_address >> __cvmx_nand_get_column_bits(chip),
                                  NAND_COMMAND_ERASE_FIN))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* WAIT for R_B to signal erase is complete  */
    if (__wait_for_busy_done(chip))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    if (__cvmx_nand_build_post_cmd())
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* Wait for the command queue to be idle, which means the wait is done */
    WATCHDOG_RESET();
    if (CVMX_WAIT_FOR_FIELD64(CVMX_NDF_ST_REG, cvmx_ndf_st_reg_t, exe_idle, ==, 1, NAND_TIMEOUT_USECS_BLOCK_ERASE))
    {
        WATCHDOG_RESET();
        CVMX_NAND_RETURN(CVMX_NAND_TIMEOUT);
    }

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_block_erase);
#endif


/* Some reads (read ID, read parameter page) only use the low 8 bits of the bus
** in 16 bit mode.  We remove the unused bytes so that the data we present to the
** caller is as expected (same as 8 bit mode.)
*/
static void __cvmx_nand_fixup_16bit_id_reads(uint8_t *buf, int buffer_length)
{
    /* Decimate data, taking only every other byte. */
    int i;
    for (i = 0; i < buffer_length/2; i++)
        buf[i] = buf[2*i + 1];
}

/**
 * Read the NAND ID information
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               NAND address to read ID from. Usually this is either 0x0 or 0x20.
 * @param buffer_address
 *               Physical address to store data in
 * @param buffer_length
 *               Length of the buffer. Usually this is 4-8 bytes.  For 16 bit mode, this must be twice
 *               as large as the actual expected data.
 *
 * @return Bytes read on success, a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_read_id(int chip, uint64_t nand_address, uint64_t buffer_address, int buffer_length)
{
    int bytes;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)nand_address);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);
    CVMX_NAND_LOG_PARAM("%d", buffer_length);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_address)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_address & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_length)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    bytes = __cvmx_nand_low_level_read(chip, NAND_COMMAND_READ_ID, 1, nand_address, 0, buffer_address, buffer_length);
    if (cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT)
        __cvmx_nand_fixup_16bit_id_reads(cvmx_phys_to_ptr(buffer_address), buffer_length);

    CVMX_NAND_RETURN(bytes);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_read_id);
#endif


/**
 * Read the NAND parameter page
 *
 * @param chip   Chip select for NAND flash
 * @param buffer_address
 *               Physical address to store data in
 * @param buffer_length
 *               Length of the buffer.  Usually 1024 bytes for 8 bit, 2048 for 16 bit mode.
 *
 * @return Bytes read on success, a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_read_param_page(int chip, uint64_t buffer_address, int buffer_length)
{
    int bytes;

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);
    CVMX_NAND_LOG_PARAM("0x%llx", (ULL)buffer_address);
    CVMX_NAND_LOG_PARAM("%d", buffer_length);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_address)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_address & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (buffer_length & 7)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!buffer_length)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    bytes = __cvmx_nand_low_level_read(chip, NAND_COMMAND_READ_PARAM_PAGE, 1, 0x0, 0, buffer_address, buffer_length);
    if (cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT)
        __cvmx_nand_fixup_16bit_id_reads(cvmx_phys_to_ptr(buffer_address), buffer_length);
    CVMX_NAND_RETURN(bytes);
}


/**
 * Get the status of the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return NAND status or a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_get_status(int chip)
{
    int status;
    int offset = !!(cvmx_nand_state[chip].flags & CVMX_NAND_STATE_16BIT);  /* Normalize flag to 0/1 */

    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    *((uint8_t*)cvmx_nand_buffer + offset)  = 0xff;
    status = __cvmx_nand_low_level_read(chip, NAND_COMMAND_STATUS, 0, 0, 0, cvmx_ptr_to_phys(cvmx_nand_buffer), 8);
    if (status > 0)
        status = *((uint8_t*)cvmx_nand_buffer + offset);

    CVMX_NAND_RETURN(status);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_get_status);
#endif


/**
 * Get the page size, excluding out of band data. This  function
 * will return zero for chip selects not connected to NAND.
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Page size in bytes or a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_get_page_size(int chip)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    CVMX_NAND_RETURN(cvmx_nand_state[chip].page_size);
}


/**
 * Get the OOB size.
 *
 * @param chip   Chip select for NAND flash
 *
 * @return OOB in bytes or a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_get_oob_size(int chip)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    CVMX_NAND_RETURN(cvmx_nand_state[chip].oob_size);
}


/**
 * Get the number of pages per NAND block
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Number of pages in each block or a negative cvmx_nand_status_t error
 *         code on failure
 */
int cvmx_nand_get_pages_per_block(int chip)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    CVMX_NAND_RETURN(cvmx_nand_state[chip].pages_per_block);
}


/**
 * Get the number of blocks in the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Number of blocks or a negative cvmx_nand_status_t error code on failure
 */
int cvmx_nand_get_blocks(int chip)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    CVMX_NAND_RETURN(cvmx_nand_state[chip].blocks);
}


/**
 * Reset the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
cvmx_nand_status_t cvmx_nand_reset(int chip)
{
    CVMX_NAND_LOG_CALLED();
    CVMX_NAND_LOG_PARAM("%d", chip);

    if ((chip < 0) || (chip > 7))
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);
    if (!cvmx_nand_state[chip].page_size)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    if (__cvmx_nand_build_pre_cmd(chip, NAND_COMMAND_RESET, 0, 0, 0))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    /* WAIT for R_B to signal reset is complete  */
    if (__wait_for_busy_done(chip))
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    if (__cvmx_nand_build_post_cmd())
        CVMX_NAND_RETURN(CVMX_NAND_NO_MEMORY);

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_nand_reset);
#endif




/**
 * This function computes the Octeon specific ECC data used by the NAND boot
 * feature.
 *
 * @param block  pointer to 256 bytes of data
 * @param eccp   pointer to where 8 bytes of ECC data will be stored
 */
void cvmx_nand_compute_boot_ecc(unsigned char *block, unsigned char *eccp)
{
	unsigned char pd0, pd1, pd2;
	int i, j;

	pd0 = pd1 = pd2 = 0;

	for (i = 0; i < 256; i++)	/* PD0<0> */
		pd0 ^= (block[i] ^ (block[i] >> 2) ^ (block[i] >> 4) ^ (block[i] >> 6)) & 1;
	for (i = 0; i < 256; i++)	/* PD0<1> */
		pd0 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 4) ^ (block[i] >> 5)) & 1) << 1;
	for (i = 0; i < 256; i++)	/* PD0<2> */
		pd0 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^ (block[i] >> 3)) & 1) << 2;
	for (i = 0; i < 128; i++)	/* PD0<3> */
		pd0 ^= ((block[2*i] ^ (block[2*i] >> 1) ^ (block[2*i] >> 2) ^
			(block[2*i] >> 3) ^ (block[2*i] >> 4) ^ (block[2*i] >> 5) ^
			(block[2*i] >> 6) ^ (block[2*i] >> 7)) & 1) << 3;
	for (i = 0; i < 64; i++)	/* PD0<4> */
		for (j = 0; j < 2; j++)
			pd0 ^= ((block[4*i+j] ^ (block[4*i+j] >> 1) ^ (block[4*i+j] >> 2) ^
				(block[4*i+j] >> 3) ^ (block[4*i+j] >> 4) ^ (block[4*i+j] >> 5) ^
				(block[4*i+j] >> 6) ^ (block[4*i+j] >> 7)) & 1) << 4;
	for (i = 0; i < 32; i++)	/* PD0<5> */
		for (j = 0; j < 4; j++)
			pd0 ^= ((block[8*i+j] ^ (block[8*i+j] >> 1) ^ (block[8*i+j] >> 2) ^
				(block[8*i+j] >> 3) ^ (block[8*i+j] >> 4) ^ (block[8*i+j] >> 5) ^
				(block[8*i+j] >> 6) ^ (block[8*i+j] >> 7)) & 1) << 5;
	for (i = 0; i < 16; i++)	/* PD0<6> */
		for (j = 0; j < 8; j++)
			pd0 ^= ((block[16*i+j] ^ (block[16*i+j] >> 1) ^ (block[16*i+j] >> 2) ^
				(block[16*i+j] >> 3) ^ (block[16*i+j] >> 4) ^ (block[16*i+j] >> 5) ^
				(block[16*i+j] >> 6) ^ (block[16*i+j] >> 7)) & 1) << 6;
	for (i = 0; i < 8; i++)		/* PD0<7> */
		for (j = 0; j < 16; j++)
			pd0 ^= ((block[32*i+j] ^ (block[32*i+j] >> 1) ^ (block[32*i+j] >> 2) ^
				(block[32*i+j] >> 3) ^ (block[32*i+j] >> 4) ^ (block[32*i+j] >> 5) ^
				(block[32*i+j] >> 6) ^ (block[32*i+j] >> 7)) & 1) << 7;
	for (i = 0; i < 4; i++)		/* PD1<0> */
		for (j = 0; j < 32; j++)
			pd1 ^= ((block[64*i+j] ^ (block[64*i+j] >> 1) ^ (block[64*i+j] >> 2) ^
				(block[64*i+j] >> 3) ^ (block[64*i+j] >> 4) ^ (block[64*i+j] >> 5) ^
				(block[64*i+j] >> 6) ^ (block[64*i+j] >> 7)) & 1) << 0;
	for (i = 0; i < 2; i++)		/* PD1<1> */
		for (j = 0; j < 64; j++)
			pd1 ^= ((block[128*i+j] ^ (block[128*i+j] >> 1) ^ (block[128*i+j] >> 2) ^
				(block[128*i+j] >> 3) ^ (block[128*i+j] >> 4) ^ (block[128*i+j] >> 5) ^
				(block[128*i+j] >> 6) ^ (block[128*i+j] >> 7)) & 1) << 1;
	for (i = 0; i < 128; i++)	/* PD1<2> */
		pd1 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^
			(block[i] >> 3) ^ (block[i] >> 4) ^ (block[i] >> 5) ^
			(block[i] >> 6) ^ (block[i] >> 7)) & 1) << 2;
	/* PD1<3> */
	/* PD1<4> */
	for (i = 0; i < 256; i++)	/* PD1<5> */
		pd1 ^= (((block[i] >> 1) ^ (block[i] >> 3) ^ (block[i] >> 5) ^ (block[i] >> 7)) & 1) << 5;
	for (i = 0; i < 256; i++)	/* PD1<6> */
		pd1 ^= (((block[i] >> 2) ^ (block[i] >> 3) ^ (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 6;
	for (i = 0; i < 256; i++)	/* PD1<7> */
		pd1 ^= (((block[i] >> 4) ^ (block[i] >> 5) ^ (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 7;
	for (i = 0; i < 128; i++)	/* PD2<0> */
		pd2 ^= ((block[2*i+1] ^ (block[2*i+1] >> 1) ^ (block[2*i+1] >> 2) ^
			(block[2*i+1] >> 3) ^ (block[2*i+1] >> 4) ^ (block[2*i+1] >> 5) ^
			(block[2*i+1] >> 6) ^ (block[2*i+1] >> 7)) & 1) << 0;
	for (i = 0; i < 64; i++)	/* PD2<1> */
		for (j = 2; j < 4; j++)
			pd2 ^= ((block[4*i+j] ^ (block[4*i+j] >> 1) ^ (block[4*i+j] >> 2) ^
				(block[4*i+j] >> 3) ^ (block[4*i+j] >> 4) ^ (block[4*i+j] >> 5) ^
				(block[4*i+j] >> 6) ^ (block[4*i+j] >> 7)) & 1) << 1;
	for (i = 0; i < 32; i++)	/* PD2<2> */
		for (j = 4; j < 8; j++)
			pd2 ^= ((block[8*i+j] ^ (block[8*i+j] >> 1) ^ (block[8*i+j] >> 2) ^
				(block[8*i+j] >> 3) ^ (block[8*i+j] >> 4) ^ (block[8*i+j] >> 5) ^
				(block[8*i+j] >> 6) ^ (block[8*i+j] >> 7)) & 1) << 2;
	for (i = 0; i < 16; i++)	/* PD2<3> */
		for (j = 8; j < 16; j++)
			pd2 ^= ((block[16*i+j] ^ (block[16*i+j] >> 1) ^ (block[16*i+j] >> 2) ^
				(block[16*i+j] >> 3) ^ (block[16*i+j] >> 4) ^ (block[16*i+j] >> 5) ^
				(block[16*i+j] >> 6) ^ (block[16*i+j] >> 7)) & 1) << 3;
	for (i = 0; i < 8; i++)		/* PD2<4> */
		for (j = 16; j < 32; j++)
			pd2 ^= ((block[32*i+j] ^ (block[32*i+j] >> 1) ^ (block[32*i+j] >> 2) ^
				(block[32*i+j] >> 3) ^ (block[32*i+j] >> 4) ^ (block[32*i+j] >> 5) ^
				(block[32*i+j] >> 6) ^ (block[32*i+j] >> 7)) & 1) << 4;
	for (i = 0; i < 4; i++)		/* PD2<5> */
		for (j = 32; j < 64; j++)
			pd2 ^= ((block[64*i+j] ^ (block[64*i+j] >> 1) ^ (block[64*i+j] >> 2) ^
				(block[64*i+j] >> 3) ^ (block[64*i+j] >> 4) ^ (block[64*i+j] >> 5) ^
				(block[64*i+j] >> 6) ^ (block[64*i+j] >> 7)) & 1) << 5;
	for (i = 0; i < 2; i++)		/* PD2<6> */
		for (j = 64; j < 128; j++)
			pd2 ^= ((block[128*i+j] ^ (block[128*i+j] >> 1) ^ (block[128*i+j] >> 2) ^
				(block[128*i+j] >> 3) ^ (block[128*i+j] >> 4) ^ (block[128*i+j] >> 5) ^
				(block[128*i+j] >> 6) ^ (block[128*i+j] >> 7)) & 1) << 6;
	for (i = 128; i < 256; i++)	/* PD2<7> */
		pd2 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^
			(block[i] >> 3) ^ (block[i] >> 4) ^ (block[i] >> 5) ^
			(block[i] >> 6) ^ (block[i] >> 7)) & 1) << 7;

	eccp[0] = pd0;
	eccp[1] = pd1;
	eccp[2] = pd2;
}

/**
 * Check an Octeon ECC block, fixing errors if possible
 *
 * @param block  Pointer to block to check
 *
 * @return Zero if block has no errors, one if errors were corrected, two
 *         if the errors could not be corrected.
 */
int cvmx_nand_correct_boot_ecc(uint8_t *block)
{
    unsigned char pd0, pd1, pd2;
    int i, j;
    unsigned char xorpd0, xorpd1, xorpd2;
    int xor_num;
    unsigned int check;

    asm volatile ("pref 0,0(%0);pref 0,128(%0);pref 0,256(%0)\n" :: "r" (block));

    pd0 = pd1 = pd2 = 0;

    for (i = 0; i < 256; i++)   /* PD0<0> */
        pd0 ^= (block[i] ^ (block[i] >> 2) ^ (block[i] >> 4) ^ (block[i] >> 6)) & 1;
    for (i = 0; i < 256; i++)   /* PD0<1> */
        pd0 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 4) ^ (block[i] >> 5)) & 1) << 1;
    for (i = 0; i < 256; i++)   /* PD0<2> */
        pd0 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^ (block[i] >> 3)) & 1) << 2;
    for (i = 0; i < 128; i++)   /* PD0<3> */
        pd0 ^= ((block[2*i] ^ (block[2*i] >> 1) ^ (block[2*i] >> 2) ^
                 (block[2*i] >> 3) ^ (block[2*i] >> 4) ^ (block[2*i] >> 5) ^
                 (block[2*i] >> 6) ^ (block[2*i] >> 7)) & 1) << 3;
    for (i = 0; i < 64; i++)    /* PD0<4> */
        for (j = 0; j < 2; j++)
            pd0 ^= ((block[4*i+j] ^ (block[4*i+j] >> 1) ^ (block[4*i+j] >> 2) ^
                     (block[4*i+j] >> 3) ^ (block[4*i+j] >> 4) ^ (block[4*i+j] >> 5) ^
                     (block[4*i+j] >> 6) ^ (block[4*i+j] >> 7)) & 1) << 4;
    for (i = 0; i < 32; i++)    /* PD0<5> */
        for (j = 0; j < 4; j++)
            pd0 ^= ((block[8*i+j] ^ (block[8*i+j] >> 1) ^ (block[8*i+j] >> 2) ^
                     (block[8*i+j] >> 3) ^ (block[8*i+j] >> 4) ^ (block[8*i+j] >> 5) ^
                     (block[8*i+j] >> 6) ^ (block[8*i+j] >> 7)) & 1) << 5;
    for (i = 0; i < 16; i++)    /* PD0<6> */
        for (j = 0; j < 8; j++)
            pd0 ^= ((block[16*i+j] ^ (block[16*i+j] >> 1) ^ (block[16*i+j] >> 2) ^
                     (block[16*i+j] >> 3) ^ (block[16*i+j] >> 4) ^ (block[16*i+j] >> 5) ^
                     (block[16*i+j] >> 6) ^ (block[16*i+j] >> 7)) & 1) << 6;
    for (i = 0; i < 8; i++)     /* PD0<7> */
        for (j = 0; j < 16; j++)
            pd0 ^= ((block[32*i+j] ^ (block[32*i+j] >> 1) ^ (block[32*i+j] >> 2) ^
                     (block[32*i+j] >> 3) ^ (block[32*i+j] >> 4) ^ (block[32*i+j] >> 5) ^
                     (block[32*i+j] >> 6) ^ (block[32*i+j] >> 7)) & 1) << 7;
    for (i = 0; i < 4; i++)     /* PD1<0> */
        for (j = 0; j < 32; j++)
            pd1 ^= ((block[64*i+j] ^ (block[64*i+j] >> 1) ^ (block[64*i+j] >> 2) ^
                     (block[64*i+j] >> 3) ^ (block[64*i+j] >> 4) ^ (block[64*i+j] >> 5) ^
                     (block[64*i+j] >> 6) ^ (block[64*i+j] >> 7)) & 1) << 0;
    for (i = 0; i < 2; i++)     /* PD1<1> */
        for (j = 0; j < 64; j++)
            pd1 ^= ((block[128*i+j] ^ (block[128*i+j] >> 1) ^ (block[128*i+j] >> 2) ^
                     (block[128*i+j] >> 3) ^ (block[128*i+j] >> 4) ^ (block[128*i+j] >> 5) ^
                     (block[128*i+j] >> 6) ^ (block[128*i+j] >> 7)) & 1) << 1;
    for (i = 0; i < 128; i++)   /* PD1<2> */
        pd1 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^
                 (block[i] >> 3) ^ (block[i] >> 4) ^ (block[i] >> 5) ^
                 (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 2;
    /* PD1<3> */
    /* PD1<4> */
    for (i = 0; i < 256; i++)   /* PD1<5> */
        pd1 ^= (((block[i] >> 1) ^ (block[i] >> 3) ^ (block[i] >> 5) ^ (block[i] >> 7)) & 1) << 5;
    for (i = 0; i < 256; i++)   /* PD1<6> */
        pd1 ^= (((block[i] >> 2) ^ (block[i] >> 3) ^ (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 6;
    for (i = 0; i < 256; i++)   /* PD1<7> */
        pd1 ^= (((block[i] >> 4) ^ (block[i] >> 5) ^ (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 7;
    for (i = 0; i < 128; i++)   /* PD2<0> */
        pd2 ^= ((block[2*i+1] ^ (block[2*i+1] >> 1) ^ (block[2*i+1] >> 2) ^
                 (block[2*i+1] >> 3) ^ (block[2*i+1] >> 4) ^ (block[2*i+1] >> 5) ^
                 (block[2*i+1] >> 6) ^ (block[2*i+1] >> 7)) & 1) << 0;
    for (i = 0; i < 64; i++)    /* PD2<1> */
        for (j = 2; j < 4; j++)
            pd2 ^= ((block[4*i+j] ^ (block[4*i+j] >> 1) ^ (block[4*i+j] >> 2) ^
                     (block[4*i+j] >> 3) ^ (block[4*i+j] >> 4) ^ (block[4*i+j] >> 5) ^
                     (block[4*i+j] >> 6) ^ (block[4*i+j] >> 7)) & 1) << 1;
    for (i = 0; i < 32; i++)    /* PD2<2> */
        for (j = 4; j < 8; j++)
            pd2 ^= ((block[8*i+j] ^ (block[8*i+j] >> 1) ^ (block[8*i+j] >> 2) ^
                     (block[8*i+j] >> 3) ^ (block[8*i+j] >> 4) ^ (block[8*i+j] >> 5) ^
                     (block[8*i+j] >> 6) ^ (block[8*i+j] >> 7)) & 1) << 2;
    for (i = 0; i < 16; i++)    /* PD2<3> */
        for (j = 8; j < 16; j++)
            pd2 ^= ((block[16*i+j] ^ (block[16*i+j] >> 1) ^ (block[16*i+j] >> 2) ^
                     (block[16*i+j] >> 3) ^ (block[16*i+j] >> 4) ^ (block[16*i+j] >> 5) ^
                     (block[16*i+j] >> 6) ^ (block[16*i+j] >> 7)) & 1) << 3;
    for (i = 0; i < 8; i++)     /* PD2<4> */
        for (j = 16; j < 32; j++)
            pd2 ^= ((block[32*i+j] ^ (block[32*i+j] >> 1) ^ (block[32*i+j] >> 2) ^
                     (block[32*i+j] >> 3) ^ (block[32*i+j] >> 4) ^ (block[32*i+j] >> 5) ^
                     (block[32*i+j] >> 6) ^ (block[32*i+j] >> 7)) & 1) << 4;
    for (i = 0; i < 4; i++)     /* PD2<5> */
        for (j = 32; j < 64; j++)
            pd2 ^= ((block[64*i+j] ^ (block[64*i+j] >> 1) ^ (block[64*i+j] >> 2) ^
                     (block[64*i+j] >> 3) ^ (block[64*i+j] >> 4) ^ (block[64*i+j] >> 5) ^
                     (block[64*i+j] >> 6) ^ (block[64*i+j] >> 7)) & 1) << 5;
    for (i = 0; i < 2; i++)     /* PD2<6> */
        for (j = 64; j < 128; j++)
            pd2 ^= ((block[128*i+j] ^ (block[128*i+j] >> 1) ^ (block[128*i+j] >> 2) ^
                     (block[128*i+j] >> 3) ^ (block[128*i+j] >> 4) ^ (block[128*i+j] >> 5) ^
                     (block[128*i+j] >> 6) ^ (block[128*i+j] >> 7)) & 1) << 6;
    for (i = 128; i < 256; i++) /* PD2<7> */
        pd2 ^= ((block[i] ^ (block[i] >> 1) ^ (block[i] >> 2) ^
                 (block[i] >> 3) ^ (block[i] >> 4) ^ (block[i] >> 5) ^
                 (block[i] >> 6) ^ (block[i] >> 7)) & 1) << 7;

    xorpd0 = pd0 ^ block[256];
    xorpd1 = pd1 ^ block[257];
    xorpd2 = pd2 ^ block[258];

    xor_num = __builtin_popcount((xorpd0 << 16) | (xorpd1 << 8) | xorpd2);
    check = (((xorpd1 & 7) << 8) | xorpd0) ^ ((xorpd2 << 3) | (xorpd1 >> 5));

    if (xor_num == 0)
        return 0;
    else if ((xor_num > 1) && (check != 0x7FF))
        return 2;

    if (check == 0x7FF)
    {
        /* Correct the error */
        block[xorpd2] ^= 1 << (xorpd1 >> 5);
    }

    return 1;
}

cvmx_nand_status_t cvmx_nand_set_defaults(int page_size, int oob_size, int pages_per_block, int blocks, int onfi_timing_mode)
{
    if (!page_size || !oob_size || !pages_per_block || !blocks || onfi_timing_mode > 5)
        CVMX_NAND_RETURN(CVMX_NAND_INVALID_PARAM);

    cvmx_nand_default.page_size = page_size;
    cvmx_nand_default.oob_size = oob_size;
    cvmx_nand_default.pages_per_block = pages_per_block;
    cvmx_nand_default.blocks = blocks;
    cvmx_nand_default.onfi_timing = onfi_timing_mode;

    CVMX_NAND_RETURN(CVMX_NAND_SUCCESS);
}
