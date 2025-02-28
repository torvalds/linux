// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>

#include <cstdint>
#include <vector>

extern "C" {
#include "binder.h"
}

#include "binder.pb.h"

#include "src/libfuzzer/libfuzzer_macro.h"

#define NUM_CLIENT 3
#define MAX_HANDLE 10
#define MAX_READ_BUF_SIZE 1024
#define MAX_EXTRA_BUF_SIZE 256
#define MAX_BINDER_BUF_OBJ_SIZE 32

static void bwr_buf_clear_txnouts(std::vector<txnout_t *> *txnouts)
{
	txnout_t *txnout;
	while (!txnouts->empty()) {
		txnout = txnouts->back();
		txnouts->pop_back();
		if (txnout->fd != -1) {
			lkl_close_fd(txnout->fd);
		}
		free(txnout);
	}
}

static void binder_bwr_transaction_binder_object(txnout_t *txnout,
						 const BinderObject &fuzz_bo)
{
	switch (fuzz_bo.type_case()) {
	case BinderObject::kBinder:
		binder_transaction_put_object_binder(
			txnout, fuzz_bo.binder().ptr() % MAX_HANDLE);
		break;
	case BinderObject::kWeakBinder:
		binder_transaction_put_object_weak_binder(
			txnout, fuzz_bo.weak_binder().ptr() % MAX_HANDLE);
		break;
	case BinderObject::kHandle:
		binder_transaction_put_object_handle(
			txnout, fuzz_bo.handle() % MAX_HANDLE);
		break;
	case BinderObject::kWeakHandle:
		binder_transaction_put_object_weak_handle(
			txnout, fuzz_bo.weak_handle() % MAX_HANDLE);
		break;
	case BinderObject::kFd: {
		binder_transaction_put_object_fd(txnout);
	} break;
	case BinderObject::kFda: {
		binder_transaction_put_object_fda(
			txnout, fuzz_bo.fda().num_fds(), fuzz_bo.fda().parent(),
			fuzz_bo.fda().parent_offset());
	} break;
	case BinderObject::kPtr: {
		binder_transaction_put_object_ptr(
			txnout, fuzz_bo.ptr().has_parent_flag(),
			(void *)fuzz_bo.ptr().buffer().data(),
			fuzz_bo.ptr().buffer_size() % MAX_BINDER_BUF_OBJ_SIZE,
			fuzz_bo.ptr().parent(), fuzz_bo.ptr().parent_offset());
	} break;
	case BinderObject::TYPE_NOT_SET:
		break;
	}
}

static void *binder_bwr_transaction(bwr_buf_t *bb,
				    std::vector<txnout_t *> *txnouts,
				    bool reply, const Transaction &fuzz_tr)
{
	txnout_t *txnout;

	txnout = (txnout_t *)malloc(sizeof(*txnout));
	if (txnout == NULL)
		return NULL;

	txnout_init(txnout);
	txnouts->push_back(txnout);

	// Generate random binder objects
	for (const BinderObject &fuzz_bo : fuzz_tr.binder_objects()) {
		binder_bwr_transaction_binder_object(txnout, fuzz_bo);
	}

	unsigned int target_handle = fuzz_tr.target_handle() % MAX_HANDLE;
	unsigned int flags = 0;
	for (const auto &flag : fuzz_tr.flags()) {
		flags |= flag;
	}
	size_t extra_data_size = fuzz_tr.extra_data_size() % RANDOM_SIZE_RANGE;
	size_t extra_offsets_size =
		fuzz_tr.extra_offsets_size() % RANDOM_SIZE_RANGE;

	return bwr_buf_alloc_transaction(bb, reply, txnout, target_handle,
					 flags, extra_data_size,
					 extra_offsets_size);
}

