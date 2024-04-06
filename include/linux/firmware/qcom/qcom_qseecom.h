/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Qualcomm Secure Execution Environment (SEE) interface (QSEECOM).
 * Responsible for setting up and managing QSEECOM client devices.
 *
 * Copyright (C) 2023 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef __QCOM_QSEECOM_H
#define __QCOM_QSEECOM_H

#include <linux/auxiliary_bus.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

#include <linux/firmware/qcom/qcom_scm.h>

/**
 * struct qseecom_client - QSEECOM client device.
 * @aux_dev: Underlying auxiliary device.
 * @app_id: ID of the loaded application.
 */
struct qseecom_client {
	struct auxiliary_device aux_dev;
	u32 app_id;
};

/**
 * qseecom_scm_dev() - Get the SCM device associated with the QSEECOM client.
 * @client: The QSEECOM client device.
 *
 * Returns the SCM device under which the provided QSEECOM client device
 * operates. This function is intended to be used for DMA allocations.
 */
static inline struct device *qseecom_scm_dev(struct qseecom_client *client)
{
	return client->aux_dev.dev.parent->parent;
}

/**
 * qseecom_dma_alloc() - Allocate DMA memory for a QSEECOM client.
 * @client:     The QSEECOM client to allocate the memory for.
 * @size:       The number of bytes to allocate.
 * @dma_handle: Pointer to where the DMA address should be stored.
 * @gfp:        Allocation flags.
 *
 * Wrapper function for dma_alloc_coherent(), allocating DMA memory usable for
 * TZ/QSEECOM communication. Refer to dma_alloc_coherent() for details.
 */
static inline void *qseecom_dma_alloc(struct qseecom_client *client, size_t size,
				      dma_addr_t *dma_handle, gfp_t gfp)
{
	return dma_alloc_coherent(qseecom_scm_dev(client), size, dma_handle, gfp);
}

/**
 * dma_free_coherent() - Free QSEECOM DMA memory.
 * @client:     The QSEECOM client for which the memory has been allocated.
 * @size:       The number of bytes allocated.
 * @cpu_addr:   Virtual memory address to free.
 * @dma_handle: DMA memory address to free.
 *
 * Wrapper function for dma_free_coherent(), freeing memory previously
 * allocated with qseecom_dma_alloc(). Refer to dma_free_coherent() for
 * details.
 */
static inline void qseecom_dma_free(struct qseecom_client *client, size_t size,
				    void *cpu_addr, dma_addr_t dma_handle)
{
	return dma_free_coherent(qseecom_scm_dev(client), size, cpu_addr, dma_handle);
}

/**
 * qcom_qseecom_app_send() - Send to and receive data from a given QSEE app.
 * @client:   The QSEECOM client associated with the target app.
 * @req:      DMA address of the request buffer sent to the app.
 * @req_size: Size of the request buffer.
 * @rsp:      DMA address of the response buffer, written to by the app.
 * @rsp_size: Size of the response buffer.
 *
 * Sends a request to the QSEE app associated with the given client and read
 * back its response. The caller must provide two DMA memory regions, one for
 * the request and one for the response, and fill out the @req region with the
 * respective (app-specific) request data. The QSEE app reads this and returns
 * its response in the @rsp region.
 *
 * Note: This is a convenience wrapper around qcom_scm_qseecom_app_send().
 * Clients should prefer to use this wrapper.
 *
 * Return: Zero on success, nonzero on failure.
 */
static inline int qcom_qseecom_app_send(struct qseecom_client *client,
					dma_addr_t req, size_t req_size,
					dma_addr_t rsp, size_t rsp_size)
{
	return qcom_scm_qseecom_app_send(client->app_id, req, req_size, rsp, rsp_size);
}

#endif /* __QCOM_QSEECOM_H */
