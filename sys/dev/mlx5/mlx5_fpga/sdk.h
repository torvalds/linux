/*-
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef MLX5_FPGA_SDK_H
#define MLX5_FPGA_SDK_H

#include <dev/mlx5/driver.h>
#include <linux/types.h>
#include <linux/list.h>
/* #include <linux/dma-direction.h> */

#include <dev/mlx5/mlx5_fpga/cmd.h>
#include <dev/mlx5/mlx5io.h>

/**
 * DOC: Innova SDK
 * This header defines the in-kernel API for Innova FPGA client drivers.
 */

#define MLX5_FPGA_CLIENT_NAME_MAX 64

struct mlx5_fpga_conn;
struct mlx5_fpga_device;

/**
 * struct mlx5_fpga_client - Describes an Innova client driver
 */
struct mlx5_fpga_client {
	/**
	 * @create: Informs the client that an Innova device was created.
	 * The device is not yet operational at this stage
	 * This callback is optional
	 * @fdev: The FPGA device
	 */
	void (*create)(struct mlx5_fpga_device *fdev);
	/**
	 * @add: Informs the client that a core device is ready and operational.
	 * @fdev: The FPGA device
	 * @param vid SBU Vendor ID
	 * @param pid SBU Product ID
	 * Any SBU-specific initialization should happen at this stage
	 * Return: 0 on success, nonzero error value otherwise
	 */
	int  (*add)(struct mlx5_fpga_device *fdev, u32 vid, u16 pid);
	/**
	 * @remove: Informs the client that a core device is not operational
	 *          anymore.
	 * @fdev: The FPGA device
	 * SBU-specific cleanup should happen at this stage
	 * This callback is called once for every successful call to add()
	 */
	void (*remove)(struct mlx5_fpga_device *fdev);
	/**
	 * @destroy: Informs the client that a core device is being destroyed.
	 * @fdev: The FPGA device
	 * The device is not operational at this stage
	 */
	void (*destroy)(struct mlx5_fpga_device *fdev);
	/** The name of this client driver */
	char name[MLX5_FPGA_CLIENT_NAME_MAX];
	/** For use by core. A link in the list of client drivers */
	struct list_head list;
};

/**
 * struct mlx5_fpga_dma_entry - A scatter-gather DMA entry
 */
struct mlx5_fpga_dma_entry {
	/** @data: Virtual address pointer to the data */
	void *data;
	/** @size: Size in bytes of the data */
	unsigned int size;
	/** @dma_addr: Private member. Physical DMA-mapped address of the data */
	dma_addr_t dma_addr;
};

/**
 * struct mlx5_fpga_dma_buf - A packet buffer
 * May contain up to 2 scatter-gather data entries
 */
struct mlx5_fpga_dma_buf {
	/** @dma_dir: DMA direction */
	enum dma_data_direction dma_dir;
	/** @sg: Scatter-gather entries pointing to the data in memory */
	struct mlx5_fpga_dma_entry sg[2];
	/** @list: Item in SQ backlog, for TX packets */
	struct list_head list;
	/**
	 * @complete: Completion routine, for TX packets
	 * @conn: FPGA Connection this packet was sent to
	 * @fdev: FPGA device this packet was sent to
	 * @buf: The packet buffer
	 * @status: 0 if successful, or an error code otherwise
	 */
	void (*complete)(struct mlx5_fpga_conn *conn,
			 struct mlx5_fpga_device *fdev,
			 struct mlx5_fpga_dma_buf *buf, u8 status);
};

/**
 * struct mlx5_fpga_conn_attr - FPGA connection attributes
 * Describes the attributes of a connection
 */
struct mlx5_fpga_conn_attr {
	/** @tx_size: Size of connection TX queue, in packets */
	unsigned int tx_size;
	/** @rx_size: Size of connection RX queue, in packets */
	unsigned int rx_size;
	/**
	 * @recv_cb: Callback function which is called for received packets
	 * @cb_arg: The value provided in mlx5_fpga_conn_attr.cb_arg
	 * @buf: A buffer containing a received packet
	 *
	 * buf is guaranteed to only contain a single scatter-gather entry.
	 * The size of the actual packet received is specified in buf.sg[0].size
	 * When this callback returns, the packet buffer may be re-used for
	 * subsequent receives.
	 */
	void (*recv_cb)(void *cb_arg, struct mlx5_fpga_dma_buf *buf);
	void *cb_arg;
};

