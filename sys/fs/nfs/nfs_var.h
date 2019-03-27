/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
 * XXX needs <nfs/rpcv2.h> and <nfs/nfs.h> because of typedefs
 */

struct uio;
struct ucred;
struct nfscred;
NFSPROC_T;
struct buf;
struct sockaddr_in;
struct nfs_dlmount;
struct file;
struct nfsmount;
struct socket;
struct nfsreq;
struct nfssockreq;
struct vattr;
struct nameidata;
struct nfsnode;
struct nfsfh;
struct sillyrename;
struct componentname;
struct nfsd_srvargs;
struct nfsrv_descript;
struct nfs_fattr;
union nethostaddr;
struct nfsstate;
struct nfslock;
struct nfsclient;
struct nfslayout;
struct nfsdsession;
struct nfslockconflict;
struct nfsd_idargs;
struct nfsd_clid;
struct nfsusrgrp;
struct nfsclowner;
struct nfsclopen;
struct nfsclopenhead;
struct nfsclclient;
struct nfsclsession;
struct nfscllockowner;
struct nfscllock;
struct nfscldeleg;
struct nfscllayout;
struct nfscldevinfo;
struct nfsv4lock;
struct nfsvattr;
struct nfs_vattr;
struct NFSSVCARGS;
struct nfsdevice;
struct pnfsdsfile;
struct pnfsdsattr;
#ifdef __FreeBSD__
NFS_ACCESS_ARGS;
NFS_OPEN_ARGS;
NFS_GETATTR_ARGS;
NFS_LOOKUP_ARGS;
NFS_READDIR_ARGS;
#endif

/* nfs_nfsdstate.c */
int nfsrv_setclient(struct nfsrv_descript *, struct nfsclient **,
    nfsquad_t *, nfsquad_t *, NFSPROC_T *);
int nfsrv_getclient(nfsquad_t, int, struct nfsclient **, struct nfsdsession *,
    nfsquad_t, uint32_t, struct nfsrv_descript *, NFSPROC_T *);
int nfsrv_destroyclient(nfsquad_t, NFSPROC_T *);
int nfsrv_destroysession(struct nfsrv_descript *, uint8_t *);
int nfsrv_bindconnsess(struct nfsrv_descript *, uint8_t *, int *);
int nfsrv_freestateid(struct nfsrv_descript *, nfsv4stateid_t *, NFSPROC_T *);
int nfsrv_teststateid(struct nfsrv_descript *, nfsv4stateid_t *, NFSPROC_T *);
int nfsrv_adminrevoke(struct nfsd_clid *, NFSPROC_T *);
void nfsrv_dumpclients(struct nfsd_dumpclients *, int);
void nfsrv_dumplocks(vnode_t, struct nfsd_dumplocks *, int, NFSPROC_T *);
int nfsrv_lockctrl(vnode_t, struct nfsstate **,
    struct nfslock **, struct nfslockconflict *, nfsquad_t, nfsv4stateid_t *,
    struct nfsexstuff *, struct nfsrv_descript *, NFSPROC_T *);
int nfsrv_openctrl(struct nfsrv_descript *, vnode_t,
    struct nfsstate **, nfsquad_t, nfsv4stateid_t *, nfsv4stateid_t *, 
    u_int32_t *, struct nfsexstuff *, NFSPROC_T *, u_quad_t);
int nfsrv_opencheck(nfsquad_t, nfsv4stateid_t *, struct nfsstate *,
    vnode_t, struct nfsrv_descript *, NFSPROC_T *, int);
int nfsrv_openupdate(vnode_t, struct nfsstate *, nfsquad_t,
    nfsv4stateid_t *, struct nfsrv_descript *, NFSPROC_T *, int *);
int nfsrv_delegupdate(struct nfsrv_descript *, nfsquad_t, nfsv4stateid_t *,
    vnode_t, int, struct ucred *, NFSPROC_T *, int *);
int nfsrv_releaselckown(struct nfsstate *, nfsquad_t, NFSPROC_T *);
void nfsrv_zapclient(struct nfsclient *, NFSPROC_T *);
int nfssvc_idname(struct nfsd_idargs *);
void nfsrv_servertimer(void);
int nfsrv_getclientipaddr(struct nfsrv_descript *, struct nfsclient *);
void nfsrv_setupstable(NFSPROC_T *);
void nfsrv_updatestable(NFSPROC_T *);
void nfsrv_writestable(u_char *, int, int, NFSPROC_T *);
void nfsrv_throwawayopens(NFSPROC_T *);
int nfsrv_checkremove(vnode_t, int, NFSPROC_T *);
void nfsd_recalldelegation(vnode_t, NFSPROC_T *);
void nfsd_disabledelegation(vnode_t, NFSPROC_T *);
int nfsrv_checksetattr(vnode_t, struct nfsrv_descript *,
    nfsv4stateid_t *, struct nfsvattr *, nfsattrbit_t *, struct nfsexstuff *,
    NFSPROC_T *);
