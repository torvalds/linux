/*
 * Copyright (C) 2007 Casey Schaufler <casey@schaufler-ca.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *  	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, version 2.
 *
 * Authors:
 * 	Casey Schaufler <casey@schaufler-ca.com>
 * 	Ahmed S. Darwish <darwish.07@gmail.com>
 *
 * Special thanks to the authors of selinuxfs.
 *
 *	Karl MacMillan <kmacmillan@tresys.com>
 *	James Morris <jmorris@redhat.com>
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/mutex.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/audit.h>
#include "smack.h"

/*
 * smackfs pseudo filesystem.
 */

enum smk_inos {
	SMK_ROOT_INO	= 2,
	SMK_LOAD	= 3,	/* load policy */
	SMK_CIPSO	= 4,	/* load label -> CIPSO mapping */
	SMK_DOI		= 5,	/* CIPSO DOI */
	SMK_DIRECT	= 6,	/* CIPSO level indicating direct label */
	SMK_AMBIENT	= 7,	/* internet ambient label */
	SMK_NLTYPE	= 8,	/* label scheme to use by default */
};

/*
 * List locks
 */
static DEFINE_MUTEX(smack_list_lock);
static DEFINE_MUTEX(smack_cipso_lock);
static DEFINE_MUTEX(smack_ambient_lock);

/*
 * This is the "ambient" label for network traffic.
 * If it isn't somehow marked, use this.
 * It can be reset via smackfs/ambient
 */
char *smack_net_ambient = smack_known_floor.smk_known;

/*
 * This is the default packet marking scheme for network traffic.
 * It can be reset via smackfs/nltype
 */
int smack_net_nltype = NETLBL_NLTYPE_CIPSOV4;

/*
 * This is the level in a CIPSO header that indicates a
 * smack label is contained directly in the category set.
 * It can be reset via smackfs/direct
 */
int smack_cipso_direct = SMACK_CIPSO_DIRECT_DEFAULT;

static int smk_cipso_doi_value = SMACK_CIPSO_DOI_DEFAULT;
struct smk_list_entry *smack_list;

#define	SEQ_READ_FINISHED	1

/*
 * Values for parsing cipso rules
 * SMK_DIGITLEN: Length of a digit field in a rule.
 * SMK_CIPSOMIN: Minimum possible cipso rule length.
 * SMK_CIPSOMAX: Maximum possible cipso rule length.
 */
#define SMK_DIGITLEN 4
#define SMK_CIPSOMIN (SMK_LABELLEN + 2 * SMK_DIGITLEN)
#define SMK_CIPSOMAX (SMK_CIPSOMIN + SMACK_CIPSO_MAXCATNUM * SMK_DIGITLEN)

/*
 * Values for parsing MAC rules
 * SMK_ACCESS: Maximum possible combination of access permissions
 * SMK_ACCESSLEN: Maximum length for a rule access field
 * SMK_LOADLEN: Smack rule length
 */
#define SMK_ACCESS    "rwxa"
#define SMK_ACCESSLEN (sizeof(SMK_ACCESS) - 1)
#define SMK_LOADLEN   (SMK_LABELLEN + SMK_LABELLEN + SMK_ACCESSLEN)


/*
 * Seq_file read operations for /smack/load
 */

static void *load_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == SEQ_READ_FINISHED)
		return NULL;

	return smack_list;
}

static void *load_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct smk_list_entry *skp = ((struct smk_list_entry *) v)->smk_next;

	if (skp == NULL)
		*pos = SEQ_READ_FINISHED;

	return skp;
}

static int load_seq_show(struct seq_file *s, void *v)
{
	struct smk_list_entry *slp = (struct smk_list_entry *) v;
	struct smack_rule *srp = &slp->smk_rule;

	seq_printf(s, "%s %s", (char *)srp->smk_subject,
		   (char *)srp->smk_object);

	seq_putc(s, ' ');

	if (srp->smk_access & MAY_READ)
		seq_putc(s, 'r');
	if (srp->smk_access & MAY_WRITE)
		seq_putc(s, 'w');
	if (srp->smk_access & MAY_EXEC)
		seq_putc(s, 'x');
	if (srp->smk_access & MAY_APPEND)
		seq_putc(s, 'a');
	if (srp->smk_access == 0)
		seq_putc(s, '-');

	seq_putc(s, '\n');

	return 0;
}

