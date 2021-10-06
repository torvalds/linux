// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *   Copyright (C) 2018 Namjae Jeon <linkinjeon@kernel.org>
 */

#include "smb_common.h"
#include "server.h"
#include "misc.h"
#include "smbstatus.h"
#include "connection.h"
#include "ksmbd_work.h"
#include "mgmt/user_session.h"
#include "mgmt/user_config.h"
#include "mgmt/tree_connect.h"
#include "mgmt/share_config.h"

/*for shortname implementation */
static const char basechars[43] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_-!@#$%";
#define MANGLE_BASE (sizeof(basechars) / sizeof(char) - 1)
#define MAGIC_CHAR '~'
#define PERIOD '.'
#define mangle(V) ((char)(basechars[(V) % MANGLE_BASE]))
#define KSMBD_MIN_SUPPORTED_HEADER_SIZE	(sizeof(struct smb2_hdr))

struct smb_protocol {
	int		index;
	char		*name;
	char		*prot;
	__u16		prot_id;
};

static struct smb_protocol smb1_protos[] = {
	{
		SMB21_PROT,
		"\2SMB 2.1",
		"SMB2_10",
		SMB21_PROT_ID
	},
	{
		SMB2X_PROT,
		"\2SMB 2.???",
		"SMB2_22",
		SMB2X_PROT_ID
	},
};

static struct smb_protocol smb2_protos[] = {
	{
		SMB21_PROT,
		"\2SMB 2.1",
		"SMB2_10",
		SMB21_PROT_ID
	},
	{
		SMB30_PROT,
		"\2SMB 3.0",
		"SMB3_00",
		SMB30_PROT_ID
	},
	{
		SMB302_PROT,
		"\2SMB 3.02",
		"SMB3_02",
		SMB302_PROT_ID
	},
	{
		SMB311_PROT,
		"\2SMB 3.1.1",
		"SMB3_11",
		SMB311_PROT_ID
	},
};

unsigned int ksmbd_server_side_copy_max_chunk_count(void)
{
	return 256;
}

unsigned int ksmbd_server_side_copy_max_chunk_size(void)
{
	return (2U << 30) - 1;
}

unsigned int ksmbd_server_side_copy_max_total_size(void)
{
	return (2U << 30) - 1;
}

inline int ksmbd_min_protocol(void)
{
	return SMB2_PROT;
}

inline int ksmbd_max_protocol(void)
{
	return SMB311_PROT;
}

int ksmbd_lookup_protocol_idx(char *str)
{
	int offt = ARRAY_SIZE(smb1_protos) - 1;
	int len = strlen(str);

	while (offt >= 0) {
		if (!strncmp(str, smb1_protos[offt].prot, len)) {
			ksmbd_debug(SMB, "selected %s dialect idx = %d\n",
				    smb1_protos[offt].prot, offt);
			return smb1_protos[offt].index;
		}
		offt--;
	}

	offt = ARRAY_SIZE(smb2_protos) - 1;
	while (offt >= 0) {
		if (!strncmp(str, smb2_protos[offt].prot, len)) {
			ksmbd_debug(SMB, "selected %s dialect idx = %d\n",
				    smb2_protos[offt].prot, offt);
			return smb2_protos[offt].index;
		}
		offt--;
	}
	return -1;
}

/**
 * ksmbd_verify_smb_message() - check for valid smb2 request header
 * @work:	smb work
 *
 * check for valid smb signature and packet direction(request/response)
 *
 * Return:      0 on success, otherwise 1
 */
int ksmbd_verify_smb_message(struct ksmbd_work *work)
{
	struct smb2_hdr *smb2_hdr = work->request_buf;

	if (smb2_hdr->ProtocolId == SMB2_PROTO_NUMBER)
		return ksmbd_smb2_check_message(work);

	return 0;
}

/**
 * ksmbd_smb_request() - check for valid smb request type
 * @conn:	connection instance
 *
 * Return:      true on success, otherwise false
 */
