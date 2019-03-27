# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/posix1e

CFLAGS+=-D_ACL_PRIVATE

# Copy kern/subr_acl_nfs4.c to the libc object directory.
subr_acl_nfs4.c: ${SRCTOP}/sys/kern/subr_acl_nfs4.c
	cat ${.ALLSRC} > ${.TARGET}

CONFS+=	posix1e/mac.conf
SRCS+=	acl_branding.c			\
	acl_calc_mask.c			\
	acl_copy.c			\
	acl_delete.c			\
	acl_delete_entry.c		\
	acl_entry.c			\
	acl_flag.c			\
	acl_free.c			\
	acl_from_text.c			\
	acl_from_text_nfs4.c		\
	acl_get.c			\
	acl_id_to_name.c		\
	acl_init.c			\
	acl_perm.c			\
	acl_set.c			\
	acl_strip.c			\
	acl_support.c			\
	acl_support_nfs4.c		\
	acl_to_text.c			\
	acl_to_text_nfs4.c		\
	acl_valid.c			\
	extattr.c			\
	mac.c				\
	mac_exec.c			\
	mac_get.c			\
	mac_set.c			\
	subr_acl_nfs4.c
.if ${MK_SYMVER} == yes
SRCS+=	acl_compat.c
.endif

SYM_MAPS+=${LIBC_SRCTOP}/posix1e/Symbol.map

MAN+=	acl.3				\
	acl_add_flag_np.3		\
	acl_add_perm.3			\
	acl_calc_mask.3			\
	acl_clear_flags_np.3		\
	acl_clear_perms.3		\
	acl_copy_entry.3		\
	acl_create_entry.3		\
	acl_delete.3			\
	acl_delete_entry.3		\
	acl_delete_flag_np.3		\
	acl_delete_perm.3		\
	acl_dup.3			\
	acl_free.3			\
	acl_from_text.3			\
	acl_get.3			\
	acl_get_brand_np.3		\
	acl_get_entry.3			\
	acl_get_entry_type_np.3		\
	acl_get_flagset_np.3		\
	acl_get_flag_np.3		\
	acl_get_permset.3		\
	acl_get_perm_np.3		\
	acl_get_qualifier.3		\
	acl_get_tag_type.3		\
	acl_init.3			\
	acl_is_trivial_np.3		\
	acl_set.3			\
	acl_set_entry_type_np.3		\
	acl_set_flagset_np.3		\
	acl_set_permset.3		\
	acl_set_qualifier.3		\
	acl_set_tag_type.3		\
	acl_strip_np.3			\
	acl_to_text.3			\
	acl_valid.3			\
	extattr.3			\
	mac.3				\
	mac.conf.5			\
	mac_free.3			\
	mac_is_present.3		\
	mac_get.3			\
	mac_prepare.3			\
	mac_set.3			\
	mac_text.3			\
	posix1e.3

MLINKS+=acl_create_entry.3 acl_create_entry_np.3\
	acl_delete.3 acl_delete_def_file.3	\
	acl_delete.3 acl_delete_file_np.3	\
	acl_delete.3 acl_delete_fd_np.3		\
	acl_delete_entry.3 acl_delete_entry_np.3\
	acl_get.3 acl_get_file.3		\
	acl_get.3 acl_get_fd.3			\
	acl_get.3 acl_get_fd_np.3		\
	acl_get.3 acl_get_link_np.3		\
	acl_set.3 acl_set_file.3		\
	acl_set.3 acl_set_fd.3			\
	acl_set.3 acl_set_fd_np.3		\
	acl_set.3 acl_set_link_np.3		\
	acl_to_text.3 acl_to_text_np.3		\
	acl_valid.3 acl_valid_file_np.3		\
	acl_valid.3 acl_valid_fd_np.3		\
	extattr.3 extattr_namespace_to_string.3	\
	extattr.3 extattr_string_to_namespace.3 \
	mac_get.3 mac_get_fd.3			\
	mac_get.3 mac_get_file.3		\
	mac_get.3 mac_get_link.3		\
	mac_get.3 mac_get_peer.3		\
	mac_get.3 mac_get_pid.3			\
	mac_get.3 mac_get_proc.3		\
	mac_prepare.3 mac_prepare_file_label.3	\
	mac_prepare.3 mac_prepare_ifnet_label.3	\
	mac_prepare.3 mac_prepare_process_label.3	\
	mac_prepare.3 mac_prepare_type.3	\
	mac_set.3 mac_set_fd.3			\
	mac_set.3 mac_set_file.3		\
	mac_set.3 mac_set_link.3		\
	mac_set.3 mac_set_proc.3		\
	mac_text.3 mac_from_text.3		\
	mac_text.3 mac_to_text.3

CLEANFILES+=	subr_acl_nfs4.c
