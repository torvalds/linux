// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <lkl.h>
#include <lkl_host.h>
#include <lkl/linux/android/binder.h>
#include <lkl/linux/mman.h>

#include "binder.h"

#ifndef LKL_MAP_FAILED
#define LKL_MAP_FAILED ((void *)-1)
#endif

#define ERR(fmt, ...) printf("ERROR: " fmt "\n", ##__VA_ARGS__)

#define BINDER_DEVICE "/dev/binder"
#define BINDER_VM_SIZE (1 * 1024 * 1024)

#define PAD_SIZE_UNSAFE(s) (((s) + 3) & ~3UL)

// Copied from drivers/android/binder_internal.h
struct binder_object {
	union {
		struct lkl_binder_object_header hdr;
		struct lkl_flat_binder_object fbo;
		struct lkl_binder_fd_object fdo;
		struct lkl_binder_buffer_object bbo;
		struct lkl_binder_fd_array_object fdao;
	};
};

void bwr_buf_init(struct bwr_buf_t *bb, uint8_t *ptr, size_t size)
{
	bb->data0 = bb->data = ptr;
	bb->data_avail = size;
}

void *bwr_buf_alloc(struct bwr_buf_t *bb, size_t size)
{
	void *ptr;

	if (size > bb->data_avail)
		return NULL;

	ptr = bb->data;
	bb->data += size;
	bb->data_avail -= size;

	return ptr;
}

static void bwr_buf_put_uint32(struct bwr_buf_t *bb, uint32_t value)
{
	uint32_t *ptr = (uint32_t *)bwr_buf_alloc(bb, sizeof(value));

	if (ptr)
		*ptr = value;
}

static void bwr_buf_put_uintptr(struct bwr_buf_t *bb, uintptr_t value)
{
	uintptr_t *ptr = (uintptr_t *)bwr_buf_alloc(bb, sizeof(value));

	if (ptr)
		*ptr = value;
}

static void bwr_buf_put_bc(struct bwr_buf_t *bb, uint32_t bc)
{
	bwr_buf_put_uint32(bb, bc);
}

static void bwr_buf_put_binder_req(struct bwr_buf_t *bb, uintptr_t binder_ptr,
				   uintptr_t cookie, uint32_t bc)
{
	bwr_buf_put_uint32(bb, bc);
	bwr_buf_put_uintptr(bb, binder_ptr);
	bwr_buf_put_uintptr(bb, cookie);
}

static void bwr_buf_put_handle_req(struct bwr_buf_t *bb, uint32_t handle, uint32_t bc)
{
	bwr_buf_put_uint32(bb, bc);
	bwr_buf_put_uint32(bb, handle);
}

static void bwr_buf_put_death_notif_req(struct bwr_buf_t *bb, uintptr_t binder_ptr,
					uintptr_t cookie, uint32_t bc)
{
	bwr_buf_put_uint32(bb, bc);
	bwr_buf_put_uint32(bb, binder_ptr);
	bwr_buf_put_uintptr(bb, cookie);
}

static void *bwr_buf_pop(struct bwr_buf_t *bb, size_t size)
{
	void *ptr;

	if (size > bb->data_avail)
		return NULL;

	ptr = bb->data;
	bb->data += size;
	bb->data_avail -= size;

	return ptr;
}

static uint32_t bwr_buf_pop_uint32(struct bwr_buf_t *bb)
{
	uint32_t *value;

	value = (uint32_t *)bwr_buf_pop(bb, sizeof(uint32_t));
	if (!value)
		return 0;

	return *value;
}

void txnout_init(struct txnout_t *txnout)
{
	txnout->data = txnout->data0;
	txnout->data_avail = sizeof(txnout->data0) - RANDOM_SIZE_RANGE;
	txnout->offs = txnout->offs0;
	txnout->offs_avail = (sizeof(txnout->offs0) - 8) / sizeof(size_t);
	txnout->extra_data = txnout->extra_data0;
	txnout->extra_data_avail =
		sizeof(txnout->extra_data0) - RANDOM_SIZE_RANGE;
	txnout->fd = -1;
}

static void *txnout_alloc(struct txnout_t *txnout, size_t size)
{
	void *ptr;

	if (size > txnout->data_avail || !txnout->offs_avail)
		return NULL;

	ptr = txnout->data;
	txnout->data += size;
	txnout->data_avail -= size;
	txnout->offs_avail--;
	*txnout->offs++ = ((uint8_t *)ptr) - ((uint8_t *)txnout->data0);

	return ptr;
}

