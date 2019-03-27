/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2001 Robert N. M. Watson
 * Copyright (c) 2008 Edward Tomasz Napierała <trasz@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/* 
 * Developed by the TrustedBSD Project.
 * Support for POSIX.1e and NFSv4 access control lists.
 */

#ifndef _SYS_ACL_H_
#define	_SYS_ACL_H_

#include <sys/param.h>
#include <sys/queue.h>
#include <vm/uma.h>

/*
 * POSIX.1e and NFSv4 ACL types and related constants.
 */

typedef uint32_t	acl_tag_t;
typedef uint32_t	acl_perm_t;
typedef uint16_t	acl_entry_type_t;
typedef uint16_t	acl_flag_t;
typedef int		acl_type_t;
typedef int		*acl_permset_t;
typedef uint16_t	*acl_flagset_t;

/*
 * With 254 entries, "struct acl_t_struct" is exactly one 4kB page big.
 * Note that with NFSv4 ACLs, the maximum number of ACL entries one
 * may set on file or directory is about half of ACL_MAX_ENTRIES.
 *
 * If you increase this, you might also need to increase
 * _ACL_T_ALIGNMENT_BITS in lib/libc/posix1e/acl_support.h.
 *
 * The maximum number of POSIX.1e ACLs is controlled
 * by OLDACL_MAX_ENTRIES.  Changing that one will break binary
 * compatibility with pre-8.0 userland and change on-disk ACL layout.
 */
#define	ACL_MAX_ENTRIES				254

#if defined(_KERNEL) || defined(_ACL_PRIVATE)

#define	POSIX1E_ACL_ACCESS_EXTATTR_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	POSIX1E_ACL_ACCESS_EXTATTR_NAME		"posix1e.acl_access"
#define	POSIX1E_ACL_DEFAULT_EXTATTR_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	POSIX1E_ACL_DEFAULT_EXTATTR_NAME	"posix1e.acl_default"
#define	NFS4_ACL_EXTATTR_NAMESPACE		EXTATTR_NAMESPACE_SYSTEM
#define	NFS4_ACL_EXTATTR_NAME			"nfs4.acl"
#define	OLDACL_MAX_ENTRIES			32

/*
 * "struct oldacl" is used in compatibility ACL syscalls and for on-disk
 * storage of POSIX.1e ACLs.
 */
typedef int	oldacl_tag_t;
typedef mode_t	oldacl_perm_t;

struct oldacl_entry {
	oldacl_tag_t	ae_tag;
	uid_t		ae_id;
	oldacl_perm_t	ae_perm;
};
typedef struct oldacl_entry	*oldacl_entry_t;

struct oldacl {
	int			acl_cnt;
	struct oldacl_entry	acl_entry[OLDACL_MAX_ENTRIES];
};

/*
 * Current "struct acl".
 */
struct acl_entry {
	acl_tag_t		ae_tag;
	uid_t			ae_id;
	acl_perm_t		ae_perm;
	/* NFSv4 entry type, "allow" or "deny".  Unused in POSIX.1e ACLs. */
	acl_entry_type_t	ae_entry_type;
	/* NFSv4 ACL inheritance.  Unused in POSIX.1e ACLs. */
	acl_flag_t		ae_flags;
};
typedef struct acl_entry	*acl_entry_t;

/*
 * Internal ACL structure, used in libc, kernel APIs and for on-disk
 * storage of NFSv4 ACLs.  POSIX.1e ACLs use "struct oldacl" for on-disk
 * storage.
 */
struct acl {
	unsigned int		acl_maxcnt;
	unsigned int		acl_cnt;
	/* Will be required e.g. to implement NFSv4.1 ACL inheritance. */
	int			acl_spare[4];
	struct acl_entry	acl_entry[ACL_MAX_ENTRIES];
};

/*
 * ACL structure internal to libc.
 */
