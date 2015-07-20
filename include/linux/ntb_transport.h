/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * PCIe NTB Transport Linux driver
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */

struct ntb_transport_qp;

struct ntb_transport_client {
	struct device_driver driver;
	int (*probe)(struct device *client_dev);
	void (*remove)(struct device *client_dev);
};

int ntb_transport_register_client(struct ntb_transport_client *drvr);
void ntb_transport_unregister_client(struct ntb_transport_client *drvr);
int ntb_transport_register_client_dev(char *device_name);
void ntb_transport_unregister_client_dev(char *device_name);

struct ntb_queue_handlers {
	void (*rx_handler)(struct ntb_transport_qp *qp, void *qp_data,
			   void *data, int len);
	void (*tx_handler)(struct ntb_transport_qp *qp, void *qp_data,
			   void *data, int len);
	void (*event_handler)(void *data, int status);
};

unsigned char ntb_transport_qp_num(struct ntb_transport_qp *qp);
unsigned int ntb_transport_max_size(struct ntb_transport_qp *qp);
struct ntb_transport_qp *
ntb_transport_create_queue(void *data, struct device *client_dev,
			   const struct ntb_queue_handlers *handlers);
void ntb_transport_free_queue(struct ntb_transport_qp *qp);
int ntb_transport_rx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len);
int ntb_transport_tx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len);
void *ntb_transport_rx_remove(struct ntb_transport_qp *qp, unsigned int *len);
void ntb_transport_link_up(struct ntb_transport_qp *qp);
void ntb_transport_link_down(struct ntb_transport_qp *qp);
bool ntb_transport_link_query(struct ntb_transport_qp *qp);
