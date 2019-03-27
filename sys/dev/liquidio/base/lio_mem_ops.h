/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

/*
 *   \file lio_mem_ops.h
 *   \brief Host Driver: Routines used to read/write Octeon memory.
 */

#ifndef __LIO_MEM_OPS_H__
#define __LIO_MEM_OPS_H__

/*
 *   Read a 64-bit value from a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to read from.
 *
 *   The range_idx gives the BAR1 index register for the range of address
 *   in which core_addr is mapped.
 *
 *   @return  64-bit value read from Core memory
 */
uint64_t	lio_read_device_mem64(struct octeon_device *oct,
				      uint64_t core_addr);

/*
 *   Read a 32-bit value from a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to read from.
 *
 *   @return  32-bit value read from Core memory
 */
uint32_t	lio_read_device_mem32(struct octeon_device *oct,
				      uint64_t core_addr);

/*
 *   Write a 32-bit value to a BAR1 mapped core memory address.
 *   @param  oct        -  pointer to the octeon device.
 *   @param  core_addr  -  the address to write to.
 *   @param  val        -  32-bit value to write.
 */
void		lio_write_device_mem32(struct octeon_device *oct,
				       uint64_t core_addr, uint32_t val);

/* Read multiple bytes from Octeon memory. */
void		lio_pci_read_core_mem(struct octeon_device *oct,
				      uint64_t coreaddr, uint8_t *buf,
				      uint32_t len);

/* Write multiple bytes into Octeon memory. */
void		lio_pci_write_core_mem(struct octeon_device *oct,
				       uint64_t coreaddr, uint8_t *buf,
				       uint32_t len);

#endif	/* __LIO_MEM_OPS_H__ */