struct acl_t_struct {
	struct acl		ats_acl;
	int			ats_cur_entry;
	/*
	 * ats_brand is for libc internal bookkeeping only.
	 * Applications should use acl_get_brand_np(3).
	 * Kernel code should use the "type" argument passed
	 * to VOP_SETACL, VOP_GETACL or VOP_ACLCHECK calls;
	 * ACL_TYPE_ACCESS or ACL_TYPE_DEFAULT mean POSIX.1e
	 * ACL, ACL_TYPE_NFS4 means NFSv4 ACL.
	 */
	int			ats_brand;
};
typedef struct acl_t_struct *acl_t;

#else /* _KERNEL || _ACL_PRIVATE */

typedef void *acl_entry_t;
typedef void *acl_t;

#endif /* !_KERNEL && !_ACL_PRIVATE */

/*
 * Possible valid values for ats_brand field.
 */
#define	ACL_BRAND_UNKNOWN	0
#define	ACL_BRAND_POSIX		1
#define	ACL_BRAND_NFS4		2

/*
 * Possible valid values for ae_tag field.  For explanation, see acl(9).
 */
#define	ACL_UNDEFINED_TAG	0x00000000
#define	ACL_USER_OBJ		0x00000001
#define	ACL_USER		0x00000002
#define	ACL_GROUP_OBJ		0x00000004
#define	ACL_GROUP		0x00000008
#define	ACL_MASK		0x00000010
#define	ACL_OTHER		0x00000020
#define	ACL_OTHER_OBJ		ACL_OTHER
#define	ACL_EVERYONE		0x00000040

/*
 * Possible valid values for ae_entry_type field, valid only for NFSv4 ACLs.
 */
#define	ACL_ENTRY_TYPE_ALLOW	0x0100
#define	ACL_ENTRY_TYPE_DENY	0x0200
#define	ACL_ENTRY_TYPE_AUDIT	0x0400
#define	ACL_ENTRY_TYPE_ALARM	0x0800

/*
 * Possible valid values for acl_type_t arguments.  First two
 * are provided only for backwards binary compatibility.
 */
#define	ACL_TYPE_ACCESS_OLD	0x00000000
#define	ACL_TYPE_DEFAULT_OLD	0x00000001
#define	ACL_TYPE_ACCESS		0x00000002
#define	ACL_TYPE_DEFAULT	0x00000003
#define	ACL_TYPE_NFS4		0x00000004

/*
 * Possible bits in ae_perm field for POSIX.1e ACLs.  Note
 * that ACL_EXECUTE may be used in both NFSv4 and POSIX.1e ACLs.
 */
#define	ACL_EXECUTE		0x0001
#define	ACL_WRITE		0x0002
#define	ACL_READ		0x0004
#define	ACL_PERM_NONE		0x0000
#define	ACL_PERM_BITS		(ACL_EXECUTE | ACL_WRITE | ACL_READ)
#define	ACL_POSIX1E_BITS	(ACL_EXECUTE | ACL_WRITE | ACL_READ)

/*
 * Possible bits in ae_perm field for NFSv4 ACLs.
 */
#define	ACL_READ_DATA		0x00000008
#define	ACL_LIST_DIRECTORY	0x00000008
#define	ACL_WRITE_DATA		0x00000010
#define	ACL_ADD_FILE		0x00000010
#define	ACL_APPEND_DATA		0x00000020
#define	ACL_ADD_SUBDIRECTORY	0x00000020
#define	ACL_READ_NAMED_ATTRS	0x00000040
#define	ACL_WRITE_NAMED_ATTRS	0x00000080
/* ACL_EXECUTE is defined above. */
#define	ACL_DELETE_CHILD	0x00000100
#define	ACL_READ_ATTRIBUTES	0x00000200
#define	ACL_WRITE_ATTRIBUTES	0x00000400
#define	ACL_DELETE		0x00000800
#define	ACL_READ_ACL		0x00001000
#define	ACL_WRITE_ACL		0x00002000
#define	ACL_WRITE_OWNER		0x00004000
#define	ACL_SYNCHRONIZE		0x00008000

