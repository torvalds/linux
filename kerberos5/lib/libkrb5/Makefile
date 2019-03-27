# $FreeBSD$

LIB=	krb5
LDFLAGS=	-Wl,--no-undefined
VERSION_MAP= ${KRB5DIR}/lib/krb5/version-script.map
LIBADD=	asn1 com_err crypt crypto hx509 roken wind heimbase heimipcc

INCS=	heim_err.h \
	heim_threads.h \
	k524_err.h \
	krb5-protos.h \
	krb5-private.h \
	krb5-types.h \
	krb5.h \
	krb5_err.h \
	krb5_ccapi.h

KRB5INCS=	locate_plugin.h \
		send_to_kdc_plugin.h \
		ccache_plugin.h
KRB5INCSDIR=	${INCLUDEDIR}/krb5
INCSGROUPS=	INCS KRB5INCS

MAN=	krb5.3 \
	krb524_convert_creds_kdc.3 \
	krb5_425_conv_principal.3 \
	krb5_acl_match_file.3 \
	krb5_address.3 \
	krb5_aname_to_localname.3 \
	krb5_appdefault.3 \
	krb5_auth.3 \
	krb5_auth_context.3 \
	krb5_c_make_checksum.3 \
	krb5_ccache.3 \
	krb5_ccache_intro.3 \
	krb5_check_transited.3 \
	krb5_create_checksum.3 \
	krb5_credential.3 \
	krb5_creds.3 \
	krb5_crypto.3 \
	krb5_crypto_iov.3 \
	krb5_deprecated.3 \
	krb5_digest.3 \
	krb5_eai_to_heim_errno.3 \
	krb5_encrypt.3 \
	krb5_error.3 \
	krb5_fileformats.3 \
	krb5_find_padata.3 \
	krb5_generate_random_block.3 \
	krb5_get_all_client_addrs.3 \
	krb5_get_credentials.3 \
	krb5_get_creds.3 \
	krb5_get_forwarded_creds.3 \
	krb5_get_in_cred.3 \
	krb5_get_init_creds.3 \
	krb5_get_krbhst.3 \
	krb5_getportbyname.3 \
	krb5_init_context.3 \
	krb5_init_creds_intro.3 \
	krb5_introduction.3 \
	krb5_is_thread_safe.3 \
	krb5_keytab.3 \
	krb5_keytab_intro.3 \
	krb5_krbhst_init.3 \
	krb5_mk_req.3 \
	krb5_mk_safe.3 \
	krb5_openlog.3 \
	krb5_pac.3 \
	krb5_parse_name.3 \
	krb5_principal.3 \
	krb5_principal_intro.3 \
	krb5_rcache.3 \
	krb5_rd_error.3 \
	krb5_rd_safe.3 \
	krb5_set_default_realm.3 \
	krb5_set_password.3 \
	krb5_storage.3 \
	krb5_string_to_key.3 \
	krb5_support.3 \
	krb5_ticket.3 \
	krb5_timeofday.3 \
	krb5_v4compat.3 \
	krb5_verify_init_creds.3 \
	krb5_verify_user.3

MAN+=	krb5.conf.5
MAN+=	kerberos.8

