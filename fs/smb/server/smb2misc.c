// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include "glob.h"
#include "nterr.h"
#include "smb_common.h"
#include "smbstatus.h"
#include "mgmt/user_session.h"
#include "connection.h"

static int check_smb2_hdr(struct smb2_hdr *hdr)
{
	/*
	 * Make sure that this really is an SMB, that it is a response.
	 */
	if (hdr->Flags & SMB2_FLAGS_SERVER_TO_REDIR)
		return 1;
	return 0;
}

/*
 *  The following table defines the expected "StructureSize" of SMB2 requests
 *  in order by SMB2 command.  This is similar to "wct" in SMB/CIFS requests.
 *
 *  Note that commands are defined in smb2pdu.h in le16 but the array below is
 *  indexed by command in host byte order
 */
static const __le16 smb2_req_struct_sizes[NUMBER_OF_SMB2_COMMANDS] = {
	/* SMB2_NEGOTIATE */ cpu_to_le16(36),
	/* SMB2_SESSION_SETUP */ cpu_to_le16(25),
	/* SMB2_LOGOFF */ cpu_to_le16(4),
	/* SMB2_TREE_CONNECT */ cpu_to_le16(9),
	/* SMB2_TREE_DISCONNECT */ cpu_to_le16(4),
	/* SMB2_CREATE */ cpu_to_le16(57),
	/* SMB2_CLOSE */ cpu_to_le16(24),
	/* SMB2_FLUSH */ cpu_to_le16(24),
	/* SMB2_READ */ cpu_to_le16(49),
	/* SMB2_WRITE */ cpu_to_le16(49),
	/* SMB2_LOCK */ cpu_to_le16(48),
	/* SMB2_IOCTL */ cpu_to_le16(57),
	/* SMB2_CANCEL */ cpu_to_le16(4),
	/* SMB2_ECHO */ cpu_to_le16(4),
	/* SMB2_QUERY_DIRECTORY */ cpu_to_le16(33),
	/* SMB2_CHANGE_NOTIFY */ cpu_to_le16(32),
	/* SMB2_QUERY_INFO */ cpu_to_le16(41),
	/* SMB2_SET_INFO */ cpu_to_le16(33),
	/* use 44 for lease break */
	/* SMB2_OPLOCK_BREAK */ cpu_to_le16(36)
};

/*
 * The size of the variable area depends on the offset and length fields
 * located in different fields for various SMB2 requests. SMB2 requests
 * with no variable length info, show an offset of zero for the offset field.
 */
static const bool has_smb2_data_area[NUMBER_OF_SMB2_COMMANDS] = {
	/* SMB2_NEGOTIATE */ true,
	/* SMB2_SESSION_SETUP */ true,
	/* SMB2_LOGOFF */ false,
	/* SMB2_TREE_CONNECT */	true,
	/* SMB2_TREE_DISCONNECT */ false,
	/* SMB2_CREATE */ true,
	/* SMB2_CLOSE */ false,
	/* SMB2_FLUSH */ false,
	/* SMB2_READ */	true,
	/* SMB2_WRITE */ true,
	/* SMB2_LOCK */	true,
	/* SMB2_IOCTL */ true,
	/* SMB2_CANCEL */ false, /* BB CHECK this not listed in documentation */
	/* SMB2_ECHO */ false,
	/* SMB2_QUERY_DIRECTORY */ true,
	/* SMB2_CHANGE_NOTIFY */ false,
	/* SMB2_QUERY_INFO */ true,
	/* SMB2_SET_INFO */ true,
	/* SMB2_OPLOCK_BREAK */ false
};

/*
 * Set length of the data area and the offset to arguments.
 * if they are invalid, return error.
 */
static int smb2_get_data_area_len(unsigned int *off, unsigned int *len,
				  struct smb2_hdr *hdr)
{
	int ret = 0;

	*off = 0;
	*len = 0;

