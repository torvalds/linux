/*
 * Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, version 2.
 *
 * Author:
 *      Casey Schaufler <casey@schaufler-ca.com>
 *
 */

#ifndef _SECURITY_SMACK_H
#define _SECURITY_SMACK_H

#include <linux/capability.h>
#include <linux/spinlock.h>
#include <linux/security.h>
#include <linux/in.h>
#include <net/netlabel.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/lsm_audit.h>

/*
 * Why 23? CIPSO is constrained to 30, so a 32 byte buffer is
 * bigger than can be used, and 24 is the next lower multiple
 * of 8, and there are too many issues if there isn't space set
 * aside for the terminating null byte.
 */
#define SMK_MAXLEN	23
#define SMK_LABELLEN	(SMK_MAXLEN+1)

struct superblock_smack {
	char		*smk_root;
	char		*smk_floor;
	char		*smk_hat;
	char		*smk_default;
	int		smk_initialized;
	spinlock_t	smk_sblock;	/* for initialization */
};

struct socket_smack {
	char		*smk_out;			/* outbound label */
	char		*smk_in;			/* inbound label */
	char		smk_packet[SMK_LABELLEN];	/* TCP peer label */
};

/*
 * Inode smack data
 */
struct inode_smack {
	char		*smk_inode;	/* label of the fso */
	char		*smk_task;	/* label of the task */
	char		*smk_mmap;	/* label of the mmap domain */
	struct mutex	smk_lock;	/* initialization lock */
	int		smk_flags;	/* smack inode flags */
};

struct task_smack {
	char			*smk_task;	/* label for access control */
	char			*smk_forked;	/* label when forked */
	struct list_head	smk_rules;	/* per task access rules */
	struct mutex		smk_rules_lock;	/* lock for the rules */
};

#define	SMK_INODE_INSTANT	0x01	/* inode is instantiated */
#define	SMK_INODE_TRANSMUTE	0x02	/* directory is transmuting */

/*
 * A label access rule.
 */
struct smack_rule {
	struct list_head	list;
	char			*smk_subject;
	char			*smk_object;
	int			smk_access;
};

/*
 * An entry in the table mapping smack values to
 * CIPSO level/category-set values.
 */
struct smack_cipso {
	int	smk_level;
	char	smk_catset[SMK_LABELLEN];
};

/*
 * An entry in the table identifying hosts.
 */
struct smk_netlbladdr {
	struct list_head	list;
	struct sockaddr_in	smk_host;	/* network address */
	struct in_addr		smk_mask;	/* network mask */
	char			*smk_label;	/* label */
};

/*
 * This is the repository for labels seen so that it is
 * not necessary to keep allocating tiny chuncks of memory
 * and so that they can be shared.
 *
 * Labels are never modified in place. Anytime a label
 * is imported (e.g. xattrset on a file) the list is checked
 * for it and it is added if it doesn't exist. The address
 * is passed out in either case. Entries are added, but
 * never deleted.
 *
 * Since labels are hanging around anyway it doesn't
 * hurt to maintain a secid for those awkward situations
 * where kernel components that ought to use LSM independent
 * interfaces don't. The secid should go away when all of
 * these components have been repaired.
 *
 * If there is a cipso value associated with the label it
 * gets stored here, too. This will most likely be rare as
 * the cipso direct mapping in used internally.
 */
struct smack_known {
	struct list_head	list;
	char			smk_known[SMK_LABELLEN];
	u32			smk_secid;
	struct smack_cipso	*smk_cipso;
	spinlock_t		smk_cipsolock; /* for changing cipso map */
};

/*
 * Mount options
 */
#define SMK_FSDEFAULT	"smackfsdef="
#define SMK_FSFLOOR	"smackfsfloor="
#define SMK_FSHAT	"smackfshat="
#define SMK_FSROOT	"smackfsroot="

#define SMACK_CIPSO_OPTION 	"-CIPSO"

/*
 * How communications on this socket are treated.
 * Usually it's determined by the underlying netlabel code
 * but there are certain cases, including single label hosts
 * and potentially single label interfaces for which the
 * treatment can not be known in advance.
 *
 * The possibility of additional labeling schemes being
 * introduced in the future exists as well.
 */
#define SMACK_UNLABELED_SOCKET	0
#define SMACK_CIPSO_SOCKET	1

/*
 * smackfs magic number
 * smackfs macic number
 */
#define SMACK_MAGIC	0x43415d53 /* "SMAC" */

/*
 * CIPSO defaults.
 */
#define SMACK_CIPSO_DOI_DEFAULT		3	/* Historical */
#define SMACK_CIPSO_DOI_INVALID		-1	/* Not a DOI */
#define SMACK_CIPSO_DIRECT_DEFAULT	250	/* Arbitrary */
#define SMACK_CIPSO_MAXCATVAL		63	/* Bigger gets harder */
#define SMACK_CIPSO_MAXLEVEL            255     /* CIPSO 2.2 standard */
#define SMACK_CIPSO_MAXCATNUM           239     /* CIPSO 2.2 standard */

/*
 * Flag for transmute access
 */
#define MAY_TRANSMUTE	64
/*
 * Just to make the common cases easier to deal with
 */
#define MAY_ANYREAD	(MAY_READ | MAY_EXEC)
#define MAY_READWRITE	(MAY_READ | MAY_WRITE)
#define MAY_NOT		0

/*
 * Number of access types used by Smack (rwxa)
 */
