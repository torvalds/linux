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
 *   cvmx-shmem supplies the cross application shared memory implementation
 *
 * <hr>$Revision: 41586 $<hr>
 */
#include "cvmx.h"
#include "cvmx-bootmem.h"
#include "cvmx-tlb.h"
#include "cvmx-shmem.h"

//#define DEBUG

struct cvmx_shmem_smdr *__smdr = NULL;

#ifdef CVMX_BUILD_FOR_LINUX_USER
static int __cvmx_shmem_devmemfd = 0;   /* fd for /dev/mem */
#endif

#define __CHECK_APP_SMDR  do { \
                if (__smdr == NULL) { \
                    cvmx_dprintf("cvmx_shmem: %s is not set up, Quit line %d \n", \
                        CVMX_SHMEM_DSCPTR_NAME, __LINE__ ); \
                    exit(-1); \
                } \
              }while(0)



/**
 * @INTERNAL
 * Virtual sbrk, assigning virtual address in a global virtual address space.
 *
 * @param alignment   alignment requirement in bytes
 * @param size        size in bytes
 */
static inline void *__cvmx_shmem_vsbrk_64(uint64_t alignment, uint64_t size)
{
    uint64_t nbase_64 = CAST64(__smdr->break64);
    void *nbase = NULL;

    /* Skip unaligned bytes */
    if (nbase_64 & alignment)
        nbase_64 += ~(nbase_64 & alignment) + 1;

    if (nbase_64 + size  <  CVMX_SHMEM_VADDR64_END)
    {
        nbase = CASTPTR(void *, nbase_64);
        __smdr->break64 = nbase + size;
    }

    return nbase;
}

/**
 * @INTERNAL
 * Initialize all SMDR entries, only need to be called once
 *
 * @param smdr pointer to the SMDR
 */
static inline void __smdr_new(struct cvmx_shmem_smdr *smdr) {

    if (smdr != NULL)
    {
        int i;

        cvmx_spinlock_init (&smdr->lock);
        cvmx_spinlock_lock (&smdr->lock);

        for ( i = 0; i < CVMX_SHMEM_NUM_DSCPTR; i++ )
        {
            smdr -> shmd[i].owner = CVMX_SHMEM_OWNER_NONE;
            smdr -> shmd[i].is_named_block = 0;
            smdr -> shmd[i].use_count = 0;
            smdr -> shmd[i].name = NULL;
            smdr -> shmd[i].vaddr = NULL;
            smdr -> shmd[i].paddr = 0;
            smdr -> shmd[i].size = 0;
            smdr -> shmd[i].alignment = 0;
        };

        /* Init vaddr */
        smdr->break64 = (void *)CVMX_SHMEM_VADDR64_START;
        cvmx_spinlock_unlock (&smdr->lock);
    }

    /* Make sure the shmem descriptor region is created */
    __CHECK_APP_SMDR;
};



/**
 * @INTERNAL
 * Initialize __smdr pointer, if SMDR exits already. If not, create a new
 * one.  Once SMDR is created (as a bootmem named block), it is persistent.
 */
static inline struct cvmx_shmem_smdr *__smdr_init()
{
    const cvmx_bootmem_named_block_desc_t *smdr_nblk = NULL;
    size_t smdr_size = sizeof(*__smdr);
    char *smdr_name = CVMX_SHMEM_DSCPTR_NAME;

    __smdr = (struct cvmx_shmem_smdr *) cvmx_bootmem_alloc_named(smdr_size, 0x10000, smdr_name);

    if (__smdr)
       __smdr_new (__smdr);
    else
    {
        /* Check if SMDR exists already */
        smdr_nblk = cvmx_bootmem_find_named_block(smdr_name);
        if (smdr_nblk)
        {
            __smdr = (struct cvmx_shmem_smdr *)
            (cvmx_phys_to_ptr(smdr_nblk->base_addr));

            cvmx_spinlock_lock (&__smdr->lock);
            if (smdr_nblk->size != smdr_size)
            {
                cvmx_dprintf("SMDR named block is created by another "
                    "application with different size %lu, "
                    "expecting %lu \n",
                    (long unsigned int)smdr_nblk->size, (long unsigned int)smdr_size);
                __smdr = NULL;
            }
            cvmx_spinlock_unlock (&__smdr->lock);
        }
    }

   if (!__smdr)
       cvmx_dprintf("cvmx_shmem: Failed to allocate or find SMDR from bootmem \n");

   return __smdr;
};


