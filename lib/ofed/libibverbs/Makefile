# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/libibverbs
.PATH: ${_spath} ${_spath}/man

LIB=		ibverbs
SHLIB_MAJOR=	1
SHLIBDIR?=	/lib
MK_PROFILE=	no
VERSION_MAP=	${_spath}/libibverbs.map

CFLAGS+= -I${_spath}

SRCS= \
cmd.c \
compat-1_0.c \
device.c \
enum_strs.c \
init.c \
marshall.c \
memory.c \
sysfs.c \
verbs.c

MAN= \
ibv_alloc_mw.3 \
ibv_alloc_pd.3 \
ibv_attach_mcast.3 \
ibv_bind_mw.3 \
ibv_create_ah.3 \
ibv_create_ah_from_wc.3 \
ibv_create_comp_channel.3 \
ibv_create_cq.3 \
ibv_create_cq_ex.3 \
ibv_create_flow.3 \
ibv_create_qp.3 \
ibv_create_qp_ex.3 \
ibv_create_rwq_ind_table.3 \
ibv_create_srq.3 \
ibv_create_srq_ex.3 \
ibv_create_wq.3 \
ibv_event_type_str.3 \
ibv_fork_init.3 \
ibv_get_async_event.3 \
ibv_get_cq_event.3 \
ibv_get_device_guid.3 \
ibv_get_device_list.3 \
ibv_get_device_name.3 \
ibv_get_srq_num.3 \
ibv_inc_rkey.3 \
ibv_modify_qp.3 \
ibv_modify_srq.3 \
ibv_modify_wq.3 \
ibv_open_device.3 \
ibv_open_qp.3 \
ibv_open_xrcd.3 \
ibv_poll_cq.3 \
ibv_post_recv.3 \
ibv_post_send.3 \
ibv_post_srq_recv.3 \
ibv_query_device.3 \
ibv_query_device_ex.3 \
ibv_query_gid.3 \
ibv_query_pkey.3 \
ibv_query_port.3 \
ibv_query_qp.3 \
ibv_query_rt_values_ex.3 \
ibv_query_srq.3 \
ibv_rate_to_mbps.3 \
ibv_rate_to_mult.3 \
ibv_reg_mr.3 \
ibv_req_notify_cq.3 \
ibv_rereg_mr.3 \
ibv_resize_cq.3

.include <bsd.lib.mk>
