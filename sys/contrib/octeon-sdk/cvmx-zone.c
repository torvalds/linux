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
 * Support library for the Zone Allocator.
 *
 * <hr>$Revision: 70030 $<hr>
 */


#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-spinlock.h"
#include "cvmx-malloc.h"




#ifndef CVMX_BUILD_FOR_LINUX_USER
cvmx_zone_t cvmx_zone_create_from_addr(char *name, uint32_t elem_size, uint32_t num_elem,
                             void* mem_ptr, uint64_t mem_size, uint32_t flags)
{
    cvmx_zone_t zone;
    unsigned int i;

    if ((unsigned long)mem_ptr & (sizeof(void *) -1))
    {
        return(NULL);  //invalid alignment
    }
    if (mem_size  < sizeof(struct cvmx_zone) + elem_size * num_elem)
    {
        return(NULL);  // not enough room
    }

    zone = (cvmx_zone_t) ((char *)mem_ptr + elem_size * num_elem);
    zone->elem_size = elem_size;
    zone->num_elem = num_elem;
    zone->name = name;
    zone->align = 0;  // not used
    zone->baseptr = NULL;
    zone->freelist = NULL;
    zone->lock.value = CVMX_SPINLOCK_UNLOCKED_VAL;

    zone->baseptr = (char *)mem_ptr;

    for(i=0;i<num_elem;i++)
    {
        *(void **)(zone->baseptr + (i*elem_size)) = zone->freelist;
        zone->freelist = (void *)(zone->baseptr + (i*elem_size));
    }

    return(zone);

}

cvmx_zone_t cvmx_zone_create_from_arena(char *name, uint32_t elem_size, uint32_t num_elem, uint32_t align, cvmx_arena_list_t arena_list, uint32_t flags)
{
    unsigned int i;
    cvmx_zone_t zone;

    zone = (cvmx_zone_t)cvmx_malloc(arena_list, sizeof(struct cvmx_zone));

    if (NULL == zone)
    {
        return(NULL);
    }
    zone->elem_size = elem_size;
    zone->num_elem = num_elem;
    zone->name = name;
    zone->align = align;
    zone->baseptr = NULL;
    zone->freelist = NULL;
    zone->lock.value = CVMX_SPINLOCK_UNLOCKED_VAL;

    zone->baseptr = (char *)cvmx_memalign(arena_list, align, num_elem * elem_size);
    if (NULL == zone->baseptr)
    {
        return(NULL);
    }

    for(i=0;i<num_elem;i++)
    {
        *(void **)(zone->baseptr + (i*elem_size)) = zone->freelist;
        zone->freelist = (void *)(zone->baseptr + (i*elem_size));
    }

    return(zone);

}
#endif



void * cvmx_zone_alloc(cvmx_zone_t zone, uint32_t flags)
{
    cvmx_zone_t item;

    assert(zone != NULL);
    assert(zone->baseptr != NULL);
    cvmx_spinlock_lock(&zone->lock);

	item = (cvmx_zone_t)zone->freelist;
	if(item != NULL)
	{
		zone->freelist = *(void **)item;
	}
	else
	{
//		cvmx_dprintf("No more elements in zone %s\n", zone->name);
	}

    cvmx_spinlock_unlock(&zone->lock);
    return(item);
}

void cvmx_zone_free(cvmx_zone_t zone, void *ptr)
{

    assert(zone != NULL);
    assert(zone->baseptr != NULL);
    assert((unsigned long)ptr - (unsigned long)zone->baseptr < zone->num_elem * zone->elem_size);

    cvmx_spinlock_lock(&zone->lock);
	*(void **)ptr = zone->freelist;
	zone->freelist = ptr;
    cvmx_spinlock_unlock(&zone->lock);
}


