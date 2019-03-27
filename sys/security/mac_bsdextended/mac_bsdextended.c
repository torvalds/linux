/*-
 * Copyright (c) 1999-2002, 2007-2008 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005 Tom Rhodes
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 * It was later enhanced by Tom Rhodes for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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
 *
 * "BSD Extended" MAC policy, allowing the administrator to impose mandatory
 * firewall-like rules regarding users and file system objects.
 */

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/kernel.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/stat.h>

#include <security/mac/mac_policy.h>
#include <security/mac_bsdextended/mac_bsdextended.h>
#include <security/mac_bsdextended/ugidfw_internal.h>

static struct mtx ugidfw_mtx;

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, bsdextended, CTLFLAG_RW, 0,
    "TrustedBSD extended BSD MAC policy controls");

static int	ugidfw_enabled = 1;
SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, enabled, CTLFLAG_RWTUN,
    &ugidfw_enabled, 0, "Enforce extended BSD policy");

static MALLOC_DEFINE(M_MACBSDEXTENDED, "mac_bsdextended",
    "BSD Extended MAC rule");

#define	MAC_BSDEXTENDED_MAXRULES	250
static struct mac_bsdextended_rule *rules[MAC_BSDEXTENDED_MAXRULES];
static int rule_count = 0;
static int rule_slots = 0;
static int rule_version = MB_VERSION;

SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, rule_count, CTLFLAG_RD,
    &rule_count, 0, "Number of defined rules\n");
SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, rule_slots, CTLFLAG_RD,
    &rule_slots, 0, "Number of used rule slots\n");
SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, rule_version, CTLFLAG_RD,
    &rule_version, 0, "Version number for API\n");

/*
 * This is just used for logging purposes, eventually we would like to log
 * much more then failed requests.
 */
static int ugidfw_logging;
SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, logging, CTLFLAG_RW,
    &ugidfw_logging, 0, "Log failed authorization requests");

/*
 * This tunable is here for compatibility.  It will allow the user to switch
 * between the new mode (first rule matches) and the old functionality (all
 * rules match).
 */
static int ugidfw_firstmatch_enabled;
SYSCTL_INT(_security_mac_bsdextended, OID_AUTO, firstmatch_enabled,
    CTLFLAG_RW, &ugidfw_firstmatch_enabled, 1,
    "Disable/enable match first rule functionality");

static int
ugidfw_rule_valid(struct mac_bsdextended_rule *rule)
{

	if ((rule->mbr_subject.mbs_flags | MBS_ALL_FLAGS) != MBS_ALL_FLAGS)
		return (EINVAL);
	if ((rule->mbr_subject.mbs_neg | MBS_ALL_FLAGS) != MBS_ALL_FLAGS)
		return (EINVAL);
	if ((rule->mbr_object.mbo_flags | MBO_ALL_FLAGS) != MBO_ALL_FLAGS)
		return (EINVAL);
	if ((rule->mbr_object.mbo_neg | MBO_ALL_FLAGS) != MBO_ALL_FLAGS)
		return (EINVAL);
	if (((rule->mbr_object.mbo_flags & MBO_TYPE_DEFINED) != 0) &&
	    (rule->mbr_object.mbo_type | MBO_ALL_TYPE) != MBO_ALL_TYPE)
		return (EINVAL);
	if ((rule->mbr_mode | MBI_ALLPERM) != MBI_ALLPERM)
		return (EINVAL);
	return (0);
}