bool ksmbd_smb_request(struct ksmbd_conn *conn)
{
	int type = *(char *)conn->request_buf;

	switch (type) {
	case RFC1002_SESSION_MESSAGE:
		/* Regular SMB request */
		return true;
	case RFC1002_SESSION_KEEP_ALIVE:
		ksmbd_debug(SMB, "RFC 1002 session keep alive\n");
		break;
	default:
		ksmbd_debug(SMB, "RFC 1002 unknown request type 0x%x\n", type);
	}

	return false;
}

static bool supported_protocol(int idx)
{
	if (idx == SMB2X_PROT &&
	    (server_conf.min_protocol >= SMB21_PROT ||
	     server_conf.max_protocol <= SMB311_PROT))
		return true;

	return (server_conf.min_protocol <= idx &&
		idx <= server_conf.max_protocol);
}

static char *next_dialect(char *dialect, int *next_off)
{
	dialect = dialect + *next_off;
	*next_off = strlen(dialect);
	return dialect;
}

static int ksmbd_lookup_dialect_by_name(char *cli_dialects, __le16 byte_count)
{
	int i, seq_num, bcount, next;
	char *dialect;

	for (i = ARRAY_SIZE(smb1_protos) - 1; i >= 0; i--) {
		seq_num = 0;
		next = 0;
		dialect = cli_dialects;
		bcount = le16_to_cpu(byte_count);
		do {
			dialect = next_dialect(dialect, &next);
			ksmbd_debug(SMB, "client requested dialect %s\n",
				    dialect);
			if (!strcmp(dialect, smb1_protos[i].name)) {
				if (supported_protocol(smb1_protos[i].index)) {
					ksmbd_debug(SMB,
						    "selected %s dialect\n",
						    smb1_protos[i].name);
					if (smb1_protos[i].index == SMB1_PROT)
						return seq_num;
					return smb1_protos[i].prot_id;
				}
			}
			seq_num++;
			bcount -= (++next);
		} while (bcount > 0);
	}

	return BAD_PROT_ID;
}

int ksmbd_lookup_dialect_by_id(__le16 *cli_dialects, __le16 dialects_count)
{
	int i;
	int count;

	for (i = ARRAY_SIZE(smb2_protos) - 1; i >= 0; i--) {
		count = le16_to_cpu(dialects_count);
		while (--count >= 0) {
			ksmbd_debug(SMB, "client requested dialect 0x%x\n",
				    le16_to_cpu(cli_dialects[count]));
			if (le16_to_cpu(cli_dialects[count]) !=
					smb2_protos[i].prot_id)
				continue;

			if (supported_protocol(smb2_protos[i].index)) {
				ksmbd_debug(SMB, "selected %s dialect\n",
					    smb2_protos[i].name);
				return smb2_protos[i].prot_id;
			}
		}
	}

	return BAD_PROT_ID;
}

static int ksmbd_negotiate_smb_dialect(void *buf)
{
	__le32 proto;

	proto = ((struct smb2_hdr *)buf)->ProtocolId;
	if (proto == SMB2_PROTO_NUMBER) {
		struct smb2_negotiate_req *req;

		req = (struct smb2_negotiate_req *)buf;
		return ksmbd_lookup_dialect_by_id(req->Dialects,
						  req->DialectCount);
	}

	proto = *(__le32 *)((struct smb_hdr *)buf)->Protocol;
	if (proto == SMB1_PROTO_NUMBER) {
		struct smb_negotiate_req *req;

		req = (struct smb_negotiate_req *)buf;
		return ksmbd_lookup_dialect_by_name(req->DialectsArray,
						    req->ByteCount);
	}

	return BAD_PROT_ID;
}

#define SMB_COM_NEGOTIATE	0x72
int ksmbd_init_smb_server(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;

	if (conn->need_neg == false)
		return 0;

	init_smb3_11_server(conn);

	if (conn->ops->get_cmd_val(work) != SMB_COM_NEGOTIATE)
		conn->need_neg = false;
	return 0;
}

bool ksmbd_pdu_size_has_room(unsigned int pdu)
{
	return (pdu >= KSMBD_MIN_SUPPORTED_HEADER_SIZE - 4);
}