MLINKS=	krb5.3 krb5_add_et_list.3 \
	krb5.3 krb5_add_extra_addresses.3 \
	krb5.3 krb5_add_ignore_addresses.3 \
	krb5.3 krb5_compare_creds.3 \
	krb5.3 krb5_copy_context.3 \
	krb5.3 krb5_copy_creds.3 \
	krb5.3 krb5_copy_creds_contents.3 \
	krb5.3 krb5_copy_data.3 \
	krb5.3 krb5_copy_host_realm.3 \
	krb5.3 krb5_copy_ticket.3 \
	krb5.3 krb5_creds_get_ticket_flags.3 \
	krb5.3 krb5_data_alloc.3 \
	krb5.3 krb5_data_cmp.3 \
	krb5.3 krb5_data_copy.3 \
	krb5.3 krb5_data_ct_cmp.3 \
	krb5.3 krb5_data_free.3 \
	krb5.3 krb5_data_realloc.3 \
	krb5.3 krb5_data_zero.3 \
	krb5.3 krb5_free_config_files.3 \
	krb5.3 krb5_free_cred_contents.3 \
	krb5.3 krb5_free_creds.3 \
	krb5.3 krb5_free_data.3 \
	krb5.3 krb5_free_ticket.3 \
	krb5.3 krb5_get_default_config_files.3 \
	krb5.3 krb5_get_default_in_tkt_etypes.3 \
	krb5.3 krb5_get_dns_canonicalize_hostname.3 \
	krb5.3 krb5_get_extra_addresses.3 \
	krb5.3 krb5_get_fcache_version.3 \
	krb5.3 krb5_get_ignore_addresses.3 \
	krb5.3 krb5_get_kdc_sec_offset.3 \
	krb5.3 krb5_get_max_time_skew.3 \
	krb5.3 krb5_get_use_admin_kdc.3 \
	krb5.3 krb5_init_ets.3 \
	krb5.3 krb5_kerberos_enctypes.3 \
	krb5.3 krb5_prepend_config_files_default.3 \
	krb5.3 krb5_set_config_files.3 \
	krb5.3 krb5_set_default_in_tkt_etypes.3 \
	krb5.3 krb5_set_dns_canonicalize_hostname.3 \
	krb5.3 krb5_set_extra_addresses.3 \
	krb5.3 krb5_set_fcache_version.3 \
	krb5.3 krb5_set_home_dir_access.3 \
	krb5.3 krb5_set_ignore_addresses.3 \
	krb5.3 krb5_set_kdc_sec_offset.3 \
	krb5.3 krb5_set_max_time_skew.3 \
	krb5.3 krb5_set_real_time.3 \
	krb5.3 krb5_set_use_admin_kdc.3 \
	krb5.3 krb5_ticket_get_authorization_data_type.3 \
	krb5.3 krb5_ticket_get_client.3 \
	krb5.3 krb5_ticket_get_endtime.3 \
	krb5.3 krb5_ticket_get_server.3 \
	krb5_425_conv_principal.3 krb5_425_conv_principal_ext.3 \
	krb5_425_conv_principal.3 krb5_524_conv_principal.3 \
	krb5_address.3 krb5_addr2sockaddr.3 \
	krb5_address.3 krb5_address_compare.3 \
	krb5_address.3 krb5_address_order.3 \
	krb5_address.3 krb5_address_prefixlen_boundary.3 \
	krb5_address.3 krb5_address_search.3 \
	krb5_address.3 krb5_anyaddr.3 \
	krb5_address.3 krb5_append_addresses.3 \
	krb5_address.3 krb5_copy_address.3 \
	krb5_address.3 krb5_copy_addresses.3 \
	krb5_address.3 krb5_free_address.3 \
	krb5_address.3 krb5_free_addresses.3 \
	krb5_address.3 krb5_h_addr2addr.3 \
	krb5_address.3 krb5_h_addr2sockaddr.3 \
	krb5_address.3 krb5_make_addrport.3 \
	krb5_address.3 krb5_max_sockaddr_size.3 \
	krb5_address.3 krb5_parse_address.3 \
	krb5_address.3 krb5_print_address.3 \
	krb5_address.3 krb5_sockaddr2address.3 \
	krb5_address.3 krb5_sockaddr2port.3 \
	krb5_address.3 krb5_sockaddr_uninteresting.3 \
	krb5_appdefault.3 krb5_appdefault_boolean.3 \
	krb5_appdefault.3 krb5_appdefault_string.3 \
	krb5_appdefault.3 krb5_appdefault_time.3 \
	krb5_auth.3 krb5_rd_req_ctx.3 \
	krb5_auth.3 krb5_rd_req_in_ctx_alloc.3 \
	krb5_auth.3 krb5_rd_req_in_set_keytab.3 \
	krb5_auth.3 krb5_rd_req_in_set_pac_check.3 \
	krb5_auth.3 krb5_rd_req_out_ctx_free.3 \
	krb5_auth.3 krb5_rd_req_out_get_server.3 \
	krb5_auth_context.3 krb5_auth_con_free.3 \
	krb5_auth_context.3 krb5_auth_con_genaddrs.3 \
	krb5_auth_context.3 krb5_auth_con_getaddrs.3 \
	krb5_auth_context.3 krb5_auth_con_getflags.3 \
	krb5_auth_context.3 krb5_auth_con_getkey.3 \
	krb5_auth_context.3 krb5_auth_con_getlocalsubkey.3 \
	krb5_auth_context.3 krb5_auth_con_getrcache.3 \
	krb5_auth_context.3 krb5_auth_con_getremotesubkey.3 \
	krb5_auth_context.3 krb5_auth_con_getuserkey.3 \
	krb5_auth_context.3 krb5_auth_con_init.3 \
	krb5_auth_context.3 krb5_auth_con_initivector.3 \
	krb5_auth_context.3 krb5_auth_con_setaddrs.3 \
	krb5_auth_context.3 krb5_auth_con_setaddrs_from_fd.3 \
	krb5_auth_context.3 krb5_auth_con_setflags.3 \
	krb5_auth_context.3 krb5_auth_con_setivector.3 \
	krb5_auth_context.3 krb5_auth_con_setkey.3 \
	krb5_auth_context.3 krb5_auth_con_setlocalsubkey.3 \
	krb5_auth_context.3 krb5_auth_con_setrcache.3 \
	krb5_auth_context.3 krb5_auth_con_setremotesubkey.3 \
	krb5_auth_context.3 krb5_auth_con_setuserkey.3 \
	krb5_auth_context.3 krb5_auth_getauthenticator.3 \
	krb5_auth_context.3 krb5_auth_getcksumtype.3 \
	krb5_auth_context.3 krb5_auth_getkeytype.3 \
	krb5_auth_context.3 krb5_auth_getlocalseqnumber.3 \
	krb5_auth_context.3 krb5_auth_setcksumtype.3 \
	krb5_auth_context.3 krb5_auth_setkeytype.3 \
	krb5_auth_context.3 krb5_auth_setlocalseqnumber.3 \
	krb5_auth_context.3 krb5_auth_setremoteseqnumber.3 \
	krb5_ccache.3 krb5_acc_ops.3 \
	krb5_ccache.3 krb5_cc_cache_end_seq_get.3 \
	krb5_ccache.3 krb5_cc_cache_get_first.3 \
	krb5_ccache.3 krb5_cc_cache_match.3 \
	krb5_ccache.3 krb5_cc_cache_next.3 \
	krb5_ccache.3 krb5_cc_clear_mcred.3 \
	krb5_ccache.3 krb5_cc_close.3 \
	krb5_ccache.3 krb5_cc_copy_cache.3 \
	krb5_ccache.3 krb5_cc_copy_creds.3 \
	krb5_ccache.3 krb5_cc_copy_match_f.3 \
	krb5_ccache.3 krb5_cc_default.3 \
	krb5_ccache.3 krb5_cc_default_name.3 \
	krb5_ccache.3 krb5_cc_destroy.3 \
	krb5_ccache.3 krb5_cc_end_seq_get.3 \
	krb5_ccache.3 krb5_cc_gen_new.3 \
	krb5_ccache.3 krb5_cc_get_config.3 \
	krb5_ccache.3 krb5_cc_get_flags.3 \
	krb5_ccache.3 krb5_cc_get_friendly_name.3 \
	krb5_ccache.3 krb5_cc_get_full_name.3 \
	krb5_ccache.3 krb5_cc_get_kdc_offset.3 \
	krb5_ccache.3 krb5_cc_get_lifetime.3 \
	krb5_ccache.3 krb5_cc_get_name.3 \
	krb5_ccache.3 krb5_cc_get_ops.3 \
	krb5_ccache.3 krb5_cc_get_prefix_ops.3 \
	krb5_ccache.3 krb5_cc_get_principal.3 \
	krb5_ccache.3 krb5_cc_get_type.3 \
	krb5_ccache.3 krb5_cc_get_version.3 \
	krb5_ccache.3 krb5_cc_initialize.3 \
	krb5_ccache.3 krb5_cc_last_change_time.3 \
	krb5_ccache.3 krb5_cc_move.3 \
	krb5_ccache.3 krb5_cc_new_unique.3 \
	krb5_ccache.3 krb5_cc_next_cred.3 \
	krb5_ccache.3 krb5_cc_register.3 \
	krb5_ccache.3 krb5_cc_remove_cred.3 \
	krb5_ccache.3 krb5_cc_resolve.3 \
	krb5_ccache.3 krb5_cc_retrieve_cred.3 \
	krb5_ccache.3 krb5_cc_set_config.3 \
	krb5_ccache.3 krb5_cc_set_default_name.3 \
	krb5_ccache.3 krb5_cc_set_flags.3 \
	krb5_ccache.3 krb5_cc_set_friendly_name.3 \
	krb5_ccache.3 krb5_cc_set_kdc_offset.3 \
	krb5_ccache.3 krb5_cc_start_seq_get.3 \
	krb5_ccache.3 krb5_cc_store_cred.3 \
	krb5_ccache.3 krb5_cc_support_switch.3 \
	krb5_ccache.3 krb5_cc_switch.3 \
	krb5_ccache.3 krb5_cccol_cursor_free.3 \
	krb5_ccache.3 krb5_cccol_cursor_new.3 \
	krb5_ccache.3 krb5_cccol_cursor_next.3 \
	krb5_ccache.3 krb5_cccol_last_change_time.3 \
	krb5_ccache.3 krb5_fcc_ops.3 \
	krb5_ccache.3 krb5_get_validated_creds.3 \
	krb5_ccache.3 krb5_is_config_principal.3 \
	krb5_ccache.3 krb5_mcc_ops.3 \
	krb5_create_checksum.3 krb5_checksum_is_collision_proof.3 \
	krb5_create_checksum.3 krb5_checksum_is_keyed.3 \
	krb5_create_checksum.3 krb5_checksumsize.3 \
	krb5_create_checksum.3 krb5_verify_checksum.3 \
	krb5_credential.3 krb5_fwd_tgt_creds.3 \
	krb5_credential.3 krb5_get_init_creds_keyblock.3 \
	krb5_credential.3 krb5_get_init_creds_keytab.3 \
	krb5_credential.3 krb5_get_init_creds_opt_alloc.3 \
	krb5_credential.3 krb5_get_init_creds_opt_free.3 \
	krb5_credential.3 krb5_get_init_creds_password.3 \
	krb5_credential.3 krb5_init_creds_free.3 \
	krb5_credential.3 krb5_init_creds_get.3 \
	krb5_credential.3 krb5_init_creds_get_error.3 \
	krb5_credential.3 krb5_init_creds_init.3 \
	krb5_credential.3 krb5_init_creds_set_keytab.3 \
	krb5_credential.3 krb5_init_creds_set_password.3 \
	krb5_credential.3 krb5_init_creds_set_service.3 \
	krb5_credential.3 krb5_init_creds_step.3 \
	krb5_crypto.3 krb5_allow_weak_crypto.3 \
	krb5_crypto.3 krb5_cksumtype_to_enctype.3 \
	krb5_crypto.3 krb5_copy_keyblock.3 \
	krb5_crypto.3 krb5_copy_keyblock_contents.3 \
	krb5_crypto.3 krb5_create_checksum_iov.3 \
	krb5_crypto.3 krb5_crypto_destroy.3 \
	krb5_crypto.3 krb5_crypto_fx_cf2.3 \
	krb5_crypto.3 krb5_crypto_getblocksize.3 \
	krb5_crypto.3 krb5_crypto_getconfoundersize.3 \
	krb5_crypto.3 krb5_crypto_getenctype.3 \
	krb5_crypto.3 krb5_crypto_getpadsize.3 \
	krb5_crypto.3 krb5_crypto_init.3 \
	krb5_crypto.3 krb5_decrypt_iov_ivec.3 \
	krb5_crypto.3 krb5_encrypt_iov_ivec.3 \
	krb5_crypto.3 krb5_enctype_disable.3 \
	krb5_crypto.3 krb5_enctype_enable.3 \
	krb5_crypto.3 krb5_enctype_valid.3 \
	krb5_crypto.3 krb5_free_keyblock.3 \
	krb5_crypto.3 krb5_free_keyblock_contents.3 \
	krb5_crypto.3 krb5_generate_subkey_extended.3 \
	krb5_crypto.3 krb5_keyblock_get_enctype.3 \
	krb5_crypto.3 krb5_keyblock_init.3 \
	krb5_crypto.3 krb5_keyblock_zero.3 \
	krb5_crypto.3 krb5_random_to_key.3 \
	krb5_crypto.3 krb5_verify_checksum_iov.3 \
	krb5_deprecated.3 krb5_auth_getremoteseqnumber.3 \
	krb5_deprecated.3 krb5_c_enctype_compare.3 \
	krb5_deprecated.3 krb5_change_password.3 \
	krb5_deprecated.3 krb5_clear_error_string.3 \
	krb5_deprecated.3 krb5_config_parse_string_multi.3 \
	krb5_deprecated.3 krb5_enctypes_compatible_keys.3 \
	krb5_deprecated.3 krb5_free_creds_contents.3 \
	krb5_deprecated.3 krb5_free_data_contents.3 \
	krb5_deprecated.3 krb5_free_error_string.3 \
	krb5_deprecated.3 krb5_free_unparsed_name.3 \
	krb5_deprecated.3 krb5_generate_subkey.3 \
	krb5_deprecated.3 krb5_get_cred_from_kdc.3 \
	krb5_deprecated.3 krb5_get_cred_from_kdc_opt.3 \
	krb5_deprecated.3 krb5_get_in_tkt_with_keytab.3 \
	krb5_deprecated.3 krb5_get_in_tkt_with_password.3 \
	krb5_deprecated.3 krb5_get_in_tkt_with_skey.3 \
	krb5_deprecated.3 krb5_get_init_creds_opt_get_error.3 \
	krb5_deprecated.3 krb5_get_init_creds_opt_init.3 \
	krb5_deprecated.3 krb5_keytab_key_proc.3 \
	krb5_deprecated.3 krb5_keytype_to_enctypes.3 \
	krb5_deprecated.3 krb5_keytype_to_enctypes_default.3 \
	krb5_deprecated.3 krb5_keytype_to_string.3 \
	krb5_deprecated.3 krb5_password_key_proc.3 \
	krb5_deprecated.3 krb5_princ_realm.3 \
	krb5_deprecated.3 krb5_princ_set_realm.3 \
	krb5_deprecated.3 krb5_set_error_string.3 \
	krb5_deprecated.3 krb5_string_to_keytype.3 \
	krb5_deprecated.3 krb5_vset_error_string.3 \
	krb5_digest.3 krb5_digest_probe.3 \
	krb5_encrypt.3 krb5_decrypt.3 \
	krb5_encrypt.3 krb5_decrypt_EncryptedData.3 \
	krb5_encrypt.3 krb5_encrypt_EncryptedData.3 \
	krb5_error.3 krb5_clear_error_message.3 \
	krb5_error.3 krb5_h_errno_to_heim_errno.3 \
	krb5_error.3 krb5_set_error_message.3 \
	krb5_error.3 krb5_vwarn.3 \
	krb5_get_all_client_addrs.3 krb5_get_all_server_addrs.3 \
	krb5_get_krbhst.3 krb5_free_krbhst.3 \
	krb5_get_krbhst.3 krb5_get_krb524hst.3 \
	krb5_get_krbhst.3 krb5_get_krb_admin_hst.3 \
	krb5_get_krbhst.3 krb5_get_krb_changepw_hst.3 \
	krb5_init_context.3 krb5_free_context.3 \
	krb5_keytab.3 krb5_kt_add_entry.3 \
	krb5_keytab.3 krb5_kt_close.3 \
	krb5_keytab.3 krb5_kt_compare.3 \
	krb5_keytab.3 krb5_kt_copy_entry_contents.3 \
	krb5_keytab.3 krb5_kt_default.3 \
	krb5_keytab.3 krb5_kt_default_modify_name.3 \
	krb5_keytab.3 krb5_kt_default_name.3 \
	krb5_keytab.3 krb5_kt_destroy.3 \
	krb5_keytab.3 krb5_kt_end_seq_get.3 \
	krb5_keytab.3 krb5_kt_free_entry.3 \
	krb5_keytab.3 krb5_kt_get_entry.3 \
	krb5_keytab.3 krb5_kt_get_full_name.3 \
	krb5_keytab.3 krb5_kt_get_name.3 \
	krb5_keytab.3 krb5_kt_get_type.3 \
	krb5_keytab.3 krb5_kt_have_content.3 \
	krb5_keytab.3 krb5_kt_next_entry.3 \
	krb5_keytab.3 krb5_kt_read_service_key.3 \
	krb5_keytab.3 krb5_kt_register.3 \
	krb5_keytab.3 krb5_kt_remove_entry.3 \
	krb5_keytab.3 krb5_kt_resolve.3 \
	krb5_keytab.3 krb5_kt_start_seq_get.3 \
	krb5_krbhst_init.3 krb5_krbhst_format_string.3 \
	krb5_krbhst_init.3 krb5_krbhst_free.3 \
	krb5_krbhst_init.3 krb5_krbhst_get_addrinfo.3 \
	krb5_krbhst_init.3 krb5_krbhst_next.3 \
	krb5_krbhst_init.3 krb5_krbhst_next_as_string.3 \
	krb5_krbhst_init.3 krb5_krbhst_reset.3 \
	krb5_openlog.3 krb5_addlog_dest.3 \
	krb5_openlog.3 krb5_addlog_func.3 \
	krb5_openlog.3 krb5_closelog.3 \
	krb5_openlog.3 krb5_initlog.3 \
	krb5_openlog.3 krb5_log.3 \
	krb5_openlog.3 krb5_log_msg.3 \
	krb5_openlog.3 krb5_vlog.3 \
	krb5_openlog.3 krb5_vlog_msg.3 \
	krb5_pac.3 krb5_pac_get_buffer.3 \
	krb5_pac.3 krb5_pac_verify.3 \
	krb5_principal.3 krb5_build_principal.3 \
	krb5_principal.3 krb5_build_principal_ext.3 \
	krb5_principal.3 krb5_build_principal_va.3 \
	krb5_principal.3 krb5_build_principal_va_ext.3 \
	krb5_principal.3 krb5_copy_principal.3 \
	krb5_principal.3 krb5_free_principal.3 \
	krb5_principal.3 krb5_get_default_principal.3 \
	krb5_principal.3 krb5_make_principal.3 \
	krb5_principal.3 krb5_parse_name_flags.3 \
	krb5_principal.3 krb5_parse_nametype.3 \
	krb5_principal.3 krb5_principal_compare.3 \
	krb5_principal.3 krb5_principal_compare_any_realm.3 \
	krb5_principal.3 krb5_principal_get_comp_string.3 \
	krb5_principal.3 krb5_principal_get_num_comp.3 \
	krb5_principal.3 krb5_principal_get_realm.3 \
	krb5_principal.3 krb5_principal_get_type.3 \
	krb5_principal.3 krb5_principal_is_krbtgt.3 \
	krb5_principal.3 krb5_principal_match.3 \
	krb5_principal.3 krb5_principal_set_realm.3 \
	krb5_principal.3 krb5_principal_set_type.3 \
	krb5_principal.3 krb5_realm_compare.3 \
	krb5_principal.3 krb5_sname_to_principal.3 \
	krb5_principal.3 krb5_sock_to_principal.3 \
	krb5_principal.3 krb5_unparse_name.3 \
	krb5_principal.3 krb5_unparse_name_fixed.3 \
	krb5_principal.3 krb5_unparse_name_fixed_flags.3 \
	krb5_principal.3 krb5_unparse_name_fixed_short.3 \
	krb5_principal.3 krb5_unparse_name_flags.3 \
	krb5_principal.3 krb5_unparse_name_short.3 \
	krb5_set_default_realm.3 krb5_free_host_realm.3 \
	krb5_set_default_realm.3 krb5_get_default_realm.3 \
	krb5_set_default_realm.3 krb5_get_default_realms.3 \
	krb5_set_default_realm.3 krb5_get_host_realm.3 \
	krb5_storage.3 krb5_ret_address.3 \
	krb5_storage.3 krb5_ret_addrs.3 \
	krb5_storage.3 krb5_ret_authdata.3 \
	krb5_storage.3 krb5_ret_creds.3 \
	krb5_storage.3 krb5_ret_creds_tag.3 \
	krb5_storage.3 krb5_ret_data.3 \
	krb5_storage.3 krb5_ret_int16.3 \
	krb5_storage.3 krb5_ret_int32.3 \
	krb5_storage.3 krb5_ret_int8.3 \
	krb5_storage.3 krb5_ret_keyblock.3 \
	krb5_storage.3 krb5_ret_principal.3 \
	krb5_storage.3 krb5_ret_string.3 \
	krb5_storage.3 krb5_ret_stringz.3 \
	krb5_storage.3 krb5_ret_times.3 \
	krb5_storage.3 krb5_ret_uint16.3 \
	krb5_storage.3 krb5_ret_uint32.3 \
	krb5_storage.3 krb5_ret_uint8.3 \
	krb5_storage.3 krb5_storage_clear_flags.3 \
	krb5_storage.3 krb5_storage_emem.3 \
	krb5_storage.3 krb5_storage_free.3 \
	krb5_storage.3 krb5_storage_from_data.3 \
	krb5_storage.3 krb5_storage_from_fd.3 \
	krb5_storage.3 krb5_storage_from_mem.3 \
	krb5_storage.3 krb5_storage_from_readonly_mem.3 \
	krb5_storage.3 krb5_storage_get_byteorder.3 \
	krb5_storage.3 krb5_storage_get_eof_code.3 \
	krb5_storage.3 krb5_storage_is_flags.3 \
	krb5_storage.3 krb5_storage_read.3 \
	krb5_storage.3 krb5_storage_seek.3 \
	krb5_storage.3 krb5_storage_set_byteorder.3 \
	krb5_storage.3 krb5_storage_set_eof_code.3 \
	krb5_storage.3 krb5_storage_set_flags.3 \
	krb5_storage.3 krb5_storage_set_max_alloc.3 \
	krb5_storage.3 krb5_storage_to_data.3 \
	krb5_storage.3 krb5_storage_truncate.3 \
	krb5_storage.3 krb5_storage_write.3 \
	krb5_storage.3 krb5_store_address.3 \
	krb5_storage.3 krb5_store_addrs.3 \
	krb5_storage.3 krb5_store_authdata.3 \
	krb5_storage.3 krb5_store_creds.3 \
	krb5_storage.3 krb5_store_creds_tag.3 \
	krb5_storage.3 krb5_store_data.3 \
	krb5_storage.3 krb5_store_int16.3 \
	krb5_storage.3 krb5_store_int32.3 \
	krb5_storage.3 krb5_store_int8.3 \
	krb5_storage.3 krb5_store_keyblock.3 \
	krb5_storage.3 krb5_store_principal.3 \
	krb5_storage.3 krb5_store_string.3 \
	krb5_storage.3 krb5_store_stringz.3 \
	krb5_storage.3 krb5_store_times.3 \
	krb5_storage.3 krb5_store_uint16.3 \
	krb5_storage.3 krb5_store_uint32.3 \
	krb5_storage.3 krb5_store_uint8.3 \
	krb5_support.3 krb5_acl_match_string.3 \
	krb5_support.3 krb5_config_file_free.3 \
	krb5_support.3 krb5_config_free_strings.3 \
	krb5_support.3 krb5_config_get_bool.3 \
	krb5_support.3 krb5_config_get_bool_default.3 \
	krb5_support.3 krb5_config_get_list.3 \
	krb5_support.3 krb5_config_get_string.3 \
	krb5_support.3 krb5_config_get_string_default.3 \
	krb5_support.3 krb5_config_get_strings.3 \
	krb5_support.3 krb5_config_get_time.3 \
	krb5_support.3 krb5_config_get_time_default.3 \
	krb5_support.3 krb5_config_parse_file_multi.3 \
	krb5_support.3 krb5_config_vget_bool.3 \
	krb5_support.3 krb5_config_vget_bool_default.3 \
	krb5_support.3 krb5_config_vget_list.3 \
	krb5_support.3 krb5_config_vget_string.3 \
	krb5_support.3 krb5_config_vget_string_default.3 \
	krb5_support.3 krb5_config_vget_strings.3 \
	krb5_support.3 krb5_config_vget_time.3 \
	krb5_support.3 krb5_config_vget_time_default.3 \
	krb5_support.3 krb5_expand_hostname.3 \
	krb5_support.3 krb5_expand_hostname_realms.3 \
	krb5_support.3 krb5_kuserok.3 \
	krb5_support.3 krb5_plugin_register.3 \
	krb5_ticket.3 krb5_ticket_get_flags.3 \
	krb5_timeofday.3 krb5_us_timeofday.3 \
	krb5_v4compat.3 krb524_convert_creds_kdc_ccache.3 \
	krb5_verify_user.3 krb5_verify_opt_init.3 \
	krb5_verify_user.3 krb5_verify_opt_set_flags.3 \
	krb5_verify_user.3 krb5_verify_opt_set_keytab.3 \
	krb5_verify_user.3 krb5_verify_opt_set_secure.3 \
	krb5_verify_user.3 krb5_verify_opt_set_service.3 \
	krb5_verify_user.3 krb5_verify_user_lrealm.3 \
	krb5_verify_user.3 krb5_verify_user_opt.3