static int
sysctl_rule(SYSCTL_HANDLER_ARGS)
{
	struct mac_bsdextended_rule temprule, *ruleptr;
	u_int namelen;
	int error, index, *name;

	error = 0;
	name = (int *)arg1;
	namelen = arg2;
	if (namelen != 1)
		return (EINVAL);
	index = name[0];
        if (index >= MAC_BSDEXTENDED_MAXRULES)
		return (ENOENT);

	ruleptr = NULL;
	if (req->newptr && req->newlen != 0) {
		error = SYSCTL_IN(req, &temprule, sizeof(temprule));
		if (error)
			return (error);
		ruleptr = malloc(sizeof(*ruleptr), M_MACBSDEXTENDED,
		    M_WAITOK | M_ZERO);
	}

	mtx_lock(&ugidfw_mtx);
	if (req->oldptr) {
		if (index < 0 || index > rule_slots + 1) {
			error = ENOENT;
			goto out;
		}
		if (rules[index] == NULL) {
			error = ENOENT;
			goto out;
		}
		temprule = *rules[index];
	}
	if (req->newptr && req->newlen == 0) {
		KASSERT(ruleptr == NULL, ("sysctl_rule: ruleptr != NULL"));
		ruleptr = rules[index];
		if (ruleptr == NULL) {
			error = ENOENT;
			goto out;
		}
		rule_count--;
		rules[index] = NULL;
	} else if (req->newptr) {
		error = ugidfw_rule_valid(&temprule);
		if (error)
			goto out;
		if (rules[index] == NULL) {
			*ruleptr = temprule;
			rules[index] = ruleptr;
			ruleptr = NULL;
			if (index + 1 > rule_slots)
				rule_slots = index + 1;
			rule_count++;
		} else
			*rules[index] = temprule;
	}
out:
	mtx_unlock(&ugidfw_mtx);
	if (ruleptr != NULL)
		free(ruleptr, M_MACBSDEXTENDED);
	if (req->oldptr && error == 0)
		error = SYSCTL_OUT(req, &temprule, sizeof(temprule));
	return (error);
}

static SYSCTL_NODE(_security_mac_bsdextended, OID_AUTO, rules,
    CTLFLAG_MPSAFE | CTLFLAG_RW, sysctl_rule, "BSD extended MAC rules");

static void
ugidfw_init(struct mac_policy_conf *mpc)
{

	mtx_init(&ugidfw_mtx, "mac_bsdextended lock", NULL, MTX_DEF);
}

static void
ugidfw_destroy(struct mac_policy_conf *mpc)
{
	int i;

	for (i = 0; i < MAC_BSDEXTENDED_MAXRULES; i++) {
		if (rules[i] != NULL)
			free(rules[i], M_MACBSDEXTENDED);
	}
	mtx_destroy(&ugidfw_mtx);
}

static int
ugidfw_rulecheck(struct mac_bsdextended_rule *rule,
    struct ucred *cred, struct vnode *vp, struct vattr *vap, int acc_mode)
{
	int mac_granted, match, priv_granted;
	int i;

