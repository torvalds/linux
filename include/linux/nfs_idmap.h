/*
 * include/linux/nfs_idmap.h
 *
 *  UID and GID to name mapping for clients.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NFS_IDMAP_H
#define NFS_IDMAP_H

#include <linux/types.h>

/* XXX from bits/utmp.h  */
#define IDMAP_NAMESZ  128

#define IDMAP_TYPE_USER  0
#define IDMAP_TYPE_GROUP 1

#define IDMAP_CONV_IDTONAME 0
#define IDMAP_CONV_NAMETOID 1

#define IDMAP_STATUS_INVALIDMSG 0x01
#define IDMAP_STATUS_AGAIN      0x02
#define IDMAP_STATUS_LOOKUPFAIL 0x04
#define IDMAP_STATUS_SUCCESS    0x08

struct idmap_msg {
	__u8  im_type;
	__u8  im_conv;
	char  im_name[IDMAP_NAMESZ];
	__u32 im_id;
	__u8  im_status;
};

#ifdef __KERNEL__

/* Forward declaration to make this header independent of others */
struct nfs_client;
struct nfs_server;
struct nfs_fattr;
struct nfs4_string;

#ifdef CONFIG_NFS_USE_NEW_IDMAPPER

int nfs_idmap_init(void);
void nfs_idmap_quit(void);

static inline int nfs_idmap_new(struct nfs_client *clp)
{
	return 0;
}

static inline void nfs_idmap_delete(struct nfs_client *clp)
{
}

#else /* CONFIG_NFS_USE_NEW_IDMAPPER not set */

static inline int nfs_idmap_init(void)
{
	return 0;
}

static inline void nfs_idmap_quit(void)
{
}

int nfs_idmap_new(struct nfs_client *);
void nfs_idmap_delete(struct nfs_client *);

#endif /* CONFIG_NFS_USE_NEW_IDMAPPER */

void nfs_fattr_init_names(struct nfs_fattr *fattr,
		struct nfs4_string *owner_name,
		struct nfs4_string *group_name);
void nfs_fattr_free_names(struct nfs_fattr *);
void nfs_fattr_map_and_free_names(struct nfs_server *, struct nfs_fattr *);

int nfs_map_name_to_uid(const struct nfs_server *, const char *, size_t, __u32 *);
int nfs_map_group_to_gid(const struct nfs_server *, const char *, size_t, __u32 *);
int nfs_map_uid_to_name(const struct nfs_server *, __u32, char *, size_t);
int nfs_map_gid_to_group(const struct nfs_server *, __u32, char *, size_t);

extern unsigned int nfs_idmap_cache_timeout;
#endif /* __KERNEL__ */

#endif /* NFS_IDMAP_H */
