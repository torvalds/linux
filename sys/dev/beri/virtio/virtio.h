/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define READ2(_sc, _reg) \
	bus_read_2((_sc)->res[0], _reg)
#define READ4(_sc, _reg) \
	bus_read_4((_sc)->res[0], _reg)
#define WRITE2(_sc, _reg, _val) \
	bus_write_2((_sc)->res[0], _reg, _val)
#define WRITE4(_sc, _reg, _val) \
	bus_write_4((_sc)->res[0], _reg, _val)

#define	PAGE_SHIFT		12
#define	VRING_ALIGN		4096

#define	VQ_ALLOC		0x01	/* set once we have a pfn */
#define	VQ_MAX_DESCRIPTORS	512

struct vqueue_info {
	uint16_t vq_qsize;	/* size of this queue (a power of 2) */
	uint16_t vq_num;
	uint16_t vq_flags;
	uint16_t vq_last_avail;	/* a recent value of vq_avail->va_idx */
	uint16_t vq_save_used;	/* saved vq_used->vu_idx; see vq_endchains */
	uint32_t vq_pfn;	/* PFN of virt queue (not shifted!) */

	volatile struct vring_desc *vq_desc;	/* descriptor array */
	volatile struct vring_avail *vq_avail;	/* the "avail" ring */
	volatile struct vring_used *vq_used;	/* the "used" ring */
};

int vq_ring_ready(struct vqueue_info *vq);
int vq_has_descs(struct vqueue_info *vq);
void * paddr_map(uint32_t offset, uint32_t phys, uint32_t size);
void paddr_unmap(void *phys, uint32_t size);
int vq_getchain(uint32_t beri_mem_offset, struct vqueue_info *vq,
		struct iovec *iov, int n_iov, uint16_t *flags);
void vq_relchain(struct vqueue_info *vq, struct iovec *iov, int n, uint32_t iolen);
struct iovec * getcopy(struct iovec *iov, int n);

int setup_pio(device_t dev, char *name, device_t *pio_dev);
int setup_offset(device_t dev, uint32_t *offset);
