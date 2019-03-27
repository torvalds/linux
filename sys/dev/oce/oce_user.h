/*-
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

struct oce_mbx;
struct oce_softc;
struct mbx_hdr;

enum oce_interrupt_mode {
	OCE_INTERRUPT_MODE_MSIX = 0,
	OCE_INTERRUPT_MODE_INTX = 1,
	OCE_INTERRUPT_MODE_MSI = 2,
};

#define MAX_ROCE_MSIX_VECTORS   16
#define MIN_ROCE_MSIX_VECTORS   1
#define ROCE_MSIX_VECTORS       2

struct oce_dev_info {
	device_t dev;
	struct ifnet *ifp;
	struct oce_softc *softc;

	bus_space_handle_t db_bhandle;
	bus_space_tag_t db_btag;
	uint64_t unmapped_db;
	uint32_t unmapped_db_len;
	uint32_t db_page_size;
	uint64_t dpp_unmapped_addr;
	uint32_t dpp_unmapped_len;
	uint8_t mac_addr[6];
	uint32_t dev_family;
	uint16_t vendor_id;
	uint16_t dev_id;
	enum oce_interrupt_mode intr_mode;
	struct {
		int num_vectors;
		int start_vector;
		uint32_t vector_list[MAX_ROCE_MSIX_VECTORS];
	} msix;
	uint32_t flags;
#define OCE_RDMA_INFO_RDMA_SUPPORTED     0x00000001
};


#define OCE_GEN2_FAMILY     2

#ifdef notdef
struct oce_mbx_ctx {
	struct oce_mbx *mbx;
	void (*cb) (void *ctx);
	void *cb_ctx;
};
#endif

struct oce_mbx_ctx;

typedef struct oce_rdma_info {
	int size;
	void (*close)(void);
	int (*mbox_post)(struct oce_softc *sc, 
			 struct oce_mbx *mbx, 
			 struct oce_mbx_ctx *mbxctx);
	void (*common_req_hdr_init)(struct mbx_hdr *hdr,
				    uint8_t dom,
				    uint8_t port,
				    uint8_t subsys,
				    uint8_t opcode,
				    uint32_t timeout,
				    uint32_t pyld_len,
				    uint8_t version);
	void (*get_mac_addr)(struct oce_softc *sc,
			     uint8_t *macaddr);
} OCE_RDMA_INFO, *POCE_RDMA_INFO;

#define OCE_RDMA_INFO_SIZE (sizeof(OCE_RDMA_INFO))

typedef struct oce_rdma_if {
	int size;
	int (*announce)(struct oce_dev_info *devinfo);
} OCE_RDMA_IF, *POCE_RDMA_IF;

#define OCE_RDMA_IF_SIZE (sizeof(OCE_RDMA_IF))

int oce_register_rdma(POCE_RDMA_INFO rdma_info, POCE_RDMA_IF rdma_if);