/**
 * @INTERNAL
 * Generic Iterator function for all SMDR entries
 *
 * @param void(*f)(dscptr) the function to be invoked for every descriptor
 * @param param
 *
 * @return the descriptor iterator stopped at.
 */
static struct cvmx_shmem_dscptr *__smdr_iterator(struct cvmx_shmem_dscptr *(*f)(struct cvmx_shmem_dscptr *dscptr, void *p), void *param )
{
    struct cvmx_shmem_dscptr *d, *dscptr = NULL;
    int i;

    __CHECK_APP_SMDR;

    for (i = 0; i < CVMX_SHMEM_NUM_DSCPTR; i++)
    {
        d = &__smdr->shmd[i];
        if ((dscptr = (*f)(d, param)) != NULL)
            break;      /* stop iteration */
    }

   return dscptr;
}


/**
 * @INTERNAL
 * SMDR name match functor. to be used for iterator.
 *
 * @param dscptr  descriptor passed in by the iterator
 * @param   name    string to match against
 *
 * @return !NULL   descriptor matched
 *     NULL    not match
 */
static struct cvmx_shmem_dscptr *__cvmx_shmem_smdr_match_name(struct cvmx_shmem_dscptr *dscptr, void *name)
{
    char *name_to_match = (char *) name;
    struct cvmx_shmem_dscptr *ret = NULL;

    if (dscptr->owner == CVMX_SHMEM_OWNER_NONE)
        return NULL;

    if (strcmp(dscptr->name, name_to_match) == 0)
        ret =  dscptr;

    return ret;
}

/**
 * @INTERNAL
 * Find by name
 *
 * @param   name    string to match against
 *
 * @return !NULL    descriptor matched
 *          NULL    not match
 */
static struct cvmx_shmem_dscptr *__cvmx_shmem_smdr_find_by_name(char *name)
{
    return __smdr_iterator( __cvmx_shmem_smdr_match_name, name);
}

/**
 * @INTERNAL
 * SMDR is free functor. to be used for iterator.
 *
 * @param dscptr  descriptor passed in by the iterator
 * @param nouse
 *
 * @return !NULL  descriptor is free
 *          NULL  descriptor is not free
 */
static struct cvmx_shmem_dscptr *__cvmx_shmem_smdr_is_free(struct cvmx_shmem_dscptr* dscptr, void *nouse)
{
    if (dscptr->owner == CVMX_SHMEM_OWNER_NONE)
        return dscptr;
    else
        return NULL;
}

/**
 * @INTERNAL
 * Search SMDR to find the first free descriptor
 *
 * @return !NULL   free descriptor found
 *     NULL    nothing found
 */
struct cvmx_shmem_dscptr *__cvmx_shmem_smdr_find_free_dscptr(void)
{
    return __smdr_iterator(__cvmx_shmem_smdr_is_free, NULL);
}

/**
 * @INTERNAL
 * free a descriptor
 *
 * @param dscptr  descriptor to be freed
 */
static void __cvmx_shmem_smdr_free(struct cvmx_shmem_dscptr *dscptr)
{
    dscptr->owner = CVMX_SHMEM_OWNER_NONE;
}


/**
 * Per core shmem init function
 *
 * @return  cvmx_shmem_smdr*   pointer to __smdr
 */
struct cvmx_shmem_smdr *cvmx_shmem_init()
{
    return __smdr_init();
}

/**
 * Open shared memory based on named block
 *
 * @return  dscptr descriptor of the opened named block
 */
struct cvmx_shmem_dscptr *cvmx_shmem_named_block_open(char *name, uint32_t size, int oflag)
{
    const cvmx_bootmem_named_block_desc_t *shmem_nblk = NULL;
    struct cvmx_shmem_dscptr *dscptr = NULL;
    int nblk_allocated = 0; /* Assume we don't need to allocate a new
                               bootmem block */
    void *vaddr = NULL;
    const uint64_t size_4k = 4*1024, size_512mb = 512*1024*1024;

    __CHECK_APP_SMDR;

