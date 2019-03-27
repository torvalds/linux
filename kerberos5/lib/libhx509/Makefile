# $FreeBSD$

LIB=	hx509
LDFLAGS=	-Wl,--no-undefined
VERSION_MAP= ${KRB5DIR}/lib/hx509/version-script.map
LIBADD=	asn1 com_err crypto roken wind

MAN=	hx509.3 \
	hx509_ca.3 \
	hx509_cert.3 \
	hx509_cms.3 \
	hx509_crypto.3 \
	hx509_env.3 \
	hx509_error.3 \
	hx509_keyset.3 \
	hx509_lock.3 \
	hx509_misc.3 \
	hx509_name.3 \
	hx509_peer.3 \
	hx509_print.3 \
	hx509_query.3 \
	hx509_revoke.3 \
	hx509_verify.3

MLINKS=	hx509.3 hx509_context_free.3 \
	hx509.3 hx509_context_init.3 \
	hx509_ca.3 hx509_ca_sign.3 \
	hx509_ca.3 hx509_ca_sign_self.3 \
	hx509_ca.3 hx509_ca_tbs_add_crl_dp_uri.3 \
	hx509_ca.3 hx509_ca_tbs_add_eku.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_hostname.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_jid.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_ms_upn.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_otherName.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_pkinit.3 \
	hx509_ca.3 hx509_ca_tbs_add_san_rfc822name.3 \
	hx509_ca.3 hx509_ca_tbs_free.3 \
	hx509_ca.3 hx509_ca_tbs_init.3 \
	hx509_ca.3 hx509_ca_tbs_set_ca.3 \
	hx509_ca.3 hx509_ca_tbs_set_domaincontroller.3 \
	hx509_ca.3 hx509_ca_tbs_set_notAfter.3 \
	hx509_ca.3 hx509_ca_tbs_set_notAfter_lifetime.3 \
	hx509_ca.3 hx509_ca_tbs_set_notBefore.3 \
	hx509_ca.3 hx509_ca_tbs_set_proxy.3 \
	hx509_ca.3 hx509_ca_tbs_set_serialnumber.3 \
	hx509_ca.3 hx509_ca_tbs_set_spki.3 \
	hx509_ca.3 hx509_ca_tbs_set_subject.3 \
	hx509_ca.3 hx509_ca_tbs_set_template.3 \
	hx509_ca.3 hx509_ca_tbs_set_unique.3 \
	hx509_ca.3 hx509_ca_tbs_subject_expand.3 \
	hx509_ca.3 hx509_ca_tbs_template_units.3 \
	hx509_cert.3 hx509_cert_binary.3 \
	hx509_cert.3 hx509_cert_check_eku.3 \
	hx509_cert.3 hx509_cert_cmp.3 \
	hx509_cert.3 hx509_cert_find_subjectAltName_otherName.3 \
	hx509_cert.3 hx509_cert_free.3 \
	hx509_cert.3 hx509_cert_get_SPKI.3 \
	hx509_cert.3 hx509_cert_get_SPKI_AlgorithmIdentifier.3 \
	hx509_cert.3 hx509_cert_get_attribute.3 \
	hx509_cert.3 hx509_cert_get_base_subject.3 \
	hx509_cert.3 hx509_cert_get_friendly_name.3 \
	hx509_cert.3 hx509_cert_get_issuer.3 \
	hx509_cert.3 hx509_cert_get_issuer_unique_id.3 \
	hx509_cert.3 hx509_cert_get_notAfter.3 \
	hx509_cert.3 hx509_cert_get_notBefore.3 \
	hx509_cert.3 hx509_cert_get_serialnumber.3 \
	hx509_cert.3 hx509_cert_get_subject.3 \
	hx509_cert.3 hx509_cert_get_subject_unique_id.3 \
	hx509_cert.3 hx509_cert_init.3 \
	hx509_cert.3 hx509_cert_init_data.3 \
	hx509_cert.3 hx509_cert_ref.3 \
	hx509_cert.3 hx509_cert_set_friendly_name.3 \
	hx509_cert.3 hx509_print_cert.3 \
	hx509_cert.3 hx509_query_alloc.3 \
	hx509_cert.3 hx509_query_free.3 \
	hx509_cert.3 hx509_query_match_cmp_func.3 \
	hx509_cert.3 hx509_query_match_eku.3 \
	hx509_cert.3 hx509_query_match_friendly_name.3 \
	hx509_cert.3 hx509_query_match_issuer_serial.3 \
	hx509_cert.3 hx509_query_match_option.3 \
	hx509_cert.3 hx509_query_statistic_file.3 \
	hx509_cert.3 hx509_query_unparse_stats.3 \
	hx509_cert.3 hx509_verify_ctx_f_allow_default_trustanchors.3 \
	hx509_cert.3 hx509_verify_hostname.3 \
	hx509_cms.3 hx509_cms_create_signed_1.3 \
	hx509_cms.3 hx509_cms_envelope_1.3 \
	hx509_cms.3 hx509_cms_unenvelope.3 \
	hx509_cms.3 hx509_cms_unwrap_ContentInfo.3 \
	hx509_cms.3 hx509_cms_verify_signed.3 \
	hx509_cms.3 hx509_cms_wrap_ContentInfo.3 \
	hx509_crypto.3 hx509_verify_signature.3 \
	hx509_env.3 hx509_env_add.3 \
	hx509_env.3 hx509_env_add_binding.3 \
	hx509_env.3 hx509_env_find.3 \
	hx509_env.3 hx509_env_find_binding.3 \
	hx509_env.3 hx509_env_free.3 \
	hx509_env.3 hx509_env_lfind.3 \
	hx509_error.3 hx509_clear_error_string.3 \
	hx509_error.3 hx509_err.3 \
	hx509_error.3 hx509_free_error_string.3 \
	hx509_error.3 hx509_get_error_string.3 \
	hx509_error.3 hx509_set_error_string.3 \
	hx509_error.3 hx509_set_error_stringv.3 \
	hx509_keyset.3 hx509_certs_add.3 \
	hx509_keyset.3 hx509_certs_append.3 \
	hx509_keyset.3 hx509_certs_end_seq.3 \
	hx509_keyset.3 hx509_certs_filter.3 \
	hx509_keyset.3 hx509_certs_find.3 \
	hx509_keyset.3 hx509_certs_free.3 \
	hx509_keyset.3 hx509_certs_info.3 \
	hx509_keyset.3 hx509_certs_init.3 \
	hx509_keyset.3 hx509_certs_iter_f.3 \
	hx509_keyset.3 hx509_certs_merge.3 \
	hx509_keyset.3 hx509_certs_next_cert.3 \
	hx509_keyset.3 hx509_certs_start_seq.3 \
	hx509_keyset.3 hx509_certs_store.3 \
	hx509_keyset.3 hx509_ci_print_names.3 \
	hx509_keyset.3 hx509_get_one_cert.3 \
	hx509_misc.3 hx509_free_octet_string_list.3 \
	hx509_misc.3 hx509_xfree.3 \
	hx509_name.3 hx509_general_name_unparse.3 \
	hx509_name.3 hx509_name_binary.3 \
	hx509_name.3 hx509_name_cmp.3 \
	hx509_name.3 hx509_name_copy.3 \
	hx509_name.3 hx509_name_expand.3 \
	hx509_name.3 hx509_name_free.3 \
	hx509_name.3 hx509_name_is_null_p.3 \
	hx509_name.3 hx509_name_to_Name.3 \
	hx509_name.3 hx509_name_to_string.3 \
	hx509_name.3 hx509_parse_name.3 \
	hx509_name.3 hx509_unparse_der_name.3 \
	hx509_peer.3 hx509_peer_info_add_cms_alg.3 \
	hx509_peer.3 hx509_peer_info_alloc.3 \
	hx509_peer.3 hx509_peer_info_free.3 \
	hx509_peer.3 hx509_peer_info_set_cert.3 \
	hx509_peer.3 hx509_peer_info_set_cms_algs.3 \
	hx509_print.3 hx509_bitstring_print.3 \
	hx509_print.3 hx509_cert_keyusage_print.3 \
	hx509_print.3 hx509_oid_print.3 \
	hx509_print.3 hx509_oid_sprint.3 \
	hx509_print.3 hx509_print_stdout.3 \
	hx509_print.3 hx509_validate_cert.3 \
	hx509_print.3 hx509_validate_ctx_add_flags.3 \
	hx509_print.3 hx509_validate_ctx_free.3 \
	hx509_print.3 hx509_validate_ctx_init.3 \
	hx509_print.3 hx509_validate_ctx_set_print.3 \
	hx509_revoke.3 hx509_ocsp_request.3 \
	hx509_revoke.3 hx509_revoke_add_crl.3 \
	hx509_revoke.3 hx509_revoke_add_ocsp.3 \
	hx509_revoke.3 hx509_revoke_free.3 \
	hx509_revoke.3 hx509_revoke_init.3 \
	hx509_revoke.3 hx509_revoke_ocsp_print.3 \
	hx509_revoke.3 hx509_revoke_verify.3 \
	hx509_verify.3 hx509_context_set_missing_revoke.3 \
	hx509_verify.3 hx509_crl_add_revoked_certs.3 \
	hx509_verify.3 hx509_crl_alloc.3 \
	hx509_verify.3 hx509_crl_free.3 \
	hx509_verify.3 hx509_crl_lifetime.3 \
	hx509_verify.3 hx509_crl_sign.3 \
	hx509_verify.3 hx509_ocsp_verify.3 \
	hx509_verify.3 hx509_verify_attach_anchors.3 \
	hx509_verify.3 hx509_verify_attach_revoke.3 \
	hx509_verify.3 hx509_verify_destroy_ctx.3 \
	hx509_verify.3 hx509_verify_init_ctx.3 \
	hx509_verify.3 hx509_verify_path.3 \
	hx509_verify.3 hx509_verify_set_max_depth.3 \
	hx509_verify.3 hx509_verify_set_proxy_certificate.3 \
	hx509_verify.3 hx509_verify_set_strict_rfc3280_verification.3 \
	hx509_verify.3 hx509_verify_set_time.3

