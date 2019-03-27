/***********************license start***************
 * Copyright (c) 2003-2011  Cavium, Inc. (support@cavium.com). All rights
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
 * Interface to the Level 2 Cache (L2C) control, measurement, and debugging
 * facilities.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_L2C_H__
#define __CVMX_L2C_H__

#define CVMX_L2C_IDX_ADDR_SHIFT 7  /* based on 128 byte cache line size */
#define CVMX_L2C_IDX_MASK       (cvmx_l2c_get_num_sets() - 1)

/* Defines for index aliasing computations */
#define CVMX_L2C_TAG_ADDR_ALIAS_SHIFT (CVMX_L2C_IDX_ADDR_SHIFT + cvmx_l2c_get_set_bits())
#define CVMX_L2C_ALIAS_MASK (CVMX_L2C_IDX_MASK << CVMX_L2C_TAG_ADDR_ALIAS_SHIFT)
#define CVMX_L2C_MEMBANK_SELECT_SIZE  4096

/* Defines for Virtualizations, valid only from Octeon II onwards. */
#define CVMX_L2C_VRT_MAX_VIRTID_ALLOWED ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 64 : 0)
#define CVMX_L2C_MAX_MEMSZ_ALLOWED ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 32 : 0)

  /*------------*/
  /*  TYPEDEFS  */
  /*------------*/

union cvmx_l2c_tag {
	uint64_t u64;
#ifdef __BIG_ENDIAN_BITFIELD
	struct {
		uint64_t reserved:28;
		uint64_t V:1;		/* Line valid */
		uint64_t D:1;		/* Line dirty */
		uint64_t L:1;		/* Line locked */
		uint64_t U:1;		/* Use, LRU eviction */
		uint64_t addr:32;	/* Phys mem (not all bits valid) */
	} s;
#else
	struct {
		uint64_t addr:32;	/* Phys mem (not all bits valid) */
		uint64_t U:1;		/* Use, LRU eviction */
		uint64_t L:1;		/* Line locked */
		uint64_t D:1;		/* Line dirty */
		uint64_t V:1;		/* Line valid */
		uint64_t reserved:28;
	} s;

#endif
};
typedef union cvmx_l2c_tag cvmx_l2c_tag_t;

/* Maximium number of TADs */
#define CVMX_L2C_MAX_TADS     4
/* Maximium number of L2C performance counters */
#define CVMX_L2C_MAX_PCNT     4

/* Number of L2C Tag-and-data sections (TADs) that are connected to LMC. */
#define CVMX_L2C_TADS  ((OCTEON_IS_MODEL(OCTEON_CN68XX)) ? 4 : 1)
/* Number of L2C IOBs connected to LMC. */ 
#define CVMX_L2C_IOBS  ((OCTEON_IS_MODEL(OCTEON_CN68XX)) ? 2 : 1)

  /* L2C Performance Counter events. */