static void *txnout_extra_buf_alloc(struct txnout_t *txnout, size_t size)
{
	void *ptr;

	if (size > txnout->extra_data_avail)
		return NULL;

	ptr = txnout->extra_data;
	txnout->extra_data += size;
	txnout->extra_data_avail -= size;

	return ptr;
}

struct txnin_t {
	uint8_t *data0;
	uint8_t *data;
	size_t data_avail;
};

static void txnin_init(struct txnin_t *txnin, void *tr)
{
	struct lkl_binder_transaction_data *tr_data =
		(struct lkl_binder_transaction_data *)tr;

	txnin->data0 = txnin->data = (uint8_t *)tr_data->data.ptr.buffer;
	txnin->data_avail = tr_data->data_size;
}

struct binder_ctx *binder_open(void)
{
	struct binder_ctx *ctx;

	ctx = (struct binder_ctx *)malloc(sizeof(struct binder_ctx));
	if (!ctx)
		return NULL;

	ctx->fd = lkl_sys_open(BINDER_DEVICE,
			       LKL_O_RDWR | LKL_O_CLOEXEC | LKL_O_NONBLOCK, 0);
	if (ctx->fd == -1) {
		ERR("Failed to open binder device");
		goto err_open;
	}

	ctx->epoll_fd = -1;
	ctx->map_size = BINDER_VM_SIZE;
	ctx->map_ptr = lkl_sys_mmap(NULL, BINDER_VM_SIZE, LKL_PROT_READ,
				    LKL_MAP_PRIVATE, ctx->fd, 0);
	if (ctx->map_ptr == LKL_MAP_FAILED) {
		ERR("Failed to mmap binder device");
		goto err_mmap;
	}

	init_binder_buffers_queue(&ctx->buffers);

	return ctx;
err_mmap:
	lkl_sys_close(ctx->fd);
err_open:
	free(ctx);
	return NULL;
}

void binder_close(struct binder_ctx *ctx)
{
	if (ctx) {
		lkl_sys_munmap((unsigned long)ctx->map_ptr, ctx->map_size);
		lkl_sys_close(ctx->fd);
		if (ctx->epoll_fd != -1)
			lkl_sys_close(ctx->epoll_fd);
		free(ctx);
	}
}

int binder_ioctl_set_context_manager(struct binder_ctx *ctx)
{
	int ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_SET_CONTEXT_MGR, 0);

	if (ret < 0)
		ERR("Failed to set context manager");

	return ret;
}

int binder_ioctl_write(struct binder_ctx *ctx, void *buffer, size_t size)
{
	struct lkl_binder_write_read bwr = {
		.write_size = size,
		.write_consumed = 0,
		.write_buffer = (lkl_binder_uintptr_t)buffer
	};

	int ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_WRITE_READ,
				(unsigned long)&bwr);

	if (ret < 0)
		return ret;

	return bwr.write_consumed;
}

int binder_ioctl_read(struct binder_ctx *ctx, void *buffer, size_t size,
		      size_t *read_consumed)
{
	struct lkl_binder_write_read bwr = {
		.read_size = size,
		.read_consumed = 0,
		.read_buffer = (lkl_binder_uintptr_t)buffer
	};

	int ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_WRITE_READ,
				(unsigned long)&bwr);

	if (ret == 0)
		*read_consumed = bwr.read_consumed;

	return ret;
}

int binder_ioctl_thread_exit(struct binder_ctx *ctx)
{
	int ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_THREAD_EXIT, 0);

	if (ret < 0)
		ERR("Failed to perform thread exit");

	return ret;
}

int binder_ioctl_check_version(struct binder_ctx *ctx)
{
	int ret;
	struct lkl_binder_version version = { 0 };

	ret = lkl_sys_ioctl(ctx->fd, LKL_BINDER_VERSION,
			    (unsigned long)&version);
	if (ret < 0) {
		return ret;
	} else if (version.protocol_version !=
		   LKL_BINDER_CURRENT_PROTOCOL_VERSION) {
		ERR("Binder version does not match: %u",
		    version.protocol_version);
		return -1;
	}

	return 0;
}

int binder_ioctl_get_node_debug_info(struct binder_ctx *ctx, uintptr_t ptr)
{
	struct lkl_binder_node_debug_info info = { .ptr = ptr };

	return lkl_sys_ioctl(ctx->fd, LKL_BINDER_GET_NODE_DEBUG_INFO,
			     (unsigned long)&info);
}

int binder_ioctl_get_node_info_for_ref(struct binder_ctx *ctx, uint32_t handle)
{
	struct lkl_binder_node_info_for_ref info = { .handle = handle };

	return lkl_sys_ioctl(ctx->fd, LKL_BINDER_GET_NODE_INFO_FOR_REF,
			     (unsigned long)&info);
}