	/*
	 * Following commands have data areas so we have to get the location
	 * of the data buffer offset and data buffer length for the particular
	 * command.
	 */
	switch (hdr->Command) {
	case SMB2_SESSION_SETUP:
		*off = le16_to_cpu(((struct smb2_sess_setup_req *)hdr)->SecurityBufferOffset);
		*len = le16_to_cpu(((struct smb2_sess_setup_req *)hdr)->SecurityBufferLength);
		break;
	case SMB2_TREE_CONNECT:
		*off = max_t(unsigned short int,
			     le16_to_cpu(((struct smb2_tree_connect_req *)hdr)->PathOffset),
			     offsetof(struct smb2_tree_connect_req, Buffer));
		*len = le16_to_cpu(((struct smb2_tree_connect_req *)hdr)->PathLength);
		break;
	case SMB2_CREATE:
	{
		unsigned short int name_off =
			max_t(unsigned short int,
			      le16_to_cpu(((struct smb2_create_req *)hdr)->NameOffset),
			      offsetof(struct smb2_create_req, Buffer));
		unsigned short int name_len =
			le16_to_cpu(((struct smb2_create_req *)hdr)->NameLength);

		if (((struct smb2_create_req *)hdr)->CreateContextsLength) {
			*off = le32_to_cpu(((struct smb2_create_req *)
				hdr)->CreateContextsOffset);
			*len = le32_to_cpu(((struct smb2_create_req *)
				hdr)->CreateContextsLength);
			if (!name_len)
				break;

			if (name_off + name_len < (u64)*off + *len)
				break;
		}

		*off = name_off;
		*len = name_len;
		break;
	}
	case SMB2_QUERY_INFO:
		*off = max_t(unsigned int,
			     le16_to_cpu(((struct smb2_query_info_req *)hdr)->InputBufferOffset),
			     offsetof(struct smb2_query_info_req, Buffer));
		*len = le32_to_cpu(((struct smb2_query_info_req *)hdr)->InputBufferLength);
		break;
	case SMB2_SET_INFO:
		*off = max_t(unsigned int,
			     le16_to_cpu(((struct smb2_set_info_req *)hdr)->BufferOffset),
			     offsetof(struct smb2_set_info_req, Buffer));
		*len = le32_to_cpu(((struct smb2_set_info_req *)hdr)->BufferLength);
		break;
	case SMB2_READ:
		*off = le16_to_cpu(((struct smb2_read_req *)hdr)->ReadChannelInfoOffset);
		*len = le16_to_cpu(((struct smb2_read_req *)hdr)->ReadChannelInfoLength);
		break;
	case SMB2_WRITE:
		if (((struct smb2_write_req *)hdr)->DataOffset ||
		    ((struct smb2_write_req *)hdr)->Length) {
			*off = max_t(unsigned short int,
				     le16_to_cpu(((struct smb2_write_req *)hdr)->DataOffset),
				     offsetof(struct smb2_write_req, Buffer));
			*len = le32_to_cpu(((struct smb2_write_req *)hdr)->Length);
			break;
		}

		*off = le16_to_cpu(((struct smb2_write_req *)hdr)->WriteChannelInfoOffset);
		*len = le16_to_cpu(((struct smb2_write_req *)hdr)->WriteChannelInfoLength);
		break;
	case SMB2_QUERY_DIRECTORY:
		*off = max_t(unsigned short int,
			     le16_to_cpu(((struct smb2_query_directory_req *)hdr)->FileNameOffset),
			     offsetof(struct smb2_query_directory_req, Buffer));
		*len = le16_to_cpu(((struct smb2_query_directory_req *)hdr)->FileNameLength);
		break;
	case SMB2_LOCK:
	{
		unsigned short lock_count;

		lock_count = le16_to_cpu(((struct smb2_lock_req *)hdr)->LockCount);
		if (lock_count > 0) {
			*off = offsetof(struct smb2_lock_req, locks);
			*len = sizeof(struct smb2_lock_element) * lock_count;
		}
		break;
	}
	case SMB2_IOCTL:
		*off = max_t(unsigned int,
			     le32_to_cpu(((struct smb2_ioctl_req *)hdr)->InputOffset),
			     offsetof(struct smb2_ioctl_req, Buffer));
		*len = le32_to_cpu(((struct smb2_ioctl_req *)hdr)->InputCount);
		break;
	default:
		ksmbd_debug(SMB, "no length check for command\n");
		break;
	}

	if (*off > 4096) {
		ksmbd_debug(SMB, "offset %d too large\n", *off);
		ret = -EINVAL;
	} else if ((u64)*off + *len > MAX_STREAM_PROT_LEN) {
		ksmbd_debug(SMB, "Request is larger than maximum stream protocol length(%u): %llu\n",
			    MAX_STREAM_PROT_LEN, (u64)*off + *len);
		ret = -EINVAL;
	}

	return ret;
}

/*
 * Calculate the size of the SMB message based on the fixed header
 * portion, the number of word parameters and the data portion of the message.
 */