enum cvmx_l2c_event {
	CVMX_L2C_EVENT_CYCLES           =  0,    /**< Cycles */
	CVMX_L2C_EVENT_INSTRUCTION_MISS =  1,    /**< L2 Instruction Miss */
	CVMX_L2C_EVENT_INSTRUCTION_HIT  =  2,    /**< L2 Instruction Hit */
	CVMX_L2C_EVENT_DATA_MISS        =  3,    /**< L2 Data Miss */
	CVMX_L2C_EVENT_DATA_HIT         =  4,    /**< L2 Data Hit */
	CVMX_L2C_EVENT_MISS             =  5,    /**< L2 Miss (I/D) */
	CVMX_L2C_EVENT_HIT              =  6,    /**< L2 Hit (I/D) */
	CVMX_L2C_EVENT_VICTIM_HIT       =  7,    /**< L2 Victim Buffer Hit (Retry Probe) */
	CVMX_L2C_EVENT_INDEX_CONFLICT   =  8,    /**< LFB-NQ Index Conflict */
	CVMX_L2C_EVENT_TAG_PROBE        =  9,    /**< L2 Tag Probe (issued - could be VB-Retried) */
	CVMX_L2C_EVENT_TAG_UPDATE       = 10,    /**< L2 Tag Update (completed). Note: Some CMD types do not update */
	CVMX_L2C_EVENT_TAG_COMPLETE     = 11,    /**< L2 Tag Probe Completed (beyond VB-RTY window) */
	CVMX_L2C_EVENT_TAG_DIRTY        = 12,    /**< L2 Tag Dirty Victim */
	CVMX_L2C_EVENT_DATA_STORE_NOP   = 13,    /**< L2 Data Store NOP */
	CVMX_L2C_EVENT_DATA_STORE_READ  = 14,    /**< L2 Data Store READ */
	CVMX_L2C_EVENT_DATA_STORE_WRITE = 15,    /**< L2 Data Store WRITE */
	CVMX_L2C_EVENT_FILL_DATA_VALID  = 16,    /**< Memory Fill Data valid */
	CVMX_L2C_EVENT_WRITE_REQUEST    = 17,    /**< Memory Write Request */
	CVMX_L2C_EVENT_READ_REQUEST     = 18,    /**< Memory Read Request */
	CVMX_L2C_EVENT_WRITE_DATA_VALID = 19,    /**< Memory Write Data valid */
	CVMX_L2C_EVENT_XMC_NOP          = 20,    /**< XMC NOP */
	CVMX_L2C_EVENT_XMC_LDT          = 21,    /**< XMC LDT */
	CVMX_L2C_EVENT_XMC_LDI          = 22,    /**< XMC LDI */
	CVMX_L2C_EVENT_XMC_LDD          = 23,    /**< XMC LDD */
	CVMX_L2C_EVENT_XMC_STF          = 24,    /**< XMC STF */
	CVMX_L2C_EVENT_XMC_STT          = 25,    /**< XMC STT */
	CVMX_L2C_EVENT_XMC_STP          = 26,    /**< XMC STP */
	CVMX_L2C_EVENT_XMC_STC          = 27,    /**< XMC STC */
	CVMX_L2C_EVENT_XMC_DWB          = 28,    /**< XMC DWB */
	CVMX_L2C_EVENT_XMC_PL2          = 29,    /**< XMC PL2 */
	CVMX_L2C_EVENT_XMC_PSL1         = 30,    /**< XMC PSL1 */
	CVMX_L2C_EVENT_XMC_IOBLD        = 31,    /**< XMC IOBLD */
	CVMX_L2C_EVENT_XMC_IOBST        = 32,    /**< XMC IOBST */
	CVMX_L2C_EVENT_XMC_IOBDMA       = 33,    /**< XMC IOBDMA */
	CVMX_L2C_EVENT_XMC_IOBRSP       = 34,    /**< XMC IOBRSP */
	CVMX_L2C_EVENT_XMC_BUS_VALID    = 35,    /**< XMC Bus valid (all) */
	CVMX_L2C_EVENT_XMC_MEM_DATA     = 36,    /**< XMC Bus valid (DST=L2C) Memory */
	CVMX_L2C_EVENT_XMC_REFL_DATA    = 37,    /**< XMC Bus valid (DST=IOB) REFL Data */
	CVMX_L2C_EVENT_XMC_IOBRSP_DATA  = 38,    /**< XMC Bus valid (DST=PP) IOBRSP Data */
	CVMX_L2C_EVENT_RSC_NOP          = 39,    /**< RSC NOP */
	CVMX_L2C_EVENT_RSC_STDN         = 40,    /**< RSC STDN */
	CVMX_L2C_EVENT_RSC_FILL         = 41,    /**< RSC FILL */
	CVMX_L2C_EVENT_RSC_REFL         = 42,    /**< RSC REFL */
	CVMX_L2C_EVENT_RSC_STIN         = 43,    /**< RSC STIN */
	CVMX_L2C_EVENT_RSC_SCIN         = 44,    /**< RSC SCIN */
	CVMX_L2C_EVENT_RSC_SCFL         = 45,    /**< RSC SCFL */
	CVMX_L2C_EVENT_RSC_SCDN         = 46,    /**< RSC SCDN */
	CVMX_L2C_EVENT_RSC_DATA_VALID   = 47,    /**< RSC Data Valid */
	CVMX_L2C_EVENT_RSC_VALID_FILL   = 48,    /**< RSC Data Valid (FILL) */
	CVMX_L2C_EVENT_RSC_VALID_STRSP  = 49,    /**< RSC Data Valid (STRSP) */
	CVMX_L2C_EVENT_RSC_VALID_REFL   = 50,    /**< RSC Data Valid (REFL) */
	CVMX_L2C_EVENT_LRF_REQ          = 51,    /**< LRF-REQ (LFB-NQ) */
	CVMX_L2C_EVENT_DT_RD_ALLOC      = 52,    /**< DT RD-ALLOC */
	CVMX_L2C_EVENT_DT_WR_INVAL      = 53,    /**< DT WR-INVAL */
	CVMX_L2C_EVENT_MAX
};
typedef enum cvmx_l2c_event cvmx_l2c_event_t;

