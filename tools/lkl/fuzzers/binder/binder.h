/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BINDER_H_
#define BINDER_H_

#include <stdint.h>

#define RANDOM_SIZE_RANGE 8

#define BUFFERS_QUEUE_SIZE 256

struct bwr_buf_t {
	uint8_t *data0;
	uint8_t *data;
	size_t data_avail;
};

void bwr_buf_init(struct bwr_buf_t *bb, uint8_t *ptr, size_t size);

struct txnout_t {
	uint8_t data0[256];
	uint8_t *data;
	size_t data_avail;
	size_t offs0[256];
	size_t *offs;
	size_t offs_avail;
	uint8_t extra_data0[256];
	uint8_t *extra_data;
	size_t extra_data_avail;
	int fd;
};

void txnout_init(struct txnout_t *txnout);

struct binder_buffers_queue {
	uintptr_t _queue[BUFFERS_QUEUE_SIZE];
	int size;
	int front;
};

void init_binder_buffers_queue(struct binder_buffers_queue *q);
int size_binder_buffers_queue(struct binder_buffers_queue *q);
uintptr_t front_binder_buffers_queue(struct binder_buffers_queue *q);
void pop_binder_buffers_queue(struct binder_buffers_queue *q);
void push_binder_buffers_queue(struct binder_buffers_queue *q, uintptr_t val);

/*
 * Binder context
 * @fd - file description to a binder device
 * @epoll_fd - epoll for the binder fd
 * @map_ptr - pointer to mmaped memory
 * @map_size - mmapped memory size
 * @task - a pointer to the task_struct that opened the binder device
 * @buffers - buffers of incoming transactions
 */
struct binder_ctx {
	int fd;
	int epoll_fd;
	void *map_ptr;
	size_t map_size;
	void *task;
	struct binder_buffers_queue buffers;
};

/*
 * Binder open, close
 */
struct binder_ctx *binder_open(void);
void binder_close(struct binder_ctx *ctx);

/*
 * IOCTL
 */
int binder_ioctl_set_context_manager(struct binder_ctx *ctx);
int binder_ioctl_write(struct binder_ctx *ctx, void *buffer, size_t size);
int binder_ioctl_read(struct binder_ctx *ctx, void *buffer, size_t size,
		      size_t *read_consumed);
int binder_ioctl_thread_exit(struct binder_ctx *ctx);
int binder_ioctl_check_version(struct binder_ctx *ctx);
int binder_ioctl_get_node_debug_info(struct binder_ctx *ctx, uintptr_t ptr);
int binder_ioctl_get_node_info_for_ref(struct binder_ctx *ctx, uint32_t handle);
int binder_ioctl_enable_oneway_spam_detection(struct binder_ctx *ctx, uint32_t e);

void *bwr_buf_alloc_transaction(struct bwr_buf_t *bb, int reply, struct txnout_t *txnout,
				unsigned int target_handle, unsigned int flags,
				size_t extra_data_size,
				size_t extra_offsets_size);
void *bwr_buf_alloc_transaction_sg(struct bwr_buf_t *bb, int reply, struct txnout_t *txnout,
				   unsigned int target_handle,
				   unsigned int flags, size_t extra_data_size,
				   size_t extra_offsets_size,
				   size_t extra_buffers_size);

void binder_bwr_free_buffer(struct bwr_buf_t *bb, uintptr_t buffer_addr);

void binder_bwr_dead_binder_done(struct bwr_buf_t *bb, uintptr_t cookie);

int binder_recv(struct binder_ctx *ctx, size_t size);
int binder_send(struct binder_ctx *ctx, struct bwr_buf_t *bb);

void binder_transaction_put_object_binder(struct txnout_t *txnout, uintptr_t binder);
void binder_transaction_put_object_weak_binder(struct txnout_t *txnout,
					       uintptr_t weak_binder);
void binder_transaction_put_object_handle(struct txnout_t *txnout,
					  unsigned int handle);
void binder_transaction_put_object_weak_handle(struct txnout_t *txnout,
					       unsigned int weak_handle);
void binder_transaction_put_object_fd(struct txnout_t *txnout);
void binder_transaction_put_object_fda(struct txnout_t *txnout, size_t num_fds,
				       size_t parent, size_t parent_offset);
void binder_transaction_put_object_ptr(struct txnout_t *txnout,
				       unsigned int has_parent_flag,
				       void *bbo_buf, size_t buffer_size,
				       size_t parent, size_t parent_offset);

void binder_bwr_acquire(struct bwr_buf_t *bb, uint32_t bc);
void binder_bwr_increfs(struct bwr_buf_t *bb, uint32_t bc);
void binder_bwr_release(struct bwr_buf_t *bb, uint32_t bc);
void binder_bwr_decrefs(struct bwr_buf_t *bb, uint32_t bc);
void binder_bwr_increfs_done(struct bwr_buf_t *bb, uintptr_t binder);
void binder_bwr_acquire_done(struct bwr_buf_t *bb, uintptr_t binder);
void binder_bwr_request_death_notification(struct bwr_buf_t *bb, uintptr_t binder);
void binder_bwr_clear_death_notification(struct bwr_buf_t *bb, uintptr_t binder);
void binder_bwr_register_looper(struct bwr_buf_t *bb);
void binder_bwr_enter_looper(struct bwr_buf_t *bb);
void binder_bwr_exit_looper(struct bwr_buf_t *bb);

void initialize_lkl(void);

void lkl_close_fd(int fd);

void run_in_new_process(void);

#endif // BINDER_H_
