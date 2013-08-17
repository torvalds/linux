/*
 * fs/nfs_common/nfsacl.c
 *
 *  Copyright (C) 2002-2003 Andreas Gruenbacher <agruen@suse.de>
 */

/*
 * The Solaris nfsacl protocol represents some ACLs slightly differently
 * than POSIX 1003.1e draft 17 does (and we do):
 *
 *  - Minimal ACLs always have an ACL_MASK entry, so they have
 *    four instead of three entries.
 *  - The ACL_MASK entry in such minimal ACLs always has the same
 *    permissions as the ACL_GROUP_OBJ entry. (In extended ACLs
 *    the ACL_MASK and ACL_GROUP_OBJ entries may differ.)
 *  - The identifier fields of the ACL_USER_OBJ and ACL_GROUP_OBJ
 *    entries contain the identifiers of the owner and owning group.
 *    (In POSIX ACLs we always set them to ACL_UNDEFINED_ID).
 *  - ACL entries in the kernel are kept sorted in ascending order
 *    of (e_tag, e_id). Solaris ACLs are unsorted.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/sunrpc/xdr.h>
#include <linux/nfsacl.h>
#include <linux/nfs3.h>
#include <linux/sort.h>

MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(nfsacl_encode);
EXPORT_SYMBOL_GPL(nfsacl_decode);

struct nfsacl_encode_desc {
	struct xdr_array2_desc desc;
	unsigned int count;
	struct posix_acl *acl;
	int typeflag;
	uid_t uid;
	gid_t gid;
};

struct nfsacl_simple_acl {
	struct posix_acl acl;
	struct posix_acl_entry ace[4];
};

static int
xdr_nfsace_encode(struct xdr_array2_desc *desc, void *elem)
{
	struct nfsacl_encode_desc *nfsacl_desc =
		(struct nfsacl_encode_desc *) desc;
	__be32 *p = elem;

	struct posix_acl_entry *entry =
		&nfsacl_desc->acl->a_entries[nfsacl_desc->count++];

	*p++ = htonl(entry->e_tag | nfsacl_desc->typeflag);
	switch(entry->e_tag) {
		case ACL_USER_OBJ:
			*p++ = htonl(nfsacl_desc->uid);
			break;
		case ACL_GROUP_OBJ:
			*p++ = htonl(nfsacl_desc->gid);
			break;
		case ACL_USER:
		case ACL_GROUP:
			*p++ = htonl(entry->e_id);
			break;
		default:  /* Solaris depends on that! */
			*p++ = 0;
			break;
	}
	*p++ = htonl(entry->e_perm & S_IRWXO);
	return 0;
}

/**
 * nfsacl_encode - Encode an NFSv3 ACL
 *
 * @buf: destination xdr_buf to contain XDR encoded ACL
 * @base: byte offset in xdr_buf where XDR'd ACL begins
 * @inode: inode of file whose ACL this is
 * @acl: posix_acl to encode
 * @encode_entries: whether to encode ACEs as well
 * @typeflag: ACL type: NFS_ACL_DEFAULT or zero
 *
 * Returns size of encoded ACL in bytes or a negative errno value.
 */
int nfsacl_encode(struct xdr_buf *buf, unsigned int base, struct inode *inode,
		  struct posix_acl *acl, int encode_entries, int typeflag)
{
	int entries = (acl && acl->a_count) ? max_t(int, acl->a_count, 4) : 0;
	struct nfsacl_encode_desc nfsacl_desc = {
		.desc = {
			.elem_size = 12,
			.array_len = encode_entries ? entries : 0,
			.xcode = xdr_nfsace_encode,
		},
		.acl = acl,
		.typeflag = typeflag,
		.uid = inode->i_uid,
		.gid = inode->i_gid,
	};
	struct nfsacl_simple_acl aclbuf;
	int err;

	if (entries > NFS_ACL_MAX_ENTRIES ||
	    xdr_encode_word(buf, base, entries))
		return -EINVAL;
	if (encode_entries && acl && acl->a_count == 3) {
		struct posix_acl *acl2 = &aclbuf.acl;

		/* Avoid the use of posix_acl_alloc().  nfsacl_encode() is
		 * invoked in contexts where a memory allocation failure is
		 * fatal.  Fortunately this fake ACL is small enough to
		 * construct on the stack. */
		posix_acl_init(acl2, 4);

		/* Insert entries in canonical order: other orders seem
		 to confuse Solaris VxFS. */
		acl2->a_entries[0] = acl->a_entries[0];  /* ACL_USER_OBJ */
		acl2->a_entries[1] = acl->a_entries[1];  /* ACL_GROUP_OBJ */
		acl2->a_entries[2] = acl->a_entries[1];  /* ACL_MASK */
		acl2->a_entries[2].e_tag = ACL_MASK;
		acl2->a_entries[3] = acl->a_entries[2];  /* ACL_OTHER */
		nfsacl_desc.acl = acl2;
	}
	err = xdr_encode_array2(buf, base + 4, &nfsacl_desc.desc);
	if (!err)
		err = 8 + nfsacl_desc.desc.elem_size *
			  nfsacl_desc.desc.array_len;
	return err;
}