/* L2C Performance Counter events for Octeon2. */
enum cvmx_l2c_tad_event {
	CVMX_L2C_TAD_EVENT_NONE          = 0,     /* None */
	CVMX_L2C_TAD_EVENT_TAG_HIT       = 1,     /* L2 Tag Hit */
	CVMX_L2C_TAD_EVENT_TAG_MISS      = 2,     /* L2 Tag Miss */
	CVMX_L2C_TAD_EVENT_TAG_NOALLOC   = 3,     /* L2 Tag NoAlloc (forced no-allocate) */
	CVMX_L2C_TAD_EVENT_TAG_VICTIM    = 4,     /* L2 Tag Victim */
	CVMX_L2C_TAD_EVENT_SC_FAIL       = 5,     /* SC Fail */
	CVMX_L2C_TAD_EVENT_SC_PASS       = 6,     /* SC Pass */
	CVMX_L2C_TAD_EVENT_LFB_VALID     = 7,     /* LFB Occupancy (each cycle adds \# of LFBs valid) */
	CVMX_L2C_TAD_EVENT_LFB_WAIT_LFB  = 8,     /* LFB Wait LFB (each cycle adds \# LFBs waiting for other LFBs) */
	CVMX_L2C_TAD_EVENT_LFB_WAIT_VAB  = 9,     /* LFB Wait VAB (each cycle adds \# LFBs waiting for VAB) */
	CVMX_L2C_TAD_EVENT_QUAD0_INDEX   = 128,   /* Quad 0 index bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD0_READ    = 129,   /* Quad 0 read data bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD0_BANK    = 130,   /* Quad 0 \# banks inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD0_WDAT    = 131,   /* Quad 0 wdat flops inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD1_INDEX   = 144,   /* Quad 1 index bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD1_READ    = 145,   /* Quad 1 read data bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD1_BANK    = 146,   /* Quad 1 \# banks inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD1_WDAT    = 147,   /* Quad 1 wdat flops inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD2_INDEX   = 160,   /* Quad 2 index bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD2_READ    = 161,   /* Quad 2 read data bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD2_BANK    = 162,   /* Quad 2 \# banks inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD2_WDAT    = 163,   /* Quad 2 wdat flops inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD3_INDEX   = 176,   /* Quad 3 index bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD3_READ    = 177,   /* Quad 3 read data bus inuse */
	CVMX_L2C_TAD_EVENT_QUAD3_BANK    = 178,   /* Quad 3 \# banks inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_QUAD3_WDAT    = 179,   /* Quad 3 wdat flops inuse (0-4/cycle) */
	CVMX_L2C_TAD_EVENT_MAX
};
typedef enum cvmx_l2c_tad_event cvmx_l2c_tad_event_t;

/**
 * Configure one of the four L2 Cache performance counters to capture event
 * occurences.
 *
 * @param counter        The counter to configure. Range 0..3.
 * @param event          The type of L2 Cache event occurrence to count.
 * @param clear_on_read  When asserted, any read of the performance counter
 *                       clears the counter.
 *
 * @note The routine does not clear the counter.
 */
void cvmx_l2c_config_perf(uint32_t counter, cvmx_l2c_event_t event, uint32_t clear_on_read);

/**
 * Read the given L2 Cache performance counter. The counter must be configured
 * before reading, but this routine does not enforce this requirement.
 *
 * @param counter  The counter to configure. Range 0..3.
 *
 * @return The current counter value.
 */
uint64_t cvmx_l2c_read_perf(uint32_t counter);

/**
 * Return the L2 Cache way partitioning for a given core.
 *
 * @param core  The core processor of interest.
 *
 * @return    The mask specifying the partitioning. 0 bits in mask indicates
 *              the cache 'ways' that a core can evict from.
 *            -1 on error
 */
int cvmx_l2c_get_core_way_partition(uint32_t core);

/**
 * Partitions the L2 cache for a core
 *
 * @param core The core that the partitioning applies to.
 * @param mask The partitioning of the ways expressed as a binary
 *             mask. A 0 bit allows the core to evict cache lines from
 *             a way, while a 1 bit blocks the core from evicting any
 *             lines from that way. There must be at least one allowed
 *             way (0 bit) in the mask.
 *

 * @note If any ways are blocked for all cores and the HW blocks, then
 *       those ways will never have any cache lines evicted from them.
 *       All cores and the hardware blocks are free to read from all
 *       ways regardless of the partitioning.
 */
