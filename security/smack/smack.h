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
#include <net/netlabel.h>

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
	struct mutex	smk_lock;	/* initialization lock */
	int		smk_flags;	/* smack inode flags */
};

#define	SMK_INODE_INSTANT	0x01	/* inode is instantiated */

/*
 * A label access rule.
 */
struct smack_rule {
	char	*smk_subject;
	char	*smk_object;
	int	smk_access;
};

/*
 * An entry in the table of permitted label accesses.
 */
struct smk_list_entry {
	struct smk_list_entry	*smk_next;
	struct smack_rule	smk_rule;
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
	struct smack_known	*smk_next;
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

/*
 * xattr names
 */
#define XATTR_SMACK_SUFFIX	"SMACK64"
#define XATTR_SMACK_IPIN	"SMACK64IPIN"
#define XATTR_SMACK_IPOUT	"SMACK64IPOUT"
#define XATTR_NAME_SMACK	XATTR_SECURITY_PREFIX XATTR_SMACK_SUFFIX
#define XATTR_NAME_SMACKIPIN	XATTR_SECURITY_PREFIX XATTR_SMACK_IPIN
#define XATTR_NAME_SMACKIPOUT	XATTR_SECURITY_PREFIX XATTR_SMACK_IPOUT

/*
 * smackfs macic number
 */
#define SMACK_MAGIC	0x43415d53 /* "SMAC" */

/*
 * A limit on the number of entries in the lists
 * makes some of the list administration easier.
 */
#define SMACK_LIST_MAX	10000

/*
 * CIPSO defaults.
 */
#define SMACK_CIPSO_DOI_DEFAULT		3	/* Historical */
#define SMACK_CIPSO_DIRECT_DEFAULT	250	/* Arbitrary */
#define SMACK_CIPSO_MAXCATVAL		63	/* Bigger gets harder */
#define SMACK_CIPSO_MAXLEVEL            255     /* CIPSO 2.2 standard */
#define SMACK_CIPSO_MAXCATNUM           239     /* CIPSO 2.2 standard */

/*
 * Just to make the common cases easier to deal with
 */
#define MAY_ANY		(MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
#define MAY_ANYREAD	(MAY_READ | MAY_EXEC)
#define MAY_ANYWRITE	(MAY_WRITE | MAY_APPEND)
#define MAY_READWRITE	(MAY_READ | MAY_WRITE)
#define MAY_NOT		0

/*
 * These functions are in smack_lsm.c
 */
struct inode_smack *new_inode_smack(char *);

/*
 * These functions are in smack_access.c
 */
int smk_access(char *, char *, int);
int smk_curacc(char *, u32);
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
extern int smack_net_nltype;
extern char *smack_net_ambient;

extern struct smack_known *smack_known;
extern struct smack_known smack_known_floor;
extern struct smack_known smack_known_hat;
extern struct smack_known smack_known_huh;
extern struct smack_known smack_known_invalid;
extern struct smack_known smack_known_star;
extern struct smack_known smack_known_unset;

extern struct smk_list_entry *smack_list;

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
 * Present a pointer to the smack label in an inode blob.
 */
static inline char *smk_of_inode(const struct inode *isp)
{
	struct inode_smack *sip = isp->i_security;
	return sip->smk_inode;
}

#endif  /* _SECURITY_SMACK_H */
