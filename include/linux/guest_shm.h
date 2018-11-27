/*
 * Copyright 2018, QNX Software Systems Limited (“QSS”).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Additional Patent Grant
 *
 * QSS hereby grants to you a perpetual, worldwide, non-exclusive,
 * no-charge, irrevocable (except as stated in this section) patent
 * license to make, have made, use, offer to sell, sell, import,
 * transfer, and otherwise run, modify and propagate the contents of this
 * header file (“Implementation”) , where such license applies
 * only to those patent claims, both currently owned by QSS and
 * acquired in the future, licensable by QSS that are necessarily
 * infringed by this Implementation. This grant does
 * not include claims that would be infringed only as a consequence of
 * further modification of this Implementation. If you or your agent or
 * exclusive licensee institute or order or agree to the institution of
 * patent litigation against any entity (including a cross-claim or
 * counterclaim in a lawsuit) alleging that this Implementation constitutes
 * direct or contributory patent infringement, or inducement of patent
 * infringement, then any patent rights granted to you under this license for
 * this Implementation shall terminate as of the date such litigation is filed.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 */

/**
 * @file
 * definitions guest shared memory device
 */

#ifndef _QVM_GUEST_SHM_H
#define _QVM_GUEST_SHM_H

#ifdef __linux__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/*
 * Temporary VID definition until the updated <pci/pci_id.h> propogates around
 */
#define PCI_VID_BlackBerry_QNX	0x1C05

#define PCI_DID_QNX_GUEST_SHM	0x0001

/** status of last creation request */
enum guest_shm_status {
	GSS_OK,					/**< creation succeeded */
	GSS_UNKNOWN_FAILURE,	/**< creation failed for an unknown reason */
	GSS_NOMEM,				/**< creation failed due to lack of memory */
	GSS_CLIENT_MAX,			/**< creation failed due to region already being used by the maximum number of guests */
	GSS_ILLEGAL_NAME,		/**< creation failed due to illegal region name */
	GSS_NO_PERMISSION,		/**< creation failed due to lack of permission */
	GSS_DOES_NOT_EXIST,		/**< A find request failed */
};

/** Maximum number of clients allowed to connect to a shared memory region */
#define GUEST_SHM_MAX_CLIENTS	16

/** Maximum length allowed for region name */
#define GUEST_SHM_MAX_NAME	32

/** Signature value to verify that vdev is present */
#define GUEST_SHM_SIGNATURE 0x4d534732474d5651


/** Register layout for factory registers */
struct guest_shm_factory {
	uint64_t		signature; /**< == GUEST_SHM_SIGNATURE (R/O) */
	uint64_t		shmem;	/**< shared memory paddr (R/O) */
	uint32_t		vector;	/**< interrupt vector number (R/O) */
	uint32_t		status; /**< status of last creation (R/O) */
	uint32_t		size;	/**< requested size in 4K pages, write causes creation */
	char			name[GUEST_SHM_MAX_NAME];	/**< name of shared memory region */
	uint32_t		find;	/**< find an existing shared memory connection */
};

/** Register layout for a region control page */
struct guest_shm_control {
	uint32_t		status;	/**< lower 16 bits: pending notification bitset, upper 16 bits: current active clients (R/O) */
	uint32_t		idx;	/**< connection index for this client (R/O) */
	uint32_t		notify;	/**< write a bitset of clients to notify */
	uint32_t		detach; /**< write here to detach from the shared memory region */
};


static inline void
guest_shm_create(volatile struct guest_shm_factory *const __factory, unsigned const __size) {
	/* Surround the size assignment with memory barriers so that
	 * the compiler doesn't try to shift the assignment before/after
	 * necessary bits (e.g. setting the name of the region) */
	asm volatile( "" ::: "memory");
	__factory->size = __size;
	asm volatile( "" ::: "memory");
}


static inline void
guest_shm_find(volatile struct guest_shm_factory *const __factory, unsigned const __find_num) {
	/* Surround the find assignment with memory barriers so that
	 * the compiler doesn't try to shift the assignment before/after
	 * necessary bits (e.g. setting the name of the region) */
	asm volatile( "" ::: "memory");
	__factory->find = __find_num;
	asm volatile( "" ::: "memory");
}

#endif

#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL$ $Rev$")
#endif
