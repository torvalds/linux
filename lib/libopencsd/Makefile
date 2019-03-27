# $FreeBSD$

PACKAGE=lib${LIB}
SHLIBDIR?=	/lib

.include <src.opts.mk>

OPENCSDSRC=	${SRCTOP}/contrib/opencsd

.PATH:	${OPENCSDSRC}/decoder/source/etmv4/		\
	${OPENCSDSRC}/decoder/source/etmv3/		\
	${OPENCSDSRC}/decoder/source/pkt_printers/	\
	${OPENCSDSRC}/decoder/source/mem_acc/		\
	${OPENCSDSRC}/decoder/source/i_dec/		\
	${OPENCSDSRC}/decoder/source/c_api/		\
	${OPENCSDSRC}/decoder/source/ptm/		\
	${OPENCSDSRC}/decoder/source/stm/		\
	${OPENCSDSRC}/decoder/source/			\
	${OPENCSDSRC}/decoder/include/opencsd/etmv4/	\
	${OPENCSDSRC}/decoder/include/opencsd/etmv3/	\
	${OPENCSDSRC}/decoder/include/opencsd/stm/	\
	${OPENCSDSRC}/decoder/include/opencsd/ptm/	\
	${OPENCSDSRC}/decoder/include/opencsd/c_api/	\
	${OPENCSDSRC}/decoder/include/opencsd/		\
	${OPENCSDSRC}/decoder/include

LIB=	opencsd
SHLIB_MAJOR=0

# ETMv3
SRCS=	\
	trc_cmp_cfg_etmv3.cpp		\
	trc_pkt_decode_etmv3.cpp	\
	trc_pkt_elem_etmv3.cpp		\
	trc_pkt_proc_etmv3.cpp		\
	trc_pkt_proc_etmv3_impl.cpp

# ETMv4
SRCS+=	\
	trc_cmp_cfg_etmv4.cpp			\
	trc_etmv4_stack_elem.cpp		\
	trc_pkt_decode_etmv4i.cpp		\
	trc_pkt_elem_etmv4d.cpp			\
	trc_pkt_elem_etmv4i.cpp			\
	trc_pkt_proc_etmv4.cpp			\
	trc_pkt_proc_etmv4i_impl.cpp

# PKT_PRINTERS
SRCS+=	\
	raw_frame_printer.cpp			\
	trc_print_fact.cpp

# PTM
SRCS+=	\
	trc_cmp_cfg_ptm.cpp			\
	trc_pkt_decode_ptm.cpp			\
	trc_pkt_elem_ptm.cpp			\
	trc_pkt_proc_ptm.cpp

# STM
SRCS+=	\
	trc_pkt_decode_stm.cpp			\
	trc_pkt_elem_stm.cpp			\
	trc_pkt_proc_stm.cpp

# C_API
SRCS+=	\
	ocsd_c_api_custom_obj.cpp		\
	ocsd_c_api.cpp

# SRC
SRCS+=	\
	ocsd_code_follower.cpp			\
	ocsd_dcd_tree.cpp			\
	ocsd_error.cpp				\
	ocsd_error_logger.cpp			\
	ocsd_gen_elem_list.cpp			\
	ocsd_lib_dcd_register.cpp		\
	ocsd_msg_logger.cpp			\
	ocsd_version.cpp			\
	trc_component.cpp			\
	trc_core_arch_map.cpp			\
	trc_frame_deformatter.cpp		\
	trc_gen_elem.cpp			\
	trc_printable_elem.cpp			\
	trc_ret_stack.cpp

# MEM_ACC
SRCS+=	\
	trc_mem_acc_base.cpp			\
	trc_mem_acc_cb.cpp			\
	trc_mem_acc_mapper.cpp			\
	trc_mem_acc_bufptr.cpp			\
	trc_mem_acc_file.cpp

# I_DEC
SRCS+=	\
	trc_i_decode.cpp			\
	trc_idec_arminst.cpp

CFLAGS+=	\
	-I${OPENCSDSRC}/decoder/include/	\
	-I${.CURDIR}

INCS=	\
	ocsd_if_types.h			\
	trc_gen_elem_types.h		\
	trc_pkt_types.h

INCSDIR=${INCLUDEDIR}/opencsd

APIINCS=	\
	ocsd_c_api_cust_fact.h		\
	ocsd_c_api_cust_impl.h		\
	ocsd_c_api_custom.h		\
	ocsd_c_api_types.h		\
	opencsd_c_api.h

APIINCSDIR=${INCLUDEDIR}/opencsd/c_api/

ETMV4INCS=	\
	etmv4_decoder.h			\
	trc_cmp_cfg_etmv4.h		\
	trc_dcd_mngr_etmv4i.h		\
	trc_etmv4_stack_elem.h		\
	trc_pkt_decode_etmv4i.h		\
	trc_pkt_elem_etmv4d.h		\
	trc_pkt_elem_etmv4i.h		\
	trc_pkt_proc_etmv4.h		\
	trc_pkt_types_etmv4.h

ETMV4INCSDIR=${INCLUDEDIR}/opencsd/etmv4/

ETMV3INCS=	\
	etmv3_decoder.h			\
	trc_cmp_cfg_etmv3.h		\
	trc_dcd_mngr_etmv3.h		\
	trc_pkt_decode_etmv3.h		\
	trc_pkt_elem_etmv3.h		\
	trc_pkt_proc_etmv3.h		\
	trc_pkt_types_etmv3.h

ETMV3INCSDIR=${INCLUDEDIR}/opencsd/etmv3/

PTMINCS=	\
	ptm_decoder.h			\
	trc_cmp_cfg_ptm.h		\
	trc_dcd_mngr_ptm.h		\
	trc_pkt_decode_ptm.h		\
	trc_pkt_elem_ptm.h		\
	trc_pkt_proc_ptm.h		\
	trc_pkt_types_ptm.h

PTMINCSDIR=${INCLUDEDIR}/opencsd/ptm/

STMINCS=	\
	stm_decoder.h			\
	trc_cmp_cfg_stm.h		\
	trc_dcd_mngr_stm.h		\
	trc_pkt_decode_stm.h		\
	trc_pkt_elem_stm.h		\
	trc_pkt_proc_stm.h		\
	trc_pkt_types_stm.h

STMINCSDIR=${INCLUDEDIR}/opencsd/stm/

INCSGROUPS=INCS APIINCS ETMV3INCS ETMV4INCS PTMINCS STMINCS

LIBADD= cxxrt

WARNS?= 1

HAS_TESTS=

.include <bsd.lib.mk>