static int smb2_calc_size(void *buf, unsigned int *len)
{
	struct smb2_pdu *pdu = (struct smb2_pdu *)buf;
	struct smb2_hdr *hdr = &pdu->hdr;
	unsigned int offset; /* the offset from the beginning of SMB to data area */
	unsigned int data_length; /* the length of the variable length data area */
	int ret;

	/* Structure Size has already been checked to make sure it is 64 */
	*len = le16_to_cpu(hdr->StructureSize);

	/*
	 * StructureSize2, ie length of fixed parameter area has already
	 * been checked to make sure it is the correct length.
	 */
	*len += le16_to_cpu(pdu->StructureSize2);
	/*
	 * StructureSize2 of smb2_lock pdu is set to 48, indicating
	 * the size of smb2 lock request with single smb2_lock_element
	 * regardless of number of locks. Subtract single
	 * smb2_lock_element for correct buffer size check.
	 */
	if (hdr->Command == SMB2_LOCK)
		*len -= sizeof(struct smb2_lock_element);

	if (has_smb2_data_area[le16_to_cpu(hdr->Command)] == false)
		goto calc_size_exit;

	ret = smb2_get_data_area_len(&offset, &data_length, hdr);
	if (ret)
		return ret;
	ksmbd_debug(SMB, "SMB2 data length %u offset %u\n", data_length,
		    offset);

	if (data_length > 0) {
		/*
		 * Check to make sure that data area begins after fixed area,
		 * Note that last byte of the fixed area is part of data area
		 * for some commands, typically those with odd StructureSize,
		 * so we must add one to the calculation.
		 */
		if (offset + 1 < *len) {
			ksmbd_debug(SMB,
				    "data area offset %d overlaps SMB2 header %u\n",
				    offset + 1, *len);
			return -EINVAL;
		}

		*len = offset + data_length;
	}

calc_size_exit:
	ksmbd_debug(SMB, "SMB2 len %u\n", *len);
	return 0;
}

static inline int smb2_query_info_req_len(struct smb2_query_info_req *h)
{
	return le32_to_cpu(h->InputBufferLength) +
		le32_to_cpu(h->OutputBufferLength);
}

static inline int smb2_set_info_req_len(struct smb2_set_info_req *h)
{
	return le32_to_cpu(h->BufferLength);
}

static inline int smb2_read_req_len(struct smb2_read_req *h)
{
	return le32_to_cpu(h->Length);
}

static inline int smb2_write_req_len(struct smb2_write_req *h)
{
	return le32_to_cpu(h->Length);
}

static inline int smb2_query_dir_req_len(struct smb2_query_directory_req *h)
{
	return le32_to_cpu(h->OutputBufferLength);
}

static inline int smb2_ioctl_req_len(struct smb2_ioctl_req *h)
{
	return le32_to_cpu(h->InputCount) +
		le32_to_cpu(h->OutputCount);
}

static inline int smb2_ioctl_resp_len(struct smb2_ioctl_req *h)
{
	return le32_to_cpu(h->MaxInputResponse) +
		le32_to_cpu(h->MaxOutputResponse);
}

static int smb2_validate_credit_charge(struct ksmbd_conn *conn,
				       struct smb2_hdr *hdr)
{
	unsigned int req_len = 0, expect_resp_len = 0, calc_credit_num, max_len;
	unsigned short credit_charge = le16_to_cpu(hdr->CreditCharge);
	void *__hdr = hdr;
	int ret = 0;

	switch (hdr->Command) {
	case SMB2_QUERY_INFO:
		req_len = smb2_query_info_req_len(__hdr);
		break;
	case SMB2_SET_INFO:
		req_len = smb2_set_info_req_len(__hdr);
		break;
	case SMB2_READ:
		req_len = smb2_read_req_len(__hdr);
		break;
	case SMB2_WRITE:
		req_len = smb2_write_req_len(__hdr);
		break;
	case SMB2_QUERY_DIRECTORY:
		req_len = smb2_query_dir_req_len(__hdr);
		break;
	case SMB2_IOCTL:
		req_len = smb2_ioctl_req_len(__hdr);
		expect_resp_len = smb2_ioctl_resp_len(__hdr);
		break;
	case SMB2_CANCEL:
		return 0;
	default:
		req_len = 1;
		break;
	}

	credit_charge = max_t(unsigned short, credit_charge, 1);
	max_len = max_t(unsigned int, req_len, expect_resp_len);
	calc_credit_num = DIV_ROUND_UP(max_len, SMB2_MAX_BUFFER_SIZE);

	if (credit_charge < calc_credit_num) {
		ksmbd_debug(SMB, "Insufficient credit charge, given: %d, needed: %d\n",
			    credit_charge, calc_credit_num);
		return 1;
	} else if (credit_charge > conn->vals->max_credits) {
		ksmbd_debug(SMB, "Too large credit charge: %d\n", credit_charge);
		return 1;
	}