#define	ACL_FULL_SET		(ACL_READ_DATA | ACL_WRITE_DATA | \
    ACL_APPEND_DATA | ACL_READ_NAMED_ATTRS | ACL_WRITE_NAMED_ATTRS | \
    ACL_EXECUTE | ACL_DELETE_CHILD | ACL_READ_ATTRIBUTES | \
    ACL_WRITE_ATTRIBUTES | ACL_DELETE | ACL_READ_ACL | ACL_WRITE_ACL | \
    ACL_WRITE_OWNER | ACL_SYNCHRONIZE)

#define	ACL_MODIFY_SET		(ACL_FULL_SET & \
    ~(ACL_WRITE_ACL | ACL_WRITE_OWNER))

#define	ACL_READ_SET		(ACL_READ_DATA | ACL_READ_NAMED_ATTRS | \
    ACL_READ_ATTRIBUTES | ACL_READ_ACL)

#define	ACL_WRITE_SET		(ACL_WRITE_DATA | ACL_APPEND_DATA | \
    ACL_WRITE_NAMED_ATTRS | ACL_WRITE_ATTRIBUTES)

#define	ACL_NFS4_PERM_BITS	ACL_FULL_SET

/*
 * Possible entry_id values for acl_get_entry(3).
 */
#define	ACL_FIRST_ENTRY		0
#define	ACL_NEXT_ENTRY		1

/*
 * Possible values in ae_flags field; valid only for NFSv4 ACLs.
 */
#define	ACL_ENTRY_FILE_INHERIT		0x0001
#define	ACL_ENTRY_DIRECTORY_INHERIT	0x0002
#define	ACL_ENTRY_NO_PROPAGATE_INHERIT	0x0004
#define	ACL_ENTRY_INHERIT_ONLY		0x0008
#define	ACL_ENTRY_SUCCESSFUL_ACCESS	0x0010
#define	ACL_ENTRY_FAILED_ACCESS		0x0020
#define	ACL_ENTRY_INHERITED		0x0080

#define	ACL_FLAGS_BITS			(ACL_ENTRY_FILE_INHERIT | \
    ACL_ENTRY_DIRECTORY_INHERIT | ACL_ENTRY_NO_PROPAGATE_INHERIT | \
    ACL_ENTRY_INHERIT_ONLY | ACL_ENTRY_SUCCESSFUL_ACCESS | \
    ACL_ENTRY_FAILED_ACCESS | ACL_ENTRY_INHERITED)

/*
 * Undefined value in ae_id field.  ae_id should be set to this value
 * iff ae_tag is ACL_USER_OBJ, ACL_GROUP_OBJ, ACL_OTHER or ACL_EVERYONE.
 */
#define	ACL_UNDEFINED_ID	((uid_t)-1)

/*
 * Possible values for _flags parameter in acl_to_text_np(3).
 */
#define	ACL_TEXT_VERBOSE	0x01
#define	ACL_TEXT_NUMERIC_IDS	0x02
#define	ACL_TEXT_APPEND_ID	0x04

/*
 * POSIX.1e ACLs are capable of expressing the read, write, and execute bits
 * of the POSIX mode field.  We provide two masks: one that defines the bits
 * the ACL will replace in the mode, and the other that defines the bits that
 * must be preseved when an ACL is updating a mode.
 */
#define	ACL_OVERRIDE_MASK	(S_IRWXU | S_IRWXG | S_IRWXO)
#define	ACL_PRESERVE_MASK	(~ACL_OVERRIDE_MASK)

#ifdef _KERNEL

/*
 * Filesystem-independent code to move back and forth between POSIX mode and
 * POSIX.1e ACL representations.
 */