#define SMK_NUM_ACCESS_TYPE 4

/*
 * Smack audit data; is empty if CONFIG_AUDIT not set
 * to save some stack
 */
struct smk_audit_info {
#ifdef CONFIG_AUDIT
	struct common_audit_data a;
#endif
};
/*
 * These functions are in smack_lsm.c
 */
struct inode_smack *new_inode_smack(char *);

/*
 * These functions are in smack_access.c
 */
int smk_access_entry(char *, char *, struct list_head *);
int smk_access(char *, char *, int, struct smk_audit_info *);
int smk_curacc(char *, u32, struct smk_audit_info *);
int smack_to_cipso(const char *, struct smack_cipso *);
void smack_from_cipso(u32, char *, char *);
char *smack_from_secid(const u32);
char *smk_import(const char *, int);
struct smack_known *smk_import_entry(const char *, int);
u32 smack_to_secid(const char *);

/*
 * Shared data.
 */
extern int smack_cipso_direct;
extern char *smack_net_ambient;
extern char *smack_onlycap;
extern const char *smack_cipso_option;

extern struct smack_known smack_known_floor;
extern struct smack_known smack_known_hat;
extern struct smack_known smack_known_huh;
extern struct smack_known smack_known_invalid;
extern struct smack_known smack_known_star;
extern struct smack_known smack_known_web;

extern struct list_head smack_known_list;
extern struct list_head smack_rule_list;
extern struct list_head smk_netlbladdr_list;

extern struct security_operations smack_ops;

/*
 * Stricly for CIPSO level manipulation.
 * Set the category bit number in a smack label sized buffer.
 */
static inline void smack_catset_bit(int cat, char *catsetp)
{
	if (cat > SMK_LABELLEN * 8)
		return;

	catsetp[(cat - 1) / 8] |= 0x80 >> ((cat - 1) % 8);
}

/*
 * Is the directory transmuting?
 */
static inline int smk_inode_transmutable(const struct inode *isp)
{
	struct inode_smack *sip = isp->i_security;
	return (sip->smk_flags & SMK_INODE_TRANSMUTE) != 0;
}

/*
 * Present a pointer to the smack label in an inode blob.
 */
static inline char *smk_of_inode(const struct inode *isp)
{
	struct inode_smack *sip = isp->i_security;
	return sip->smk_inode;
}

/*
 * Present a pointer to the smack label in an task blob.
 */
static inline char *smk_of_task(const struct task_smack *tsp)
{
	return tsp->smk_task;
}

/*
 * Present a pointer to the forked smack label in an task blob.
 */
static inline char *smk_of_forked(const struct task_smack *tsp)
{
	return tsp->smk_forked;
}

/*
 * Present a pointer to the smack label in the current task blob.
 */
static inline char *smk_of_current(void)
{
	return smk_of_task(current_security());
}

/*
 * logging functions
 */
#define SMACK_AUDIT_DENIED 0x1
#define SMACK_AUDIT_ACCEPT 0x2
extern int log_policy;

void smack_log(char *subject_label, char *object_label,
		int request,
		int result, struct smk_audit_info *auditdata);

#ifdef CONFIG_AUDIT

/*
 * some inline functions to set up audit data
 * they do nothing if CONFIG_AUDIT is not set
 *
 */
static inline void smk_ad_init(struct smk_audit_info *a, const char *func,
			       char type)
{
	memset(a, 0, sizeof(*a));
	a->a.type = type;
	a->a.smack_audit_data.function = func;
}

static inline void smk_ad_setfield_u_tsk(struct smk_audit_info *a,
					 struct task_struct *t)
{
	a->a.u.tsk = t;
}
static inline void smk_ad_setfield_u_fs_path_dentry(struct smk_audit_info *a,
						    struct dentry *d)
{
	a->a.u.fs.path.dentry = d;
}
static inline void smk_ad_setfield_u_fs_path_mnt(struct smk_audit_info *a,
						 struct vfsmount *m)
{
	a->a.u.fs.path.mnt = m;
}
static inline void smk_ad_setfield_u_fs_inode(struct smk_audit_info *a,
					      struct inode *i)
{
	a->a.u.fs.inode = i;
}
static inline void smk_ad_setfield_u_fs_path(struct smk_audit_info *a,
					     struct path p)
{
	a->a.u.fs.path = p;
}
static inline void smk_ad_setfield_u_net_sk(struct smk_audit_info *a,
					    struct sock *sk)
{
	a->a.u.net.sk = sk;
}

#else /* no AUDIT */

static inline void smk_ad_init(struct smk_audit_info *a, const char *func,
			       char type)
{
}
static inline void smk_ad_setfield_u_tsk(struct smk_audit_info *a,
					 struct task_struct *t)
{
}
static inline void smk_ad_setfield_u_fs_path_dentry(struct smk_audit_info *a,
						    struct dentry *d)
{
}
static inline void smk_ad_setfield_u_fs_path_mnt(struct smk_audit_info *a,
						 struct vfsmount *m)
{
}
static inline void smk_ad_setfield_u_fs_inode(struct smk_audit_info *a,
					      struct inode *i)
{
}
static inline void smk_ad_setfield_u_fs_path(struct smk_audit_info *a,
					     struct path p)
{
}
static inline void smk_ad_setfield_u_net_sk(struct smk_audit_info *a,
					    struct sock *sk)
{
}
#endif

#endif  /* _SECURITY_SMACK_H */
