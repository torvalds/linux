/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2018, Intel Corporation
 */

#ifndef __STRATIX10_SVC_CLIENT_H
#define __STRATIX10_SVC_CLIENT_H

/**
 * Service layer driver supports client names
 *
 * fpga: for FPGA configuration
 * rsu: for remote status update
 */
#define SVC_CLIENT_FPGA			"fpga"
#define SVC_CLIENT_RSU			"rsu"

/**
 * Status of the sent command, in bit number
 *
 * SVC_COMMAND_STATUS_RECONFIG_REQUEST_OK:
 * Secure firmware accepts the request of FPGA reconfiguration.
 *
 * SVC_STATUS_RECONFIG_BUFFER_SUBMITTED:
 * Service client successfully submits FPGA configuration
 * data buffer to secure firmware.
 *
 * SVC_COMMAND_STATUS_RECONFIG_BUFFER_DONE:
 * Secure firmware completes data process, ready to accept the
 * next WRITE transaction.
 *
 * SVC_COMMAND_STATUS_RECONFIG_COMPLETED:
 * Secure firmware completes FPGA configuration successfully, FPGA should
 * be in user mode.
 *
 * SVC_COMMAND_STATUS_RECONFIG_BUSY:
 * FPGA configuration is still in process.
 *
 * SVC_COMMAND_STATUS_RECONFIG_ERROR:
 * Error encountered during FPGA configuration.
 *
 * SVC_STATUS_RSU_OK:
 * Secure firmware accepts the request of remote status update (RSU).
 *
 * SVC_STATUS_RSU_ERROR:
 * Error encountered during remote system update.
 *
 * SVC_STATUS_RSU_NO_SUPPORT:
 * Secure firmware doesn't support RSU retry or notify feature.
 */
#define SVC_STATUS_RECONFIG_REQUEST_OK		0
#define SVC_STATUS_RECONFIG_BUFFER_SUBMITTED	1
#define SVC_STATUS_RECONFIG_BUFFER_DONE		2
#define SVC_STATUS_RECONFIG_COMPLETED		3
#define SVC_STATUS_RECONFIG_BUSY		4
#define SVC_STATUS_RECONFIG_ERROR		5
#define SVC_STATUS_RSU_OK			6
#define SVC_STATUS_RSU_ERROR			7
#define SVC_STATUS_RSU_NO_SUPPORT		8

/**
 * Flag bit for COMMAND_RECONFIG
 *
 * COMMAND_RECONFIG_FLAG_PARTIAL:
 * Set to FPGA configuration type (full or partial), the default
 * is full reconfig.
 */
#define COMMAND_RECONFIG_FLAG_PARTIAL	0

/**
 * Timeout settings for service clients:
 * timeout value used in Stratix10 FPGA manager driver.
 * timeout value used in RSU driver
 */
#define SVC_RECONFIG_REQUEST_TIMEOUT_MS         100
#define SVC_RECONFIG_BUFFER_TIMEOUT_MS          240
#define SVC_RSU_REQUEST_TIMEOUT_MS              300

struct stratix10_svc_chan;

/**
 * enum stratix10_svc_command_code - supported service commands
 *
 * @COMMAND_NOOP: do 'dummy' request for integration/debug/trouble-shooting
 *
 * @COMMAND_RECONFIG: ask for FPGA configuration preparation, return status
 * is SVC_STATUS_RECONFIG_REQUEST_OK
 *
 * @COMMAND_RECONFIG_DATA_SUBMIT: submit buffer(s) of bit-stream data for the
 * FPGA configuration, return status is SVC_STATUS_RECONFIG_BUFFER_SUBMITTED,
 * or SVC_STATUS_RECONFIG_ERROR
 *
 * @COMMAND_RECONFIG_DATA_CLAIM: check the status of the configuration, return
 * status is SVC_STATUS_RECONFIG_COMPLETED, or SVC_STATUS_RECONFIG_BUSY, or
 * SVC_STATUS_RECONFIG_ERROR
 *
 * @COMMAND_RECONFIG_STATUS: check the status of the configuration, return
 * status is SVC_STATUS_RECONFIG_COMPLETED, or  SVC_STATUS_RECONFIG_BUSY, or
 * SVC_STATUS_RECONFIG_ERROR
 *
 * @COMMAND_RSU_STATUS: request remote system update boot log, return status
 * is log data or SVC_STATUS_RSU_ERROR
 *
 * @COMMAND_RSU_UPDATE: set the offset of the bitstream to boot after reboot,
 * return status is SVC_STATUS_RSU_OK or SVC_STATUS_RSU_ERROR
 *
 * @COMMAND_RSU_NOTIFY: report the status of hard processor system
 * software to firmware, return status is SVC_STATUS_RSU_OK or
 * SVC_STATUS_RSU_ERROR
 *
 * @COMMAND_RSU_RETRY: query firmware for the current image's retry counter,
 * return status is SVC_STATUS_RSU_OK or SVC_STATUS_RSU_ERROR
 */