int nfsrv_checkgetattr(struct nfsrv_descript *, vnode_t,
    struct nfsvattr *, nfsattrbit_t *, NFSPROC_T *);
int nfsrv_nfsuserdport(struct sockaddr *, u_short, NFSPROC_T *);
void nfsrv_nfsuserddelport(void);
void nfsrv_throwawayallstate(NFSPROC_T *);
int nfsrv_checksequence(struct nfsrv_descript *, uint32_t, uint32_t *,
    uint32_t *, int, uint32_t *, NFSPROC_T *);
int nfsrv_checkreclaimcomplete(struct nfsrv_descript *, int);
void nfsrv_cache_session(uint8_t *, uint32_t, int, struct mbuf **);
void nfsrv_freeallbackchannel_xprts(void);
int nfsrv_layoutcommit(struct nfsrv_descript *, vnode_t, int, int, uint64_t,
    uint64_t, uint64_t, int, struct timespec *, int, nfsv4stateid_t *,
    int, char *, int *, uint64_t *, struct ucred *, NFSPROC_T *);
int nfsrv_layoutget(struct nfsrv_descript *, vnode_t, struct nfsexstuff *,
    int, int *, uint64_t *, uint64_t *, uint64_t, nfsv4stateid_t *, int, int *,
    int *, char *, struct ucred *, NFSPROC_T *);
void nfsrv_flexmirrordel(char *, NFSPROC_T *);
void nfsrv_recalloldlayout(NFSPROC_T *);
int nfsrv_layoutreturn(struct nfsrv_descript *, vnode_t, int, int, uint64_t,
    uint64_t, int, int, nfsv4stateid_t *, int, uint32_t *, int *,
    struct ucred *, NFSPROC_T *);
int nfsrv_getdevinfo(char *, int, uint32_t *, uint32_t *, int *, char **);
void nfsrv_freeonedevid(struct nfsdevice *);
void nfsrv_freealllayoutsanddevids(void);
void nfsrv_freefilelayouts(fhandle_t *);
int nfsrv_deldsserver(int, char *, NFSPROC_T *);
struct nfsdevice *nfsrv_deldsnmp(int, struct nfsmount *, NFSPROC_T *);
int nfsrv_createdevids(struct nfsd_nfsd_args *, NFSPROC_T *);
int nfsrv_checkdsattr(struct nfsrv_descript *, vnode_t, NFSPROC_T *);
int nfsrv_copymr(vnode_t, vnode_t, vnode_t, struct nfsdevice *,
    struct pnfsdsfile *, struct pnfsdsfile *, int, struct ucred *, NFSPROC_T *);
int nfsrv_mdscopymr(char *, char *, char *, char *, int *, char *, NFSPROC_T *,
    struct vnode **, struct vnode **, struct pnfsdsfile **, struct nfsdevice **,
    struct nfsdevice **);