static void load_seq_stop(struct seq_file *s, void *v)
{
	/* No-op */
}

static struct seq_operations load_seq_ops = {
	.start = load_seq_start,
	.next  = load_seq_next,
	.show  = load_seq_show,
	.stop  = load_seq_stop,
};

/**
 * smk_open_load - open() for /smack/load
 * @inode: inode structure representing file
 * @file: "load" file pointer
 *
 * For reading, use load_seq_* seq_file reading operations.
 */
static int smk_open_load(struct inode *inode, struct file *file)
{
	return seq_open(file, &load_seq_ops);
}

/**
 * smk_set_access - add a rule to the rule list
 * @srp: the new rule to add
 *
 * Looks through the current subject/object/access list for
 * the subject/object pair and replaces the access that was
 * there. If the pair isn't found add it with the specified
 * access.
 */
static void smk_set_access(struct smack_rule *srp)
{
	struct smk_list_entry *sp;
	struct smk_list_entry *newp;

	mutex_lock(&smack_list_lock);

	for (sp = smack_list; sp != NULL; sp = sp->smk_next)
		if (sp->smk_rule.smk_subject == srp->smk_subject &&
		    sp->smk_rule.smk_object == srp->smk_object) {
			sp->smk_rule.smk_access = srp->smk_access;
			break;
		}

	if (sp == NULL) {
		newp = kzalloc(sizeof(struct smk_list_entry), GFP_KERNEL);
		newp->smk_rule = *srp;
		newp->smk_next = smack_list;
		smack_list = newp;
	}

	mutex_unlock(&smack_list_lock);

	return;
}

/**
 * smk_write_load - write() for /smack/load
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start - must be 0
 *
 * Get one smack access rule from above.
 * The format is exactly:
 *     char subject[SMK_LABELLEN]
 *     char object[SMK_LABELLEN]
 *     char access[SMK_ACCESSLEN]
 *
 * writes must be SMK_LABELLEN+SMK_LABELLEN+SMK_ACCESSLEN bytes.
 */
static ssize_t smk_write_load(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct smack_rule rule;
	char *data;
	int rc = -EINVAL;

	/*
	 * Must have privilege.
	 * No partial writes.
	 * Enough data must be present.
	 */
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	if (*ppos != 0)
		return -EINVAL;
	if (count != SMK_LOADLEN)
		return -EINVAL;

	data = kzalloc(count, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, count) != 0) {
		rc = -EFAULT;
		goto out;
	}

	rule.smk_subject = smk_import(data, 0);
	if (rule.smk_subject == NULL)
		goto out;

	rule.smk_object = smk_import(data + SMK_LABELLEN, 0);
	if (rule.smk_object == NULL)
		goto out;

	rule.smk_access = 0;

	switch (data[SMK_LABELLEN + SMK_LABELLEN]) {
	case '-':
		break;
	case 'r':
	case 'R':
		rule.smk_access |= MAY_READ;
		break;
	default:
		goto out;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 1]) {
	case '-':
		break;
	case 'w':
	case 'W':
		rule.smk_access |= MAY_WRITE;
		break;
	default:
		goto out;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 2]) {
	case '-':
		break;
	case 'x':
	case 'X':
		rule.smk_access |= MAY_EXEC;
		break;
	default:
		goto out;
	}

	switch (data[SMK_LABELLEN + SMK_LABELLEN + 3]) {
	case '-':
		break;
	case 'a':
	case 'A':
		rule.smk_access |= MAY_READ;
		break;
	default:
		goto out;
	}

	smk_set_access(&rule);
	rc = count;

out:
	kfree(data);
	return rc;
}

