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
 * Module to support operations on bitmap of cores. Coremask can be used to
 * select a specific core, a group of cores, or all available cores, for
 * initialization and differentiation of roles within a single shared binary
 * executable image.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */


#ifndef __CVMX_COREMASK_H__
#define __CVMX_COREMASK_H__

#include "cvmx-asm.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef uint64_t cvmx_coremask_holder_t;	/* basic type to hold the
						   coremask bits */

#define CVMX_COREMASK_HLDRSZ ((int)(sizeof(cvmx_coremask_holder_t) * 8))
						/* bits per holder */

#define CVMX_COREMASK_BMPSZ ((int)(CVMX_MAX_CORES / CVMX_COREMASK_HLDRSZ + 1))
						/* bit map size */

/*
 * The macro pair implement a way to iterate active cores in the mask.
 * @param fec_pcm points to the coremask.
 * @param fec_ppid is the active core's id.
 */
#define CVMX_COREMASK_FOR_EACH_CORE_BEGIN(fec_pcm, fec_ppid)		\
    do {								\
    	int fec_i, fec_j;						\
    				 					\
	for (fec_i = 0; fec_i < CVMX_COREMASK_BMPSZ; fec_i++)		\
	{								\
	    for (fec_j = 0; fec_j < CVMX_COREMASK_HLDRSZ; fec_j++)	\
	    { 								\
		if (((cvmx_coremask_holder_t)1 << fec_j) & 		\
		    (fec_pcm)->coremask_bitmap[fec_i])			\
		{							\
	            fec_ppid = fec_i * CVMX_COREMASK_HLDRSZ + fec_j;


#define CVMX_COREMASK_FOR_EACH_CORE_END					\
		}							\
	    }								\
	}								\
   } while (0)

struct cvmx_coremask {
    /*
     * Big-endian. Array elems of larger indices represent cores of
     * bigger ids. So do MSBs within a cvmx_coremask_holder_t. Ditto
     * MSbs within a byte.
     */
    cvmx_coremask_holder_t coremask_bitmap[CVMX_COREMASK_BMPSZ];
};

/*
 * Is ``core'' set in the coremask?
 *
 * @param pcm is the pointer to the coremask.
 * @param core
 * @return 1 if core is set and 0 if not.
 */
static inline int cvmx_coremask_is_set_core(struct cvmx_coremask *pcm,
    int core)
{
    int n, i;

    n = core % CVMX_COREMASK_HLDRSZ;
    i = core / CVMX_COREMASK_HLDRSZ;

    return (int)((pcm->coremask_bitmap[i] & (1ull << n)) != 0);
}

/*
 * Set ``core'' in the coremask.
 *
 * @param pcm is the pointer to the coremask.
 * @param core
 * @return 0.
 */
static inline int cvmx_coremask_set_core(struct cvmx_coremask *pcm,
    int core)
{
    int n, i;

    n = core % CVMX_COREMASK_HLDRSZ;
    i = core / CVMX_COREMASK_HLDRSZ;
    pcm->coremask_bitmap[i] |= (1ull << n);

    return 0;
}

/*
 * Clear ``core'' from the coremask.
 *
 * @param pcm is the pointer to the coremask.
 * @param core
 * @return 0.
 */
static inline int cvmx_coremask_clear_core(struct cvmx_coremask *pcm,
    int core)
{
    int n, i;

    n = core % CVMX_COREMASK_HLDRSZ;
    i = core / CVMX_COREMASK_HLDRSZ;
    pcm->coremask_bitmap[i] &= ~(1ull << n);

    return 0;
}

/*
 * Clear the coremask.
 *
 * @param pcm is the pointer to the coremask.
 * @return 0.
 */
static inline int cvmx_coremask_clear_all(struct cvmx_coremask *pcm)
{
    int i;

    for (i = 0; i < CVMX_COREMASK_BMPSZ; i++)
        pcm->coremask_bitmap[i] = 0;

    return 0;
}

/*
 * Is the current core the first in the coremask?
 *
 * @param pcm is the pointer to the coremask.
 * @return 1 for yes and 0 for no.
 */
static inline int cvmx_coremask_first_core_bmp(struct cvmx_coremask *pcm)
{
     int n, i;

     n = (int) cvmx_get_core_num();
     for (i = 0; i < CVMX_COREMASK_BMPSZ; i++)
     {
         if (pcm->coremask_bitmap[i])
         {
             if (n == 0 && pcm->coremask_bitmap[i] & 1)
                 return 1;

             if (n >= CVMX_COREMASK_HLDRSZ)
                 return 0;

             return ((((1ull << n) - 1) & pcm->coremask_bitmap[i]) == 0);
         }
         else
             n -= CVMX_COREMASK_HLDRSZ;
     }

     return 0;
}

/*
 * Is the current core a member of the coremask?
 *
 * @param pcm is the pointer to the coremask.
 * @return 1 for yes and 0 for no.
 */
static inline int cvmx_coremask_is_member_bmp(struct cvmx_coremask *pcm)
{
    return cvmx_coremask_is_set_core(pcm, (int)cvmx_get_core_num());
}

/*
 * coremask is simply unsigned int (32 bits).
 *
 * NOTE: supports up to 32 cores maximum.
 *
 * union of coremasks is simply bitwise-or.
 * intersection of coremasks is simply bitwise-and.
 *
 */

#define  CVMX_COREMASK_MAX  0xFFFFFFFFu    /* maximum supported mask */


/**
 * Compute coremask for a specific core.
 *
 * @param  core_id  The core ID
 *
 * @return  coremask for a specific core
 *
 */
static inline unsigned int cvmx_coremask_core(unsigned int core_id)
{
    return (1u << core_id);
}

/**
 * Compute coremask for num_cores cores starting with core 0.
 *
 * @param  num_cores  number of cores
 *
 * @return  coremask for num_cores cores
 *
 */
static inline unsigned int cvmx_coremask_numcores(unsigned int num_cores)
{
    return (CVMX_COREMASK_MAX >> (CVMX_MAX_CORES - num_cores));
}

/**
 * Compute coremask for a range of cores from core low to core high.
 *
 * @param  low   first core in the range
 * @param  high  last core in the range
 *
 * @return  coremask for the range of cores
 *
 */
static inline unsigned int cvmx_coremask_range(unsigned int low, unsigned int high)
{
    return ((CVMX_COREMASK_MAX >> (CVMX_MAX_CORES - 1 - high + low)) << low);
}


/**
 * Test to see if current core is a member of coremask.
 *
 * @param  coremask  the coremask to test against
 *
 * @return  1 if current core is a member of coremask, 0 otherwise
 *
 */
static inline int cvmx_coremask_is_member(unsigned int coremask)
{
    return ((cvmx_coremask_core(cvmx_get_core_num()) & coremask) != 0);
}

/**
 * Test to see if current core is first core in coremask.
 *
 * @param  coremask  the coremask to test against
 *
 * @return  1 if current core is first core in the coremask, 0 otherwise
 *
 */
static inline int cvmx_coremask_first_core(unsigned int coremask)
{
    return cvmx_coremask_is_member(coremask)
        && ((cvmx_get_core_num() == 0) ||
            ((cvmx_coremask_numcores(cvmx_get_core_num()) & coremask) == 0));
}

/**
 * Wait (stall) until all cores in the given coremask has reached this point
 * in the program execution before proceeding.
 *
 * @param  coremask  the group of cores performing the barrier sync
 *
 */
extern void cvmx_coremask_barrier_sync(unsigned int coremask);

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_COREMASK_H__ */