    /* Check size, Make sure it is minimal 4K, no bigger than 512MB */
    if (size > size_512mb) {
        cvmx_dprintf("Shared memory size can not be bigger than 512MB \n");
        return NULL;
    }
    if (size < size_4k)
        size = size_4k;

    size = __upper_power_of_two(size);

    cvmx_spinlock_lock(&__smdr->lock);

    shmem_nblk = cvmx_bootmem_find_named_block(name);
    if ((shmem_nblk == NULL) &&  (oflag & CVMX_SHMEM_O_CREAT))
    {
       void *p;
       /* The named block does not exist, create it if caller specifies
          the O_CREAT flag */
        nblk_allocated = 1;
        p = cvmx_bootmem_alloc_named(size, size, name);
        if (p)
            shmem_nblk = cvmx_bootmem_find_named_block(name);
#ifdef DEBUG
        cvmx_dprintf("cvmx-shmem-dbg:"
             "creating a new block %s: blk %p, shmem_nblk %p \n",
             name, p, shmem_nblk);
#endif
    }

    if (shmem_nblk == NULL)
        goto err;

    /* We are now holding a valid named block */

    dscptr = __cvmx_shmem_smdr_find_by_name(name);
    if (dscptr)
    {
        if (nblk_allocated)
        {
            /* name conflict between bootmem name space and SMDR name space */
            cvmx_dprintf("cvmx-shmem: SMDR descriptor name conflict, %s \n", name);
            goto err;
        }
        /* Make sure size and alignment matches with existing descriptor */
        if ((size != dscptr->size) ||  (size != dscptr -> alignment))
            goto err;
    }
    else
    {
        /* Create a new descriptor */
        dscptr = __cvmx_shmem_smdr_find_free_dscptr();
        if (dscptr)
            goto init;
        else
        {
            cvmx_dprintf("cvmx-shmem: SMDR out of descriptors \n");
            goto err;
        }
    }

    /* Maintain the reference count */
    if (dscptr != NULL)
        dscptr->use_count += 1;

    cvmx_spinlock_unlock(&__smdr->lock);
    return dscptr;

err:
#ifdef DEBUG
    cvmx_dprintf("cvmx-shmem-dbg: named block open failed \n");
#endif

    if (dscptr)
        __cvmx_shmem_smdr_free(dscptr);
    if (shmem_nblk && nblk_allocated)
        cvmx_bootmem_free_named(name);
    cvmx_spinlock_unlock(&__smdr->lock);

    return NULL;

init:

#ifdef DEBUG
    cvmx_dprintf("cvmx-shmem-dbg: init SMDR descriptor %p \n", dscptr);
#endif

    /* Assign vaddr for single address space mapping */
    vaddr = __cvmx_shmem_vsbrk_64(size, size);
    if (vaddr == NULL) {
        /* Failed to allocate virtual address, clean up */
        goto err;
    }

#ifdef DEBUG
    cvmx_dprintf("cmvx-shmem-dbg: allocated vaddr %p \n", vaddr);
#endif
    dscptr->vaddr = vaddr;

    /* Store descriptor information,  name, alignment,size... */
    dscptr->owner = cvmx_get_core_num();
    dscptr->is_named_block = 1;
    dscptr->use_count = 1;
    dscptr->name =shmem_nblk->name ;
    dscptr->paddr = shmem_nblk->base_addr;
    dscptr->size = size;
    dscptr->alignment = size;

    /* Store permission bits */
    if (oflag & CVMX_SHMEM_O_WRONLY)
        dscptr->p_wronly = 1;
    if (oflag & CVMX_SHMEM_O_RDWR)
        dscptr->p_rdwr = 1;

   cvmx_spinlock_unlock(&__smdr->lock);
   return dscptr;
}

/**
 * @INTERNAL
 *
 *  For stand along SE application only.
 *
 *  Add TLB mapping to map the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *  @param pflag   protection flags
 *
 *  @return vaddr  the virtual address mapped for the shared memory
 */
