/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */


/*
 * File: qlnx_rdma.h
 * Author: David C Somayajulu
 */

#ifndef _QLNX_RDMA_H_
#define _QLNX_RDMA_H_

enum qlnx_rdma_event {
	QLNX_ETHDEV_UP = 0x10,
	QLNX_ETHDEV_DOWN = 0x11,
	QLNX_ETHDEV_CHANGE_ADDR = 0x12
};

struct qlnx_rdma_if {
	void *	(*add)(void *ha);
	int 	(*remove)(void *ha, void *qlnx_rdma_dev);
	void 	(*notify)(void *ha, void *qlnx_rdma_dev, enum qlnx_rdma_event);
};
typedef struct qlnx_rdma_if qlnx_rdma_if_t;

extern int qlnx_rdma_register_if(qlnx_rdma_if_t *rdma_if);
extern int qlnx_rdma_deregister_if(qlnx_rdma_if_t *rdma_if);

#define QLNX_NUM_CNQ	1

extern int qlnx_rdma_get_num_irqs(struct qlnx_host *ha);
extern void qlnx_rdma_dev_add(struct qlnx_host *ha);
extern void qlnx_rdma_dev_open(struct qlnx_host *ha);
extern void qlnx_rdma_dev_close(struct qlnx_host *ha);
extern int qlnx_rdma_dev_remove(struct qlnx_host *ha);
extern void qlnx_rdma_changeaddr(struct qlnx_host *ha);

extern void qlnx_rdma_init(void);
extern void qlnx_rdma_deinit(void);

#endif /* #ifndef _QLNX_RDMA_H_ */