enum stratix10_svc_command_code {
	COMMAND_NOOP = 0,
	COMMAND_RECONFIG,
	COMMAND_RECONFIG_DATA_SUBMIT,
	COMMAND_RECONFIG_DATA_CLAIM,
	COMMAND_RECONFIG_STATUS,
	COMMAND_RSU_STATUS,
	COMMAND_RSU_UPDATE,
	COMMAND_RSU_NOTIFY,
	COMMAND_RSU_RETRY,
};

/**
 * struct stratix10_svc_client_msg - message sent by client to service
 * @payload: starting address of data need be processed
 * @payload_length: data size in bytes
 * @command: service command
 * @arg: args to be passed via registers and not physically mapped buffers
 */
struct stratix10_svc_client_msg {
	void *payload;
	size_t payload_length;
	enum stratix10_svc_command_code command;
	u64 arg[3];
};

/**
 * struct stratix10_svc_command_config_type - config type
 * @flags: flag bit for the type of FPGA configuration
 */
struct stratix10_svc_command_config_type {
	u32 flags;
};

/**
 * struct stratix10_svc_cb_data - callback data structure from service layer
 * @status: the status of sent command
 * @kaddr1: address of 1st completed data block
 * @kaddr2: address of 2nd completed data block
 * @kaddr3: address of 3rd completed data block
 */
struct stratix10_svc_cb_data {
	u32 status;
	void *kaddr1;
	void *kaddr2;
	void *kaddr3;
};

/**
 * struct stratix10_svc_client - service client structure
 * @dev: the client device
 * @receive_cb: callback to provide service client the received data
 * @priv: client private data
 */
struct stratix10_svc_client {
	struct device *dev;
	void (*receive_cb)(struct stratix10_svc_client *client,
			   struct stratix10_svc_cb_data *cb_data);
	void *priv;
};

/**
 * stratix10_svc_request_channel_byname() - request service channel
 * @client: identity of the client requesting the channel
 * @name: supporting client name defined above
 *
 * Return: a pointer to channel assigned to the client on success,
 * or ERR_PTR() on error.
 */
struct stratix10_svc_chan
*stratix10_svc_request_channel_byname(struct stratix10_svc_client *client,
	const char *name);

/**
 * stratix10_svc_free_channel() - free service channel.
 * @chan: service channel to be freed
 */
void stratix10_svc_free_channel(struct stratix10_svc_chan *chan);

/**
 * stratix10_svc_allocate_memory() - allocate the momory
 * @chan: service channel assigned to the client
 * @size: number of bytes client requests
 *
 * Service layer allocates the requested number of bytes from the memory
 * pool for the client.
 *
 * Return: the starting address of allocated memory on success, or
 * ERR_PTR() on error.
 */
void *stratix10_svc_allocate_memory(struct stratix10_svc_chan *chan,
				    size_t size);

/**
 * stratix10_svc_free_memory() - free allocated memory
 * @chan: service channel assigned to the client
 * @kaddr: starting address of memory to be free back to pool
 */
void stratix10_svc_free_memory(struct stratix10_svc_chan *chan, void *kaddr);

/**
 * stratix10_svc_send() - send a message to the remote
 * @chan: service channel assigned to the client
 * @msg: message data to be sent, in the format of
 * struct stratix10_svc_client_msg
 *
 * Return: 0 for success, -ENOMEM or -ENOBUFS on error.
 */
int stratix10_svc_send(struct stratix10_svc_chan *chan, void *msg);

/**
 * intel_svc_done() - complete service request
 * @chan: service channel assigned to the client
 *
 * This function is used by service client to inform service layer that
 * client's service requests are completed, or there is an error in the
 * request process.
 */
void stratix10_svc_done(struct stratix10_svc_chan *chan);
#endif