acl_perm_t		acl_posix1e_mode_to_perm(acl_tag_t tag, mode_t mode);
struct acl_entry	acl_posix1e_mode_to_entry(acl_tag_t tag, uid_t uid,
			    gid_t gid, mode_t mode);
mode_t			acl_posix1e_perms_to_mode(
			    struct acl_entry *acl_user_obj_entry,
			    struct acl_entry *acl_group_obj_entry,
			    struct acl_entry *acl_other_entry);
mode_t			acl_posix1e_acl_to_mode(struct acl *acl);
mode_t			acl_posix1e_newfilemode(mode_t cmode,
			    struct acl *dacl);
struct acl		*acl_alloc(int flags);
void			acl_free(struct acl *aclp);

void			acl_nfs4_sync_acl_from_mode(struct acl *aclp,
			    mode_t mode, int file_owner_id);
void			acl_nfs4_sync_mode_from_acl(mode_t *mode,
			    const struct acl *aclp);
int			acl_nfs4_is_trivial(const struct acl *aclp,
			    int file_owner_id);
void			acl_nfs4_compute_inherited_acl(
			    const struct acl *parent_aclp,
			    struct acl *child_aclp, mode_t mode,
			    int file_owner_id, int is_directory);
int			acl_copy_oldacl_into_acl(const struct oldacl *source,
			    struct acl *dest);
int			acl_copy_acl_into_oldacl(const struct acl *source,
			    struct oldacl *dest);

/*
 * To allocate 'struct acl', use acl_alloc()/acl_free() instead of this.
 */
MALLOC_DECLARE(M_ACL);
/*
 * Filesystem-independent syntax check for a POSIX.1e ACL.
 */
int			acl_posix1e_check(struct acl *acl);
int 			acl_nfs4_check(const struct acl *aclp, int is_directory);

#else /* !_KERNEL */

#if defined(_ACL_PRIVATE)

/*
 * Syscall interface -- use the library calls instead as the syscalls have
 * strict ACL entry ordering requirements.
 */
__BEGIN_DECLS
int	__acl_aclcheck_fd(int _filedes, acl_type_t _type, struct acl *_aclp);
int	__acl_aclcheck_file(const char *_path, acl_type_t _type,
	    struct acl *_aclp);
int	__acl_aclcheck_link(const char *_path, acl_type_t _type,
	    struct acl *_aclp);
int	__acl_delete_fd(int _filedes, acl_type_t _type);
int	__acl_delete_file(const char *_path_p, acl_type_t _type);
int	__acl_delete_link(const char *_path_p, acl_type_t _type);
int	__acl_get_fd(int _filedes, acl_type_t _type, struct acl *_aclp);
int	__acl_get_file(const char *_path, acl_type_t _type, struct acl *_aclp);
int	__acl_get_link(const char *_path, acl_type_t _type, struct acl *_aclp);
int	__acl_set_fd(int _filedes, acl_type_t _type, struct acl *_aclp);
int	__acl_set_file(const char *_path, acl_type_t _type, struct acl *_aclp);
int	__acl_set_link(const char *_path, acl_type_t _type, struct acl *_aclp);
__END_DECLS

#endif /* _ACL_PRIVATE */

/*
 * Supported POSIX.1e ACL manipulation and assignment/retrieval API _np calls
 * are local extensions that reflect an environment capable of opening file
 * descriptors of directories, and allowing additional ACL type for different
 * filesystems (i.e., AFS).
 */