/**
 * mlx5_fpga_client_register() - Register a client driver
 * @client: The properties of the client driver
 *
 * Should be called from a client driver's module init routine.
 * Note: The core will immediately callback create() and add() for any existing
 * devices in the system, as well as new ones added later on.
 */
void mlx5_fpga_client_register(struct mlx5_fpga_client *client);
/**
 * mlx5_fpga_client_unregister() - Unregister a client driver
 * @client: The client driver to unregister
 *
 * Should be called from a client driver's module exit routine.
 * Note: The core will immediately callback delete() and destroy() for any
 * created/added devices in the system, to clean up their state.
 */
void mlx5_fpga_client_unregister(struct mlx5_fpga_client *client);

/**
 * mlx5_fpga_device_reload() - Force the FPGA to reload its synthesis from flash
 * @fdev: The FPGA device
 * @image: Which flash image to load
 *
 * This routine attempts graceful teardown of all device resources before
 * loading. This includes a callback to client driver delete().
 * Calls client driver add() once device is operational again.
 * Blocks until the new synthesis is loaded, and the device is fully
 * initialized.
 *
 * Return: 0 if successful, or a negative error value otherwise
 */
int mlx5_fpga_device_reload(struct mlx5_fpga_device *fdev,
			    enum mlx5_fpga_image image);

/**
 * mlx5_fpga_flash_select() - Select the current active flash
 * @fdev: The FPGA device
 * @image: Which flash image will be active
 *
 * This routine selects the active flash by programming the relevant MUX.
 * Useful prior to burning a new image on flash.
 * This setting is volatile and is reset upon reboot or power-cycle
 *
 * Return: 0 if successful, or a negative error value otherwise
 */
int mlx5_fpga_flash_select(struct mlx5_fpga_device *fdev,
			   enum mlx5_fpga_image image);

/**
 * mlx5_fpga_sbu_conn_create() - Initialize a new FPGA SBU connection
 * @fdev: The FPGA device
 * @attr: Attributes of the new connection
 *
 * Sets up a new FPGA SBU connection with the specified attributes.
 * The receive callback function may be called for incoming messages even
 * before this function returns.
 *
 * The caller must eventually destroy the connection by calling
 * mlx5_fpga_sbu_conn_destroy.
 *
 * Return: A new connection, or ERR_PTR() error value otherwise.
 */
struct mlx5_fpga_conn *
mlx5_fpga_sbu_conn_create(struct mlx5_fpga_device *fdev,
			  struct mlx5_fpga_conn_attr *attr);

/**
 * mlx5_fpga_sbu_conn_destroy() - Destroy an FPGA SBU connection
 * @conn: The FPGA SBU connection to destroy
 *
 * Cleans up an FPGA SBU connection which was previously created with
 * mlx5_fpga_sbu_conn_create.
 */
void mlx5_fpga_sbu_conn_destroy(struct mlx5_fpga_conn *conn);

/**
 * mlx5_fpga_sbu_conn_sendmsg() - Queue the transmission of a packet
 * @fdev: An FPGA SBU connection
 * @buf: The packet buffer
 *
 * Queues a packet for transmission over an FPGA SBU connection.
 * The buffer should not be modified or freed until completion.
 * Upon completion, the buf's complete() callback is invoked, indicating the
 * success or error status of the transmission.
 *
 * Return: 0 if successful, or an error value otherwise.
 */
int mlx5_fpga_sbu_conn_sendmsg(struct mlx5_fpga_conn *conn,
			       struct mlx5_fpga_dma_buf *buf);

/**
 * mlx5_fpga_mem_read() - Read from FPGA memory address space
 * @fdev: The FPGA device
 * @size: Size of chunk to read, in bytes
 * @addr: Starting address to read from, in FPGA address space
 * @buf: Buffer to read into
 * @access_type: Method for reading
 *
 * Reads from the specified address into the specified buffer.
 * The address may point to configuration space or to DDR.
 * Large reads may be performed internally as several non-atomic operations.
 * This function may sleep, so should not be called from atomic contexts.
 *
 * Return: 0 if successful, or an error value otherwise.
 */
int mlx5_fpga_mem_read(struct mlx5_fpga_device *fdev, size_t size, u64 addr,
		       void *buf, enum mlx5_fpga_access_type access_type);

