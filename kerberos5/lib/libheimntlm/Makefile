# $FreeBSD$

LIB=	heimntlm
LDFLAGS=	-Wl,--no-undefined
LIBADD=	crypto com_err krb5 roken
SRCS=	ntlm.c ntlm_err.c ntlm_err.h
INCS=	heimntlm.h heimntlm-protos.h ntlm_err.h
CFLAGS+=-I${KRB5DIR}/lib/ntlm -I${KRB5DIR}/lib/roken \
	-I${SRCTOP}/contrib/com_err
VERSION_MAP= ${KRB5DIR}/lib/ntlm/version-script.map

MAN=	ntlm_buf.3 \
	ntlm_core.3 \
	ntlm_type1.3 \
	ntlm_type2.3 \
	ntlm_type3.3

MLINKS=	ntlm_core.3 heim_ntlm_build_ntlm1_master.3 \
	ntlm_core.3 heim_ntlm_build_ntlm2_master.3 \
	ntlm_core.3 heim_ntlm_calculate_lm2.3 \
	ntlm_core.3 heim_ntlm_calculate_ntlm1.3 \
	ntlm_core.3 heim_ntlm_calculate_ntlm2.3 \
	ntlm_core.3 heim_ntlm_decode_targetinfo.3 \
	ntlm_core.3 heim_ntlm_encode_targetinfo.3 \
	ntlm_core.3 heim_ntlm_encode_type1.3 \
	ntlm_core.3 heim_ntlm_encode_type2.3 \
	ntlm_core.3 heim_ntlm_encode_type3.3 \
	ntlm_core.3 heim_ntlm_free_buf.3 \
	ntlm_core.3 heim_ntlm_free_targetinfo.3 \
	ntlm_core.3 heim_ntlm_free_type1.3 \
	ntlm_core.3 heim_ntlm_free_type2.3 \
	ntlm_core.3 heim_ntlm_free_type3.3 \
	ntlm_core.3 heim_ntlm_keyex_unwrap.3 \
	ntlm_core.3 heim_ntlm_nt_key.3 \
	ntlm_core.3 heim_ntlm_ntlmv2_key.3 \
	ntlm_core.3 heim_ntlm_verify_ntlm2.3

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/ntlm ${KRB5DIR}/doc/doxyout/ntlm/man/man3