SRCS=	acache.c \
	acl.c \
	add_et_list.c \
	addr_families.c \
	aname_to_localname.c \
	appdefault.c \
	asn1_glue.c \
	auth_context.c \
	build_ap_req.c \
	build_auth.c \
	cache.c \
	changepw.c \
	codec.c \
	config_file.c \
	constants.c \
	context.c \
	convert_creds.c \
	copy_host_realm.c \
	crc.c \
	creds.c \
	crypto-aes.c \
	crypto-algs.c \
	crypto-arcfour.c \
	crypto-des-common.c \
	crypto-des.c \
	crypto-des3.c \
	crypto-evp.c \
	crypto-null.c \
	crypto-pk.c \
	crypto-rand.c \
	crypto.c \
	data.c \
	deprecated.c \
	digest.c \
	doxygen.c \
	eai_to_heim_errno.c \
	error_string.c \
	expand_hostname.c \
	expand_path.c \
	fcache.c \
	free.c \
	free_host_realm.c \
	generate_seq_number.c \
	generate_subkey.c \
	get_addrs.c \
	get_cred.c \
	get_default_principal.c \
	get_default_realm.c \
	get_for_creds.c \
	get_host_realm.c \
	get_in_tkt.c \
	get_port.c \
	init_creds.c \
	init_creds_pw.c \
	kcm.c \
	keyblock.c \
	keytab.c \
	keytab_any.c \
	keytab_file.c \
	keytab_keyfile.c \
	keytab_memory.c \
	krbhst.c \
	kuserok.c \
	log.c \
	mcache.c \
	misc.c \
	mit_glue.c \
	mk_error.c \
	mk_priv.c \
	mk_rep.c \
	mk_req.c \
	mk_req_ext.c \
	mk_safe.c \
	n-fold.c \
	net_read.c \
	net_write.c \
	pac.c \
	padata.c \
	pcache.c \
	pkinit.c \
	plugin.c \
	principal.c \
	prog_setup.c \
	prompter_posix.c \
	rd_cred.c \
	rd_error.c \
	rd_priv.c \
	rd_rep.c \
	rd_req.c \
	rd_safe.c \
	read_message.c \
	recvauth.c \
	replay.c \
	salt-aes.c \
	salt-arcfour.c \
	salt-des.c \
	salt-des3.c \
	salt.c \
	scache.c \
	send_to_kdc.c \
	sendauth.c \
	set_default_realm.c \
	sock_principal.c \
	store-int.c \
	store.c \
	store_emem.c \
	store_fd.c \
	store_mem.c \
	ticket.c \
	time.c \
	transited.c \
	verify_init.c \
	verify_user.c \
	version.c \
	warn.c \
	write_message.c

SRCS+=	heim_err.c \
	heim_err.h \
	k524_err.c \
	k524_err.h \
	krb5_err.c \
	krb5_err.h \
	krb_err.c \
	krb_err.h

CFLAGS+=	-I${KRB5DIR}/lib/krb5 \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/lib/ipc \
		-I${KRB5DIR}/base \
		-I${SRCTOP}/contrib/com_err \
		-I.

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/krb5 ${KRB5DIR}/lib/asn1 ${KRB5DIR}/include ${SRCTOP}/kerberos5/include ${KRB5DIR}/doc/doxyout/krb5/man/man3/
