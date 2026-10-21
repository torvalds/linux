/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nfsd-specific authentication stuff.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_AUTH_H
#define LINUX_NFSD_AUTH_H

struct user_namespace;
struct svc_export;
struct svc_rqst;

/*
 * Set the current process's fsuid/fsgid etc to those of the NFS
 * client user
 */
int nfsd_setuser(struct svc_cred *cred, struct svc_export *exp);

struct user_namespace *nfsd_user_namespace(const struct svc_rqst *rqstp);

#endif /* LINUX_NFSD_AUTH_H */