static void *binder_bwr_transaction_sg(bwr_buf_t *bb,
				       std::vector<txnout_t *> *txnouts,
				       bool reply,
				       const TransactionSg &fuzz_tr_sg)
{
	txnout_t *txnout;

	txnout = (txnout_t *)malloc(sizeof(*txnout));
	if (txnout == NULL)
		return NULL;

	txnout_init(txnout);
	txnouts->push_back(txnout);

	// Generate random binder objects
	for (const BinderObject &fuzz_bo :
	     fuzz_tr_sg.transaction().binder_objects()) {
		binder_bwr_transaction_binder_object(txnout, fuzz_bo);
	}

	unsigned int target_handle =
		fuzz_tr_sg.transaction().target_handle() % MAX_HANDLE;
	unsigned int flags = 0;
	for (const auto &flag : fuzz_tr_sg.transaction().flags()) {
		flags |= flag;
	}
	size_t extra_data_size =
		fuzz_tr_sg.transaction().extra_data_size() % RANDOM_SIZE_RANGE;
	size_t extra_offsets_size =
		fuzz_tr_sg.transaction().extra_offsets_size() %
		RANDOM_SIZE_RANGE;

	size_t extra_buffers_size =
		fuzz_tr_sg.extra_buffers_size() % MAX_EXTRA_BUF_SIZE;

	return bwr_buf_alloc_transaction_sg(bb, reply, txnout, target_handle,
					    flags, extra_data_size,
					    extra_offsets_size,
					    extra_buffers_size);
}

static void binder_bwr(binder_ctx *ctx, bwr_buf_t *bb,
		       std::vector<txnout_t *> *txnouts,
		       const BinderWrite &fuzz_binder_write)
{
	for (const BinderCommand &command :
	     fuzz_binder_write.binder_commands()) {
		switch (command.bc_case()) {
		case BinderCommand::kAcquire:
			binder_bwr_acquire(bb, command.acquire() % MAX_HANDLE);
			break;
		case BinderCommand::kIncrefs:
			binder_bwr_increfs(bb, command.increfs() % MAX_HANDLE);
			break;
		case BinderCommand::kRelease:
			binder_bwr_release(bb, command.release() % MAX_HANDLE);
			break;
		case BinderCommand::kDecrefs:
			binder_bwr_decrefs(bb, command.decrefs() % MAX_HANDLE);
			break;
		case BinderCommand::kIncrefsDone:
			binder_bwr_increfs_done(
				bb, command.increfs_done().ptr() % MAX_HANDLE);
			break;
		case BinderCommand::kAcquireDone:
			binder_bwr_acquire_done(
				bb, command.acquire_done().ptr() % MAX_HANDLE);
			break;
		case BinderCommand::kTransaction: {
			binder_bwr_transaction(bb, txnouts, false,
					       command.transaction());
		} break;
		case BinderCommand::kReply: {
			binder_bwr_transaction(bb, txnouts, true,
					       command.transaction());
		} break;
		case BinderCommand::kTransactionSg: {
			binder_bwr_transaction_sg(bb, txnouts, false,
						  command.transaction_sg());
		} break;
		case BinderCommand::kReplySg: {
			binder_bwr_transaction_sg(bb, txnouts, true,
						  command.transaction_sg());
		} break;
		case BinderCommand::kFreeBuffer: {
			if (size_binder_buffers_queue(&ctx->buffers)) {
				binder_bwr_free_buffer(
					bb, front_binder_buffers_queue(
						    &ctx->buffers));
				pop_binder_buffers_queue(&ctx->buffers);
			} else {
				binder_bwr_free_buffer(bb, 0);
			}
		} break;
		case BinderCommand::kRequestDeathNotification:
			binder_bwr_request_death_notification(
				bb, command.request_death_notification().ptr() %
					    MAX_HANDLE);
			break;
		case BinderCommand::kClearDeathNotification:
			binder_bwr_clear_death_notification(
				bb, command.clear_death_notification().ptr() %
					    MAX_HANDLE);
			break;
		case BinderCommand::kDeadBinderDone:
			binder_bwr_dead_binder_done(bb,
						    command.dead_binder_done());
			break;
		case BinderCommand::kRegisterLooper:
			binder_bwr_register_looper(bb);
			break;
		case BinderCommand::kEnterLooper:
			binder_bwr_enter_looper(bb);
			break;
		case BinderCommand::kExitLooper:
			binder_bwr_exit_looper(bb);
			break;
		case BinderCommand::BC_NOT_SET:
			break;
		}
	}
}