	/*
	 * Is there a subject match?
	 */
	mtx_assert(&ugidfw_mtx, MA_OWNED);
	if (rule->mbr_subject.mbs_flags & MBS_UID_DEFINED) {
		match =  ((cred->cr_uid <= rule->mbr_subject.mbs_uid_max &&
		    cred->cr_uid >= rule->mbr_subject.mbs_uid_min) ||
		    (cred->cr_ruid <= rule->mbr_subject.mbs_uid_max &&
		    cred->cr_ruid >= rule->mbr_subject.mbs_uid_min) ||
		    (cred->cr_svuid <= rule->mbr_subject.mbs_uid_max &&
		    cred->cr_svuid >= rule->mbr_subject.mbs_uid_min));
		if (rule->mbr_subject.mbs_neg & MBS_UID_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_subject.mbs_flags & MBS_GID_DEFINED) {
		match = ((cred->cr_rgid <= rule->mbr_subject.mbs_gid_max &&
		    cred->cr_rgid >= rule->mbr_subject.mbs_gid_min) ||
		    (cred->cr_svgid <= rule->mbr_subject.mbs_gid_max &&
		    cred->cr_svgid >= rule->mbr_subject.mbs_gid_min));
		if (!match) {
			for (i = 0; i < cred->cr_ngroups; i++) {
				if (cred->cr_groups[i]
				    <= rule->mbr_subject.mbs_gid_max &&
				    cred->cr_groups[i]
				    >= rule->mbr_subject.mbs_gid_min) {
					match = 1;
					break;
				}
			}
		}
		if (rule->mbr_subject.mbs_neg & MBS_GID_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_subject.mbs_flags & MBS_PRISON_DEFINED) {
		match =
		    (cred->cr_prison->pr_id == rule->mbr_subject.mbs_prison);
		if (rule->mbr_subject.mbs_neg & MBS_PRISON_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	/*
	 * Is there an object match?
	 */
	if (rule->mbr_object.mbo_flags & MBO_UID_DEFINED) {
		match = (vap->va_uid <= rule->mbr_object.mbo_uid_max &&
		    vap->va_uid >= rule->mbr_object.mbo_uid_min);
		if (rule->mbr_object.mbo_neg & MBO_UID_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_GID_DEFINED) {
		match = (vap->va_gid <= rule->mbr_object.mbo_gid_max &&
		    vap->va_gid >= rule->mbr_object.mbo_gid_min);
		if (rule->mbr_object.mbo_neg & MBO_GID_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_FSID_DEFINED) {
		match = (bcmp(&(vp->v_mount->mnt_stat.f_fsid),
		    &(rule->mbr_object.mbo_fsid),
		    sizeof(rule->mbr_object.mbo_fsid)) == 0);
		if (rule->mbr_object.mbo_neg & MBO_FSID_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_SUID) {
		match = (vap->va_mode & S_ISUID);
		if (rule->mbr_object.mbo_neg & MBO_SUID)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_SGID) {
		match = (vap->va_mode & S_ISGID);
		if (rule->mbr_object.mbo_neg & MBO_SGID)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_UID_SUBJECT) {
		match = (vap->va_uid == cred->cr_uid ||
		    vap->va_uid == cred->cr_ruid ||
		    vap->va_uid == cred->cr_svuid);
		if (rule->mbr_object.mbo_neg & MBO_UID_SUBJECT)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_GID_SUBJECT) {
		match = (groupmember(vap->va_gid, cred) ||
		    vap->va_gid == cred->cr_rgid ||
		    vap->va_gid == cred->cr_svgid);
		if (rule->mbr_object.mbo_neg & MBO_GID_SUBJECT)
			match = !match;
		if (!match)
			return (0);
	}

	if (rule->mbr_object.mbo_flags & MBO_TYPE_DEFINED) {
		switch (vap->va_type) {
		case VREG:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_REG);
			break;
		case VDIR:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_DIR);
			break;
		case VBLK:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_BLK);
			break;
		case VCHR:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_CHR);
			break;
		case VLNK:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_LNK);
			break;
		case VSOCK:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_SOCK);
			break;
		case VFIFO:
			match = (rule->mbr_object.mbo_type & MBO_TYPE_FIFO);
			break;
		default:
			match = 0;
		}
		if (rule->mbr_object.mbo_neg & MBO_TYPE_DEFINED)
			match = !match;
		if (!match)
			return (0);
	}

	/*
	 * MBI_APPEND should not be here as it should get converted to
	 * MBI_WRITE.
	 */
	priv_granted = 0;
	mac_granted = rule->mbr_mode;
	if ((acc_mode & MBI_ADMIN) && (mac_granted & MBI_ADMIN) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_ADMIN) == 0)
		priv_granted |= MBI_ADMIN;
	if ((acc_mode & MBI_EXEC) && (mac_granted & MBI_EXEC) == 0 &&
	    priv_check_cred(cred, (vap->va_type == VDIR) ? PRIV_VFS_LOOKUP : PRIV_VFS_EXEC) == 0)
		priv_granted |= MBI_EXEC;
	if ((acc_mode & MBI_READ) && (mac_granted & MBI_READ) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_READ) == 0)
		priv_granted |= MBI_READ;
	if ((acc_mode & MBI_STAT) && (mac_granted & MBI_STAT) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_STAT) == 0)
		priv_granted |= MBI_STAT;
	if ((acc_mode & MBI_WRITE) && (mac_granted & MBI_WRITE) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_WRITE) == 0)
		priv_granted |= MBI_WRITE;
	/*
	 * Is the access permitted?
	 */
	if (((mac_granted | priv_granted) & acc_mode) != acc_mode) {
		if (ugidfw_logging)
			log(LOG_AUTHPRIV, "mac_bsdextended: %d:%d request %d"
			    " on %d:%d failed. \n", cred->cr_ruid,
			    cred->cr_rgid, acc_mode, vap->va_uid,
			    vap->va_gid);
		return (EACCES);
	}

	/*
	 * If the rule matched, permits access, and first match is enabled,
	 * return success.
	 */
	if (ugidfw_firstmatch_enabled)
		return (EJUSTRETURN);
	else
		return (0);
}