int ksmbd_populate_dot_dotdot_entries(struct ksmbd_work *work, int info_level,
				      struct ksmbd_file *dir,
				      struct ksmbd_dir_info *d_info,
				      char *search_pattern,
				      int (*fn)(struct ksmbd_conn *, int,
						struct ksmbd_dir_info *,
						struct ksmbd_kstat *))
{
	int i, rc = 0;
	struct ksmbd_conn *conn = work->conn;
	struct user_namespace *user_ns = file_mnt_user_ns(dir->filp);

	for (i = 0; i < 2; i++) {
		struct kstat kstat;
		struct ksmbd_kstat ksmbd_kstat;

		if (!dir->dot_dotdot[i]) { /* fill dot entry info */
			if (i == 0) {
				d_info->name = ".";
				d_info->name_len = 1;
			} else {
				d_info->name = "..";
				d_info->name_len = 2;
			}

			if (!match_pattern(d_info->name, d_info->name_len,
					   search_pattern)) {
				dir->dot_dotdot[i] = 1;
				continue;
			}

			ksmbd_kstat.kstat = &kstat;
			ksmbd_vfs_fill_dentry_attrs(work,
						    user_ns,
						    dir->filp->f_path.dentry->d_parent,
						    &ksmbd_kstat);
			rc = fn(conn, info_level, d_info, &ksmbd_kstat);
			if (rc)
				break;
			if (d_info->out_buf_len <= 0)
				break;

			dir->dot_dotdot[i] = 1;
			if (d_info->flags & SMB2_RETURN_SINGLE_ENTRY) {
				d_info->out_buf_len = 0;
				break;
			}
		}
	}

	return rc;
}

/**
 * ksmbd_extract_shortname() - get shortname from long filename
 * @conn:	connection instance
 * @longname:	source long filename
 * @shortname:	destination short filename
 *
 * Return:	shortname length or 0 when source long name is '.' or '..'
 * TODO: Though this function comforms the restriction of 8.3 Filename spec,
 * but the result is different with Windows 7's one. need to check.
 */
int ksmbd_extract_shortname(struct ksmbd_conn *conn, const char *longname,
			    char *shortname)
{
	const char *p;
	char base[9], extension[4];
	char out[13] = {0};
	int baselen = 0;
	int extlen = 0, len = 0;
	unsigned int csum = 0;
	const unsigned char *ptr;
	bool dot_present = true;

	p = longname;
	if ((*p == '.') || (!(strcmp(p, "..")))) {
		/*no mangling required */
		return 0;
	}

	p = strrchr(longname, '.');
	if (p == longname) { /*name starts with a dot*/
		strscpy(extension, "___", strlen("___"));
	} else {
		if (p) {
			p++;
			while (*p && extlen < 3) {
				if (*p != '.')
					extension[extlen++] = toupper(*p);
				p++;
			}
			extension[extlen] = '\0';
		} else {
			dot_present = false;
		}
	}

	p = longname;
	if (*p == '.') {
		p++;
		longname++;
	}
	while (*p && (baselen < 5)) {
		if (*p != '.')
			base[baselen++] = toupper(*p);
		p++;
	}

	base[baselen] = MAGIC_CHAR;
	memcpy(out, base, baselen + 1);

	ptr = longname;
	len = strlen(longname);
	for (; len > 0; len--, ptr++)
		csum += *ptr;

	csum = csum % (MANGLE_BASE * MANGLE_BASE);
	out[baselen + 1] = mangle(csum / MANGLE_BASE);
	out[baselen + 2] = mangle(csum);
	out[baselen + 3] = PERIOD;

	if (dot_present)
		memcpy(&out[baselen + 4], extension, 4);
	else
		out[baselen + 4] = '\0';
	smbConvertToUTF16((__le16 *)shortname, out, PATH_MAX,
			  conn->local_nls, 0);
	len = strlen(out) * 2;
	return len;
}

static int __smb2_negotiate(struct ksmbd_conn *conn)
{
	return (conn->dialect >= SMB20_PROT_ID &&
		conn->dialect <= SMB311_PROT_ID);
}