int cvmx_l2c_set_core_way_partition(uint32_t core, uint32_t mask);

/**
 * Return the L2 Cache way partitioning for the hw blocks.
 *
 * @return    The mask specifying the reserved way. 0 bits in mask indicates
 *              the cache 'ways' that a core can evict from.
 *            -1 on error
 */
int cvmx_l2c_get_hw_way_partition(void);

/**
 * Partitions the L2 cache for the hardware blocks.
 *
 * @param mask The partitioning of the ways expressed as a binary
 *             mask. A 0 bit allows the core to evict cache lines from
 *             a way, while a 1 bit blocks the core from evicting any
 *             lines from that way. There must be at least one allowed
 *             way (0 bit) in the mask.
 *

 * @note If any ways are blocked for all cores and the HW blocks, then
 *       those ways will never have any cache lines evicted from them.
 *       All cores and the hardware blocks are free to read from all
 *       ways regardless of the partitioning.
 */
int cvmx_l2c_set_hw_way_partition(uint32_t mask);


/**
 * Return the L2 Cache way partitioning for the second set of hw blocks.
 *
 * @return    The mask specifying the reserved way. 0 bits in mask indicates
 *              the cache 'ways' that a core can evict from.
 *            -1 on error
 */
int cvmx_l2c_get_hw_way_partition2(void);

/**
 * Partitions the L2 cache for the second set of  blocks.
 *
 * @param mask The partitioning of the ways expressed as a binary
 *             mask. A 0 bit allows the core to evict cache lines from
 *             a way, while a 1 bit blocks the core from evicting any
 *             lines from that way. There must be at least one allowed
 *             way (0 bit) in the mask.
 *

 * @note If any ways are blocked for all cores and the HW blocks, then
 *       those ways will never have any cache lines evicted from them.
 *       All cores and the hardware blocks are free to read from all
 *       ways regardless of the partitioning.
 */
int cvmx_l2c_set_hw_way_partition2(uint32_t mask);

/**
 * Locks a line in the L2 cache at the specified physical address
 *
 * @param addr   physical address of line to lock
 *
 * @return 0 on success,
 *         1 if line not locked.
 */
int cvmx_l2c_lock_line(uint64_t addr);

/**
 * Locks a specified memory region in the L2 cache.
 *
 * Note that if not all lines can be locked, that means that all
 * but one of the ways (associations) available to the locking
 * core are locked.  Having only 1 association available for
 * normal caching may have a significant adverse affect on performance.
 * Care should be taken to ensure that enough of the L2 cache is left
 * unlocked to allow for normal caching of DRAM.
 *
 * @param start  Physical address of the start of the region to lock
 * @param len    Length (in bytes) of region to lock
 *
 * @return Number of requested lines that where not locked.
 *         0 on success (all locked)
 */
int cvmx_l2c_lock_mem_region(uint64_t start, uint64_t len);


/**
 * Unlock and flush a cache line from the L2 cache.
 * IMPORTANT: Must only be run by one core at a time due to use
 * of L2C debug features.
 * Note that this function will flush a matching but unlocked cache line.
 * (If address is not in L2, no lines are flushed.)
 *
 * @param address Physical address to unlock
 *
 * @return 0: line not unlocked
 *         1: line unlocked
 */
int cvmx_l2c_unlock_line(uint64_t address);

/**
 * Unlocks a region of memory that is locked in the L2 cache
 *
 * @param start  start physical address
 * @param len    length (in bytes) to unlock
 *
 * @return Number of locked lines that the call unlocked
 */
int cvmx_l2c_unlock_mem_region(uint64_t start, uint64_t len);


/**
 * Read the L2 controller tag for a given location in L2
 *
 * @param association
 *               Which association to read line from
 * @param index  Which way to read from.
 *
 * @return l2c tag structure for line requested.
 * 
 * NOTE: This function is deprecated and cannot be used on devices with 
 *       multiple L2C interfaces such as the OCTEON CN68XX.
 *       Please use cvmx_l2c_get_tag_v2 instead.
 */
cvmx_l2c_tag_t cvmx_l2c_get_tag(uint32_t association, uint32_t index)
        __attribute__ ((deprecated));

/**
 * Read the L2 controller tag for a given location in L2
 *
 * @param association
 *               Which association to read line from
 * @param index  Which way to read from.
 * 
 * @param tad    Which TAD to read from, set to 0 except on OCTEON CN68XX.
 *
 * @return l2c tag structure for line requested.
 */
cvmx_l2c_tag_t cvmx_l2c_get_tag_v2(uint32_t association, uint32_t index, uint32_t tad);