	spin_lock(&conn->credits_lock);
	if (credit_charge > conn->total_credits) {
		ksmbd_debug(SMB, "Insufficient credits granted, given: %u, granted: %u\n",
			    credit_charge, conn->total_credits);
		ret = 1;
	}

	if ((u64)conn->outstanding_credits + credit_charge > conn->total_credits) {
		ksmbd_debug(SMB, "Limits exceeding the maximum allowable outstanding requests, given : %u, pending : %u\n",
			    credit_charge, conn->outstanding_credits);
		ret = 1;
	} else
		conn->outstanding_credits += credit_charge;

	spin_unlock(&conn->credits_lock);

	return ret;
}

int ksmbd_smb2_check_message(struct ksmbd_work *work)
{
	struct smb2_pdu *pdu = ksmbd_req_buf_next(work);
	struct smb2_hdr *hdr = &pdu->hdr;
	int command;
	__u32 clc_len;  /* calculated length */
	__u32 len = get_rfc1002_len(work->request_buf);
	__u32 req_struct_size, next_cmd = le32_to_cpu(hdr->NextCommand);

	if ((u64)work->next_smb2_rcv_hdr_off + next_cmd > len) {
		pr_err("next command(%u) offset exceeds smb msg size\n",
				next_cmd);
		return 1;
	}

	if (next_cmd > 0)
		len = next_cmd;
	else if (work->next_smb2_rcv_hdr_off)
		len -= work->next_smb2_rcv_hdr_off;

	if (check_smb2_hdr(hdr))
		return 1;

	if (hdr->StructureSize != SMB2_HEADER_STRUCTURE_SIZE) {
		ksmbd_debug(SMB, "Illegal structure size %u\n",
			    le16_to_cpu(hdr->StructureSize));
		return 1;
	}

	command = le16_to_cpu(hdr->Command);
	if (command >= NUMBER_OF_SMB2_COMMANDS) {
		ksmbd_debug(SMB, "Illegal SMB2 command %d\n", command);
		return 1;
	}

	if (smb2_req_struct_sizes[command] != pdu->StructureSize2) {
		if (!(command == SMB2_OPLOCK_BREAK_HE &&
		    (le16_to_cpu(pdu->StructureSize2) == OP_BREAK_STRUCT_SIZE_20 ||
		    le16_to_cpu(pdu->StructureSize2) == OP_BREAK_STRUCT_SIZE_21))) {
			/* special case for SMB2.1 lease break message */
			ksmbd_debug(SMB,
				"Illegal request size %u for command %d\n",
				le16_to_cpu(pdu->StructureSize2), command);
			return 1;
		}
	}

	req_struct_size = le16_to_cpu(pdu->StructureSize2) +
		__SMB2_HEADER_STRUCTURE_SIZE;
	if (command == SMB2_LOCK_HE)
		req_struct_size -= sizeof(struct smb2_lock_element);

	if (req_struct_size > len + 1)
		return 1;

	if (smb2_calc_size(hdr, &clc_len))
		return 1;

	if (len != clc_len) {
		/* client can return one byte more due to implied bcc[0] */
		if (clc_len == len + 1)
			goto validate_credit;

		/*
		 * Some windows servers (win2016) will pad also the final
		 * PDU in a compound to 8 bytes.
		 */
		if (ALIGN(clc_len, 8) == len)
			goto validate_credit;

		/*
		 * SMB2 NEGOTIATE request will be validated when message
		 * handling proceeds.
		 */
		if (command == SMB2_NEGOTIATE_HE)
			goto validate_credit;

		/*
		 * Allow a message that padded to 8byte boundary.
		 * Linux 4.19.217 with smb 3.0.2 are sometimes
		 * sending messages where the cls_len is exactly
		 * 8 bytes less than len.
		 */
		if (clc_len < len && (len - clc_len) <= 8)
			goto validate_credit;

		pr_err_ratelimited(
			    "cli req too short, len %d not %d. cmd:%d mid:%llu\n",
			    len, clc_len, command,
			    le64_to_cpu(hdr->MessageId));

		return 1;
	}

validate_credit:
	if ((work->conn->vals->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU) &&
	    smb2_validate_credit_charge(work->conn, hdr))
		return 1;

	return 0;
}

int smb2_negotiate_request(struct ksmbd_work *work)
{
	return ksmbd_smb_negotiate_common(work, SMB2_NEGOTIATE_HE);
}