static int smb_handle_negotiate(struct ksmbd_work *work)
{
	struct smb_negotiate_rsp *neg_rsp = work->response_buf;

	ksmbd_debug(SMB, "Unsupported SMB protocol\n");
	neg_rsp->hdr.Status.CifsError = STATUS_INVALID_LOGON_TYPE;
	return -EINVAL;
}

int ksmbd_smb_negotiate_common(struct ksmbd_work *work, unsigned int command)
{
	struct ksmbd_conn *conn = work->conn;
	int ret;

	conn->dialect = ksmbd_negotiate_smb_dialect(work->request_buf);
	ksmbd_debug(SMB, "conn->dialect 0x%x\n", conn->dialect);

	if (command == SMB2_NEGOTIATE_HE) {
		struct smb2_hdr *smb2_hdr = work->request_buf;

		if (smb2_hdr->ProtocolId != SMB2_PROTO_NUMBER) {
			ksmbd_debug(SMB, "Downgrade to SMB1 negotiation\n");
			command = SMB_COM_NEGOTIATE;
		}
	}

	if (command == SMB2_NEGOTIATE_HE) {
		ret = smb2_handle_negotiate(work);
		init_smb2_neg_rsp(work);
		return ret;
	}

	if (command == SMB_COM_NEGOTIATE) {
		if (__smb2_negotiate(conn)) {
			conn->need_neg = true;
			init_smb3_11_server(conn);
			init_smb2_neg_rsp(work);
			ksmbd_debug(SMB, "Upgrade to SMB2 negotiation\n");
			return 0;
		}
		return smb_handle_negotiate(work);
	}

	pr_err("Unknown SMB negotiation command: %u\n", command);
	return -EINVAL;
}

enum SHARED_MODE_ERRORS {
	SHARE_DELETE_ERROR,
	SHARE_READ_ERROR,
	SHARE_WRITE_ERROR,
	FILE_READ_ERROR,
	FILE_WRITE_ERROR,
	FILE_DELETE_ERROR,
};

static const char * const shared_mode_errors[] = {
	"Current access mode does not permit SHARE_DELETE",
	"Current access mode does not permit SHARE_READ",
	"Current access mode does not permit SHARE_WRITE",
	"Desired access mode does not permit FILE_READ",
	"Desired access mode does not permit FILE_WRITE",
	"Desired access mode does not permit FILE_DELETE",
};

static void smb_shared_mode_error(int error, struct ksmbd_file *prev_fp,
				  struct ksmbd_file *curr_fp)
{
	ksmbd_debug(SMB, "%s\n", shared_mode_errors[error]);
	ksmbd_debug(SMB, "Current mode: 0x%x Desired mode: 0x%x\n",
		    prev_fp->saccess, curr_fp->daccess);
}

