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
 * This header defines the CVMX interface to the NAND flash controller. The
 * basic operations common to all NAND devices are supported by this API, but
 * many more advanced functions are not support. The low level hardware supports
 * all types of transactions, but this API only implements the must commonly
 * used operations. This API performs no locking, so it is the responsibility of
 * the caller to make sure only one thread of execution is accessing the NAND
 * controller at a time. Most applications should not use this API directly but
 * instead use a flash logical layer supplied through a secondary system. For
 * example, the Linux MTD layer provides a driver for running JFFS2 on top of
 * NAND flash.
 *
 * <h2>Selecting the NAND Chip</h2>
 *
 * Octeon's NAND controller assumes a single NAND chip is connected to a boot
 * bus chip select. Throughout this API, NAND chips are referred to by the chip
 * select they are connected to (0-7). Chip select 0 will only be a NAND chip
 * when you are booting from NAND flash.
 *
 * <h2>NAND Addressing</h2>
 *
 * Various functions in cvmx-nand use addresses to index into NAND flash. All
 * functions us a uniform address translation scheme to map the passed address
 * into a NAND block, page, and column. In NAND flash a page represents the
 * basic unit of reads and writes. Each page contains a power of two number of
 * bytes and some number of extra out of band (OOB) bytes. A fixed number of
 * pages fit into each NAND block. Here is the mapping of bits in the cvmx-nand
 * address to the NAND hardware:
 * <pre>
 * 63     56      48      40      32      24      16       8      0
 * +-------+-------+-------+-------+-------+-------+-------+------+
 * |                                 64 bit cvmx-nand nand_address|
 * +------------------------------------------------+----+--------+
 * |                                          block |page| column |
 * +-------+-------+-------+-------+-------+--------+----+--------+
 * 63     56      48      40      32      24      16       8      0
 * </pre>
 * Basically the block, page, and column addresses are packet together. Before
 * being sent out the NAND pins for addressing the column is padded out to an
 * even number of bytes. This means that column address are 2 bytes, or 2
 * address cycles, for page sizes between 512 and 65536 bytes. Page sizes
 * between 128KB and 16MB would use 3 column address cycles. NAND device
 * normally either have 32 or 64 pages per block, needing either 5 or 6 address
 * bits respectively. This means you have 10 bits for block address using 4
 * address cycles, or 18 for 5 address cycles. Using the cvmx-nand addressing
 * scheme, it is not possible to directly index the OOB data. Instead you can
 * access it by reading or writing more data than the normal page size would
 * allow. Logically the OOB data is appended onto the the page data. For
 * example, this means that a read of 65 bytes from a column address of 0x7ff
 * would yield byte 2047 of the page and then 64 bytes of OOB data.
 *
 * <hr>$Revision: 35726 $<hr>
 */

#ifndef __CVMX_NAND_H__
#define __CVMX_NAND_H__