int binder_ioctl_enable_oneway_spam_detection(struct binder_ctx *ctx, uint32_t e)
{
	uint32_t enable = e;

	return lkl_sys_ioctl(ctx->fd, LKL_BINDER_ENABLE_ONEWAY_SPAM_DETECTION,
			     (unsigned long)&enable);
}

static void binder_execute_cmds(struct binder_ctx *ctx, struct bwr_buf_t *bb)
{
	struct txnin_t txnin;
	uint32_t cmd;
	void *cmd_data;

	while (bb->data_avail > 0) {
		cmd = bwr_buf_pop_uint32(bb);
		cmd_data = bwr_buf_pop(bb, _LKL_IOC_SIZE(cmd));
		switch (cmd) {
		case LKL_BR_ACQUIRE:
		case LKL_BR_INCREFS: {
			uint8_t bwr_buffer[256];
			struct bwr_buf_t bb_out;
			struct lkl_binder_ptr_cookie *bpc =
				(struct lkl_binder_ptr_cookie *)cmd_data;
			bwr_buf_init(&bb_out, bwr_buffer, sizeof(bwr_buffer));
			bwr_buf_put_uint32(&bb_out,
					   cmd == LKL_BR_ACQUIRE ?
						   LKL_BC_ACQUIRE_DONE :
						   LKL_BC_INCREFS_DONE);
			bwr_buf_put_uintptr(&bb_out, bpc->ptr);
			bwr_buf_put_uintptr(&bb_out, bpc->cookie);
			binder_send(ctx, &bb_out);
		} break;
		case LKL_BR_DEAD_BINDER: {
			struct bwr_buf_t bb_out;
			uint8_t bwr_buffer[256];
			lkl_binder_uintptr_t cookie =
				*(lkl_binder_uintptr_t *)cmd_data;
			bwr_buf_init(&bb_out, bwr_buffer, sizeof(bwr_buffer));
			bwr_buf_put_uint32(&bb_out, LKL_BC_DEAD_BINDER_DONE);
			bwr_buf_put_uintptr(&bb_out, cookie);
			binder_send(ctx, &bb_out);
		} break;
		case LKL_BR_TRANSACTION:
		case LKL_BR_REPLY: {
			txnin_init(&txnin, cmd_data);
			push_binder_buffers_queue(&ctx->buffers,
						  (uintptr_t)txnin.data0);
		} break;
		default:
			break;
		}
	}
}

int binder_recv(struct binder_ctx *ctx, size_t size)
{
	int ret;
	struct bwr_buf_t bb;
	uint8_t read_buf[size];
	size_t read_consumed;

	ret = binder_ioctl_read(ctx, read_buf, sizeof(read_buf),
				&read_consumed);
	if (ret)
		return ret;
	bwr_buf_init(&bb, read_buf, read_consumed);
	binder_execute_cmds(ctx, &bb);

	return 0;
}

int binder_send(struct binder_ctx *ctx, struct bwr_buf_t *bb)
{
	return binder_ioctl_write(ctx, bb->data0,
				  PAD_SIZE_UNSAFE(bb->data - bb->data0));
}

void binder_bwr_acquire(struct bwr_buf_t *bb, uint32_t bc)
{
	bwr_buf_put_handle_req(bb, bc, LKL_BC_ACQUIRE);
}

void binder_bwr_increfs(struct bwr_buf_t *bb, uint32_t bc)
{
	bwr_buf_put_handle_req(bb, bc, LKL_BC_INCREFS);
}

void binder_bwr_release(struct bwr_buf_t *bb, uint32_t bc)
{
	bwr_buf_put_handle_req(bb, bc, LKL_BC_RELEASE);
}

void binder_bwr_decrefs(struct bwr_buf_t *bb, uint32_t bc)
{
	bwr_buf_put_handle_req(bb, bc, LKL_BC_DECREFS);
}

void binder_bwr_increfs_done(struct bwr_buf_t *bb, uintptr_t binder)
{
	bwr_buf_put_binder_req(bb, binder, 0, LKL_BC_INCREFS_DONE);
}

void binder_bwr_acquire_done(struct bwr_buf_t *bb, uintptr_t binder)
{
	bwr_buf_put_binder_req(bb, binder, 0, LKL_BC_ACQUIRE_DONE);
}

void binder_bwr_request_death_notification(struct bwr_buf_t *bb, uintptr_t binder)
{
	bwr_buf_put_death_notif_req(bb, binder, 0,
				    LKL_BC_REQUEST_DEATH_NOTIFICATION);
}