INCS=	hx509-private.h \
	hx509-protos.h \
	hx509.h \
	hx509_err.h

SRCS=	ca.c \
	cert.c \
	cms.c \
	collector.c \
	crypto.c \
	doxygen.c \
	error.c \
	env.c \
	file.c \
	sel.c \
	sel-gram.y \
	sel-lex.l \
	keyset.c \
	ks_dir.c \
	ks_file.c \
	ks_mem.c \
	ks_null.c \
	ks_p11.c \
	ks_p12.c \
	ks_keychain.c \
	lock.c \
	name.c \
	peer.c \
	print.c \
	softp11.c \
	req.c \
	revoke.c

SRCS+=	hx509_err.c \
	hx509_err.h

SRCS+=	${GEN_OCSP:S/.x$/.c/:S/.hx$/.h/} \
	${GEN_PKCS10:S/.x$/.c/:S/.hx$/.h/}

CFLAGS+=-I${KRB5DIR}/lib/hx509
CFLAGS+=-I${KRB5DIR}/lib/hx509/ref
CFLAGS+=-I${KRB5DIR}/lib/asn1
CFLAGS+=-I${KRB5DIR}/lib/wind
CFLAGS+=-I${KRB5DIR}/lib/roken
CFLAGS+=-I${SRCTOP}/contrib/com_err
CFLAGS+=-I.

