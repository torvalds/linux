/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/lockd/xdr4.h
 *
 * XDR types for the NLM protocol
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LOCKD_XDR4_H
#define LOCKD_XDR4_H

#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/lockd/xdr.h>

/* error codes new to NLMv4 */
#define	nlm4_deadlock		cpu_to_be32(NLM_DEADLCK)
#define	nlm4_rofs		cpu_to_be32(NLM_ROFS)
#define	nlm4_stale_fh		cpu_to_be32(NLM_STALE_FH)
#define	nlm4_fbig		cpu_to_be32(NLM_FBIG)
#define	nlm4_failed		cpu_to_be32(NLM_FAILED)



int	nlm4svc_decode_testargs(struct svc_rqst *, __be32 *);
int	nlm4svc_encode_testres(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_lockargs(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_cancargs(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_unlockargs(struct svc_rqst *, __be32 *);
int	nlm4svc_encode_res(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_res(struct svc_rqst *, __be32 *);
int	nlm4svc_encode_void(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_void(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_shareargs(struct svc_rqst *, __be32 *);
int	nlm4svc_encode_shareres(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_notify(struct svc_rqst *, __be32 *);
int	nlm4svc_decode_reboot(struct svc_rqst *, __be32 *);
/*
int	nlmclt_encode_testargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_lockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_cancargs(struct rpc_rqst *, u32 *, struct nlm_args *);
int	nlmclt_encode_unlockargs(struct rpc_rqst *, u32 *, struct nlm_args *);
 */
extern const struct rpc_version nlm_version4;

#endif /* LOCKD_XDR4_H */