static const struct file_operations smk_load_ops = {
	.open           = smk_open_load,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_load,
	.release        = seq_release,
};

/**
 * smk_cipso_doi - initialize the CIPSO domain
 */
static void smk_cipso_doi(void)
{
	int rc;
	struct cipso_v4_doi *doip;
	struct netlbl_audit audit_info;

	audit_info.loginuid = audit_get_loginuid(current);
	audit_info.sessionid = audit_get_sessionid(current);
	audit_info.secid = smack_to_secid(current->security);

	rc = netlbl_cfg_map_del(NULL, &audit_info);
	if (rc != 0)
		printk(KERN_WARNING "%s:%d remove rc = %d\n",
		       __func__, __LINE__, rc);

	doip = kmalloc(sizeof(struct cipso_v4_doi), GFP_KERNEL);
	if (doip == NULL)
		panic("smack:  Failed to initialize cipso DOI.\n");
	doip->map.std = NULL;
	doip->doi = smk_cipso_doi_value;
	doip->type = CIPSO_V4_MAP_PASS;
	doip->tags[0] = CIPSO_V4_TAG_RBITMAP;
	for (rc = 1; rc < CIPSO_V4_TAG_MAXCNT; rc++)
		doip->tags[rc] = CIPSO_V4_TAG_INVALID;

	rc = netlbl_cfg_cipsov4_add_map(doip, NULL, &audit_info);
	if (rc != 0) {
		printk(KERN_WARNING "%s:%d add rc = %d\n",
		       __func__, __LINE__, rc);
		kfree(doip);
	}
}

/**
 * smk_unlbl_ambient - initialize the unlabeled domain
 */
static void smk_unlbl_ambient(char *oldambient)
{
	int rc;
	struct netlbl_audit audit_info;

	audit_info.loginuid = audit_get_loginuid(current);
	audit_info.sessionid = audit_get_sessionid(current);
	audit_info.secid = smack_to_secid(current->security);

	if (oldambient != NULL) {
		rc = netlbl_cfg_map_del(oldambient, &audit_info);
		if (rc != 0)
			printk(KERN_WARNING "%s:%d remove rc = %d\n",
			       __func__, __LINE__, rc);
	}

	rc = netlbl_cfg_unlbl_add_map(smack_net_ambient, &audit_info);
	if (rc != 0)
		printk(KERN_WARNING "%s:%d add rc = %d\n",
		       __func__, __LINE__, rc);
}

/*
 * Seq_file read operations for /smack/cipso
 */

static void *cipso_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == SEQ_READ_FINISHED)
		return NULL;

	return smack_known;
}

static void *cipso_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct smack_known *skp = ((struct smack_known *) v)->smk_next;

	/*
	 * Omit labels with no associated cipso value
	 */
	while (skp != NULL && !skp->smk_cipso)
		skp = skp->smk_next;

	if (skp == NULL)
		*pos = SEQ_READ_FINISHED;

	return skp;
}

/*
 * Print cipso labels in format:
 * label level[/cat[,cat]]
 */
static int cipso_seq_show(struct seq_file *s, void *v)
{
	struct smack_known *skp = (struct smack_known *) v;
	struct smack_cipso *scp = skp->smk_cipso;
	char *cbp;
	char sep = '/';
	int cat = 1;
	int i;
	unsigned char m;

	if (scp == NULL)
		return 0;

	seq_printf(s, "%s %3d", (char *)&skp->smk_known, scp->smk_level);

	cbp = scp->smk_catset;
	for (i = 0; i < SMK_LABELLEN; i++)
		for (m = 0x80; m != 0; m >>= 1) {
			if (m & cbp[i]) {
				seq_printf(s, "%c%d", sep, cat);
				sep = ',';
			}
			cat++;
		}

	seq_putc(s, '\n');

	return 0;
}

static void cipso_seq_stop(struct seq_file *s, void *v)
{
	/* No-op */
}

