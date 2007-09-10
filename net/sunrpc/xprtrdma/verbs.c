/*
 * Placeholders for subsequent patches
 */

#include "xprt_rdma.h"

int rpcrdma_ia_open(struct rpcrdma_xprt *a, struct sockaddr *b, int c)
{ return EINVAL; }
void rpcrdma_ia_close(struct rpcrdma_ia *a) { }
int rpcrdma_ep_create(struct rpcrdma_ep *a, struct rpcrdma_ia *b,
struct rpcrdma_create_data_internal *c) { return EINVAL; }
int rpcrdma_ep_destroy(struct rpcrdma_ep *a, struct rpcrdma_ia *b)
{ return EINVAL; }
int rpcrdma_ep_connect(struct rpcrdma_ep *a, struct rpcrdma_ia *b)
{ return EINVAL; }
int rpcrdma_ep_disconnect(struct rpcrdma_ep *a, struct rpcrdma_ia *b)
{ return EINVAL; }
int rpcrdma_ep_post(struct rpcrdma_ia *a, struct rpcrdma_ep *b,
struct rpcrdma_req *c) { return EINVAL; }
int rpcrdma_ep_post_recv(struct rpcrdma_ia *a, struct rpcrdma_ep *b,
struct rpcrdma_rep *c) { return EINVAL; }
int rpcrdma_buffer_create(struct rpcrdma_buffer *a, struct rpcrdma_ep *b,
struct rpcrdma_ia *c, struct rpcrdma_create_data_internal *d) { return EINVAL; }
void rpcrdma_buffer_destroy(struct rpcrdma_buffer *a) { }
struct rpcrdma_req *rpcrdma_buffer_get(struct rpcrdma_buffer *a)
{ return NULL; }
void rpcrdma_buffer_put(struct rpcrdma_req *a) { }
void rpcrdma_recv_buffer_get(struct rpcrdma_req *a) { }
void rpcrdma_recv_buffer_put(struct rpcrdma_rep *a) { }
int rpcrdma_register_internal(struct rpcrdma_ia *a, void *b, int c,
struct ib_mr **d, struct ib_sge *e) { return EINVAL; }
int rpcrdma_deregister_internal(struct rpcrdma_ia *a, struct ib_mr *b,
struct ib_sge *c) { return EINVAL; }
int rpcrdma_register_external(struct rpcrdma_mr_seg *a, int b, int c,
struct rpcrdma_xprt *d) { return EINVAL; }
int rpcrdma_deregister_external(struct rpcrdma_mr_seg *a,
struct rpcrdma_xprt *b, void *c) { return EINVAL; }