void binder_bwr_clear_death_notification(struct bwr_buf_t *bb, uintptr_t binder)
{
	bwr_buf_put_death_notif_req(bb, binder, 0,
				    LKL_BC_CLEAR_DEATH_NOTIFICATION);
}

void binder_bwr_register_looper(struct bwr_buf_t *bb)
{
	bwr_buf_put_bc(bb, LKL_BC_REGISTER_LOOPER);
}

void binder_bwr_enter_looper(struct bwr_buf_t *bb)
{
	bwr_buf_put_bc(bb, LKL_BC_ENTER_LOOPER);
}

void binder_bwr_exit_looper(struct bwr_buf_t *bb)
{
	bwr_buf_put_bc(bb, LKL_BC_EXIT_LOOPER);
}

void binder_bwr_dead_binder_done(struct bwr_buf_t *bb, uintptr_t cookie)
{
	bwr_buf_put_uint32(bb, LKL_BC_DEAD_BINDER_DONE);
	bwr_buf_put_uintptr(bb, cookie);
}

void binder_bwr_free_buffer(struct bwr_buf_t *bb, uintptr_t buffer_addr)
{
	bwr_buf_put_uint32(bb, LKL_BC_FREE_BUFFER);
	bwr_buf_put_uintptr(bb, buffer_addr);
}

void binder_transaction_put_object_binder(struct txnout_t *txnout, uintptr_t binder)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	struct lkl_flat_binder_object fbo = {
		.hdr = { .type = LKL_BINDER_TYPE_BINDER },
		.binder = binder,
		.cookie = 0,
	};

	bo->fbo = fbo;
}

void binder_transaction_put_object_weak_binder(struct txnout_t *txnout,
					       uintptr_t weak_binder)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	struct lkl_flat_binder_object fbo = {
		.hdr = { .type = LKL_BINDER_TYPE_WEAK_BINDER },
		.binder = weak_binder,
		.cookie = 0,
	};

	bo->fbo = fbo;
}

void binder_transaction_put_object_handle(struct txnout_t *txnout, unsigned int handle)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	struct lkl_flat_binder_object fbo = {
		.hdr = { .type = LKL_BINDER_TYPE_HANDLE },
		.handle = handle,
	};

	bo->fbo = fbo;
}

void binder_transaction_put_object_weak_handle(struct txnout_t *txnout,
					       unsigned int weak_handle)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	struct lkl_flat_binder_object fbo = {
		.hdr = { .type = LKL_BINDER_TYPE_WEAK_HANDLE },
		.handle = weak_handle,
	};

	bo->fbo = fbo;
}

void binder_transaction_put_object_fd(struct txnout_t *txnout)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	if (txnout->fd == -1) {
		txnout->fd =
			lkl_sys_open(".", LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	}

	struct lkl_binder_fd_object fdo = {
		.hdr = { .type = LKL_BINDER_TYPE_FD },
		.fd = (uint32_t)txnout->fd,
		.cookie = 0,
	};

	bo->fdo = fdo;
}

void binder_transaction_put_object_fda(struct txnout_t *txnout, size_t num_fds,
				       size_t parent, size_t parent_offset)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));
	if (!bo)
		return;

	// TODO(zifantan): Open files and add them to an array buffer
	struct lkl_binder_fd_array_object fdao = {
		.hdr = { .type = LKL_BINDER_TYPE_FDA },
		.num_fds = num_fds,
		.parent = parent,
		.parent_offset = parent_offset,
	};

	bo->fdao = fdao;
}

void binder_transaction_put_object_ptr(struct txnout_t *txnout,
				       unsigned int has_parent_flag, void *data,
				       size_t buffer_size, size_t parent,
				       size_t parent_offset)
{
	struct binder_object *bo =
		(struct binder_object *)txnout_alloc(txnout, sizeof(*bo));

	if (!bo)
		return;

	void *bbo_buf = txnout_extra_buf_alloc(txnout, buffer_size);

	if (bbo_buf)
		memcpy(bbo_buf, data, buffer_size);

	struct lkl_binder_buffer_object bbo = {
		.hdr = { .type = LKL_BINDER_TYPE_PTR },
		.flags = (uint32_t)(has_parent_flag ?
					    LKL_BINDER_BUFFER_FLAG_HAS_PARENT :
					    0),
		.buffer = (lkl_binder_size_t)bbo_buf,
		.length = (lkl_binder_size_t)buffer_size,
		.parent = parent,
		.parent_offset = parent_offset,
	};

	bo->bbo = bbo;
}

