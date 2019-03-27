/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

/**
 * Fill the supplied hardware pool with mbufs
 *
 * @param pool     Pool to allocate an mbuf for
 * @param size     Size of the buffer needed for the pool
 * @param elements Number of buffers to allocate
 */
int cvm_oct_mem_fill_fpa(int pool, int size, int elements)
{
	int freed = elements;
	while (freed) {
		KASSERT(size <= MCLBYTES - 128, ("mbuf clusters are too small"));

		struct mbuf *m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (__predict_false(m == NULL)) {
			printf("Failed to allocate mbuf for hardware pool %d\n", pool);
			break;
		}

		m->m_data += 128 - (((uintptr_t)m->m_data) & 0x7f);
		*(struct mbuf **)(m->m_data - sizeof(void *)) = m;
		cvmx_fpa_free(m->m_data, pool, DONT_WRITEBACK(size/128));
		freed--;
	}
	return (elements - freed);
}


/**
 * Free the supplied hardware pool of mbufs
 *
 * @param pool     Pool to allocate an mbuf for
 * @param size     Size of the buffer needed for the pool
 * @param elements Number of buffers to allocate
 */
void cvm_oct_mem_empty_fpa(int pool, int size, int elements)
{
	char *memory;

	do {
		memory = cvmx_fpa_alloc(pool);
		if (memory) {
			struct mbuf *m = *(struct mbuf **)(memory - sizeof(void *));
			elements--;
			m_freem(m);
		}
	} while (memory);

	if (elements < 0)
		printf("Warning: Freeing of pool %u had too many mbufs (%d)\n", pool, elements);
	else if (elements > 0)
		printf("Warning: Freeing of pool %u is missing %d mbufs\n", pool, elements);
}
