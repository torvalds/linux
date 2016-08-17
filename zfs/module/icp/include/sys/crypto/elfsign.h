/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CRYPTO_ELFSIGN_H
#define	_SYS_CRYPTO_ELFSIGN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consolidation Private Interface for elfsign/libpkcs11/kcfd
 */

#include <sys/zfs_context.h>

/*
 * Project Private structures and types used for communication between kcfd
 * and KCF over the door.
 */

typedef enum ELFsign_status_e {
	ELFSIGN_UNKNOWN,
	ELFSIGN_SUCCESS,
	ELFSIGN_FAILED,
	ELFSIGN_NOTSIGNED,
	ELFSIGN_INVALID_CERTPATH,
	ELFSIGN_INVALID_ELFOBJ,
	ELFSIGN_RESTRICTED
} ELFsign_status_t;

#define	KCF_KCFD_VERSION1	1
#define	SIG_MAX_LENGTH		1024

#define	ELF_SIGNATURE_SECTION	".SUNW_signature"

typedef struct kcf_door_arg_s {
	short		da_version;
	boolean_t	da_iskernel;

	union {
		char filename[MAXPATHLEN];	/* For request */

		struct kcf_door_result_s {	/* For response */
			ELFsign_status_t	status;
			uint32_t		siglen;
			uchar_t			signature[1];
		} result;
	} da_u;
} kcf_door_arg_t;

typedef uint32_t	filesig_vers_t;

/*
 * File Signature Structure
 *	Applicable to ELF and other file formats
 */
struct filesignatures {
	uint32_t	filesig_cnt;	/* count of signatures */
	uint32_t	filesig_pad;	/* unused */
	union {
		char	filesig_data[1];
		struct filesig {	/* one of these for each signature */
			uint32_t	filesig_size;
			filesig_vers_t	filesig_version;
			union {
				struct filesig_version1 {
					uint32_t	filesig_v1_dnsize;
					uint32_t	filesig_v1_sigsize;
					uint32_t	filesig_v1_oidsize;
					char	filesig_v1_data[1];
				} filesig_v1;
				struct filesig_version3 {
					uint64_t	filesig_v3_time;
					uint32_t	filesig_v3_dnsize;
					uint32_t	filesig_v3_sigsize;
					uint32_t	filesig_v3_oidsize;
					char	filesig_v3_data[1];
				} filesig_v3;
			} _u2;
		} filesig_sig;
		uint64_t filesig_align;
	} _u1;
};
#define	filesig_sig		_u1.filesig_sig

#define	filesig_v1_dnsize	_u2.filesig_v1.filesig_v1_dnsize
#define	filesig_v1_sigsize	_u2.filesig_v1.filesig_v1_sigsize
#define	filesig_v1_oidsize	_u2.filesig_v1.filesig_v1_oidsize
#define	filesig_v1_data		_u2.filesig_v1.filesig_v1_data

#define	filesig_v3_time		_u2.filesig_v3.filesig_v3_time
#define	filesig_v3_dnsize	_u2.filesig_v3.filesig_v3_dnsize
#define	filesig_v3_sigsize	_u2.filesig_v3.filesig_v3_sigsize
#define	filesig_v3_oidsize	_u2.filesig_v3.filesig_v3_oidsize
#define	filesig_v3_data		_u2.filesig_v3.filesig_v3_data

#define	filesig_ALIGN(s)	(((s) + sizeof (uint64_t) - 1) & \
				    (-sizeof (uint64_t)))
#define	filesig_next(ptr)	(struct filesig *)((void *)((char *)(ptr) + \
				    filesig_ALIGN((ptr)->filesig_size)))

#define	FILESIG_UNKNOWN		0	/* unrecognized version */
#define	FILESIG_VERSION1	1	/* version1, all but sig section */
#define	FILESIG_VERSION2	2	/* version1 format, SHF_ALLOC only */
#define	FILESIG_VERSION3	3	/* version3, all but sig section */
#define	FILESIG_VERSION4	4	/* version3 format, SHF_ALLOC only */

#define	_PATH_KCFD_DOOR	"/etc/svc/volatile/kcfd_door"

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_ELFSIGN_H */