GEN_OCSP= \
	asn1_OCSPBasicOCSPResponse.x \
	asn1_OCSPCertID.x \
	asn1_OCSPCertStatus.x \
	asn1_OCSPInnerRequest.x \
	asn1_OCSPKeyHash.x \
	asn1_OCSPRequest.x \
	asn1_OCSPResponderID.x \
	asn1_OCSPResponse.x \
	asn1_OCSPResponseBytes.x \
	asn1_OCSPResponseData.x \
	asn1_OCSPResponseStatus.x \
	asn1_OCSPSignature.x \
	asn1_OCSPSingleResponse.x \
	asn1_OCSPTBSRequest.x \
	asn1_OCSPVersion.x \
	asn1_id_pkix_ocsp.x \
	asn1_id_pkix_ocsp_basic.x \
	asn1_id_pkix_ocsp_nonce.x \
	ocsp_asn1.hx \
	ocsp_asn1-priv.hx

GEN_PKCS10= \
	asn1_CertificationRequestInfo.x \
	asn1_CertificationRequest.x \
	pkcs10_asn1.hx \
	pkcs10_asn1-priv.hx

GEN_CRMF= \
	asn1_CRMFRDNSequence.x \
	asn1_CertReqMessages.x \
	asn1_CertReqMsg.x \
	asn1_CertRequest.x \
	asn1_CertTemplate.x \
	asn1_Controls.x \
	asn1_PBMParameter.x \
	asn1_PKMACValue.x \
	asn1_POPOPrivKey.x \
	asn1_POPOSigningKey.x \
	asn1_POPOSigningKeyInput.x \
	asn1_ProofOfPossession.x \
	asn1_SubsequentMessage.x \
	crmf_asn1.hx \
	crmf_asn1-priv.hx

GEN+=	${GEN_OCSP}
GEN+=	${GEN_PKCS10}
GEN+=	${GEN_CRMF}

CLEANFILES= ${GEN} ${GEN:S/.x$/.c/:S/.hx$/.h/} ocsp_asn1_files \
	pkcs10_asn1_files crmf_asn1_files ocsp_asn1-template.[ch]* \
	pkcs10_asn1-template.[ch]* crmf_asn1-template.[ch]*

INCS+=	ocsp_asn1.h pkcs10_asn1.h crmf_asn1.h

.ORDER: ${GEN_OCSP}
${GEN_OCSP:[2..-1]}: .NOMETA
${GEN_OCSP}: ocsp.asn1 ocsp.opt
	${ASN1_COMPILE} --option-file=${.ALLSRC:M*.opt} \
	 ${.ALLSRC:M*.asn1} ocsp_asn1

.ORDER: ${GEN_PKCS10}
${GEN_PKCS10:[2..-1]}: .NOMETA
${GEN_PKCS10}: pkcs10.asn1 pkcs10.opt
	${ASN1_COMPILE} --option-file=${.ALLSRC:M*.opt} \
	${.ALLSRC:M*.asn1} pkcs10_asn1

.ORDER: ${GEN_CRMF}
${GEN_CRMF:[2..-1]}: .NOMETA
${GEN_CRMF}: crmf.asn1
	${ASN1_COMPILE} ${.ALLSRC:M*.asn1} crmf_asn1

.SUFFIXES: .h .c .x .hx

.x.c:   
	${CP} ${.IMPSRC} ${.TARGET}

.hx.h:
	${CP} ${.IMPSRC} ${.TARGET}

.include <bsd.lib.mk>

.SUFFIXES: .h .c .x .hx

.PATH: ${KRB5DIR}/lib/hx509 ${KRB5DIR}/lib/asn1 ${KRB5DIR}/doc/doxyout/hx509/man/man3