/* nfs_nfsdserv.c */
int nfsrvd_access(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_getattr(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_setattr(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_lookup(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_readlink(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_read(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_write(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_create(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_mknod(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_remove(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_rename(struct nfsrv_descript *, int,
    vnode_t, vnode_t, struct nfsexstuff *, struct nfsexstuff *);
int nfsrvd_link(struct nfsrv_descript *, int,
    vnode_t, vnode_t, struct nfsexstuff *, struct nfsexstuff *);
int nfsrvd_symlink(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_mkdir(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_readdir(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_readdirplus(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_commit(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_statfs(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_fsinfo(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_close(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_delegpurge(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_delegreturn(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_getfh(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_lock(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_lockt(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_locku(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_openconfirm(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_opendowngrade(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_renew(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_secinfo(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_setclientid(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_setclientidcfrm(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_verify(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_open(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_openattr(struct nfsrv_descript *, int,
    vnode_t, vnode_t *, fhandle_t *, struct nfsexstuff *);
int nfsrvd_releaselckown(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_pathconf(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_exchangeid(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_createsession(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_sequence(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_reclaimcomplete(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_destroyclientid(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_bindconnsess(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_destroysession(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_freestateid(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_layoutget(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_getdevinfo(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_layoutcommit(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_layoutreturn(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_teststateid(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);
int nfsrvd_notsupp(struct nfsrv_descript *, int,
    vnode_t, struct nfsexstuff *);

/* nfs_nfsdsocket.c */
void nfsrvd_rephead(struct nfsrv_descript *);
void nfsrvd_dorpc(struct nfsrv_descript *, int, u_char *, int, u_int32_t);

/* nfs_nfsdcache.c */
void nfsrvd_initcache(void);
int nfsrvd_getcache(struct nfsrv_descript *);
struct nfsrvcache *nfsrvd_updatecache(struct nfsrv_descript *);
void nfsrvd_sentcache(struct nfsrvcache *, int, uint32_t);
void nfsrvd_cleancache(void);
void nfsrvd_refcache(struct nfsrvcache *);
void nfsrvd_derefcache(struct nfsrvcache *);
void nfsrvd_delcache(struct nfsrvcache *);
void nfsrc_trimcache(uint64_t, uint32_t, int);

/* nfs_commonsubs.c */
void nfscl_reqstart(struct nfsrv_descript *, int, struct nfsmount *,
    u_int8_t *, int, u_int32_t **, struct nfsclsession *, int, int);
void nfsm_stateidtom(struct nfsrv_descript *, nfsv4stateid_t *, int);
void nfscl_fillsattr(struct nfsrv_descript *, struct vattr *,
      vnode_t, int, u_int32_t);
void newnfs_init(void);
int nfsaddr_match(int, union nethostaddr *, NFSSOCKADDR_T);
int nfsaddr2_match(NFSSOCKADDR_T, NFSSOCKADDR_T);
int nfsm_strtom(struct nfsrv_descript *, const char *, int);
int nfsm_mbufuio(struct nfsrv_descript *, struct uio *, int);
int nfsm_fhtom(struct nfsrv_descript *, u_int8_t *, int, int);
int nfsm_advance(struct nfsrv_descript *, int, int);
void *nfsm_dissct(struct nfsrv_descript *, int, int);
void newnfs_trimleading(struct nfsrv_descript *);
void newnfs_trimtrailing(struct nfsrv_descript *, mbuf_t,
    caddr_t);
void newnfs_copycred(struct nfscred *, struct ucred *);
void newnfs_copyincred(struct ucred *, struct nfscred *);
int nfsrv_dissectacl(struct nfsrv_descript *, NFSACL_T *, int *,
    int *, NFSPROC_T *);
int nfsrv_getattrbits(struct nfsrv_descript *, nfsattrbit_t *, int *,
    int *);
int nfsv4_loadattr(struct nfsrv_descript *, vnode_t,
    struct nfsvattr *, struct nfsfh **, fhandle_t *, int,
    struct nfsv3_pathconf *, struct statfs *, struct nfsstatfs *,
    struct nfsfsinfo *, NFSACL_T *,
    int, int *, u_int32_t *, u_int32_t *, NFSPROC_T *, struct ucred *);
int nfsv4_lock(struct nfsv4lock *, int, int *, void *, struct mount *);
void nfsv4_unlock(struct nfsv4lock *, int);
void nfsv4_relref(struct nfsv4lock *);
void nfsv4_getref(struct nfsv4lock *, int *, void *, struct mount *);
int nfsv4_getref_nonblock(struct nfsv4lock *);
int nfsv4_testlock(struct nfsv4lock *);
int nfsrv_mtostr(struct nfsrv_descript *, char *, int);
void nfsrv_cleanusergroup(void);
int nfsrv_checkutf8(u_int8_t *, int);
int newnfs_sndlock(int *);
void newnfs_sndunlock(int *);
int nfsv4_getipaddr(struct nfsrv_descript *, struct sockaddr_in *,
    struct sockaddr_in6 *, sa_family_t *, int *);
int nfsv4_seqsession(uint32_t, uint32_t, uint32_t, struct nfsslot *,
    struct mbuf **, uint16_t);
void nfsv4_seqsess_cacherep(uint32_t, struct nfsslot *, int, struct mbuf **);
void nfsv4_setsequence(struct nfsmount *, struct nfsrv_descript *,
    struct nfsclsession *, int);
int nfsv4_sequencelookup(struct nfsmount *, struct nfsclsession *, int *,
    int *, uint32_t *, uint8_t *);
void nfsv4_freeslot(struct nfsclsession *, int);
struct ucred *nfsrv_getgrpscred(struct ucred *);
struct nfsdevice *nfsv4_findmirror(struct nfsmount *);

/* nfs_clcomsubs.c */
void nfsm_uiombuf(struct nfsrv_descript *, struct uio *, int);
struct mbuf *nfsm_uiombuflist(struct uio *, int, struct mbuf **, char **);
nfsuint64 *nfscl_getcookie(struct nfsnode *, off_t off, int);
u_int8_t *nfscl_getmyip(struct nfsmount *, struct in6_addr *, int *);
int nfsm_getfh(struct nfsrv_descript *, struct nfsfh **);
int nfscl_mtofh(struct nfsrv_descript *, struct nfsfh **,
        struct nfsvattr *, int *);
int nfscl_postop_attr(struct nfsrv_descript *, struct nfsvattr *, int *,
    void *);
int nfscl_wcc_data(struct nfsrv_descript *, vnode_t,
    struct nfsvattr *, int *, int *, void *);
int nfsm_loadattr(struct nfsrv_descript *, struct nfsvattr *);
int nfscl_request(struct nfsrv_descript *, vnode_t,
         NFSPROC_T *, struct ucred *, void *);

/* nfs_nfsdsubs.c */
void nfsd_fhtovp(struct nfsrv_descript *, struct nfsrvfh *, int,
    vnode_t *, struct nfsexstuff *, mount_t *, int);
int nfsd_excred(struct nfsrv_descript *, struct nfsexstuff *, struct ucred *);
int nfsrv_mtofh(struct nfsrv_descript *, struct nfsrvfh *);
int nfsrv_putattrbit(struct nfsrv_descript *, nfsattrbit_t *);
void nfsrv_wcc(struct nfsrv_descript *, int, struct nfsvattr *, int,
    struct nfsvattr *);
int nfsv4_fillattr(struct nfsrv_descript *, struct mount *, vnode_t, NFSACL_T *,
    struct vattr *, fhandle_t *, int, nfsattrbit_t *,
    struct ucred *, NFSPROC_T *, int, int, int, int, uint64_t, struct statfs *);
void nfsrv_fillattr(struct nfsrv_descript *, struct nfsvattr *);
void nfsrv_adj(mbuf_t, int, int);
void nfsrv_postopattr(struct nfsrv_descript *, int, struct nfsvattr *);
int nfsd_errmap(struct nfsrv_descript *);
void nfsv4_uidtostr(uid_t, u_char **, int *);
int nfsv4_strtouid(struct nfsrv_descript *, u_char *, int, uid_t *);
void nfsv4_gidtostr(gid_t, u_char **, int *);
int nfsv4_strtogid(struct nfsrv_descript *, u_char *, int, gid_t *);
int nfsrv_checkuidgid(struct nfsrv_descript *, struct nfsvattr *);
void nfsrv_fixattr(struct nfsrv_descript *, vnode_t,
    struct nfsvattr *, NFSACL_T *, NFSPROC_T *, nfsattrbit_t *,
    struct nfsexstuff *);
int nfsrv_errmoved(int);
int nfsrv_putreferralattr(struct nfsrv_descript *, nfsattrbit_t *,
    struct nfsreferral *, int, int *);
int nfsrv_parsename(struct nfsrv_descript *, char *, u_long *,
    NFSPATHLEN_T *);
void nfsd_init(void);
int nfsd_checkrootexp(struct nfsrv_descript *);
void nfsd_getminorvers(struct nfsrv_descript *, u_char *, u_char **, int *,
    u_int32_t *);

/* nfs_clvfsops.c */
void nfscl_retopts(struct nfsmount *, char *, size_t);

/* nfs_commonport.c */
int nfsrv_lookupfilename(struct nameidata *, char *, NFSPROC_T *);
void nfsrv_object_create(vnode_t, NFSPROC_T *);
int nfsrv_mallocmget_limit(void);
int nfsvno_v4rootexport(struct nfsrv_descript *);
void newnfs_portinit(void);
struct ucred *newnfs_getcred(void);
void newnfs_setroot(struct ucred *);
int nfs_catnap(int, int, const char *);
struct nfsreferral *nfsv4root_getreferral(vnode_t, vnode_t, u_int32_t);
int nfsvno_pathconf(vnode_t, int, long *, struct ucred *, NFSPROC_T *);
int nfsrv_atroot(vnode_t, uint64_t *);
void newnfs_timer(void *);
int nfs_supportsnfsv4acls(vnode_t);

/* nfs_commonacl.c */
int nfsrv_dissectace(struct nfsrv_descript *, struct acl_entry *,
    int *, int *, NFSPROC_T *);
int nfsrv_buildacl(struct nfsrv_descript *, NFSACL_T *, enum vtype,
    NFSPROC_T *);
int nfsrv_compareacl(NFSACL_T *, NFSACL_T *);

/* nfs_clrpcops.c */
int nfsrpc_null(vnode_t, struct ucred *, NFSPROC_T *);
int nfsrpc_access(vnode_t, int, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, int *);
int nfsrpc_accessrpc(vnode_t, u_int32_t, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, u_int32_t *, void *);
int nfsrpc_open(vnode_t, int, struct ucred *, NFSPROC_T *);
int nfsrpc_openrpc(struct nfsmount *, vnode_t, u_int8_t *, int, u_int8_t *, int,
    u_int32_t, struct nfsclopen *, u_int8_t *, int, struct nfscldeleg **, int,
    u_int32_t, struct ucred *, NFSPROC_T *, int, int);
int nfsrpc_opendowngrade(vnode_t, u_int32_t, struct nfsclopen *,
    struct ucred *, NFSPROC_T *);
int nfsrpc_close(vnode_t, int, NFSPROC_T *);
int nfsrpc_closerpc(struct nfsrv_descript *, struct nfsmount *,
    struct nfsclopen *, struct ucred *, NFSPROC_T *, int);
int nfsrpc_openconfirm(vnode_t, u_int8_t *, int, struct nfsclopen *,
    struct ucred *, NFSPROC_T *);
int nfsrpc_setclient(struct nfsmount *, struct nfsclclient *, int,
    struct ucred *, NFSPROC_T *);
int nfsrpc_getattr(vnode_t, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, void *);
int nfsrpc_getattrnovp(struct nfsmount *, u_int8_t *, int, int,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, u_int64_t *, uint32_t *);
int nfsrpc_setattr(vnode_t, struct vattr *, NFSACL_T *, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_lookup(vnode_t, char *, int, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, struct nfsvattr *, struct nfsfh **, int *, int *,
    void *);
int nfsrpc_readlink(vnode_t, struct uio *, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_read(vnode_t, struct uio *, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, int *, void *);
int nfsrpc_write(vnode_t, struct uio *, int *, int *,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, int *, void *, int);
int nfsrpc_mknod(vnode_t, char *, int, struct vattr *, u_int32_t,
    enum vtype, struct ucred *, NFSPROC_T *, struct nfsvattr *,
    struct nfsvattr *, struct nfsfh **, int *, int *, void *);
int nfsrpc_create(vnode_t, char *, int, struct vattr *, nfsquad_t,
    int, struct ucred *, NFSPROC_T *, struct nfsvattr *, struct nfsvattr *,
    struct nfsfh **, int *, int *, void *);
int nfsrpc_remove(vnode_t, char *, int, vnode_t, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, int *, void *);
int nfsrpc_rename(vnode_t, vnode_t, char *, int, vnode_t, vnode_t, char *, int,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, struct nfsvattr *,
    int *, int *, void *, void *);
int nfsrpc_link(vnode_t, vnode_t, char *, int,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, struct nfsvattr *,
    int *, int *, void *);
int nfsrpc_symlink(vnode_t, char *, int, const char *, struct vattr *,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, struct nfsvattr *,
    struct nfsfh **, int *, int *, void *);
int nfsrpc_mkdir(vnode_t, char *, int, struct vattr *,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, struct nfsvattr *,
    struct nfsfh **, int *, int *, void *);
int nfsrpc_rmdir(vnode_t, char *, int, struct ucred *, NFSPROC_T *,
    struct nfsvattr *, int *, void *);
int nfsrpc_readdir(vnode_t, struct uio *, nfsuint64 *, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, int *, void *);
int nfsrpc_readdirplus(vnode_t, struct uio *, nfsuint64 *, 
    struct ucred *, NFSPROC_T *, struct nfsvattr *, int *, int *, void *);
int nfsrpc_commit(vnode_t, u_quad_t, int, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_advlock(vnode_t, off_t, int, struct flock *, int,
    struct ucred *, NFSPROC_T *, void *, int);
int nfsrpc_lockt(struct nfsrv_descript *, vnode_t,
    struct nfsclclient *, u_int64_t, u_int64_t, struct flock *,
    struct ucred *, NFSPROC_T *, void *, int);
int nfsrpc_lock(struct nfsrv_descript *, struct nfsmount *, vnode_t,
    u_int8_t *, int, struct nfscllockowner *, int, int, u_int64_t,
    u_int64_t, short, struct ucred *, NFSPROC_T *, int);
int nfsrpc_statfs(vnode_t, struct nfsstatfs *, struct nfsfsinfo *,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_fsinfo(vnode_t, struct nfsfsinfo *, struct ucred *,
    NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_pathconf(vnode_t, struct nfsv3_pathconf *,
    struct ucred *, NFSPROC_T *, struct nfsvattr *, int *, void *);
int nfsrpc_renew(struct nfsclclient *, struct nfsclds *, struct ucred *,
    NFSPROC_T *);
int nfsrpc_rellockown(struct nfsmount *, struct nfscllockowner *, uint8_t *,
    int, struct ucred *, NFSPROC_T *);
int nfsrpc_getdirpath(struct nfsmount *, u_char *, struct ucred *,
    NFSPROC_T *);
int nfsrpc_delegreturn(struct nfscldeleg *, struct ucred *,
    struct nfsmount *, NFSPROC_T *, int);
int nfsrpc_getacl(vnode_t, struct ucred *, NFSPROC_T *, NFSACL_T *, void *);
int nfsrpc_setacl(vnode_t, struct ucred *, NFSPROC_T *, NFSACL_T *, void *);
int nfsrpc_exchangeid(struct nfsmount *, struct nfsclclient *,
    struct nfssockreq *, uint32_t, struct nfsclds **, struct ucred *,
    NFSPROC_T *);
int nfsrpc_createsession(struct nfsmount *, struct nfsclsession *,
    struct nfssockreq *, uint32_t, int, struct ucred *, NFSPROC_T *);
int nfsrpc_destroysession(struct nfsmount *, struct nfsclclient *,
    struct ucred *, NFSPROC_T *);
int nfsrpc_destroyclient(struct nfsmount *, struct nfsclclient *,
    struct ucred *, NFSPROC_T *);
int nfsrpc_getdeviceinfo(struct nfsmount *, uint8_t *, int, uint32_t *,
    struct nfscldevinfo **, struct ucred *, NFSPROC_T *);
int nfsrpc_layoutcommit(struct nfsmount *, uint8_t *, int, int,
    uint64_t, uint64_t, uint64_t, nfsv4stateid_t *, int, struct ucred *,
    NFSPROC_T *, void *);
int nfsrpc_layoutreturn(struct nfsmount *, uint8_t *, int, int, int, uint32_t,
    int, uint64_t, uint64_t, nfsv4stateid_t *, struct ucred *, NFSPROC_T *,
    uint32_t, uint32_t, char *);
int nfsrpc_reclaimcomplete(struct nfsmount *, struct ucred *, NFSPROC_T *);
int nfscl_doiods(vnode_t, struct uio *, int *, int *, uint32_t, int,
    struct ucred *, NFSPROC_T *);
int nfscl_findlayoutforio(struct nfscllayout *, uint64_t, uint32_t,
    struct nfsclflayout **);
void nfscl_freenfsclds(struct nfsclds *);

/* nfs_clstate.c */
int nfscl_open(vnode_t, u_int8_t *, int, u_int32_t, int,
    struct ucred *, NFSPROC_T *, struct nfsclowner **, struct nfsclopen **,
    int *, int *, int);
int nfscl_getstateid(vnode_t, u_int8_t *, int, u_int32_t, int, struct ucred *,
    NFSPROC_T *, nfsv4stateid_t *, void **);
void nfscl_ownerrelease(struct nfsmount *, struct nfsclowner *, int, int, int);
void nfscl_openrelease(struct nfsmount *, struct nfsclopen *, int, int);
int nfscl_getcl(struct mount *, struct ucred *, NFSPROC_T *, int,
    struct nfsclclient **);
struct nfsclclient *nfscl_findcl(struct nfsmount *);
void nfscl_clientrelease(struct nfsclclient *);
void nfscl_freelock(struct nfscllock *, int);
void nfscl_freelockowner(struct nfscllockowner *, int);
int nfscl_getbytelock(vnode_t, u_int64_t, u_int64_t, short,
    struct ucred *, NFSPROC_T *, struct nfsclclient *, int, void *, int,
    u_int8_t *, u_int8_t *, struct nfscllockowner **, int *, int *);
int nfscl_relbytelock(vnode_t, u_int64_t, u_int64_t,
    struct ucred *, NFSPROC_T *, int, struct nfsclclient *,
    void *, int, struct nfscllockowner **, int *);
int nfscl_checkwritelocked(vnode_t, struct flock *,
    struct ucred *, NFSPROC_T *, void *, int);
void nfscl_lockrelease(struct nfscllockowner *, int, int);
void nfscl_fillclid(u_int64_t, char *, u_int8_t *, u_int16_t);
void nfscl_filllockowner(void *, u_int8_t *, int);
void nfscl_freeopen(struct nfsclopen *, int);
void nfscl_umount(struct nfsmount *, NFSPROC_T *);
void nfscl_renewthread(struct nfsclclient *, NFSPROC_T *);
void nfscl_initiate_recovery(struct nfsclclient *);
int nfscl_hasexpired(struct nfsclclient *, u_int32_t, NFSPROC_T *);
void nfscl_dumpstate(struct nfsmount *, int, int, int, int);
void nfscl_dupopen(vnode_t, int);
int nfscl_getclose(vnode_t, struct nfsclclient **);
int nfscl_doclose(vnode_t, struct nfsclclient **, NFSPROC_T *);
void nfsrpc_doclose(struct nfsmount *, struct nfsclopen *, NFSPROC_T *);
int nfscl_deleg(mount_t, struct nfsclclient *, u_int8_t *, int,
    struct ucred *, NFSPROC_T *, struct nfscldeleg **);
void nfscl_lockinit(struct nfsv4lock *);
void nfscl_lockexcl(struct nfsv4lock *, void *);
void nfscl_lockunlock(struct nfsv4lock *);
void nfscl_lockderef(struct nfsv4lock *);
void nfscl_docb(struct nfsrv_descript *, NFSPROC_T *);
void nfscl_releasealllocks(struct nfsclclient *, vnode_t, NFSPROC_T *, void *,
    int);
int nfscl_lockt(vnode_t, struct nfsclclient *, u_int64_t,
    u_int64_t, struct flock *, NFSPROC_T *, void *, int);
int nfscl_mustflush(vnode_t);
int nfscl_nodeleg(vnode_t, int);
int nfscl_removedeleg(vnode_t, NFSPROC_T *, nfsv4stateid_t *);
int nfscl_getref(struct nfsmount *);
void nfscl_relref(struct nfsmount *);
int nfscl_renamedeleg(vnode_t, nfsv4stateid_t *, int *, vnode_t,
    nfsv4stateid_t *, int *, NFSPROC_T *);
void nfscl_reclaimnode(vnode_t);
void nfscl_newnode(vnode_t);
void nfscl_delegmodtime(vnode_t);
void nfscl_deleggetmodtime(vnode_t, struct timespec *);
int nfscl_tryclose(struct nfsclopen *, struct ucred *,
    struct nfsmount *, NFSPROC_T *);
void nfscl_cleanup(NFSPROC_T *);
int nfscl_layout(struct nfsmount *, vnode_t, u_int8_t *, int, nfsv4stateid_t *,
    int, int, struct nfsclflayouthead *, struct nfscllayout **, struct ucred *,
    NFSPROC_T *);
struct nfscllayout *nfscl_getlayout(struct nfsclclient *, uint8_t *, int,
    uint64_t, struct nfsclflayout **, int *);
void nfscl_dserr(uint32_t, uint32_t, struct nfscldevinfo *,
    struct nfscllayout *, struct nfsclds *);
void nfscl_cancelreqs(struct nfsclds *);
void nfscl_rellayout(struct nfscllayout *, int);
struct nfscldevinfo *nfscl_getdevinfo(struct nfsclclient *, uint8_t *,
    struct nfscldevinfo *);
void nfscl_reldevinfo(struct nfscldevinfo *);
int nfscl_adddevinfo(struct nfsmount *, struct nfscldevinfo *, int,
    struct nfsclflayout *);
void nfscl_freelayout(struct nfscllayout *);
void nfscl_freeflayout(struct nfsclflayout *);
void nfscl_freedevinfo(struct nfscldevinfo *);
int nfscl_layoutcommit(vnode_t, NFSPROC_T *);

/* nfs_clport.c */
int nfscl_nget(mount_t, vnode_t, struct nfsfh *,
    struct componentname *, NFSPROC_T *, struct nfsnode **, void *, int);
NFSPROC_T *nfscl_getparent(NFSPROC_T *);
void nfscl_start_renewthread(struct nfsclclient *);
void nfscl_loadsbinfo(struct nfsmount *, struct nfsstatfs *, void *);
void nfscl_loadfsinfo (struct nfsmount *, struct nfsfsinfo *);
void nfscl_delegreturn(struct nfscldeleg *, int, struct nfsmount *,
    struct ucred *, NFSPROC_T *);
void nfsrvd_cbinit(int);
int nfscl_checksattr(struct vattr *, struct nfsvattr *);
int nfscl_ngetreopen(mount_t, u_int8_t *, int, NFSPROC_T *,
    struct nfsnode **);
int nfscl_procdoesntexist(u_int8_t *);
int nfscl_maperr(NFSPROC_T *, int, uid_t, gid_t);

/* nfs_clsubs.c */
void nfscl_init(void);

/* nfs_clbio.c */
int ncl_flush(vnode_t, int, NFSPROC_T *, int, int);

/* nfs_clnode.c */
void ncl_invalcaches(vnode_t);

/* nfs_nfsdport.c */
int nfsvno_getattr(vnode_t, struct nfsvattr *, struct nfsrv_descript *,
    NFSPROC_T *, int, nfsattrbit_t *);
int nfsvno_setattr(vnode_t, struct nfsvattr *, struct ucred *,
    NFSPROC_T *, struct nfsexstuff *);
int nfsvno_getfh(vnode_t, fhandle_t *, NFSPROC_T *);
int nfsvno_accchk(vnode_t, accmode_t, struct ucred *,
    struct nfsexstuff *, NFSPROC_T *, int, int, u_int32_t *);
int nfsvno_namei(struct nfsrv_descript *, struct nameidata *,
    vnode_t, int, struct nfsexstuff *, NFSPROC_T *, vnode_t *);
void nfsvno_setpathbuf(struct nameidata *, char **, u_long **);
void nfsvno_relpathbuf(struct nameidata *);
int nfsvno_readlink(vnode_t, struct ucred *, NFSPROC_T *, mbuf_t *,
    mbuf_t *, int *);
int nfsvno_read(vnode_t, off_t, int, struct ucred *, NFSPROC_T *,
    mbuf_t *, mbuf_t *);
int nfsvno_write(vnode_t, off_t, int, int, int *, mbuf_t,
    char *, struct ucred *, NFSPROC_T *);
int nfsvno_createsub(struct nfsrv_descript *, struct nameidata *,
    vnode_t *, struct nfsvattr *, int *, int32_t *, NFSDEV_T,
    struct nfsexstuff *);
int nfsvno_mknod(struct nameidata *, struct nfsvattr *, struct ucred *,
    NFSPROC_T *);
int nfsvno_mkdir(struct nameidata *,
    struct nfsvattr *, uid_t, struct ucred *, NFSPROC_T *,
    struct nfsexstuff *);
int nfsvno_symlink(struct nameidata *, struct nfsvattr *, char *, int, int,
    uid_t, struct ucred *, NFSPROC_T *, struct nfsexstuff *);
int nfsvno_getsymlink(struct nfsrv_descript *, struct nfsvattr *,
    NFSPROC_T *, char **, int *);
int nfsvno_removesub(struct nameidata *, int, struct ucred *, NFSPROC_T *,
    struct nfsexstuff *);
int nfsvno_rmdirsub(struct nameidata *, int, struct ucred *, NFSPROC_T *,
    struct nfsexstuff *);
int nfsvno_rename(struct nameidata *, struct nameidata *, u_int32_t,
    u_int32_t, struct ucred *, NFSPROC_T *);
int nfsvno_link(struct nameidata *, vnode_t, struct ucred *,
    NFSPROC_T *, struct nfsexstuff *);
int nfsvno_fsync(vnode_t, u_int64_t, int, struct ucred *, NFSPROC_T *);
int nfsvno_statfs(vnode_t, struct statfs *);
void nfsvno_getfs(struct nfsfsinfo *, int);
void nfsvno_open(struct nfsrv_descript *, struct nameidata *, nfsquad_t,
    nfsv4stateid_t *, struct nfsstate *, int *, struct nfsvattr *, int32_t *,
    int, NFSACL_T *, nfsattrbit_t *, struct ucred *,
    struct nfsexstuff *, vnode_t *);
int nfsvno_updfilerev(vnode_t, struct nfsvattr *, struct nfsrv_descript *,
    NFSPROC_T *);
int nfsvno_fillattr(struct nfsrv_descript *, struct mount *, vnode_t,
    struct nfsvattr *, fhandle_t *, int, nfsattrbit_t *,
    struct ucred *, NFSPROC_T *, int, int, int, int, uint64_t);
int nfsrv_sattr(struct nfsrv_descript *, vnode_t, struct nfsvattr *, nfsattrbit_t *,
    NFSACL_T *, NFSPROC_T *);
int nfsv4_sattr(struct nfsrv_descript *, vnode_t, struct nfsvattr *, nfsattrbit_t *,
    NFSACL_T *, NFSPROC_T *);
int nfsvno_checkexp(mount_t, NFSSOCKADDR_T, struct nfsexstuff *,
    struct ucred **);
int nfsvno_fhtovp(mount_t, fhandle_t *, NFSSOCKADDR_T, int,
    vnode_t *, struct nfsexstuff *, struct ucred **);
vnode_t nfsvno_getvp(fhandle_t *);
int nfsvno_advlock(vnode_t, int, u_int64_t, u_int64_t, NFSPROC_T *);
int nfsrv_v4rootexport(void *, struct ucred *, NFSPROC_T *);
int nfsvno_testexp(struct nfsrv_descript *, struct nfsexstuff *);
uint32_t nfsrv_hashfh(fhandle_t *);
uint32_t nfsrv_hashsessionid(uint8_t *);
void nfsrv_backupstable(void);
int nfsrv_dsgetdevandfh(struct vnode *, NFSPROC_T *, int *, fhandle_t *,
    char *);
int nfsrv_dsgetsockmnt(struct vnode *, int, char *, int *, int *,
    NFSPROC_T *, struct vnode **, fhandle_t *, char *, char *,
    struct vnode **, struct nfsmount **, struct nfsmount *, int *, int *);
int nfsrv_dscreate(struct vnode *, struct vattr *, struct vattr *,
    fhandle_t *, struct pnfsdsfile *, struct pnfsdsattr *, char *,
    struct ucred *, NFSPROC_T *, struct vnode **);
int nfsrv_updatemdsattr(struct vnode *, struct nfsvattr *, NFSPROC_T *);
void nfsrv_killrpcs(struct nfsmount *);
int nfsrv_setacl(struct vnode *, NFSACL_T *, struct ucred *, NFSPROC_T *);

/* nfs_commonkrpc.c */
int newnfs_nmcancelreqs(struct nfsmount *);
void newnfs_set_sigmask(struct thread *, sigset_t *);
void newnfs_restore_sigmask(struct thread *, sigset_t *);
int newnfs_msleep(struct thread *, void *, struct mtx *, int, char *, int);
int newnfs_request(struct nfsrv_descript *, struct nfsmount *,
    struct nfsclient *, struct nfssockreq *, vnode_t, NFSPROC_T *,
    struct ucred *, u_int32_t, u_int32_t, u_char *, int, u_int64_t *,
    struct nfsclsession *);
int newnfs_connect(struct nfsmount *, struct nfssockreq *,
    struct ucred *, NFSPROC_T *, int);
void newnfs_disconnect(struct nfssockreq *);
int newnfs_sigintr(struct nfsmount *, NFSPROC_T *);

/* nfs_nfsdkrpc.c */
int nfsrvd_addsock(struct file *);
int nfsrvd_nfsd(NFSPROC_T *, struct nfsd_nfsd_args *);
void nfsrvd_init(int);

/* nfs_clkrpc.c */
int nfscbd_addsock(struct file *);
int nfscbd_nfsd(NFSPROC_T *, struct nfsd_nfscbd_args *);