void *bwr_buf_alloc_transaction(struct bwr_buf_t *bb, int reply, struct txnout_t *txnout,
				unsigned int target_handle, unsigned int flags,
				size_t extra_data_size,
				size_t extra_offsets_size)
{
	bwr_buf_put_uint32(bb, reply ? LKL_BC_REPLY : LKL_BC_TRANSACTION);

	struct lkl_binder_transaction_data *tr_data =
		(struct lkl_binder_transaction_data *)bwr_buf_alloc(
			bb, sizeof(struct lkl_binder_transaction_data));

	if (tr_data != NULL) {
		tr_data->target.handle = target_handle;
		tr_data->flags = flags;

		tr_data->data_size = txnout->data - txnout->data0;
		tr_data->offsets_size =
			((uint8_t *)txnout->offs) - ((uint8_t *)txnout->offs0);
		tr_data->data.ptr.buffer = (lkl_binder_uintptr_t)txnout->data0;
		tr_data->data.ptr.offsets = (lkl_binder_uintptr_t)txnout->offs0;

		// Introduce unaligned data and offsets size
		tr_data->data_size += extra_data_size;
		tr_data->offsets_size += extra_offsets_size;
	}
	return tr_data;
}

void *bwr_buf_alloc_transaction_sg(struct bwr_buf_t *bb, int reply, struct txnout_t *txnout,
				   unsigned int target_handle,
				   unsigned int flags, size_t extra_data_size,
				   size_t extra_offsets_size,
				   size_t extra_buffers_size)
{
	bwr_buf_put_uint32(bb, reply ? LKL_BC_REPLY_SG : LKL_BC_TRANSACTION_SG);

	struct lkl_binder_transaction_data_sg *tr_data_sg =
		(struct lkl_binder_transaction_data_sg *)bwr_buf_alloc(
			bb, sizeof(struct lkl_binder_transaction_data_sg));

	if (tr_data_sg != NULL) {
		tr_data_sg->transaction_data.target.handle = target_handle;
		tr_data_sg->transaction_data.flags = flags;

		tr_data_sg->transaction_data.data_size =
			txnout->data - txnout->data0;
		tr_data_sg->transaction_data.offsets_size =
			((uint8_t *)txnout->offs) - ((uint8_t *)txnout->offs0);
		tr_data_sg->transaction_data.data.ptr.buffer =
			(lkl_binder_uintptr_t)txnout->data0;
		tr_data_sg->transaction_data.data.ptr.offsets =
			(lkl_binder_uintptr_t)txnout->offs0;

		// Introduce unaligned data and offsets size
		tr_data_sg->transaction_data.data_size += extra_data_size;
		tr_data_sg->transaction_data.offsets_size += extra_offsets_size;

		tr_data_sg->buffers_size = extra_buffers_size;
	}
	return tr_data_sg;
}

void init_binder_buffers_queue(struct binder_buffers_queue *q)
{
	q->size = q->front = 0;
}

int size_binder_buffers_queue(struct binder_buffers_queue *q)
{
	return q->size - q->front;
}

uintptr_t front_binder_buffers_queue(struct binder_buffers_queue *q)
{
	return q->_queue[q->front];
}

void pop_binder_buffers_queue(struct binder_buffers_queue *q)
{
	if (q->front < q->size)
		q->front++;
}

void push_binder_buffers_queue(struct binder_buffers_queue *q, uintptr_t val)
{
	assert(q->size < BUFFERS_QUEUE_SIZE);
	if (q->size < BUFFERS_QUEUE_SIZE)
		q->_queue[q->size++] = val;
}

static int check_binder_version(void)
{
	int ret;
	struct binder_ctx *ctx;

	ctx = binder_open();
	if (ctx == NULL)
		return -1;

	ret = binder_ioctl_check_version(ctx);

	binder_close(ctx);
	return ret;
}

void lkl_close_fd(int fd)
{
	lkl_sys_close(fd);
}

void run_in_new_process(void)
{
	assert(lkl_sys_new_thread_group_leader() == 0);
}

void initialize_lkl(void)
{
	int ret;

	assert(lkl_init(&lkl_host_ops) == 0);
	assert(lkl_start_kernel("mem=50M loglevel=8") == 0);

	assert(lkl_mount_fs("sysfs") == 0);
	assert(lkl_mount_fs("proc") == 0);

	ret = lkl_sys_mkdir("/dev", 0770);
	assert(ret == 0 || ret == -LKL_EEXIST);

	assert(lkl_sys_mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) == 0);

	assert(check_binder_version() == 0);
}