#ifndef CVMX_BUILD_FOR_LINUX_USER
void *__cvmx_shmem_map_standalone(struct cvmx_shmem_dscptr *dscptr, int pflag)
{
    int free_index;

    /* Find a free tlb entry */
    free_index = cvmx_tlb_allocate_runtime_entry();

    if (free_index < 0 )
    {
        cvmx_dprintf("cvmx-shmem: shmem_map failed, out TLB entries \n");
        return NULL;
    }

#ifdef DEBUG
    cvmx_dprintf("cmvx-shmem-dbg:"
         "shmem_map TLB %d: vaddr %p paddr %lx, size %x \n",
         free_index, dscptr->vaddr, dscptr->paddr, dscptr->size );
#endif

    cvmx_tlb_write_runtime_entry(free_index, CAST64(dscptr->vaddr),
            dscptr->paddr, dscptr->size,
            TLB_DIRTY | TLB_VALID | TLB_GLOBAL);

    return dscptr -> vaddr;
}
#endif

/**
 * @INTERNAL
 *
 *  For Linux user application only
 *
 *  Add mmap the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *  @param pflag   protection flags
 *
 *  @return vaddr  the virtual address mapped for the shared memory
 */
#ifdef CVMX_BUILD_FOR_LINUX_USER
static inline void *__cvmx_shmem_map_linux(struct cvmx_shmem_dscptr *dscptr, int pflag)
{
    void *vaddr = NULL;

    if(__cvmx_shmem_devmemfd == 0)
    {
        __cvmx_shmem_devmemfd = open("/dev/mem", O_RDWR);
        if (__cvmx_shmem_devmemfd < 0)
        {
            cvmx_dprintf("Failed to open /dev/mem\n");
            exit(-1);
        }
    }

    vaddr = mmap(dscptr->vaddr, dscptr->size, PROT_READ|PROT_WRITE,
                 MAP_SHARED, __cvmx_shmem_devmemfd, 0);

    /* Make sure the mmap maps to the same virtual address specified in
     * descriptor
     */
    if ((vaddr!=NULL) && (vaddr != dscptr->vaddr))
    {
        munmap(vaddr, dscptr->size);
        vaddr = NULL;
    }
    return vaddr;
}
#endif

/**
 *  cvmx_shmem API
 *
 *  Add mapping for the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *  @param pflag   protection flags
 *
 *  @return vaddr  the virtual address mapped for the shared memory
 */
void *cvmx_shmem_map(struct cvmx_shmem_dscptr *dscptr, int pflag)
{
    void *vaddr = NULL;
#ifdef CVMX_BUILD_FOR_LINUX_USER
    vaddr = __cvmx_shmem_map_linux(dscptr, pflag);
#else
    vaddr = __cvmx_shmem_map_standalone(dscptr, pflag);
#endif
    return vaddr;
}


/**
 * @INTERNAL
 *
 *  For Linux user application only
 *
 *  ummap the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *
 */
#ifdef CVMX_BUILD_FOR_LINUX_USER
static inline void __cvmx_shmem_unmap_linux(struct cvmx_shmem_dscptr* dscptr)
{
    if (__cvmx_shmem_devmemfd && dscptr)
        munmap(dscptr->vaddr, dscptr->size);
}
#endif


/**
 * @INTERNAL
 *
 *  For stand along SE application only.
 *
 *  ummap the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *
 */
#ifndef CVMX_BUILD_FOR_LINUX_USER
static inline void
__cvmx_shmem_unmap_standalone(struct cvmx_shmem_dscptr *dscptr)
{
    int index;

    index = cvmx_tlb_lookup(CAST64(dscptr->vaddr));

#ifdef DEBUG
    cvmx_dprintf("cmvx-shmem-dbg:"
             "shmem_unmap TLB %d \n", index);
#endif
    cvmx_tlb_free_runtime_entry(index);
}
#endif

/**
 *  ummap the shared memory
 *
 *  @param dscptr  shared memory descriptor
 *
 */
void cvmx_shmem_unmap(struct cvmx_shmem_dscptr *dscptr)
{
#ifdef CVMX_BUILD_FOR_LINUX_USER
    __cvmx_shmem_unmap_linux(dscptr);
#else
    __cvmx_shmem_unmap_standalone(dscptr);
#endif
}

/**
 * @INTERNAL
 *
 *  Common implementation of closing a descriptor.
 *
 *  @param dscptr  shared memory descriptor
 *  @param remove  1:  remove the descriptor and named block if this
 *                  this is the last user of the descriptor
 *             0:  do not remove
 *  @return  0:   Success
 *          !0:   Failed
 *
 */