/**
 * Find the TAD for the specified address
 *
 * @param addr   physical address to get TAD for
 * 
 * @return TAD number for address.
 */
int cvmx_l2c_address_to_tad(uint64_t addr);

/**
 * Returns the cache index for a given physical address
 *
 * @param addr   physical address
 *
 * @return L2 cache index
 */
uint32_t cvmx_l2c_address_to_index (uint64_t addr);

/**
 * Returns the L2 tag that will be used for the given physical address
 *
 * @param addr   physical address
 * @return L2 cache tag. Addreses in the LMC hole are not valid.
 * Returns 0xFFFFFFFF if the address specified is in the LMC hole.
 */
uint32_t cvmx_l2c_v2_address_to_tag(uint64_t addr);

/**
 * Flushes (and unlocks) the entire L2 cache.
 * IMPORTANT: Must only be run by one core at a time due to use
 * of L2C debug features.
 */
void cvmx_l2c_flush(void);

/**
 *
 * @return Returns the size of the L2 cache in bytes,
 * -1 on error (unrecognized model)
 */
int cvmx_l2c_get_cache_size_bytes(void);

/**
 * Return the number of sets in the L2 Cache
 *
 * @return
 */
int cvmx_l2c_get_num_sets(void);

/**
 * Return log base 2 of the number of sets in the L2 cache
 * @return
 */
int cvmx_l2c_get_set_bits(void);
/**
 * Return the number of associations in the L2 Cache
 *
 * @return
 */
int cvmx_l2c_get_num_assoc(void);

/**
 * Flush a line from the L2 cache
 * This should only be called from one core at a time, as this routine
 * sets the core to the 'debug' core in order to flush the line.
 *
 * @param assoc  Association (or way) to flush
 * @param index  Index to flush
 */
void cvmx_l2c_flush_line(uint32_t assoc, uint32_t index);

/**
 * Initialize the BIG address in L2C+DRAM to generate proper error
 * on reading/writing to an non-existant memory location. 
 *
 * @param mem_size  Amount of DRAM configured in MB.
 * @param mode      Allow/Disallow reporting errors L2C_INT_SUM[BIGRD,BIGWR].
 */
void cvmx_l2c_set_big_size(uint64_t mem_size, int mode);

#if !defined(CVMX_BUILD_FOR_LINUX_HOST) && !defined(CVMX_BUILD_FOR_LINUX_KERNEL)

/*
 * Set maxium number of Virtual IDS allowed in a machine.
 *
 * @param nvid  Number of virtial ids allowed in a machine.
 * @return      Return 0 on success or -1 on failure.
 */
int cvmx_l2c_vrt_set_max_virtids(int nvid);

/**
 * Get maxium number of virtual IDs allowed in a machine.
 *
 * @return  Return number of virtual machine IDs. Return -1 on failure.
 */
int cvmx_l2c_vrt_get_max_virtids(void);

/**
 * Set the maxium size of memory space to be allocated for virtualization.
 *
 * @param memsz     Size of the virtual memory in GB
 * @return          Return 0 on success or -1 on failure.
 */
int cvmx_l2c_vrt_set_max_memsz(int memsz);

/**
 * Set a Virtual ID to a set of cores.
 *
 * @param virtid    Assign virtid to a set of cores.
 * @param coremask  The group of cores to assign a unique virtual id.
 * @return          Return 0 on success, otherwise -1.
 */
int cvmx_l2c_vrt_assign_virtid(int virtid, uint32_t coremask);

/**
 * Remove a virt id assigned to a set of cores. Update the virtid mask and
 * virtid stored for each core.
 *
 * @param coremask  the group of cores whose virtual id is removed.
 */
void cvmx_l2c_vrt_remove_virtid(int virtid);

/**
 * Block a memory region to be updated by a set of virtids.
 *
 * @param start_addr   Starting address of memory region
 * @param size         Size of the memory to protect
 * @param virtid_mask  Virtual ID to use
 * @param mode         Allow/Disallow write access
 *                        = 0,  Allow write access by virtid
 *                        = 1,  Disallow write access by virtid
 */
int cvmx_l2c_vrt_memprotect(uint64_t start_addr, int size, int virtid, int mode);

/**
 * Enable virtualization.
 */
void cvmx_l2c_vrt_enable(int mode);

/**
 * Disable virtualization.
 */
void cvmx_l2c_vrt_disable(void);

#endif /* CVMX_BUILD_FOR_LINUX_HOST */

#endif /* __CVMX_L2C_H__ */