#ifdef	__cplusplus
extern "C" {
#endif

/* Maxium PAGE + OOB size supported.  This is used to size
** buffers, some that must be statically allocated. */
#define CVMX_NAND_MAX_PAGE_AND_OOB_SIZE      (4096 + 256)


/* Block size for boot ECC */
#define CVMX_NAND_BOOT_ECC_BLOCK_SIZE    (256)
/* ECC bytes for each block */
#define CVMX_NAND_BOOT_ECC_ECC_SIZE      (8)

/**
 * Flags to be passed to the initialize function
 */
typedef enum
{
    CVMX_NAND_INITIALIZE_FLAGS_16BIT = 1<<0,
    CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE = 1<<1,
    CVMX_NAND_INITIALIZE_FLAGS_DEBUG = 1<<15,
} cvmx_nand_initialize_flags_t;

/**
 * Return codes from NAND functions
 */
typedef enum
{
    CVMX_NAND_SUCCESS = 0,
    CVMX_NAND_NO_MEMORY = -1,
    CVMX_NAND_BUSY = -2,
    CVMX_NAND_INVALID_PARAM = -3,
    CVMX_NAND_TIMEOUT = -4,
    CVMX_NAND_ERROR = -5,
    CVMX_NAND_NO_DEVICE = -6,
} cvmx_nand_status_t;

/**
 * NAND NOP command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_4_63      : 60;
    uint64_t zero               : 4;
} cvmx_nand_cmd_nop_t;

/**
 * NAND SET_TM_PAR command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t tim_par7           : 8;
    uint64_t tim_par6           : 8;
    uint64_t tim_par5           : 8;
    uint64_t tim_par4           : 8;
    uint64_t tim_par3           : 8;
    uint64_t tim_par2           : 8;
    uint64_t tim_par1           : 8;
    uint64_t tim_mult           : 4;
    uint64_t one                : 4;
} cvmx_nand_cmd_set_tm_par_t;

/**
 * NAND WAIT command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_11_63     : 53;
    uint64_t n                  : 3;
    uint64_t reserved_5_7       : 3;
    uint64_t r_b                : 1;
    uint64_t two                : 4;
} cvmx_nand_cmd_wait_t;

/**
 * NAND CHIP_EN command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_10_63     : 54;
    uint64_t width              : 2;
    uint64_t one                : 1;
    uint64_t chip               : 3;
    uint64_t three              : 4;
} cvmx_nand_cmd_chip_en_t;

/**
 * NAND CHIP_DIS command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_4_63      : 60;
    uint64_t three              : 4;
} cvmx_nand_cmd_chip_dis_t;

/**
 * NAND CLE command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_25_63     : 39;
    uint64_t clen3              : 3;
    uint64_t clen2              : 3;
    uint64_t clen1              : 3;
    uint64_t cmd_data           : 8;
    uint64_t reserved_4_7       : 4;
    uint64_t four               : 4;
} cvmx_nand_cmd_cle_t;

/**
 * NAND ALE command definition
 */
typedef struct
{
    uint64_t reserved_96_127    : 32;
    uint64_t adr_bytes_h        : 32;
    uint64_t adr_bytes_l        : 32;
    uint64_t reserved_28_31     : 4;
    uint64_t alen4              : 3;
    uint64_t alen3              : 3;
    uint64_t alen2              : 3;
    uint64_t alen1              : 3;
    uint64_t reserved_12_15     : 4;
    uint64_t adr_byte_num       : 4;
    uint64_t reserved_4_7       : 4;
    uint64_t five               : 4;
} cvmx_nand_cmd_ale_t;

/**
 * NAND WR command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_31_63     : 34;
    uint64_t wrn2               : 3;
    uint64_t wrn1               : 3;
    uint64_t reserved_20_24     : 4;
    uint64_t data_bytes         : 16;
    uint64_t eight              : 4;
} cvmx_nand_cmd_wr_t;

/**
 * NAND RD command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_32_63     : 32;
    uint64_t rdn4               : 3;
    uint64_t rdn3               : 3;
    uint64_t rdn2               : 3;
    uint64_t rdn1               : 3;
    uint64_t data_bytes         : 16;
    uint64_t nine               : 4;
} cvmx_nand_cmd_rd_t;

/**
 * NAND RD_EDO command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_32_63     : 32;
    uint64_t rdn4               : 3;
    uint64_t rdn3               : 3;
    uint64_t rdn2               : 3;
    uint64_t rdn1               : 3;
    uint64_t data_bytes         : 16;
    uint64_t ten                : 4;
} cvmx_nand_cmd_rd_edo_t;

/**
 * NAND WAIT_STATUS command definition
 */
typedef struct
{
    uint64_t rdn4               : 3;
    uint64_t rdn3               : 3;
    uint64_t rdn2               : 3;
    uint64_t rdn1               : 3;
    uint64_t comp_byte          : 8;
    uint64_t and_mask           : 8;
    uint64_t nine               : 4;
    uint64_t reserved_28_95     : 64;
    uint64_t clen4              : 3;
    uint64_t clen3              : 3;
    uint64_t clen2              : 3;
    uint64_t clen1              : 3;
    uint64_t data               : 8;
    uint64_t reserved_4_7       : 4;
    uint64_t eleven             : 4;
} cvmx_nand_cmd_wait_status_t;

/**
 * NAND WAIT_STATUS_ALE command definition
 */
typedef struct
{
    uint64_t rdn4               : 3;
    uint64_t rdn3               : 3;
    uint64_t rdn2               : 3;
    uint64_t rdn1               : 3;
    uint64_t comp_byte          : 8;
    uint64_t and_mask           : 8;
    uint64_t nine               : 4;
    uint64_t adr_bytes          : 32;
    uint64_t reserved_60_63     : 4;
    uint64_t alen4              : 3;
    uint64_t alen3              : 3;
    uint64_t alen2              : 3;
    uint64_t alen1              : 3;
    uint64_t reserved_44_47     : 4;
    uint64_t adr_byte_num       : 4;
    uint64_t five               : 4;
    uint64_t reserved_25_31     : 7;
    uint64_t clen3              : 3;
    uint64_t clen2              : 3;
    uint64_t clen1              : 3;
    uint64_t data               : 8;
    uint64_t reserved_4_7       : 4;
    uint64_t eleven             : 4;
} cvmx_nand_cmd_wait_status_ale_t;

/**
 * NAND BUS_ACQ command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_8_63      : 56;
    uint64_t one                : 4;
    uint64_t fifteen            : 4;
} cvmx_nand_cmd_bus_acq_t;

/**
 * NAND BUS_REL command definition
 */
typedef struct
{
    uint64_t reserved_64_127    : 64;
    uint64_t reserved_8_63      : 56;
    uint64_t zero               : 4;
    uint64_t fifteen            : 4;
} cvmx_nand_cmd_bus_rel_t;

/**
 * NAND command union of all possible commands
 */
typedef union
{
    uint64_t u64[2];
    cvmx_nand_cmd_nop_t             nop;
    cvmx_nand_cmd_set_tm_par_t      set_tm_par;
    cvmx_nand_cmd_wait_t            wait;
    cvmx_nand_cmd_chip_en_t         chip_en;
    cvmx_nand_cmd_chip_dis_t        chip_dis;
    cvmx_nand_cmd_cle_t             cle;
    cvmx_nand_cmd_ale_t             ale;
    cvmx_nand_cmd_rd_t              rd;
    cvmx_nand_cmd_rd_edo_t          rd_edo;
    cvmx_nand_cmd_wr_t              wr;
    cvmx_nand_cmd_wait_status_t     wait_status;
    cvmx_nand_cmd_wait_status_ale_t wait_status_ale;
    cvmx_nand_cmd_bus_acq_t         bus_acq;
    cvmx_nand_cmd_bus_rel_t         bus_rel;
    struct
    {
        uint64_t reserved_64_127: 64;
        uint64_t reserved_4_63  : 60;
        uint64_t op_code        : 4;
    } s;
} cvmx_nand_cmd_t;


typedef struct __attribute__ ((packed))
{
    char onfi[4];                   /**< Bytes 0-3: The ASCII characters 'O', 'N', 'F', 'I' */
    uint16_t revision_number;       /**< Bytes 4-5: ONFI revision number
                                        - 2-15 Reserved (0)
                                        - 1    1 = supports ONFI version 1.0
                                        - 0    Reserved (0) */
    uint16_t features;              /**< Bytes 6-7: Features supported
                                        - 5-15    Reserved (0)
                                        - 4       1 = supports odd to even page Copyback
                                        - 3       1 = supports interleaved operations
                                        - 2       1 = supports non-sequential page programming
                                        - 1       1 = supports multiple LUN operations
                                        - 0       1 = supports 16-bit data bus width */
    uint16_t optional_commands;     /**< Bytes 8-9: Optional commands supported
                                        - 6-15   Reserved (0)
                                        - 5      1 = supports Read Unique ID
                                        - 4      1 = supports Copyback
                                        - 3      1 = supports Read Status Enhanced
                                        - 2      1 = supports Get Features and Set Features
                                        - 1      1 = supports Read Cache commands
                                        - 0      1 = supports Page Cache Program command */
    uint8_t reserved_10_31[22];     /**< Bytes 10-31: Reserved */

    char manufacturer[12];          /**< Bytes 32-43: Device manufacturer (12 ASCII characters) */
    char model[20];                 /**< Bytes 40-63: Device model (20 ASCII characters) */
    uint8_t jedec_id;               /**< Byte 64: JEDEC manufacturer ID */
    uint16_t date_code;             /**< Byte 65-66: Date code */
    uint8_t reserved_67_79[13];     /**< Bytes 67-79: Reserved */

    uint32_t page_data_bytes;       /**< Bytes 80-83: Number of data bytes per page */
    uint16_t page_spare_bytes;      /**< Bytes 84-85: Number of spare bytes per page */
    uint32_t partial_page_data_bytes; /**< Bytes 86-89: Number of data bytes per partial page */
    uint16_t partial_page_spare_bytes; /**< Bytes 90-91: Number of spare bytes per partial page */
    uint32_t pages_per_block;       /**< Bytes 92-95: Number of pages per block */
    uint32_t blocks_per_lun;        /**< Bytes 96-99: Number of blocks per logical unit (LUN) */
    uint8_t number_lun;             /**< Byte 100: Number of logical units (LUNs) */
    uint8_t address_cycles;         /**< Byte 101: Number of address cycles
                                        - 4-7     Column address cycles
                                        - 0-3     Row address cycles */
    uint8_t bits_per_cell;          /**< Byte 102: Number of bits per cell */
    uint16_t bad_block_per_lun;     /**< Bytes 103-104: Bad blocks maximum per LUN */
    uint16_t block_endurance;       /**< Bytes 105-106: Block endurance */
    uint8_t good_blocks;            /**< Byte 107: Guaranteed valid blocks at beginning of target */
    uint16_t good_block_endurance;  /**< Bytes 108-109: Block endurance for guaranteed valid blocks */
    uint8_t programs_per_page;      /**< Byte 110: Number of programs per page */
    uint8_t partial_program_attrib; /**< Byte 111: Partial programming attributes
                                        - 5-7    Reserved
                                        - 4      1 = partial page layout is partial page data followed by partial page spare
                                        - 1-3    Reserved
                                        - 0      1 = partial page programming has constraints */
    uint8_t bits_ecc;               /**< Byte 112: Number of bits ECC correctability */
    uint8_t interleaved_address_bits;   /**< Byte 113: Number of interleaved address bits
                                            - 4-7    Reserved (0)
                                            - 0-3    Number of interleaved address bits */
    uint8_t interleaved_attrib;     /**< Byte 114: Interleaved operation attributes
                                        - 4-7    Reserved (0)
                                        - 3      Address restrictions for program cache
                                        - 2      1 = program cache supported
                                        - 1      1 = no block address restrictions
                                        - 0      Overlapped / concurrent interleaving support */
    uint8_t reserved_115_127[13];   /**< Bytes 115-127: Reserved (0) */

    uint8_t pin_capacitance;        /**< Byte 128: I/O pin capacitance */
    uint16_t timing_mode;           /**< Byte 129-130: Timing mode support
                                        - 6-15   Reserved (0)
                                        - 5      1 = supports timing mode 5
                                        - 4      1 = supports timing mode 4
                                        - 3      1 = supports timing mode 3
                                        - 2      1 = supports timing mode 2
                                        - 1      1 = supports timing mode 1
                                        - 0      1 = supports timing mode 0, shall be 1 */
    uint16_t cache_timing_mode;     /**< Byte 131-132: Program cache timing mode support
                                        - 6-15   Reserved (0)
                                        - 5      1 = supports timing mode 5
                                        - 4      1 = supports timing mode 4
                                        - 3      1 = supports timing mode 3
                                        - 2      1 = supports timing mode 2
                                        - 1      1 = supports timing mode 1
                                        - 0      1 = supports timing mode 0 */
    uint16_t t_prog;                /**< Byte 133-134: Maximum page program time (us) */
    uint16_t t_bers;                /**< Byte 135-136: Maximum block erase time (us) */
    uint16_t t_r;                   /**< Byte 137-148: Maximum page read time (us) */
    uint16_t t_ccs;                 /**< Byte 139-140: Minimum change column setup time (ns) */
    uint8_t reserved_141_163[23];   /**< Byte 141-163: Reserved (0) */

    uint16_t vendor_revision;       /**< Byte 164-165: Vendor specific Revision number */
    uint8_t vendor_specific[88];    /**< Byte 166-253: Vendor specific */
    uint16_t crc;                   /**< Byte 254-255: Integrity CRC */
} cvmx_nand_onfi_param_page_t;


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
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_initialize(cvmx_nand_initialize_flags_t flags, int active_chips);



/**
 * This function may be called before cvmx_nand_initialize to set default values that will be used
 * for NAND chips that do not identify themselves in a way that allows autoconfiguration. (ONFI chip with
 * missing parameter page, for example.)
 * The parameters set by this function will be used by _all_ non-autoconfigured NAND chips.
 *
 *
 *   NOTE:  This function signature is _NOT_ stable, and will change in the future as required to support
 *          various NAND chips.
 *
 * @param page_size page size in bytes
 * @param oob_size  Out of band size in bytes (per page)
 * @param pages_per_block
 *                  number of pages per block
 * @param blocks    Total number of blocks in device
 * @param onfi_timing_mode
 *                  ONFI timing mode
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_set_defaults(int page_size, int oob_size, int pages_per_block, int blocks, int onfi_timing_mode);


/**
 * Call to shutdown the NAND controller after all transactions
 * are done. In most setups this will never be called.
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_shutdown(void);


/**
 * Returns a bitmask representing the chip selects that are
 * connected to NAND chips. This can be called after the
 * initialize to determine the actual number of NAND chips
 * found. Each bit in the response coresponds to a chip select.
 *
 * @return Zero if no NAND chips were found. Otherwise a bit is set for
 *         each chip select (1<<chip).
 */
extern int cvmx_nand_get_active_chips(void);


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
extern cvmx_nand_status_t cvmx_nand_set_timing(int chip, int tim_mult, int tim_par[7], int clen[4], int alen[4], int rdn[4], int wrn[2]);


/**
 * Submit a command to the NAND command queue. Generally this
 * will not be used directly. Instead most programs will use the other
 * higher level NAND functions.
 *
 * @param cmd    Command to submit
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_submit(cvmx_nand_cmd_t cmd);

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
extern int cvmx_nand_page_read(int chip, uint64_t nand_address, uint64_t buffer_address, int buffer_length);

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
extern cvmx_nand_status_t cvmx_nand_page_write(int chip, uint64_t nand_address, uint64_t buffer_address);

/**
 * Erase a NAND block. A single block contains multiple pages.
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               Location in NAND to erase. See description in file comment
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_block_erase(int chip, uint64_t nand_address);

/**
 * Read the NAND ID information
 *
 * @param chip   Chip select for NAND flash
 * @param nand_address
 *               NAND address to read ID from. Usually this is either 0x0 or 0x20.
 * @param buffer_address
 *               Physical address to store data in
 * @param buffer_length
 *               Length of the buffer. Usually this is 4 bytes
 *
 * @return Bytes read on success, a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_read_id(int chip, uint64_t nand_address, uint64_t buffer_address, int buffer_length);

/**
 * Read the NAND parameter page
 *
 * @param chip   Chip select for NAND flash
 * @param buffer_address
 *               Physical address to store data in
 * @param buffer_length
 *               Length of the buffer. Usually this is 4 bytes
 *
 * @return Bytes read on success, a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_read_param_page(int chip, uint64_t buffer_address, int buffer_length);

/**
 * Get the status of the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return NAND status or a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_get_status(int chip);

/**
 * Get the page size, excluding out of band data. This  function
 * will return zero for chip selects not connected to NAND.
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Page size in bytes or a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_get_page_size(int chip);

/**
 * Get the OOB size.
 *
 * @param chip   Chip select for NAND flash
 *
 * @return OOB in bytes or a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_get_oob_size(int chip);

/**
 * Get the number of pages per NAND block
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Numboer of pages in each block or a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_get_pages_per_block(int chip);

/**
 * Get the number of blocks in the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Number of blocks or a negative cvmx_nand_status_t error code on failure
 */
extern int cvmx_nand_get_blocks(int chip);

/**
 * Reset the NAND flash
 *
 * @param chip   Chip select for NAND flash
 *
 * @return Zero on success, a negative cvmx_nand_status_t error code on failure
 */
extern cvmx_nand_status_t cvmx_nand_reset(int chip);

/**
 * This function computes the Octeon specific ECC data used by the NAND boot
 * feature.
 *
 * @param block  pointer to 256 bytes of data
 * @param eccp   pointer to where 8 bytes of ECC data will be stored
 */
extern void cvmx_nand_compute_boot_ecc(unsigned char *block, unsigned char *eccp);


extern int cvmx_nand_correct_boot_ecc(uint8_t *block);
#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_NAND_H__ */