struct nfsacl_decode_desc {
	struct xdr_array2_desc desc;
	unsigned int count;
	struct posix_acl *acl;
};

static int
xdr_nfsace_decode(struct xdr_array2_desc *desc, void *elem)
{
	struct nfsacl_decode_desc *nfsacl_desc =
		(struct nfsacl_decode_desc *) desc;
	__be32 *p = elem;
	struct posix_acl_entry *entry;

	if (!nfsacl_desc->acl) {
		if (desc->array_len > NFS_ACL_MAX_ENTRIES)
			return -EINVAL;
		nfsacl_desc->acl = posix_acl_alloc(desc->array_len, GFP_KERNEL);
		if (!nfsacl_desc->acl)
			return -ENOMEM;
		nfsacl_desc->count = 0;
	}

	entry = &nfsacl_desc->acl->a_entries[nfsacl_desc->count++];
	entry->e_tag = ntohl(*p++) & ~NFS_ACL_DEFAULT;
	entry->e_id = ntohl(*p++);
	entry->e_perm = ntohl(*p++);

	switch(entry->e_tag) {
		case ACL_USER_OBJ:
		case ACL_USER:
		case ACL_GROUP_OBJ:
		case ACL_GROUP:
		case ACL_OTHER:
			if (entry->e_perm & ~S_IRWXO)
				return -EINVAL;
			break;
		case ACL_MASK:
			/* Solaris sometimes sets additional bits in the mask */
			entry->e_perm &= S_IRWXO;
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static int
cmp_acl_entry(const void *x, const void *y)
{
	const struct posix_acl_entry *a = x, *b = y;

	if (a->e_tag != b->e_tag)
		return a->e_tag - b->e_tag;
	else if (a->e_id > b->e_id)
		return 1;
	else if (a->e_id < b->e_id)
		return -1;
	else
		return 0;
}

/*
 * Convert from a Solaris ACL to a POSIX 1003.1e draft 17 ACL.
 */
static int
posix_acl_from_nfsacl(struct posix_acl *acl)
{
	struct posix_acl_entry *pa, *pe,
	       *group_obj = NULL, *mask = NULL;

	if (!acl)
		return 0;

	sort(acl->a_entries, acl->a_count, sizeof(struct posix_acl_entry),
	     cmp_acl_entry, NULL);

	/* Clear undefined identifier fields and find the ACL_GROUP_OBJ
	   and ACL_MASK entries. */
	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch(pa->e_tag) {
			case ACL_USER_OBJ:
				pa->e_id = ACL_UNDEFINED_ID;
				break;
			case ACL_GROUP_OBJ:
				pa->e_id = ACL_UNDEFINED_ID;
				group_obj = pa;
				break;
			case ACL_MASK:
				mask = pa;
				/* fall through */
			case ACL_OTHER:
				pa->e_id = ACL_UNDEFINED_ID;
				break;
		}
	}
	if (acl->a_count == 4 && group_obj && mask &&
	    mask->e_perm == group_obj->e_perm) {
		/* remove bogus ACL_MASK entry */
		memmove(mask, mask+1, (3 - (mask - acl->a_entries)) *
				      sizeof(struct posix_acl_entry));
		acl->a_count = 3;
	}
	return 0;
}

/**
 * nfsacl_decode - Decode an NFSv3 ACL
 *
 * @buf: xdr_buf containing XDR'd ACL data to decode
 * @base: byte offset in xdr_buf where XDR'd ACL begins
 * @aclcnt: count of ACEs in decoded posix_acl
 * @pacl: buffer in which to place decoded posix_acl
 *
 * Returns the length of the decoded ACL in bytes, or a negative errno value.
 */
int nfsacl_decode(struct xdr_buf *buf, unsigned int base, unsigned int *aclcnt,
		  struct posix_acl **pacl)
{
	struct nfsacl_decode_desc nfsacl_desc = {
		.desc = {
			.elem_size = 12,
			.xcode = pacl ? xdr_nfsace_decode : NULL,
		},
	};
	u32 entries;
	int err;

	if (xdr_decode_word(buf, base, &entries) ||
	    entries > NFS_ACL_MAX_ENTRIES)
		return -EINVAL;
	nfsacl_desc.desc.array_maxlen = entries;
	err = xdr_decode_array2(buf, base + 4, &nfsacl_desc.desc);
	if (err)
		return err;
	if (pacl) {
		if (entries != nfsacl_desc.desc.array_len ||
		    posix_acl_from_nfsacl(nfsacl_desc.acl) != 0) {
			posix_acl_release(nfsacl_desc.acl);
			return -EINVAL;
		}
		*pacl = nfsacl_desc.acl;
	}
	if (aclcnt)
		*aclcnt = entries;
	return 8 + nfsacl_desc.desc.elem_size *
		   nfsacl_desc.desc.array_len;
}