int ksmbd_smb_check_shared_mode(struct file *filp, struct ksmbd_file *curr_fp)
{
	int rc = 0;
	struct ksmbd_file *prev_fp;

	/*
	 * Lookup fp in master fp list, and check desired access and
	 * shared mode between previous open and current open.
	 */
	read_lock(&curr_fp->f_ci->m_lock);
	list_for_each_entry(prev_fp, &curr_fp->f_ci->m_fp_list, node) {
		if (file_inode(filp) != file_inode(prev_fp->filp))
			continue;

		if (filp == prev_fp->filp)
			continue;

		if (ksmbd_stream_fd(prev_fp) && ksmbd_stream_fd(curr_fp))
			if (strcmp(prev_fp->stream.name, curr_fp->stream.name))
				continue;

		if (prev_fp->attrib_only != curr_fp->attrib_only)
			continue;

		if (!(prev_fp->saccess & FILE_SHARE_DELETE_LE) &&
		    curr_fp->daccess & FILE_DELETE_LE) {
			smb_shared_mode_error(SHARE_DELETE_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}

		/*
		 * Only check FILE_SHARE_DELETE if stream opened and
		 * normal file opened.
		 */
		if (ksmbd_stream_fd(prev_fp) && !ksmbd_stream_fd(curr_fp))
			continue;

		if (!(prev_fp->saccess & FILE_SHARE_READ_LE) &&
		    curr_fp->daccess & (FILE_EXECUTE_LE | FILE_READ_DATA_LE)) {
			smb_shared_mode_error(SHARE_READ_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}

		if (!(prev_fp->saccess & FILE_SHARE_WRITE_LE) &&
		    curr_fp->daccess & (FILE_WRITE_DATA_LE | FILE_APPEND_DATA_LE)) {
			smb_shared_mode_error(SHARE_WRITE_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}

		if (prev_fp->daccess & (FILE_EXECUTE_LE | FILE_READ_DATA_LE) &&
		    !(curr_fp->saccess & FILE_SHARE_READ_LE)) {
			smb_shared_mode_error(FILE_READ_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}

		if (prev_fp->daccess & (FILE_WRITE_DATA_LE | FILE_APPEND_DATA_LE) &&
		    !(curr_fp->saccess & FILE_SHARE_WRITE_LE)) {
			smb_shared_mode_error(FILE_WRITE_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}

		if (prev_fp->daccess & FILE_DELETE_LE &&
		    !(curr_fp->saccess & FILE_SHARE_DELETE_LE)) {
			smb_shared_mode_error(FILE_DELETE_ERROR,
					      prev_fp,
					      curr_fp);
			rc = -EPERM;
			break;
		}
	}
	read_unlock(&curr_fp->f_ci->m_lock);

	return rc;
}

bool is_asterisk(char *p)
{
	return p && p[0] == '*';
}

int ksmbd_override_fsids(struct ksmbd_work *work)
{
	struct ksmbd_session *sess = work->sess;
	struct ksmbd_share_config *share = work->tcon->share_conf;
	struct cred *cred;
	struct group_info *gi;
	unsigned int uid;
	unsigned int gid;

	uid = user_uid(sess->user);
	gid = user_gid(sess->user);
	if (share->force_uid != KSMBD_SHARE_INVALID_UID)
		uid = share->force_uid;
	if (share->force_gid != KSMBD_SHARE_INVALID_GID)
		gid = share->force_gid;

	cred = prepare_kernel_cred(NULL);
	if (!cred)
		return -ENOMEM;

	cred->fsuid = make_kuid(current_user_ns(), uid);
	cred->fsgid = make_kgid(current_user_ns(), gid);

	gi = groups_alloc(0);
	if (!gi) {
		abort_creds(cred);
		return -ENOMEM;
	}
	set_groups(cred, gi);
	put_group_info(gi);

	if (!uid_eq(cred->fsuid, GLOBAL_ROOT_UID))
		cred->cap_effective = cap_drop_fs_set(cred->cap_effective);

	WARN_ON(work->saved_cred);
	work->saved_cred = override_creds(cred);
	if (!work->saved_cred) {
		abort_creds(cred);
		return -EINVAL;
	}
	return 0;
}

void ksmbd_revert_fsids(struct ksmbd_work *work)
{
	const struct cred *cred;

	WARN_ON(!work->saved_cred);

	cred = current_cred();
	revert_creds(work->saved_cred);
	put_cred(cred);
	work->saved_cred = NULL;
}

__le32 smb_map_generic_desired_access(__le32 daccess)
{
	if (daccess & FILE_GENERIC_READ_LE) {
		daccess |= cpu_to_le32(GENERIC_READ_FLAGS);
		daccess &= ~FILE_GENERIC_READ_LE;
	}

	if (daccess & FILE_GENERIC_WRITE_LE) {
		daccess |= cpu_to_le32(GENERIC_WRITE_FLAGS);
		daccess &= ~FILE_GENERIC_WRITE_LE;
	}

	if (daccess & FILE_GENERIC_EXECUTE_LE) {
		daccess |= cpu_to_le32(GENERIC_EXECUTE_FLAGS);
		daccess &= ~FILE_GENERIC_EXECUTE_LE;
	}

	if (daccess & FILE_GENERIC_ALL_LE) {
		daccess |= cpu_to_le32(GENERIC_ALL_FLAGS);
		daccess &= ~FILE_GENERIC_ALL_LE;
	}

	return daccess;
}