static void perform_ioctl(binder_ctx *ctx, const Ioctl *ioctl)
{
	switch (ioctl->ioctl_case()) {
	case Ioctl::kBinderWrite: {
		bwr_buf_t bb;
		uint8_t bwr_buffer[256];
		bwr_buf_init(&bb, bwr_buffer, sizeof(bwr_buffer));
		std::vector<txnout_t *> txnouts;
		binder_bwr(ctx, &bb, &txnouts, ioctl->binder_write());
		binder_send(ctx, &bb);
		bwr_buf_clear_txnouts(&txnouts);
	} break;
	case Ioctl::kBinderRead: {
		binder_recv(ctx, MAX_READ_BUF_SIZE);
	} break;
	case Ioctl::kBinderThreadExit:
		binder_ioctl_thread_exit(ctx);
		break;
	case Ioctl::kBinderVersion:
		binder_ioctl_check_version(ctx);
		break;
	case Ioctl::kBinderGetNodeDebugInfo:
		binder_ioctl_get_node_debug_info(
			ctx,
			ioctl->binder_get_node_debug_info().ptr() % MAX_HANDLE);
		break;
	case Ioctl::kBinderGetNodeInfoForRef:
		binder_ioctl_get_node_info_for_ref(
			ctx, ioctl->binder_get_node_info_for_ref().handle() %
				     MAX_HANDLE);
		break;
	case Ioctl::kBinderEnableOnewaySpamDetection:
		binder_ioctl_enable_oneway_spam_detection(
			ctx, ioctl->binder_enable_oneway_spam_detection());
		break;
	case Ioctl::IOCTL_NOT_SET:
		break;
	}
}

extern "C" void __llvm_profile_initialize_file(void);
extern "C" int __llvm_profile_write_file(void);

void flush_coverage()
{
	__llvm_profile_write_file();
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	initialize_lkl();

	__llvm_profile_initialize_file();
	atexit(flush_coverage);
	return 0;
}

struct binder_thread_work {
	sem_t wait_ioctl_sem;
	sem_t done_ioctl_sem;
	const Ioctl *ioctl;
	bool done;
	int id;
	pthread_t tid;
};

void *ioctl_thread(void *arg)
{
	struct binder_thread_work *work = (binder_thread_work *)arg;

	if (work->id != 0)
		run_in_new_process();

	struct binder_ctx *ctx = binder_open();
	if (!ctx)
		return NULL;

	if (work->id == 0)
		assert(binder_ioctl_set_context_manager(ctx) == 0);

	while (!work->done) {
		assert(sem_wait(&work->wait_ioctl_sem) == 0);
		if (!work->done)
			perform_ioctl(ctx, work->ioctl);
		assert(sem_post(&work->done_ioctl_sem) == 0);
	}

	binder_close(ctx);

	return NULL;
}

DEFINE_PROTO_FUZZER(const Session &session)
{
	static int iter = 0;
	struct binder_thread_work binder_work[NUM_CLIENT];

	for (int i = 0; i < NUM_CLIENT; i++) {
		assert(sem_init(&binder_work[i].wait_ioctl_sem, 0, 0) == 0);
		assert(sem_init(&binder_work[i].done_ioctl_sem, 0, 0) == 0);
		binder_work[i].done = false;
		binder_work[i].id = i;

		assert(pthread_create(&binder_work[i].tid, NULL, ioctl_thread,
				      (void *)&binder_work[i]) == 0);
	}

	for (const Ioctl &ioctl : session.ioctls()) {
		// Pick the worker thread
		assert(ioctl.binder_client() < NUM_CLIENT);
		struct binder_thread_work *current_work =
			&binder_work[ioctl.binder_client()];

		// Send the ioctl for execution
		current_work->ioctl = &ioctl;
		assert(sem_post(&current_work->wait_ioctl_sem) == 0);

		// Wait once the ioctl is handled
		assert(sem_wait(&current_work->done_ioctl_sem) == 0);
	}

	for (int i = 0; i < NUM_CLIENT; i++) {
		binder_work[i].done = true;
		assert(sem_post(&binder_work[i].wait_ioctl_sem) == 0);
		pthread_join(binder_work[i].tid, NULL);
	}

	iter++;
	if (iter > 1000) {
		flush_coverage();
		iter = 0;
	}
}
