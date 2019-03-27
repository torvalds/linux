# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/librdmacm
.PATH: ${_spath} ${_spath}/man

SHLIBDIR?=	/usr/lib
LIB=		rdmacm
SHLIB_MAJOR=	1
MK_PROFILE=	no
CFLAGS+=	-I${_spath}

SRCS= \
acm.c \
addrinfo.c \
cma.c \
indexer.c

MAN= \
rdma_accept.3 \
rdma_ack_cm_event.3 \
rdma_bind_addr.3 \
rdma_connect.3 \
rdma_create_ep.3 \
rdma_create_event_channel.3 \
rdma_create_id.3 \
rdma_create_qp.3 \
rdma_create_srq.3 \
rdma_dereg_mr.3 \
rdma_destroy_ep.3 \
rdma_destroy_event_channel.3 \
rdma_destroy_id.3 \
rdma_destroy_qp.3 \
rdma_destroy_srq.3 \
rdma_disconnect.3 \
rdma_event_str.3 \
rdma_free_devices.3 \
rdma_get_cm_event.3 \
rdma_get_devices.3 \
rdma_get_dst_port.3 \
rdma_get_local_addr.3 \
rdma_get_peer_addr.3 \
rdma_get_recv_comp.3 \
rdma_get_request.3 \
rdma_get_send_comp.3 \
rdma_get_src_port.3 \
rdma_getaddrinfo.3 \
rdma_join_multicast.3 \
rdma_leave_multicast.3 \
rdma_listen.3 \
rdma_migrate_id.3 \
rdma_notify.3 \
rdma_post_read.3 \
rdma_post_readv.3 \
rdma_post_recv.3 \
rdma_post_recvv.3 \
rdma_post_send.3 \
rdma_post_sendv.3 \
rdma_post_ud_send.3 \
rdma_post_write.3 \
rdma_post_writev.3 \
rdma_reg_msgs.3 \
rdma_reg_read.3 \
rdma_reg_write.3 \
rdma_reject.3 \
rdma_resolve_addr.3 \
rdma_resolve_route.3 \
rdma_set_option.3

LIBADD=		ibverbs
VERSION_MAP=	${_spath}/librdmacm.map

.include <bsd.lib.mk>