static struct seq_operations cipso_seq_ops = {
	.start = cipso_seq_start,
	.stop  = cipso_seq_stop,
	.next  = cipso_seq_next,
	.show  = cipso_seq_show,
};

/**
 * smk_open_cipso - open() for /smack/cipso
 * @inode: inode structure representing file
 * @file: "cipso" file pointer
 *
 * Connect our cipso_seq_* operations with /smack/cipso
 * file_operations
 */
static int smk_open_cipso(struct inode *inode, struct file *file)
{
	return seq_open(file, &cipso_seq_ops);
}

/**
 * smk_write_cipso - write() for /smack/cipso
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Accepts only one cipso rule per write call.
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_cipso(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct smack_known *skp;
	struct smack_cipso *scp = NULL;
	char mapcatset[SMK_LABELLEN];
	int maplevel;
	int cat;
	int catlen;
	ssize_t rc = -EINVAL;
	char *data = NULL;
	char *rule;
	int ret;
	int i;

	/*
	 * Must have privilege.
	 * No partial writes.
	 * Enough data must be present.
	 */
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	if (*ppos != 0)
		return -EINVAL;
	if (count < SMK_CIPSOMIN || count > SMK_CIPSOMAX)
		return -EINVAL;

	data = kzalloc(count + 1, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, count) != 0) {
		rc = -EFAULT;
		goto unlockedout;
	}

	data[count] = '\0';
	rule = data;
	/*
	 * Only allow one writer at a time. Writes should be
	 * quite rare and small in any case.
	 */
	mutex_lock(&smack_cipso_lock);

	skp = smk_import_entry(rule, 0);
	if (skp == NULL)
		goto out;

	rule += SMK_LABELLEN;;
	ret = sscanf(rule, "%d", &maplevel);
	if (ret != 1 || maplevel > SMACK_CIPSO_MAXLEVEL)
		goto out;

	rule += SMK_DIGITLEN;
	ret = sscanf(rule, "%d", &catlen);
	if (ret != 1 || catlen > SMACK_CIPSO_MAXCATNUM)
		goto out;

	if (count != (SMK_CIPSOMIN + catlen * SMK_DIGITLEN))
		goto out;

	memset(mapcatset, 0, sizeof(mapcatset));

	for (i = 0; i < catlen; i++) {
		rule += SMK_DIGITLEN;
		ret = sscanf(rule, "%d", &cat);
		if (ret != 1 || cat > SMACK_CIPSO_MAXCATVAL)
			goto out;

		smack_catset_bit(cat, mapcatset);
	}

	if (skp->smk_cipso == NULL) {
		scp = kzalloc(sizeof(struct smack_cipso), GFP_KERNEL);
		if (scp == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	}

	spin_lock_bh(&skp->smk_cipsolock);

	if (scp == NULL)
		scp = skp->smk_cipso;
	else
		skp->smk_cipso = scp;

	scp->smk_level = maplevel;
	memcpy(scp->smk_catset, mapcatset, sizeof(mapcatset));

	spin_unlock_bh(&skp->smk_cipsolock);

	rc = count;
out:
	mutex_unlock(&smack_cipso_lock);
unlockedout:
	kfree(data);
	return rc;
}

static const struct file_operations smk_cipso_ops = {
	.open           = smk_open_cipso,
	.read		= seq_read,
	.llseek         = seq_lseek,
	.write		= smk_write_cipso,
	.release        = seq_release,
};