static inline int __cvmx_shmem_close_dscptr(struct cvmx_shmem_dscptr *dscptr, int remove)
{
    cvmx_spinlock_lock(&dscptr->lock);

    if (dscptr->use_count >0)
        dscptr->use_count-= 1;

    if ((dscptr->use_count == 0) && remove)
    {
        /* Free this descriptor */
        __cvmx_shmem_smdr_free(dscptr);

        /* Free named block if this is the last user, and the block
           is created by the application */
        if (dscptr->is_named_block)
        {
#ifdef DEBUG
            cvmx_dprintf("cvmx-shmem-dbg: remove named block %s \n", dscptr->name);
#endif
            cvmx_bootmem_phy_named_block_free(dscptr->name, 0);
        }
    }
    cvmx_spinlock_unlock(&dscptr->lock);
    return 0;
}


/**
 * @INTERNAL
 *
 *  For stand along SE application only.
 *
 *  close a descriptor.
 *
 *  @param dscptr  shared memory descriptor
 *  @param remove  1:  remove the descriptor and named block if this
 *                  this is the last user of the descriptor
 *             0:  do not remove
 *  @return  0:   Success
 *          !0:   Failed
 *
 */
#ifndef CVMX_BUILD_FOR_LINUX_USER
static inline int __cvmx_shmem_close_standalone(struct cvmx_shmem_dscptr *dscptr, int remove)
{
    return __cvmx_shmem_close_dscptr(dscptr, remove);
}
#endif

/**
 * @INTERNAL
 *
 *  For Linux user application only.
 *
 *  close a descriptor.
 *
 *  @param dscptr  shared memory descriptor
 *  @param remove  1:  remove the descriptor and named block if this
 *                  this is the last user of the descriptor
 *             0:  do not remove
 *  @return  0:   Success
 *          !0:   Failed
 *
 */
#ifdef CVMX_BUILD_FOR_LINUX_USER
int __cvmx_shmem_close_linux(struct cvmx_shmem_dscptr *dscptr, int remove)
{
    int ret;
    ret = __cvmx_shmem_close_dscptr(dscptr, remove);

    if (ret && __cvmx_shmem_devmemfd)
    {
        close(__cvmx_shmem_devmemfd);
         __cvmx_shmem_devmemfd=0;
    }

    return ret;

}
#endif

/**
 *
 *  close a descriptor.
 *
 *  @param dscptr  shared memory descriptor
 *  @param remove  1:  remove the descriptor and named block if this
 *                  this is the last user of the descriptor
 *             0:  do not remove
 *  @return  0:   Success
 *          !0:   Failed
 *
 */
int cvmx_shmem_close(struct cvmx_shmem_dscptr *dscptr, int remove)
{
    int ret;
#ifdef CVMX_BUILD_FOR_LINUX_USER
    ret = __cvmx_shmem_close_linux(dscptr, remove);
#else
    ret = __cvmx_shmem_close_standalone(dscptr, remove);
#endif
    return ret;
}

#ifdef DEBUG
/**
 * @INTERNAL
 *  SMDR non-free descriptor dump functor. to be used for iterator.
 *
 * @param dscptr  descriptor passed in by the iterator
 *
 * @return NULL  always
 */
static struct cvmx_shmem_dscptr *__cvmx_shmem_smdr_display_dscptr(struct cvmx_shmem_dscptr *dscptr, void *nouse)
{
    if ((dscptr != NULL ) && (dscptr -> owner != CVMX_SHMEM_OWNER_NONE))
    {
        cvmx_dprintf("  %s: phy: %lx, size %d, alignment %lx, virt %p use_count %d\n",
            dscptr->name, dscptr-> paddr,
            dscptr->size, dscptr-> alignment,
            dscptr->vaddr, dscptr->use_count);
    }

    return NULL;
}
#endif

/**
 *  SMDR descriptor show
 *
 *  list all non-free descriptors
 */
void cvmx_shmem_show(void)
{
    __CHECK_APP_SMDR;

#ifdef DEBUG
    cvmx_dprintf("SMDR descriptor list: \n");
    cvmx_spinlock_lock(&__smdr->lock);
    __smdr_iterator(__cvmx_shmem_smdr_display_dscptr, NULL);
    cvmx_spinlock_unlock(&__smdr->lock);
    cvmx_dprintf("\n\n");
#endif
}