int
ugidfw_check(struct ucred *cred, struct vnode *vp, struct vattr *vap,
    int acc_mode)
{
	int error, i;

	/*
	 * Since we do not separately handle append, map append to write.
	 */
	if (acc_mode & MBI_APPEND) {
		acc_mode &= ~MBI_APPEND;
		acc_mode |= MBI_WRITE;
	}
	mtx_lock(&ugidfw_mtx);
	for (i = 0; i < rule_slots; i++) {
		if (rules[i] == NULL)
			continue;
		error = ugidfw_rulecheck(rules[i], cred,
		    vp, vap, acc_mode);
		if (error == EJUSTRETURN)
			break;
		if (error) {
			mtx_unlock(&ugidfw_mtx);
			return (error);
		}
	}
	mtx_unlock(&ugidfw_mtx);
	return (0);
}

int
ugidfw_check_vp(struct ucred *cred, struct vnode *vp, int acc_mode)
{
	int error;
	struct vattr vap;

	if (!ugidfw_enabled)
		return (0);
	error = VOP_GETATTR(vp, &vap, cred);
	if (error)
		return (error);
	return (ugidfw_check(cred, vp, &vap, acc_mode));
}

int
ugidfw_accmode2mbi(accmode_t accmode)
{
	int mbi;

	mbi = 0;
	if (accmode & VEXEC)
		mbi |= MBI_EXEC;
	if (accmode & VWRITE)
		mbi |= MBI_WRITE;
	if (accmode & VREAD)
		mbi |= MBI_READ;
	if (accmode & VADMIN_PERMS)
		mbi |= MBI_ADMIN;
	if (accmode & VSTAT_PERMS)
		mbi |= MBI_STAT;
	if (accmode & VAPPEND)
		mbi |= MBI_APPEND;
	return (mbi);
}

static struct mac_policy_ops ugidfw_ops =
{
	.mpo_destroy = ugidfw_destroy,
	.mpo_init = ugidfw_init,
	.mpo_system_check_acct = ugidfw_system_check_acct,
	.mpo_system_check_auditctl = ugidfw_system_check_auditctl,
	.mpo_system_check_swapon = ugidfw_system_check_swapon,
	.mpo_vnode_check_access = ugidfw_vnode_check_access,
	.mpo_vnode_check_chdir = ugidfw_vnode_check_chdir,
	.mpo_vnode_check_chroot = ugidfw_vnode_check_chroot,
	.mpo_vnode_check_create = ugidfw_check_create_vnode,
	.mpo_vnode_check_deleteacl = ugidfw_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = ugidfw_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = ugidfw_vnode_check_exec,
	.mpo_vnode_check_getacl = ugidfw_vnode_check_getacl,
	.mpo_vnode_check_getextattr = ugidfw_vnode_check_getextattr,
	.mpo_vnode_check_link = ugidfw_vnode_check_link,
	.mpo_vnode_check_listextattr = ugidfw_vnode_check_listextattr,
	.mpo_vnode_check_lookup = ugidfw_vnode_check_lookup,
	.mpo_vnode_check_open = ugidfw_vnode_check_open,
	.mpo_vnode_check_readdir = ugidfw_vnode_check_readdir,
	.mpo_vnode_check_readlink = ugidfw_vnode_check_readdlink,
	.mpo_vnode_check_rename_from = ugidfw_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = ugidfw_vnode_check_rename_to,
	.mpo_vnode_check_revoke = ugidfw_vnode_check_revoke,
	.mpo_vnode_check_setacl = ugidfw_check_setacl_vnode,
	.mpo_vnode_check_setextattr = ugidfw_vnode_check_setextattr,
	.mpo_vnode_check_setflags = ugidfw_vnode_check_setflags,
	.mpo_vnode_check_setmode = ugidfw_vnode_check_setmode,
	.mpo_vnode_check_setowner = ugidfw_vnode_check_setowner,
	.mpo_vnode_check_setutimes = ugidfw_vnode_check_setutimes,
	.mpo_vnode_check_stat = ugidfw_vnode_check_stat,
	.mpo_vnode_check_unlink = ugidfw_vnode_check_unlink,
};

MAC_POLICY_SET(&ugidfw_ops, mac_bsdextended, "TrustedBSD MAC/BSD Extended",
    MPC_LOADTIME_FLAG_UNLOADOK, NULL);
