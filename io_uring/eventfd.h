
struct io_ring_ctx;
int io_eventfd_register(struct io_ring_ctx *ctx, void __user *arg,
			unsigned int eventfd_async);
int io_eventfd_unregister(struct io_ring_ctx *ctx);

void io_eventfd_signal(struct io_ring_ctx *ctx, bool cqe_event);