/**
 * smk_read_doi - read() for /smack/doi
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_doi(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	char temp[80];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d", smk_cipso_doi_value);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	return rc;
}

/**
 * smk_write_doi - write() for /smack/doi
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_doi(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	char temp[80];
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= sizeof(temp) || count == 0)
		return -EINVAL;

	if (copy_from_user(temp, buf, count) != 0)
		return -EFAULT;

	temp[count] = '\0';

	if (sscanf(temp, "%d", &i) != 1)
		return -EINVAL;

	smk_cipso_doi_value = i;

	smk_cipso_doi();

	return count;
}

static const struct file_operations smk_doi_ops = {
	.read		= smk_read_doi,
	.write		= smk_write_doi,
};

/**
 * smk_read_direct - read() for /smack/direct
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_direct(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char temp[80];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d", smack_cipso_direct);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	return rc;
}

/**
 * smk_write_direct - write() for /smack/direct
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_direct(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char temp[80];
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= sizeof(temp) || count == 0)
		return -EINVAL;

	if (copy_from_user(temp, buf, count) != 0)
		return -EFAULT;

	temp[count] = '\0';

	if (sscanf(temp, "%d", &i) != 1)
		return -EINVAL;

	smack_cipso_direct = i;

	return count;
}

static const struct file_operations smk_direct_ops = {
	.read		= smk_read_direct,
	.write		= smk_write_direct,
};

/**
 * smk_read_ambient - read() for /smack/ambient
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @cn: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_ambient(struct file *filp, char __user *buf,
				size_t cn, loff_t *ppos)
{
	ssize_t rc;
	int asize;

	if (*ppos != 0)
		return 0;
	/*
	 * Being careful to avoid a problem in the case where
	 * smack_net_ambient gets changed in midstream.
	 */
	mutex_lock(&smack_ambient_lock);

	asize = strlen(smack_net_ambient) + 1;

	if (cn >= asize)
		rc = simple_read_from_buffer(buf, cn, ppos,
					     smack_net_ambient, asize);
	else
		rc = -EINVAL;

	mutex_unlock(&smack_ambient_lock);

	return rc;
}

/**
 * smk_write_ambient - write() for /smack/ambient
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_ambient(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char in[SMK_LABELLEN];
	char *oldambient;
	char *smack;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= SMK_LABELLEN)
		return -EINVAL;

	if (copy_from_user(in, buf, count) != 0)
		return -EFAULT;

	smack = smk_import(in, count);
	if (smack == NULL)
		return -EINVAL;

	mutex_lock(&smack_ambient_lock);

	oldambient = smack_net_ambient;
	smack_net_ambient = smack;
	smk_unlbl_ambient(oldambient);

	mutex_unlock(&smack_ambient_lock);

	return count;
}

static const struct file_operations smk_ambient_ops = {
	.read		= smk_read_ambient,
	.write		= smk_write_ambient,
};

struct option_names {
	int	o_number;
	char	*o_name;
	char	*o_alias;
};

static struct option_names netlbl_choices[] = {
	{ NETLBL_NLTYPE_RIPSO,
		NETLBL_NLTYPE_RIPSO_NAME,	"ripso" },
	{ NETLBL_NLTYPE_CIPSOV4,
		NETLBL_NLTYPE_CIPSOV4_NAME,	"cipsov4" },
	{ NETLBL_NLTYPE_CIPSOV4,
		NETLBL_NLTYPE_CIPSOV4_NAME,	"cipso" },
	{ NETLBL_NLTYPE_CIPSOV6,
		NETLBL_NLTYPE_CIPSOV6_NAME,	"cipsov6" },
	{ NETLBL_NLTYPE_UNLABELED,
		NETLBL_NLTYPE_UNLABELED_NAME,	"unlabeled" },
};

/**
 * smk_read_nltype - read() for /smack/nltype
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t smk_read_nltype(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char bound[40];
	ssize_t rc;
	int i;

	if (count < SMK_LABELLEN)
		return -EINVAL;

	if (*ppos != 0)
		return 0;

	sprintf(bound, "unknown");

	for (i = 0; i < ARRAY_SIZE(netlbl_choices); i++)
		if (smack_net_nltype == netlbl_choices[i].o_number) {
			sprintf(bound, "%s", netlbl_choices[i].o_name);
			break;
		}

	rc = simple_read_from_buffer(buf, count, ppos, bound, strlen(bound));

	return rc;
}

/**
 * smk_write_nltype - write() for /smack/nltype
 * @filp: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t smk_write_nltype(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char bound[40];
	char *cp;
	int i;

	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (count >= 40)
		return -EINVAL;

	if (copy_from_user(bound, buf, count) != 0)
		return -EFAULT;

	bound[count] = '\0';
	cp = strchr(bound, ' ');
	if (cp != NULL)
		*cp = '\0';
	cp = strchr(bound, '\n');
	if (cp != NULL)
		*cp = '\0';

	for (i = 0; i < ARRAY_SIZE(netlbl_choices); i++)
		if (strcmp(bound, netlbl_choices[i].o_name) == 0 ||
		    strcmp(bound, netlbl_choices[i].o_alias) == 0) {
			smack_net_nltype = netlbl_choices[i].o_number;
			return count;
		}
	/*
	 * Not a valid choice.
	 */
	return -EINVAL;
}

