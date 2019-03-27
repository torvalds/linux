/*
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, 2016, Juniper Networks, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_SECURITY_MAC_VERIEXEC_INTERNAL_H
#define	_SECURITY_MAC_VERIEXEC_INTERNAL_H

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#define MAC_VERIEXEC_FULLNAME   "MAC/veriexec"

#define VERIEXEC_FILES_FIRST	1

#if defined(VERIFIED_EXEC_DEBUG) || defined(VERIFIED_EXEC_DEBUG_VERBOSE)
# define VERIEXEC_DEBUG(n, x) if (mac_veriexec_debug > (n)) printf x
#else
# define VERIEXEC_DEBUG(n, x)
#endif

struct mac_veriexec_file_info
{
	int flags;
	long fileid;
	unsigned long gen;
	struct mac_veriexec_fpops *ops;
	unsigned char fingerprint[MAXFINGERPRINTLEN];
	LIST_ENTRY(mac_veriexec_file_info) entries;
};

MALLOC_DECLARE(M_VERIEXEC);

SYSCTL_DECL(_security_mac_veriexec);

struct cred;
struct image_params;
struct proc;
struct sbuf;
struct thread;
struct ucred;
struct vattr;
struct vnode;

int	mac_veriexec_metadata_fetch_fingerprint_status(struct vnode *vp,
	    struct vattr *vap, struct thread *td, int check_files);
int	mac_veriexec_metadata_get_executable_flags(struct ucred *cred,
	    struct proc *p, int *flags, int check_files);
int	mac_veriexec_metadata_get_file_flags(dev_t fsid, long fileid,
	    unsigned long gen, int *flags, int check_files);
void	mac_veriexec_metadata_init(void);
void	mac_veriexec_metadata_print_db(struct sbuf *sbp);
int	mac_veriexec_metadata_unmounted(dev_t fsid, struct thread *td);

int	mac_veriexec_fingerprint_add_ops(struct mac_veriexec_fpops *fpops);

int	mac_veriexec_fingerprint_check_image(struct image_params *imgp,
	    int check_files, struct thread *td);
int	mac_veriexec_fingerprint_check_vnode(struct vnode *vp,
	    struct mac_veriexec_file_info *ip, struct thread *td,
	    off_t file_size, unsigned char *fingerprint);
void	mac_veriexec_fingerprint_init(void);
struct mac_veriexec_fpops *
	mac_veriexec_fingerprint_lookup_ops(const char *type);

fingerprint_status_t
	mac_veriexec_get_fingerprint_status(struct vnode *vp);
int	mac_veriexec_get_state(void);
int	mac_veriexec_in_state(int state);
void	mac_veriexec_set_fingerprint_status(struct vnode *vp,
	    fingerprint_status_t fp_status);
void	mac_veriexec_set_state(int state);

#endif	/* !_SECURITY_MAC_VERIEXEC_INTERNAL_H */