/**
 * mlx5_fpga_mem_write() - Write to FPGA memory address space
 * @fdev: The FPGA device
 * @size: Size of chunk to write, in bytes
 * @addr: Starting address to write to, in FPGA address space
 * @buf: Buffer which contains data to write
 * @access_type: Method for writing
 *
 * Writes the specified buffer data to FPGA memory at the specified address.
 * The address may point to configuration space or to DDR.
 * Large writes may be performed internally as several non-atomic operations.
 * This function may sleep, so should not be called from atomic contexts.
 *
 * Return: 0 if successful, or an error value otherwise.
 */
int mlx5_fpga_mem_write(struct mlx5_fpga_device *fdev, size_t size, u64 addr,
			void *buf, enum mlx5_fpga_access_type access_type);

/**
 * mlx5_fpga_get_sbu_caps() - Read the SBU capabilities
 * @fdev: The FPGA device
 * @size: Size of the buffer to read into
 * @buf: Buffer to read the capabilities into
 *
 * Reads the FPGA SBU capabilities into the specified buffer.
 * The format of the capabilities buffer is SBU-dependent.
 *
 * Return: 0 if successful
 *         -EINVAL if the buffer is not large enough to contain SBU caps
 *         or any other error value otherwise.
 */
int mlx5_fpga_get_sbu_caps(struct mlx5_fpga_device *fdev, int size, void *buf);

/**
 * mlx5_fpga_ddr_size_get() - Retrieve the size of FPGA DDR
 * @fdev: The FPGA device
 *
 * Return: Size of DDR avaailable for FPGA, in bytes
 */
u64 mlx5_fpga_ddr_size_get(struct mlx5_fpga_device *fdev);

/**
 * mlx5_fpga_ddr_base_get() - Retrieve the base address of FPGA DDR
 * @fdev: The FPGA device
 *
 * Return: Base address of DDR in FPGA address space
 */
u64 mlx5_fpga_ddr_base_get(struct mlx5_fpga_device *fdev);

/**
 * mlx5_fpga_client_data_set() - Attach client-defined private value to a device
 * @fdev: The FPGA device
 * @client: The client driver
 * @data: Opaque private value
 *
 * Client driver may use the private value for storing device-specific
 * state and configuration information, and may retrieve it with a call to
 * mlx5_fpga_client_data_get().
 */
void mlx5_fpga_client_data_set(struct mlx5_fpga_device *fdev,
			       struct mlx5_fpga_client *client,
			       void *data);

/**
 * mlx5_fpga_client_data_get() - Retrieve client-defined private value
 * @fdev: The FPGA device
 * @client: The client driver
 *
 * Client driver may use the private value for storing device-specific
 * state and configuration information by calling mlx5_fpga_client_data_set()
 *
 * Return: The private value
 */
void *mlx5_fpga_client_data_get(struct mlx5_fpga_device *fdev,
				struct mlx5_fpga_client *client);

/**
 * mlx5_fpga_device_query() - Query FPGA device state information
 * @fdev: The FPGA device
 * @query: Returns the device state
 *
 * Queries the device state and returns it in *query
 */
void mlx5_fpga_device_query(struct mlx5_fpga_device *fdev,
			    struct mlx5_fpga_query *query);

/**
 * mlx5_fpga_dev() - Retrieve FPGA device structure
 * @fdev: The FPGA device

 * Return: A pointer to a struct device, which may be used with dev_* logging,
 *         sysfs extensions, etc.
 */
struct device *mlx5_fpga_dev(struct mlx5_fpga_device *fdev);

/**
 * mlx5_fpga_temperature() - Retrieve FPGA sensor of temperature
 * @fdev: The FPGA device

 * Return: 0 if successful
 *         or any other error value otherwise.
 */
int mlx5_fpga_temperature(struct mlx5_fpga_device *fdev,
			  struct mlx5_fpga_temperature *temp);

/**
 * mlx5_fpga_connectdisconnect() - Connect/disconnect ConnectX to FPGA
 * @fdev: The FPGA device

 * Return: 0 if successful
 *         or any other error value otherwise.
 */
int mlx5_fpga_connectdisconnect(struct mlx5_fpga_device *fdev,
				enum mlx5_fpga_connect *connect);

/**
 * mlx5_fpga_get_cap() - Returns the FPGA cap mailbox from FW without parsing.
 * @fdev: The FPGA device
 * @fpga_caps: Is an array with a length of according to the size of
 *           mlx5_ifc_fpga_cap_bits/32
 *
 * Returns a copy of the FPGA caps mailbox and returns it in fpga_caps
 */
void mlx5_fpga_get_cap(struct mlx5_fpga_device *fdev, u32 *fpga_caps);

#endif /* MLX5_FPGA_SDK_H */