__BEGIN_DECLS
int	acl_add_flag_np(acl_flagset_t _flagset_d, acl_flag_t _flag);
int	acl_add_perm(acl_permset_t _permset_d, acl_perm_t _perm);
int	acl_calc_mask(acl_t *_acl_p);
int	acl_clear_flags_np(acl_flagset_t _flagset_d);
int	acl_clear_perms(acl_permset_t _permset_d);
int	acl_copy_entry(acl_entry_t _dest_d, acl_entry_t _src_d);
ssize_t	acl_copy_ext(void *_buf_p, acl_t _acl, ssize_t _size);
acl_t	acl_copy_int(const void *_buf_p);
int	acl_create_entry(acl_t *_acl_p, acl_entry_t *_entry_p);
int	acl_create_entry_np(acl_t *_acl_p, acl_entry_t *_entry_p, int _index);
int	acl_delete_entry(acl_t _acl, acl_entry_t _entry_d);
int	acl_delete_entry_np(acl_t _acl, int _index);
int	acl_delete_fd_np(int _filedes, acl_type_t _type);
int	acl_delete_file_np(const char *_path_p, acl_type_t _type);
int	acl_delete_link_np(const char *_path_p, acl_type_t _type);
int	acl_delete_def_file(const char *_path_p);
int	acl_delete_def_link_np(const char *_path_p);
int	acl_delete_flag_np(acl_flagset_t _flagset_d, acl_flag_t _flag);
int	acl_delete_perm(acl_permset_t _permset_d, acl_perm_t _perm);
acl_t	acl_dup(acl_t _acl);
int	acl_free(void *_obj_p);
acl_t	acl_from_text(const char *_buf_p);
int	acl_get_brand_np(acl_t _acl, int *_brand_p);
int	acl_get_entry(acl_t _acl, int _entry_id, acl_entry_t *_entry_p);
acl_t	acl_get_fd(int _fd);
acl_t	acl_get_fd_np(int fd, acl_type_t _type);
acl_t	acl_get_file(const char *_path_p, acl_type_t _type);
int	acl_get_entry_type_np(acl_entry_t _entry_d, acl_entry_type_t *_entry_type_p);
acl_t	acl_get_link_np(const char *_path_p, acl_type_t _type);
void	*acl_get_qualifier(acl_entry_t _entry_d);
int	acl_get_flag_np(acl_flagset_t _flagset_d, acl_flag_t _flag);
int	acl_get_perm_np(acl_permset_t _permset_d, acl_perm_t _perm);
int	acl_get_flagset_np(acl_entry_t _entry_d, acl_flagset_t *_flagset_p);
int	acl_get_permset(acl_entry_t _entry_d, acl_permset_t *_permset_p);
int	acl_get_tag_type(acl_entry_t _entry_d, acl_tag_t *_tag_type_p);
acl_t	acl_init(int _count);
int	acl_set_fd(int _fd, acl_t _acl);
int	acl_set_fd_np(int _fd, acl_t _acl, acl_type_t _type);
int	acl_set_file(const char *_path_p, acl_type_t _type, acl_t _acl);
int	acl_set_entry_type_np(acl_entry_t _entry_d, acl_entry_type_t _entry_type);
int	acl_set_link_np(const char *_path_p, acl_type_t _type, acl_t _acl);
int	acl_set_flagset_np(acl_entry_t _entry_d, acl_flagset_t _flagset_d);
int	acl_set_permset(acl_entry_t _entry_d, acl_permset_t _permset_d);
int	acl_set_qualifier(acl_entry_t _entry_d, const void *_tag_qualifier_p);
int	acl_set_tag_type(acl_entry_t _entry_d, acl_tag_t _tag_type);
ssize_t	acl_size(acl_t _acl);
char	*acl_to_text(acl_t _acl, ssize_t *_len_p);
char	*acl_to_text_np(acl_t _acl, ssize_t *_len_p, int _flags);
int	acl_valid(acl_t _acl);
int	acl_valid_fd_np(int _fd, acl_type_t _type, acl_t _acl);
int	acl_valid_file_np(const char *_path_p, acl_type_t _type, acl_t _acl);
int	acl_valid_link_np(const char *_path_p, acl_type_t _type, acl_t _acl);
int	acl_is_trivial_np(const acl_t _acl, int *_trivialp);
acl_t	acl_strip_np(const acl_t _acl, int recalculate_mask);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_SYS_ACL_H_ */
