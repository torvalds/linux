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
 * Simple allocate only memory allocator.  Used to allocate memory at application
 * start time.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_BOOTMEM_H__
#define __CVMX_BOOTMEM_H__

#ifdef	__cplusplus
extern "C" {
#endif

#define CVMX_BOOTMEM_NAME_LEN 128   /* Must be multiple of 8, changing breaks ABI */
#define CVMX_BOOTMEM_NUM_NAMED_BLOCKS 64  /* Can change without breaking ABI */
#define CVMX_BOOTMEM_ALIGNMENT_SIZE     (16ull)  /* minimum alignment of bootmem alloced blocks */

/* Flags for cvmx_bootmem_phy_mem* functions */
#define CVMX_BOOTMEM_FLAG_END_ALLOC    (1 << 0)     /* Allocate from end of block instead of beginning */
#define CVMX_BOOTMEM_FLAG_NO_LOCKING   (1 << 1)     /* Don't do any locking. */

/* Real physical addresses of memory regions */
#define OCTEON_DDR0_BASE    (0x0ULL)
#define OCTEON_DDR0_SIZE    (0x010000000ULL)
#define OCTEON_DDR1_BASE    ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 0x20000000ULL : 0x410000000ULL)
#define OCTEON_DDR1_SIZE    (0x010000000ULL)
#define OCTEON_DDR2_BASE    ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 0x30000000ULL : 0x20000000ULL)
#define OCTEON_DDR2_SIZE    ((OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 0x7d0000000ULL : 0x3e0000000ULL)
#define OCTEON_MAX_PHY_MEM_SIZE ((OCTEON_IS_MODEL(OCTEON_CN68XX)) ? 128*1024*1024*1024ULL : (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX)) ? 32*1024*1024*1024ull : 16*1024*1024*1024ULL)

/* First bytes of each free physical block of memory contain this structure,
 * which is used to maintain the free memory list.  Since the bootloader is
 * only 32 bits, there is a union providing 64 and 32 bit versions.  The
 * application init code converts addresses to 64 bit addresses before the
 * application starts.
 */
typedef struct
{
    /* Note: these are referenced from assembly routines in the bootloader, so this structure
    ** should not be changed without changing those routines as well. */
    uint64_t next_block_addr;
    uint64_t size;

} cvmx_bootmem_block_header_t;


/* Structure for named memory blocks
** Number of descriptors
** available can be changed without affecting compatiblity,
** but name length changes require a bump in the bootmem
** descriptor version
** Note: This structure must be naturally 64 bit aligned, as a single
** memory image will be used by both 32 and 64 bit programs.
*/
struct cvmx_bootmem_named_block_desc
{
    uint64_t base_addr;     /**< Base address of named block */
    uint64_t size;          /**< Size actually allocated for named block (may differ from requested) */
    char name[CVMX_BOOTMEM_NAME_LEN];   /**< name of named block */
};

typedef struct cvmx_bootmem_named_block_desc cvmx_bootmem_named_block_desc_t;

/* Current descriptor versions */
#define CVMX_BOOTMEM_DESC_MAJ_VER   3   /* CVMX bootmem descriptor major version */
#define CVMX_BOOTMEM_DESC_MIN_VER   0   /* CVMX bootmem descriptor minor version */

/* First three members of cvmx_bootmem_desc_t are left in original
** positions for backwards compatibility.
*/
typedef struct
{
    uint32_t    lock;       /**< spinlock to control access to list */
    uint32_t    flags;      /**< flags for indicating various conditions */
    uint64_t    head_addr;

    uint32_t    major_version;  /**< incremented changed when incompatible changes made */
    uint32_t    minor_version;  /**< incremented changed when compatible changes made, reset to zero when major incremented */
    uint64_t    app_data_addr;
    uint64_t    app_data_size;

    uint32_t    named_block_num_blocks;  /**< number of elements in named blocks array */
    uint32_t    named_block_name_len;    /**< length of name array in bootmem blocks */
    uint64_t    named_block_array_addr;  /**< address of named memory block descriptors */

} cvmx_bootmem_desc_t;


/**
 * Initialize the boot alloc memory structures. This is
 * normally called inside of cvmx_user_app_init()
 *
 * @param mem_desc_addr	Address of the free memory list
 * @return
 */
extern int cvmx_bootmem_init(uint64_t mem_desc_addr);


/**
 * Allocate a block of memory from the free list that was passed
 * to the application by the bootloader.
 * This is an allocate-only algorithm, so freeing memory is not possible.
 *
 * @param size      Size in bytes of block to allocate
 * @param alignment Alignment required - must be power of 2
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc(uint64_t size, uint64_t alignment);

/**
 * Allocate a block of memory from the free list that was
 * passed to the application by the bootloader at a specific
 * address. This is an allocate-only algorithm, so
 * freeing memory is not possible. Allocation will fail if
 * memory cannot be allocated at the specified address.
 *
 * @param size      Size in bytes of block to allocate
 * @param address   Physical address to allocate memory at.  If this memory is not
 *                  available, the allocation fails.
 * @param alignment Alignment required - must be power of 2
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_address(uint64_t size, uint64_t address, uint64_t alignment);

/**
 * Allocate a block of memory from the free list that was
 * passed to the application by the bootloader within a specified
 * address range. This is an allocate-only algorithm, so
 * freeing memory is not possible. Allocation will fail if
 * memory cannot be allocated in the requested range.
 *
 * @param size      Size in bytes of block to allocate
 * @param min_addr  defines the minimum address of the range
 * @param max_addr  defines the maximum address of the range
 * @param alignment Alignment required - must be power of 2
 * @param flags     Flags to control options for the allocation.
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_range_flags(uint64_t size, uint64_t alignment, uint64_t min_addr, uint64_t max_addr, uint32_t flags);


/**
 * Allocate a block of memory from the free list that was
 * passed to the application by the bootloader within a specified
 * address range. This is an allocate-only algorithm, so
 * freeing memory is not possible. Allocation will fail if
 * memory cannot be allocated in the requested range.
 *
 * @param size      Size in bytes of block to allocate
 * @param min_addr  defines the minimum address of the range
 * @param max_addr  defines the maximum address of the range
 * @param alignment Alignment required - must be power of 2
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_range(uint64_t size, uint64_t alignment, uint64_t min_addr, uint64_t max_addr);


/**
 * Allocate a block of memory from the free list that was passed
 * to the application by the bootloader, and assign it a name in the
 * global named block table.  (part of the cvmx_bootmem_descriptor_t structure)
 * Named blocks can later be freed.
 *
 * @param size      Size in bytes of block to allocate
 * @param alignment Alignment required - must be power of 2
 * @param name      name of block - must be less than CVMX_BOOTMEM_NAME_LEN bytes
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_named(uint64_t size, uint64_t alignment, const char *name);

/**
 * Allocate a block of memory from the free list that was passed
 * to the application by the bootloader, and assign it a name in the
 * global named block table.  (part of the cvmx_bootmem_descriptor_t structure)
 * Named blocks can later be freed.
 *
 * @param size      Size in bytes of block to allocate
 * @param alignment Alignment required - must be power of 2
 * @param name      name of block - must be less than CVMX_BOOTMEM_NAME_LEN bytes
 * @param flags     Flags to control options for the allocation.
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_named_flags(uint64_t size, uint64_t alignment, const char *name, uint32_t flags);



/**
 * Allocate a block of memory from the free list that was passed
 * to the application by the bootloader, and assign it a name in the
 * global named block table.  (part of the cvmx_bootmem_descriptor_t structure)
 * Named blocks can later be freed.
 *
 * @param size      Size in bytes of block to allocate
 * @param address   Physical address to allocate memory at.  If this memory is not
 *                  available, the allocation fails.
 * @param name      name of block - must be less than CVMX_BOOTMEM_NAME_LEN bytes
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_named_address(uint64_t size, uint64_t address, const char *name);



/**
 * Allocate a block of memory from a specific range of the free list that was passed
 * to the application by the bootloader, and assign it a name in the
 * global named block table.  (part of the cvmx_bootmem_descriptor_t structure)
 * Named blocks can later be freed.
 * If request cannot be satisfied within the address range specified, NULL is returned
 *
 * @param size      Size in bytes of block to allocate
 * @param min_addr  minimum address of range
 * @param max_addr  maximum address of range
 * @param align  Alignment of memory to be allocated. (must be a power of 2)
 * @param name      name of block - must be less than CVMX_BOOTMEM_NAME_LEN bytes
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_named_range(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t align, const char *name);

/**
 * Allocate if needed a block of memory from a specific range of the free list that was passed
 * to the application by the bootloader, and assign it a name in the
 * global named block table.  (part of the cvmx_bootmem_descriptor_t structure)
 * Named blocks can later be freed.
 * If the requested name block is already allocated, return the pointer to block of memory.
 * If request cannot be satisfied within the address range specified, NULL is returned
 *
 * @param size      Size in bytes of block to allocate
 * @param min_addr  minimum address of range
 * @param max_addr  maximum address of range
 * @param align     Alignment of memory to be allocated. (must be a power of 2)
 * @param name      name of block - must be less than CVMX_BOOTMEM_NAME_LEN bytes
 * @param init      Initialization function
 *
 * @return pointer to block of memory, NULL on error
 */
extern void *cvmx_bootmem_alloc_named_range_once(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t align, const char *name, void (*init)(void*));

/**
 * Frees a previously allocated named bootmem block.
 *
 * @param name   name of block to free
 *
 * @return 0 on failure,
 *         !0 on success
 */
extern int cvmx_bootmem_free_named(const char *name);


/**
 * Finds a named bootmem block by name.
 *
 * @param name   name of block to free
 *
 * @return pointer to named block descriptor on success
 *         0 on failure
 */
const cvmx_bootmem_named_block_desc_t *cvmx_bootmem_find_named_block(const char *name);



/**
 * Returns the size of available memory in bytes, only
 * counting blocks that are at least as big as the minimum block
 * size.
 *
 * @param min_block_size
 *               Minimum block size to count in total.
 *
 * @return Number of bytes available for allocation that meet the block size requirement
 */
uint64_t cvmx_bootmem_available_mem(uint64_t min_block_size);



/**
 * Prints out the list of named blocks that have been allocated
 * along with their addresses and sizes.
 * This is primarily used for debugging purposes
 */
void cvmx_bootmem_print_named(void);


/**
 * Allocates a block of physical memory from the free list, at (optional) requested address and alignment.
 *
 * @param req_size  size of region to allocate.  All requests are rounded up to be a multiple CVMX_BOOTMEM_ALIGNMENT_SIZE bytes size
 * @param address_min
 *                  Minimum address that block can occupy.
 * @param address_max
 *                  Specifies the maximum address_min (inclusive) that the allocation can use.
 * @param alignment Requested alignment of the block.  If this alignment cannot be met, the allocation fails.
 *                  This must be a power of 2.
 *                  (Note: Alignment of CVMX_BOOTMEM_ALIGNMENT_SIZE bytes is required, and internally enforced.  Requested alignments of
 *                  less than CVMX_BOOTMEM_ALIGNMENT_SIZE are set to CVMX_BOOTMEM_ALIGNMENT_SIZE.)
 * @param flags     Flags to control options for the allocation.
 *
 * @return physical address of block allocated, or -1 on failure
 */
int64_t cvmx_bootmem_phy_alloc(uint64_t req_size, uint64_t address_min, uint64_t address_max, uint64_t alignment, uint32_t flags);



/**
 * Allocates a named block of physical memory from the free list, at (optional) requested address and alignment.
 *
 * @param size      size of region to allocate.  All requests are rounded up to be a multiple CVMX_BOOTMEM_ALIGNMENT_SIZE bytes size
 * @param min_addr
 *                  Minimum address that block can occupy.
 * @param max_addr
 *                  Specifies the maximum address_min (inclusive) that the allocation can use.
 * @param alignment Requested alignment of the block.  If this alignment cannot be met, the allocation fails.
 *                  This must be a power of 2.
 *                  (Note: Alignment of CVMX_BOOTMEM_ALIGNMENT_SIZE bytes is required, and internally enforced.  Requested alignments of
 *                  less than CVMX_BOOTMEM_ALIGNMENT_SIZE are set to CVMX_BOOTMEM_ALIGNMENT_SIZE.)
 * @param name      name to assign to named block
 * @param flags     Flags to control options for the allocation.
 *
 * @return physical address of block allocated, or -1 on failure
 */
int64_t cvmx_bootmem_phy_named_block_alloc(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t alignment, const char *name, uint32_t flags);


/**
 * Finds a named memory block by name.
 * Also used for finding an unused entry in the named block table.
 *
 * @param name   Name of memory block to find.
 *               If NULL pointer given, then finds unused descriptor, if available.
 * @param flags  Flags to control options for the allocation.
 *
 * @return Physical address of the memory block descriptor, zero if not
 *         found. If zero returned when name parameter is NULL, then no
 *         memory block descriptors are available.
 */
uint64_t cvmx_bootmem_phy_named_block_find(const char *name, uint32_t flags);


/**
 * Returns the size of available memory in bytes, only
 * counting blocks that are at least as big as the minimum block
 * size.
 *
 * @param min_block_size
 *               Minimum block size to count in total.
 *
 * @return Number of bytes available for allocation that meet the block size requirement
 */
uint64_t cvmx_bootmem_phy_available_mem(uint64_t min_block_size);

/**
 * Frees a named block.
 *
 * @param name   name of block to free
 * @param flags  flags for passing options
 *
 * @return 0 on failure
 *         1 on success
 */
int cvmx_bootmem_phy_named_block_free(const char *name, uint32_t flags);

/**
 * Frees a block to the bootmem allocator list.  This must
 * be used with care, as the size provided must match the size
 * of the block that was allocated, or the list will become
 * corrupted.
 *
 * IMPORTANT:  This is only intended to be used as part of named block
 * frees and initial population of the free memory list.
 *                                                      *
 *
 * @param phy_addr physical address of block
 * @param size     size of block in bytes.
 * @param flags    flags for passing options
 *
 * @return 1 on success,
 *         0 on failure
 */
int __cvmx_bootmem_phy_free(uint64_t phy_addr, uint64_t size, uint32_t flags);


/**
 * Prints the list of currently allocated named blocks
 *
 */
void cvmx_bootmem_phy_named_block_print(void);


/**
 * Prints the list of available memory.
 *
 */
void cvmx_bootmem_phy_list_print(void);



/**
 * This function initializes the free memory list used by cvmx_bootmem.
 * This must be called before any allocations can be done.
 *
 * @param mem_size Total memory available, in bytes
 * @param low_reserved_bytes
 *                 Number of bytes to reserve (leave out of free list) at address 0x0.
 * @param desc_buffer
 *                 Buffer for the bootmem descriptor.  This must be a 32 bit addressable
 *                 address.
 *
 * @return 1 on success
 *         0 on failure
 */
int64_t cvmx_bootmem_phy_mem_list_init(uint64_t mem_size, uint32_t low_reserved_bytes, cvmx_bootmem_desc_t *desc_buffer);

/**
 * Locks the bootmem allocator.  This is useful in certain situations
 * where multiple allocations must be made without being interrupted.
 * This should be used with the CVMX_BOOTMEM_FLAG_NO_LOCKING flag.
 *
 */
void cvmx_bootmem_lock(void);

/**
 * Unlocks the bootmem allocator.  This is useful in certain situations
 * where multiple allocations must be made without being interrupted.
 * This should be used with the CVMX_BOOTMEM_FLAG_NO_LOCKING flag.
 *
 */
void cvmx_bootmem_unlock(void);

/**
 * Internal use function to get the current descriptor pointer */
void *__cvmx_bootmem_internal_get_desc_ptr(void);

#ifdef	__cplusplus
}
#endif

#endif /*   __CVMX_BOOTMEM_H__ */