static const struct file_operations smk_nltype_ops = {
	.read		= smk_read_nltype,
	.write		= smk_write_nltype,
};

/**
 * smk_fill_super - fill the /smackfs superblock
 * @sb: the empty superblock
 * @data: unused
 * @silent: unused
 *
 * Fill in the well known entries for /smack
 *
 * Returns 0 on success, an error code on failure
 */
static int smk_fill_super(struct super_block *sb, void *data, int silent)
{
	int rc;
	struct inode *root_inode;

	static struct tree_descr smack_files[] = {
		[SMK_LOAD]	=
			{"load", &smk_load_ops, S_IRUGO|S_IWUSR},
		[SMK_CIPSO]	=
			{"cipso", &smk_cipso_ops, S_IRUGO|S_IWUSR},
		[SMK_DOI]	=
			{"doi", &smk_doi_ops, S_IRUGO|S_IWUSR},
		[SMK_DIRECT]	=
			{"direct", &smk_direct_ops, S_IRUGO|S_IWUSR},
		[SMK_AMBIENT]	=
			{"ambient", &smk_ambient_ops, S_IRUGO|S_IWUSR},
		[SMK_NLTYPE]	=
			{"nltype", &smk_nltype_ops, S_IRUGO|S_IWUSR},
		/* last one */ {""}
	};

	rc = simple_fill_super(sb, SMACK_MAGIC, smack_files);
	if (rc != 0) {
		printk(KERN_ERR "%s failed %d while creating inodes\n",
			__func__, rc);
		return rc;
	}

	root_inode = sb->s_root->d_inode;
	root_inode->i_security = new_inode_smack(smack_known_floor.smk_known);

	return 0;
}

/**
 * smk_get_sb - get the smackfs superblock
 * @fs_type: passed along without comment
 * @flags: passed along without comment
 * @dev_name: passed along without comment
 * @data: passed along without comment
 * @mnt: passed along without comment
 *
 * Just passes everything along.
 *
 * Returns what the lower level code does.
 */
static int smk_get_sb(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data,
		      struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, smk_fill_super, mnt);
}

static struct file_system_type smk_fs_type = {
	.name		= "smackfs",
	.get_sb		= smk_get_sb,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *smackfs_mount;

/**
 * init_smk_fs - get the smackfs superblock
 *
 * register the smackfs
 *
 * Do not register smackfs if Smack wasn't enabled
 * on boot. We can not put this method normally under the
 * smack_init() code path since the security subsystem get
 * initialized before the vfs caches.
 *
 * Returns true if we were not chosen on boot or if
 * we were chosen and filesystem registration succeeded.
 */
static int __init init_smk_fs(void)
{
	int err;

	if (!security_module_enable(&smack_ops))
		return 0;

	err = register_filesystem(&smk_fs_type);
	if (!err) {
		smackfs_mount = kern_mount(&smk_fs_type);
		if (IS_ERR(smackfs_mount)) {
			printk(KERN_ERR "smackfs:  could not mount!\n");
			err = PTR_ERR(smackfs_mount);
			smackfs_mount = NULL;
		}
	}

	smk_cipso_doi();
	smk_unlbl_ambient(NULL);

	return err;
}

__initcall(init_smk_fs);
