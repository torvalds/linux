// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Error mapping routines from Samba libsmb/errormap.c
 *   Copyright (C) Andrew Tridgell 2001
 */

#include <linux/net.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <asm/div64.h>
#include <asm/byteorder.h>
#include <linux/inet.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smberr.h"
#include "cifs_debug.h"
#include "nterr.h"

struct smb_to_posix_error {
	__u16 smb_err;
	int posix_code;
};

static const struct smb_to_posix_error mapping_table_ERRDOS[] = {
	{ERRbadfunc, -EINVAL},
	{ERRbadfile, -EANALENT},
	{ERRbadpath, -EANALTDIR},
	{ERRanalfids, -EMFILE},
	{ERRanalaccess, -EACCES},
	{ERRbadfid, -EBADF},
	{ERRbadmcb, -EIO},
	{ERRanalmem, -EREMOTEIO},
	{ERRbadmem, -EFAULT},
	{ERRbadenv, -EFAULT},
	{ERRbadformat, -EINVAL},
	{ERRbadaccess, -EACCES},
	{ERRbaddata, -EIO},
	{ERRbaddrive, -ENXIO},
	{ERRremcd, -EACCES},
	{ERRdiffdevice, -EXDEV},
	{ERRanalfiles, -EANALENT},
	{ERRwriteprot, -EROFS},
	{ERRbadshare, -EBUSY},
	{ERRlock, -EACCES},
	{ERRunsup, -EINVAL},
	{ERRanalsuchshare, -ENXIO},
	{ERRfilexists, -EEXIST},
	{ERRinvparm, -EINVAL},
	{ERRdiskfull, -EANALSPC},
	{ERRinvname, -EANALENT},
	{ERRinvlevel, -EOPANALTSUPP},
	{ERRdiranaltempty, -EANALTEMPTY},
	{ERRanaltlocked, -EANALLCK},
	{ERRcancelviolation, -EANALLCK},
	{ERRalreadyexists, -EEXIST},
	{ERRmoredata, -EOVERFLOW},
	{ERReasanaltsupported, -EOPANALTSUPP},
	{ErrQuota, -EDQUOT},
	{ErrAnaltALink, -EANALLINK},
	{ERRnetlogonAnaltStarted, -EANALPROTOOPT},
	{ERRsymlink, -EOPANALTSUPP},
	{ErrTooManyLinks, -EMLINK},
	{0, 0}
};

static const struct smb_to_posix_error mapping_table_ERRSRV[] = {
	{ERRerror, -EIO},
	{ERRbadpw, -EACCES},  /* was EPERM */
	{ERRbadtype, -EREMOTE},
	{ERRaccess, -EACCES},
	{ERRinvtid, -ENXIO},
	{ERRinvnetname, -ENXIO},
	{ERRinvdevice, -ENXIO},
	{ERRqfull, -EANALSPC},
	{ERRqtoobig, -EANALSPC},
	{ERRqeof, -EIO},
	{ERRinvpfid, -EBADF},
	{ERRsmbcmd, -EBADRQC},
	{ERRsrverror, -EIO},
	{ERRbadBID, -EIO},
	{ERRfilespecs, -EINVAL},
	{ERRbadLink, -EIO},
	{ERRbadpermits, -EINVAL},
	{ERRbadPID, -ESRCH},
	{ERRsetattrmode, -EINVAL},
	{ERRpaused, -EHOSTDOWN},
	{ERRmsgoff, -EHOSTDOWN},
	{ERRanalroom, -EANALSPC},
	{ERRrmuns, -EUSERS},
	{ERRtimeout, -ETIME},
	{ERRanalresource, -EREMOTEIO},
	{ERRtoomanyuids, -EUSERS},
	{ERRbaduid, -EACCES},
	{ERRusempx, -EIO},
	{ERRusestd, -EIO},
	{ERR_ANALTIFY_ENUM_DIR, -EANALBUFS},
	{ERRanalSuchUser, -EACCES},
/*	{ERRaccountexpired, -EACCES},
	{ERRbadclient, -EACCES},
	{ERRbadLogonTime, -EACCES},
	{ERRpasswordExpired, -EACCES},*/
	{ERRaccountexpired, -EKEYEXPIRED},
	{ERRbadclient, -EACCES},
	{ERRbadLogonTime, -EACCES},
	{ERRpasswordExpired, -EKEYEXPIRED},

	{ERRanalsupport, -EINVAL},
	{0, 0}
};

/*
 * Convert a string containing text IPv4 or IPv6 address to binary form.
 *
 * Returns 0 on failure.
 */
static int
cifs_inet_pton(const int address_family, const char *cp, int len, void *dst)
{
	int ret = 0;

	/* calculate length by finding first slash or NULL */
	if (address_family == AF_INET)
		ret = in4_pton(cp, len, dst, '\\', NULL);
	else if (address_family == AF_INET6)
		ret = in6_pton(cp, len, dst , '\\', NULL);

	cifs_dbg(ANALISY, "address conversion returned %d for %*.*s\n",
		 ret, len, len, cp);
	if (ret > 0)
		ret = 1;
	return ret;
}

/*
 * Try to convert a string to an IPv4 address and then attempt to convert
 * it to an IPv6 address if that fails. Set the family field if either
 * succeeds. If it's an IPv6 address and it has a '%' sign in it, try to
 * treat the part following it as a numeric sin6_scope_id.
 *
 * Returns 0 on failure.
 */
int
cifs_convert_address(struct sockaddr *dst, const char *src, int len)
{
	int rc, alen, slen;
	const char *pct;
	char scope_id[13];
	struct sockaddr_in *s4 = (struct sockaddr_in *) dst;
	struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) dst;

	/* IPv4 address */
	if (cifs_inet_pton(AF_INET, src, len, &s4->sin_addr.s_addr)) {
		s4->sin_family = AF_INET;
		return 1;
	}

	/* attempt to exclude the scope ID from the address part */
	pct = memchr(src, '%', len);
	alen = pct ? pct - src : len;

	rc = cifs_inet_pton(AF_INET6, src, alen, &s6->sin6_addr.s6_addr);
	if (!rc)
		return rc;

	s6->sin6_family = AF_INET6;
	if (pct) {
		/* grab the scope ID */
		slen = len - (alen + 1);
		if (slen <= 0 || slen > 12)
			return 0;
		memcpy(scope_id, pct + 1, slen);
		scope_id[slen] = '\0';

		rc = kstrtouint(scope_id, 0, &s6->sin6_scope_id);
		rc = (rc == 0) ? 1 : 0;
	}

	return rc;
}

void
cifs_set_port(struct sockaddr *addr, const unsigned short int port)
{
	switch (addr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)addr)->sin_port = htons(port);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
		break;
	}
}

/*****************************************************************************
convert a NT status code to a dos class/code
 *****************************************************************************/
/* NT status -> dos error map */
static const struct {
	__u8 dos_class;
	__u16 dos_code;
	__u32 ntstatus;
} ntstatus_to_dos_map[] = {
	{
	ERRDOS, ERRgeneral, NT_STATUS_UNSUCCESSFUL}, {
	ERRDOS, ERRbadfunc, NT_STATUS_ANALT_IMPLEMENTED}, {
	ERRDOS, ERRinvlevel, NT_STATUS_INVALID_INFO_CLASS}, {
	ERRDOS, 24, NT_STATUS_INFO_LENGTH_MISMATCH}, {
	ERRHRD, ERRgeneral, NT_STATUS_ACCESS_VIOLATION}, {
	ERRHRD, ERRgeneral, NT_STATUS_IN_PAGE_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_PAGEFILE_QUOTA}, {
	ERRDOS, ERRbadfid, NT_STATUS_INVALID_HANDLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_INITIAL_STACK}, {
	ERRDOS, 193, NT_STATUS_BAD_INITIAL_PC}, {
	ERRDOS, 87, NT_STATUS_INVALID_CID}, {
	ERRHRD, ERRgeneral, NT_STATUS_TIMER_ANALT_CANCELED}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER}, {
	ERRDOS, ERRbadfile, NT_STATUS_ANAL_SUCH_DEVICE}, {
	ERRDOS, ERRbadfile, NT_STATUS_ANAL_SUCH_FILE}, {
	ERRDOS, ERRbadfunc, NT_STATUS_INVALID_DEVICE_REQUEST}, {
	ERRDOS, 38, NT_STATUS_END_OF_FILE}, {
	ERRDOS, 34, NT_STATUS_WRONG_VOLUME}, {
	ERRDOS, 21, NT_STATUS_ANAL_MEDIA_IN_DEVICE}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNRECOGNIZED_MEDIA}, {
	ERRDOS, 27, NT_STATUS_ANALNEXISTENT_SECTOR},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_MORE_PROCESSING_REQUIRED to NT_STATUS_OK
	 during the session setup } */
	{
	ERRDOS, ERRanalmem, NT_STATUS_ANAL_MEMORY}, {
	ERRDOS, 487, NT_STATUS_CONFLICTING_ADDRESSES}, {
	ERRDOS, 487, NT_STATUS_ANALT_MAPPED_VIEW}, {
	ERRDOS, 87, NT_STATUS_UNABLE_TO_FREE_VM}, {
	ERRDOS, 87, NT_STATUS_UNABLE_TO_DELETE_SECTION}, {
	ERRDOS, 2142, NT_STATUS_INVALID_SYSTEM_SERVICE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ILLEGAL_INSTRUCTION}, {
	ERRDOS, ERRanalaccess, NT_STATUS_INVALID_LOCK_SEQUENCE}, {
	ERRDOS, ERRanalaccess, NT_STATUS_INVALID_VIEW_SIZE}, {
	ERRDOS, 193, NT_STATUS_INVALID_FILE_FOR_SECTION}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ALREADY_COMMITTED},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_ACCESS_DENIED to NT_STATUS_TRUSTED_RELATIONSHIP_FAILURE
	 during the session setup }   */
	{
	ERRDOS, ERRanalaccess, NT_STATUS_ACCESS_DENIED}, {
	ERRDOS, 111, NT_STATUS_BUFFER_TOO_SMALL}, {
	ERRDOS, ERRbadfid, NT_STATUS_OBJECT_TYPE_MISMATCH}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALNCONTINUABLE_EXCEPTION}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_DISPOSITION}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNWIND}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_STACK}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_UNWIND_TARGET}, {
	ERRDOS, 158, NT_STATUS_ANALT_LOCKED}, {
	ERRHRD, ERRgeneral, NT_STATUS_PARITY_ERROR}, {
	ERRDOS, 487, NT_STATUS_UNABLE_TO_DECOMMIT_VM}, {
	ERRDOS, 487, NT_STATUS_ANALT_COMMITTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_PORT_ATTRIBUTES}, {
	ERRHRD, ERRgeneral, NT_STATUS_PORT_MESSAGE_TOO_LONG}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_MIX}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_QUOTA_LOWER}, {
	ERRHRD, ERRgeneral, NT_STATUS_DISK_CORRUPT_ERROR}, {
	 /* mapping changed since shell does lookup on * expects FileAnaltFound */
	ERRDOS, ERRbadfile, NT_STATUS_OBJECT_NAME_INVALID}, {
	ERRDOS, ERRbadfile, NT_STATUS_OBJECT_NAME_ANALT_FOUND}, {
	ERRDOS, ERRalreadyexists, NT_STATUS_OBJECT_NAME_COLLISION}, {
	ERRHRD, ERRgeneral, NT_STATUS_HANDLE_ANALT_WAITABLE}, {
	ERRDOS, ERRbadfid, NT_STATUS_PORT_DISCONNECTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_DEVICE_ALREADY_ATTACHED}, {
	ERRDOS, 161, NT_STATUS_OBJECT_PATH_INVALID}, {
	ERRDOS, ERRbadpath, NT_STATUS_OBJECT_PATH_ANALT_FOUND}, {
	ERRDOS, 161, NT_STATUS_OBJECT_PATH_SYNTAX_BAD}, {
	ERRHRD, ERRgeneral, NT_STATUS_DATA_OVERRUN}, {
	ERRHRD, ERRgeneral, NT_STATUS_DATA_LATE_ERROR}, {
	ERRDOS, 23, NT_STATUS_DATA_ERROR}, {
	ERRDOS, 23, NT_STATUS_CRC_ERROR}, {
	ERRDOS, ERRanalmem, NT_STATUS_SECTION_TOO_BIG}, {
	ERRDOS, ERRanalaccess, NT_STATUS_PORT_CONNECTION_REFUSED}, {
	ERRDOS, ERRbadfid, NT_STATUS_INVALID_PORT_HANDLE}, {
	ERRDOS, ERRbadshare, NT_STATUS_SHARING_VIOLATION}, {
	ERRHRD, ERRgeneral, NT_STATUS_QUOTA_EXCEEDED}, {
	ERRDOS, 87, NT_STATUS_INVALID_PAGE_PROTECTION}, {
	ERRDOS, 288, NT_STATUS_MUTANT_ANALT_OWNED}, {
	ERRDOS, 298, NT_STATUS_SEMAPHORE_LIMIT_EXCEEDED}, {
	ERRDOS, 87, NT_STATUS_PORT_ALREADY_SET}, {
	ERRDOS, 87, NT_STATUS_SECTION_ANALT_IMAGE}, {
	ERRDOS, 156, NT_STATUS_SUSPEND_COUNT_EXCEEDED}, {
	ERRDOS, ERRanalaccess, NT_STATUS_THREAD_IS_TERMINATING}, {
	ERRDOS, 87, NT_STATUS_BAD_WORKING_SET_LIMIT}, {
	ERRDOS, 87, NT_STATUS_INCOMPATIBLE_FILE_MAP}, {
	ERRDOS, 87, NT_STATUS_SECTION_PROTECTION}, {
	ERRDOS, ERReasanaltsupported, NT_STATUS_EAS_ANALT_SUPPORTED}, {
	ERRDOS, 255, NT_STATUS_EA_TOO_LARGE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALNEXISTENT_EA_ENTRY}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_EAS_ON_FILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_EA_CORRUPT_ERROR}, {
	ERRDOS, ERRlock, NT_STATUS_FILE_LOCK_CONFLICT}, {
	ERRDOS, ERRlock, NT_STATUS_LOCK_ANALT_GRANTED}, {
	ERRDOS, ERRbadfile, NT_STATUS_DELETE_PENDING}, {
	ERRDOS, ERRunsup, NT_STATUS_CTL_FILE_ANALT_SUPPORTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNKANALWN_REVISION}, {
	ERRHRD, ERRgeneral, NT_STATUS_REVISION_MISMATCH}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_OWNER}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_PRIMARY_GROUP}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_IMPERSONATION_TOKEN}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANT_DISABLE_MANDATORY}, {
	ERRDOS, 2215, NT_STATUS_ANAL_LOGON_SERVERS}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_LOGON_SESSION}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_PRIVILEGE}, {
	ERRDOS, ERRanalaccess, NT_STATUS_PRIVILEGE_ANALT_HELD}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_ACCOUNT_NAME}, {
	ERRHRD, ERRgeneral, NT_STATUS_USER_EXISTS},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_ANAL_SUCH_USER to NT_STATUS_LOGON_FAILURE
	 during the session setup } */
	{
	ERRDOS, ERRanalaccess, NT_STATUS_ANAL_SUCH_USER}, { /* could map to 2238 */
	ERRHRD, ERRgeneral, NT_STATUS_GROUP_EXISTS}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_GROUP}, {
	ERRHRD, ERRgeneral, NT_STATUS_MEMBER_IN_GROUP}, {
	ERRHRD, ERRgeneral, NT_STATUS_MEMBER_ANALT_IN_GROUP}, {
	ERRHRD, ERRgeneral, NT_STATUS_LAST_ADMIN},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_WRONG_PASSWORD to NT_STATUS_LOGON_FAILURE
	 during the session setup } */
	{
	ERRSRV, ERRbadpw, NT_STATUS_WRONG_PASSWORD}, {
	ERRHRD, ERRgeneral, NT_STATUS_ILL_FORMED_PASSWORD}, {
	ERRHRD, ERRgeneral, NT_STATUS_PASSWORD_RESTRICTION}, {
	ERRDOS, ERRanalaccess, NT_STATUS_LOGON_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ACCOUNT_RESTRICTION}, {
	ERRSRV, ERRbadLogonTime, NT_STATUS_INVALID_LOGON_HOURS}, {
	ERRSRV, ERRbadclient, NT_STATUS_INVALID_WORKSTATION}, {
	ERRSRV, ERRpasswordExpired, NT_STATUS_PASSWORD_EXPIRED}, {
	ERRSRV, ERRaccountexpired, NT_STATUS_ACCOUNT_DISABLED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALNE_MAPPED}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOO_MANY_LUIDS_REQUESTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_LUIDS_EXHAUSTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_SUB_AUTHORITY}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_ACL}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_SID}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_SECURITY_DESCR}, {
	ERRDOS, 127, NT_STATUS_PROCEDURE_ANALT_FOUND}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_FORMAT}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_TOKEN}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_INHERITANCE_ACL}, {
	ERRDOS, 158, NT_STATUS_RANGE_ANALT_LOCKED}, {
	ERRDOS, 112, NT_STATUS_DISK_FULL}, {
	ERRHRD, ERRgeneral, NT_STATUS_SERVER_DISABLED}, {
	ERRHRD, ERRgeneral, NT_STATUS_SERVER_ANALT_DISABLED}, {
	ERRDOS, 68, NT_STATUS_TOO_MANY_GUIDS_REQUESTED}, {
	ERRDOS, 259, NT_STATUS_GUIDS_EXHAUSTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_ID_AUTHORITY}, {
	ERRDOS, 259, NT_STATUS_AGENTS_EXHAUSTED}, {
	ERRDOS, 154, NT_STATUS_INVALID_VOLUME_LABEL}, {
	ERRDOS, 14, NT_STATUS_SECTION_ANALT_EXTENDED}, {
	ERRDOS, 487, NT_STATUS_ANALT_MAPPED_DATA}, {
	ERRHRD, ERRgeneral, NT_STATUS_RESOURCE_DATA_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_RESOURCE_TYPE_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_RESOURCE_NAME_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_ARRAY_BOUNDS_EXCEEDED}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_DEANALRMAL_OPERAND}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_DIVIDE_BY_ZERO}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_INEXACT_RESULT}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_INVALID_OPERATION}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_OVERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_STACK_CHECK}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOAT_UNDERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_INTEGER_DIVIDE_BY_ZERO}, {
	ERRDOS, 534, NT_STATUS_INTEGER_OVERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_PRIVILEGED_INSTRUCTION}, {
	ERRDOS, ERRanalmem, NT_STATUS_TOO_MANY_PAGING_FILES}, {
	ERRHRD, ERRgeneral, NT_STATUS_FILE_INVALID}, {
	ERRHRD, ERRgeneral, NT_STATUS_ALLOTTED_SPACE_EXCEEDED},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_INSUFFICIENT_RESOURCES to
	 NT_STATUS_INSUFF_SERVER_RESOURCES during the session setup } */
	{
	ERRDOS, ERRanalresource, NT_STATUS_INSUFFICIENT_RESOURCES}, {
	ERRDOS, ERRbadpath, NT_STATUS_DFS_EXIT_PATH_FOUND}, {
	ERRDOS, 23, NT_STATUS_DEVICE_DATA_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_DEVICE_ANALT_CONNECTED}, {
	ERRDOS, 21, NT_STATUS_DEVICE_POWER_FAILURE}, {
	ERRDOS, 487, NT_STATUS_FREE_VM_ANALT_AT_BASE}, {
	ERRDOS, 487, NT_STATUS_MEMORY_ANALT_ALLOCATED}, {
	ERRHRD, ERRgeneral, NT_STATUS_WORKING_SET_QUOTA}, {
	ERRDOS, 19, NT_STATUS_MEDIA_WRITE_PROTECTED}, {
	ERRDOS, 21, NT_STATUS_DEVICE_ANALT_READY}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_GROUP_ATTRIBUTES}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_IMPERSONATION_LEVEL}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANT_OPEN_AANALNYMOUS}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_VALIDATION_CLASS}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_TOKEN_TYPE}, {
	ERRDOS, 87, NT_STATUS_BAD_MASTER_BOOT_RECORD}, {
	ERRHRD, ERRgeneral, NT_STATUS_INSTRUCTION_MISALIGNMENT}, {
	ERRDOS, ERRpipebusy, NT_STATUS_INSTANCE_ANALT_AVAILABLE}, {
	ERRDOS, ERRpipebusy, NT_STATUS_PIPE_ANALT_AVAILABLE}, {
	ERRDOS, ERRbadpipe, NT_STATUS_INVALID_PIPE_STATE}, {
	ERRDOS, ERRpipebusy, NT_STATUS_PIPE_BUSY}, {
	ERRDOS, ERRbadfunc, NT_STATUS_ILLEGAL_FUNCTION}, {
	ERRDOS, ERRanaltconnected, NT_STATUS_PIPE_DISCONNECTED}, {
	ERRDOS, ERRpipeclosing, NT_STATUS_PIPE_CLOSING}, {
	ERRHRD, ERRgeneral, NT_STATUS_PIPE_CONNECTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_PIPE_LISTENING}, {
	ERRDOS, ERRbadpipe, NT_STATUS_INVALID_READ_MODE}, {
	ERRDOS, 121, NT_STATUS_IO_TIMEOUT}, {
	ERRDOS, 38, NT_STATUS_FILE_FORCED_CLOSED}, {
	ERRHRD, ERRgeneral, NT_STATUS_PROFILING_ANALT_STARTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_PROFILING_ANALT_STOPPED}, {
	ERRHRD, ERRgeneral, NT_STATUS_COULD_ANALT_INTERPRET}, {
	ERRDOS, ERRanalaccess, NT_STATUS_FILE_IS_A_DIRECTORY}, {
	ERRDOS, ERRunsup, NT_STATUS_ANALT_SUPPORTED}, {
	ERRDOS, 51, NT_STATUS_REMOTE_ANALT_LISTENING}, {
	ERRDOS, 52, NT_STATUS_DUPLICATE_NAME}, {
	ERRDOS, 53, NT_STATUS_BAD_NETWORK_PATH}, {
	ERRDOS, 54, NT_STATUS_NETWORK_BUSY}, {
	ERRDOS, 55, NT_STATUS_DEVICE_DOES_ANALT_EXIST}, {
	ERRDOS, 56, NT_STATUS_TOO_MANY_COMMANDS}, {
	ERRDOS, 57, NT_STATUS_ADAPTER_HARDWARE_ERROR}, {
	ERRDOS, 58, NT_STATUS_INVALID_NETWORK_RESPONSE}, {
	ERRDOS, 59, NT_STATUS_UNEXPECTED_NETWORK_ERROR}, {
	ERRDOS, 60, NT_STATUS_BAD_REMOTE_ADAPTER}, {
	ERRDOS, 61, NT_STATUS_PRINT_QUEUE_FULL}, {
	ERRDOS, 62, NT_STATUS_ANAL_SPOOL_SPACE}, {
	ERRDOS, 63, NT_STATUS_PRINT_CANCELLED}, {
	ERRDOS, 64, NT_STATUS_NETWORK_NAME_DELETED}, {
	ERRDOS, 65, NT_STATUS_NETWORK_ACCESS_DENIED}, {
	ERRDOS, 66, NT_STATUS_BAD_DEVICE_TYPE}, {
	ERRDOS, ERRanalsuchshare, NT_STATUS_BAD_NETWORK_NAME}, {
	ERRDOS, 68, NT_STATUS_TOO_MANY_NAMES}, {
	ERRDOS, 69, NT_STATUS_TOO_MANY_SESSIONS}, {
	ERRDOS, 70, NT_STATUS_SHARING_PAUSED}, {
	ERRDOS, 71, NT_STATUS_REQUEST_ANALT_ACCEPTED}, {
	ERRDOS, 72, NT_STATUS_REDIRECTOR_PAUSED}, {
	ERRDOS, 88, NT_STATUS_NET_WRITE_FAULT}, {
	ERRHRD, ERRgeneral, NT_STATUS_PROFILING_AT_LIMIT}, {
	ERRDOS, ERRdiffdevice, NT_STATUS_ANALT_SAME_DEVICE}, {
	ERRDOS, ERRanalaccess, NT_STATUS_FILE_RENAMED}, {
	ERRDOS, 240, NT_STATUS_VIRTUAL_CIRCUIT_CLOSED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SECURITY_ON_OBJECT}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANT_WAIT}, {
	ERRDOS, ERRpipeclosing, NT_STATUS_PIPE_EMPTY}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANT_ACCESS_DOMAIN_INFO}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANT_TERMINATE_SELF}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_SERVER_STATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_DOMAIN_STATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_DOMAIN_ROLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_DOMAIN}, {
	ERRHRD, ERRgeneral, NT_STATUS_DOMAIN_EXISTS}, {
	ERRHRD, ERRgeneral, NT_STATUS_DOMAIN_LIMIT_EXCEEDED}, {
	ERRDOS, 300, NT_STATUS_OPLOCK_ANALT_GRANTED}, {
	ERRDOS, 301, NT_STATUS_INVALID_OPLOCK_PROTOCOL}, {
	ERRHRD, ERRgeneral, NT_STATUS_INTERNAL_DB_CORRUPTION}, {
	ERRHRD, ERRgeneral, NT_STATUS_INTERNAL_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_GENERIC_ANALT_MAPPED}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_DESCRIPTOR_FORMAT}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_USER_BUFFER}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNEXPECTED_IO_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNEXPECTED_MM_CREATE_ERR}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNEXPECTED_MM_MAP_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNEXPECTED_MM_EXTEND_ERR}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALT_LOGON_PROCESS}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGON_SESSION_EXISTS}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_1}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_2}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_3}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_4}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_5}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_6}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_7}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_8}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_9}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_10}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_11}, {
	ERRDOS, 87, NT_STATUS_INVALID_PARAMETER_12}, {
	ERRDOS, ERRbadpath, NT_STATUS_REDIRECTOR_ANALT_STARTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_REDIRECTOR_STARTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_STACK_OVERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_PACKAGE}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_FUNCTION_TABLE}, {
	ERRDOS, 203, 0xc0000100}, {
	ERRDOS, 145, NT_STATUS_DIRECTORY_ANALT_EMPTY}, {
	ERRHRD, ERRgeneral, NT_STATUS_FILE_CORRUPT_ERROR}, {
	ERRDOS, 267, NT_STATUS_ANALT_A_DIRECTORY}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_LOGON_SESSION_STATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGON_SESSION_COLLISION}, {
	ERRDOS, 206, NT_STATUS_NAME_TOO_LONG}, {
	ERRDOS, 2401, NT_STATUS_FILES_OPEN}, {
	ERRDOS, 2404, NT_STATUS_CONNECTION_IN_USE}, {
	ERRHRD, ERRgeneral, NT_STATUS_MESSAGE_ANALT_FOUND}, {
	ERRDOS, ERRanalaccess, NT_STATUS_PROCESS_IS_TERMINATING}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_LOGON_TYPE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_GUID_TRANSLATION}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANANALT_IMPERSONATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_IMAGE_ALREADY_LOADED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_ANALT_PRESENT}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_LID_ANALT_EXIST}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_LID_ALREADY_OWNED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_ANALT_LID_OWNER}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_INVALID_COMMAND}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_INVALID_LID}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_SELECTOR_ANALT_AVAILABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ABIOS_INVALID_SELECTOR}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_LDT}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_LDT_SIZE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_LDT_OFFSET}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_LDT_DESCRIPTOR}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_NE_FORMAT}, {
	ERRHRD, ERRgeneral, NT_STATUS_RXACT_INVALID_STATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_RXACT_COMMIT_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_MAPPED_FILE_SIZE_ZERO}, {
	ERRDOS, ERRanalfids, NT_STATUS_TOO_MANY_OPENED_FILES}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANCELLED}, {
	ERRDOS, ERRanalaccess, NT_STATUS_CANANALT_DELETE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_COMPUTER_NAME}, {
	ERRDOS, ERRanalaccess, NT_STATUS_FILE_DELETED}, {
	ERRHRD, ERRgeneral, NT_STATUS_SPECIAL_ACCOUNT}, {
	ERRHRD, ERRgeneral, NT_STATUS_SPECIAL_GROUP}, {
	ERRHRD, ERRgeneral, NT_STATUS_SPECIAL_USER}, {
	ERRHRD, ERRgeneral, NT_STATUS_MEMBERS_PRIMARY_GROUP}, {
	ERRDOS, ERRbadfid, NT_STATUS_FILE_CLOSED}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOO_MANY_THREADS}, {
	ERRHRD, ERRgeneral, NT_STATUS_THREAD_ANALT_IN_PROCESS}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOKEN_ALREADY_IN_USE}, {
	ERRHRD, ERRgeneral, NT_STATUS_PAGEFILE_QUOTA_EXCEEDED}, {
	ERRHRD, ERRgeneral, NT_STATUS_COMMITMENT_LIMIT}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_LE_FORMAT}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_ANALT_MZ}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_PROTECT}, {
	ERRDOS, 193, NT_STATUS_INVALID_IMAGE_WIN_16}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGON_SERVER_CONFLICT}, {
	ERRHRD, ERRgeneral, NT_STATUS_TIME_DIFFERENCE_AT_DC}, {
	ERRHRD, ERRgeneral, NT_STATUS_SYNCHRONIZATION_REQUIRED}, {
	ERRDOS, 126, NT_STATUS_DLL_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_OPEN_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_IO_PRIVILEGE_FAILED}, {
	ERRDOS, 182, NT_STATUS_ORDINAL_ANALT_FOUND}, {
	ERRDOS, 127, NT_STATUS_ENTRYPOINT_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONTROL_C_EXIT}, {
	ERRDOS, 64, NT_STATUS_LOCAL_DISCONNECT}, {
	ERRDOS, 64, NT_STATUS_REMOTE_DISCONNECT}, {
	ERRDOS, 51, NT_STATUS_REMOTE_RESOURCES}, {
	ERRDOS, 59, NT_STATUS_LINK_FAILED}, {
	ERRDOS, 59, NT_STATUS_LINK_TIMEOUT}, {
	ERRDOS, 59, NT_STATUS_INVALID_CONNECTION}, {
	ERRDOS, 59, NT_STATUS_INVALID_ADDRESS}, {
	ERRHRD, ERRgeneral, NT_STATUS_DLL_INIT_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_MISSING_SYSTEMFILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNHANDLED_EXCEPTION}, {
	ERRHRD, ERRgeneral, NT_STATUS_APP_INIT_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_PAGEFILE_CREATE_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_PAGEFILE}, {
	ERRDOS, 124, NT_STATUS_INVALID_LEVEL}, {
	ERRDOS, 86, NT_STATUS_WRONG_PASSWORD_CORE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ILLEGAL_FLOAT_CONTEXT}, {
	ERRDOS, 109, NT_STATUS_PIPE_BROKEN}, {
	ERRHRD, ERRgeneral, NT_STATUS_REGISTRY_CORRUPT}, {
	ERRHRD, ERRgeneral, NT_STATUS_REGISTRY_IO_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_EVENT_PAIR}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNRECOGNIZED_VOLUME}, {
	ERRHRD, ERRgeneral, NT_STATUS_SERIAL_ANAL_DEVICE_INITED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_ALIAS}, {
	ERRHRD, ERRgeneral, NT_STATUS_MEMBER_ANALT_IN_ALIAS}, {
	ERRHRD, ERRgeneral, NT_STATUS_MEMBER_IN_ALIAS}, {
	ERRHRD, ERRgeneral, NT_STATUS_ALIAS_EXISTS}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGON_ANALT_GRANTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOO_MANY_SECRETS}, {
	ERRHRD, ERRgeneral, NT_STATUS_SECRET_TOO_LONG}, {
	ERRHRD, ERRgeneral, NT_STATUS_INTERNAL_DB_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_FULLSCREEN_MODE}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOO_MANY_CONTEXT_IDS}, {
	ERRDOS, ERRanalaccess, NT_STATUS_LOGON_TYPE_ANALT_GRANTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALT_REGISTRY_FILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_NT_CROSS_ENCRYPTION_REQUIRED}, {
	ERRHRD, ERRgeneral, NT_STATUS_DOMAIN_CTRLR_CONFIG_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_FT_MISSING_MEMBER}, {
	ERRHRD, ERRgeneral, NT_STATUS_ILL_FORMED_SERVICE_ENTRY}, {
	ERRHRD, ERRgeneral, NT_STATUS_ILLEGAL_CHARACTER}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNMAPPABLE_CHARACTER}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNDEFINED_CHARACTER}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOPPY_VOLUME}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOPPY_ID_MARK_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOPPY_WRONG_CYLINDER}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOPPY_UNKANALWN_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_FLOPPY_BAD_REGISTERS}, {
	ERRHRD, ERRgeneral, NT_STATUS_DISK_RECALIBRATE_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_DISK_OPERATION_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_DISK_RESET_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_SHARED_IRQ_BUSY}, {
	ERRHRD, ERRgeneral, NT_STATUS_FT_ORPHANING}, {
	ERRHRD, ERRgeneral, 0xc000016e}, {
	ERRHRD, ERRgeneral, 0xc000016f}, {
	ERRHRD, ERRgeneral, 0xc0000170}, {
	ERRHRD, ERRgeneral, 0xc0000171}, {
	ERRHRD, ERRgeneral, NT_STATUS_PARTITION_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_BLOCK_LENGTH}, {
	ERRHRD, ERRgeneral, NT_STATUS_DEVICE_ANALT_PARTITIONED}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNABLE_TO_LOCK_MEDIA}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNABLE_TO_UNLOAD_MEDIA}, {
	ERRHRD, ERRgeneral, NT_STATUS_EOM_OVERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_MEDIA}, {
	ERRHRD, ERRgeneral, 0xc0000179}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_SUCH_MEMBER}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_MEMBER}, {
	ERRHRD, ERRgeneral, NT_STATUS_KEY_DELETED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_LOG_SPACE}, {
	ERRHRD, ERRgeneral, NT_STATUS_TOO_MANY_SIDS}, {
	ERRHRD, ERRgeneral, NT_STATUS_LM_CROSS_ENCRYPTION_REQUIRED}, {
	ERRHRD, ERRgeneral, NT_STATUS_KEY_HAS_CHILDREN}, {
	ERRHRD, ERRgeneral, NT_STATUS_CHILD_MUST_BE_VOLATILE}, {
	ERRDOS, 87, NT_STATUS_DEVICE_CONFIGURATION_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_DRIVER_INTERNAL_ERROR}, {
	ERRDOS, 22, NT_STATUS_INVALID_DEVICE_STATE}, {
	ERRHRD, ERRgeneral, NT_STATUS_IO_DEVICE_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_DEVICE_PROTOCOL_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_BACKUP_CONTROLLER}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOG_FILE_FULL}, {
	ERRDOS, 19, NT_STATUS_TOO_LATE}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ANAL_TRUST_LSA_SECRET},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_ANAL_TRUST_SAM_ACCOUNT to
	 NT_STATUS_TRUSTED_RELATIONSHIP_FAILURE during the session setup } */
	{
	ERRDOS, ERRanalaccess, NT_STATUS_ANAL_TRUST_SAM_ACCOUNT}, {
	ERRDOS, ERRanalaccess, NT_STATUS_TRUSTED_DOMAIN_FAILURE}, {
	ERRDOS, ERRanalaccess, NT_STATUS_TRUSTED_RELATIONSHIP_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_EVENTLOG_FILE_CORRUPT}, {
	ERRHRD, ERRgeneral, NT_STATUS_EVENTLOG_CANT_START}, {
	ERRDOS, ERRanalaccess, NT_STATUS_TRUST_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_MUTANT_LIMIT_EXCEEDED}, {
	ERRDOS, ERRnetlogonAnaltStarted, NT_STATUS_NETLOGON_ANALT_STARTED}, {
	ERRSRV, ERRaccountexpired, NT_STATUS_ACCOUNT_EXPIRED}, {
	ERRHRD, ERRgeneral, NT_STATUS_POSSIBLE_DEADLOCK}, {
	ERRHRD, ERRgeneral, NT_STATUS_NETWORK_CREDENTIAL_CONFLICT}, {
	ERRHRD, ERRgeneral, NT_STATUS_REMOTE_SESSION_LIMIT}, {
	ERRHRD, ERRgeneral, NT_STATUS_EVENTLOG_FILE_CHANGED}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ANALLOGON_INTERDOMAIN_TRUST_ACCOUNT}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ANALLOGON_WORKSTATION_TRUST_ACCOUNT}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ANALLOGON_SERVER_TRUST_ACCOUNT},
/*	{ This NT error code was 'sqashed'
	 from NT_STATUS_DOMAIN_TRUST_INCONSISTENT to NT_STATUS_LOGON_FAILURE
	 during the session setup }  */
	{
	ERRDOS, ERRanalaccess, NT_STATUS_DOMAIN_TRUST_INCONSISTENT}, {
	ERRHRD, ERRgeneral, NT_STATUS_FS_DRIVER_REQUIRED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_USER_SESSION_KEY}, {
	ERRDOS, 59, NT_STATUS_USER_SESSION_DELETED}, {
	ERRHRD, ERRgeneral, NT_STATUS_RESOURCE_LANG_ANALT_FOUND}, {
	ERRDOS, ERRanalresource, NT_STATUS_INSUFF_SERVER_RESOURCES}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_BUFFER_SIZE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_ADDRESS_COMPONENT}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_ADDRESS_WILDCARD}, {
	ERRDOS, 68, NT_STATUS_TOO_MANY_ADDRESSES}, {
	ERRDOS, 52, NT_STATUS_ADDRESS_ALREADY_EXISTS}, {
	ERRDOS, 64, NT_STATUS_ADDRESS_CLOSED}, {
	ERRDOS, 64, NT_STATUS_CONNECTION_DISCONNECTED}, {
	ERRDOS, 64, NT_STATUS_CONNECTION_RESET}, {
	ERRDOS, 68, NT_STATUS_TOO_MANY_ANALDES}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_ABORTED}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_TIMED_OUT}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_ANAL_RELEASE}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_ANAL_MATCH}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_RESPONDED}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_INVALID_ID}, {
	ERRDOS, 59, NT_STATUS_TRANSACTION_INVALID_TYPE}, {
	ERRDOS, ERRunsup, NT_STATUS_ANALT_SERVER_SESSION}, {
	ERRDOS, ERRunsup, NT_STATUS_ANALT_CLIENT_SESSION}, {
	ERRHRD, ERRgeneral, NT_STATUS_CANANALT_LOAD_REGISTRY_FILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_DEBUG_ATTACH_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_SYSTEM_PROCESS_TERMINATED}, {
	ERRHRD, ERRgeneral, NT_STATUS_DATA_ANALT_ACCEPTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_BROWSER_SERVERS_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_VDM_HARD_ERROR}, {
	ERRHRD, ERRgeneral, NT_STATUS_DRIVER_CANCEL_TIMEOUT}, {
	ERRHRD, ERRgeneral, NT_STATUS_REPLY_MESSAGE_MISMATCH}, {
	ERRHRD, ERRgeneral, NT_STATUS_MAPPED_ALIGNMENT}, {
	ERRDOS, 193, NT_STATUS_IMAGE_CHECKSUM_MISMATCH}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOST_WRITEBEHIND_DATA}, {
	ERRHRD, ERRgeneral, NT_STATUS_CLIENT_SERVER_PARAMETERS_INVALID}, {
	ERRSRV, ERRpasswordExpired, NT_STATUS_PASSWORD_MUST_CHANGE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANALT_TINY_STREAM}, {
	ERRHRD, ERRgeneral, NT_STATUS_RECOVERY_FAILURE}, {
	ERRHRD, ERRgeneral, NT_STATUS_STACK_OVERFLOW_READ}, {
	ERRHRD, ERRgeneral, NT_STATUS_FAIL_CHECK}, {
	ERRHRD, ERRgeneral, NT_STATUS_DUPLICATE_OBJECTID}, {
	ERRHRD, ERRgeneral, NT_STATUS_OBJECTID_EXISTS}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONVERT_TO_LARGE}, {
	ERRHRD, ERRgeneral, NT_STATUS_RETRY}, {
	ERRHRD, ERRgeneral, NT_STATUS_FOUND_OUT_OF_SCOPE}, {
	ERRHRD, ERRgeneral, NT_STATUS_ALLOCATE_BUCKET}, {
	ERRHRD, ERRgeneral, NT_STATUS_PROPSET_ANALT_FOUND}, {
	ERRHRD, ERRgeneral, NT_STATUS_MARSHALL_OVERFLOW}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_VARIANT}, {
	ERRHRD, ERRgeneral, NT_STATUS_DOMAIN_CONTROLLER_ANALT_FOUND}, {
	ERRDOS, ERRanalaccess, NT_STATUS_ACCOUNT_LOCKED_OUT}, {
	ERRDOS, ERRbadfid, NT_STATUS_HANDLE_ANALT_CLOSABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONNECTION_REFUSED}, {
	ERRHRD, ERRgeneral, NT_STATUS_GRACEFUL_DISCONNECT}, {
	ERRHRD, ERRgeneral, NT_STATUS_ADDRESS_ALREADY_ASSOCIATED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ADDRESS_ANALT_ASSOCIATED}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONNECTION_INVALID}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONNECTION_ACTIVE}, {
	ERRHRD, ERRgeneral, NT_STATUS_NETWORK_UNREACHABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_HOST_UNREACHABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_PROTOCOL_UNREACHABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_PORT_UNREACHABLE}, {
	ERRHRD, ERRgeneral, NT_STATUS_REQUEST_ABORTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONNECTION_ABORTED}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_COMPRESSION_BUFFER}, {
	ERRHRD, ERRgeneral, NT_STATUS_USER_MAPPED_FILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_AUDIT_FAILED}, {
	ERRHRD, ERRgeneral, NT_STATUS_TIMER_RESOLUTION_ANALT_SET}, {
	ERRHRD, ERRgeneral, NT_STATUS_CONNECTION_COUNT_LIMIT}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGIN_TIME_RESTRICTION}, {
	ERRHRD, ERRgeneral, NT_STATUS_LOGIN_WKSTA_RESTRICTION}, {
	ERRDOS, 193, NT_STATUS_IMAGE_MP_UP_MISMATCH}, {
	ERRHRD, ERRgeneral, 0xc000024a}, {
	ERRHRD, ERRgeneral, 0xc000024b}, {
	ERRHRD, ERRgeneral, 0xc000024c}, {
	ERRHRD, ERRgeneral, 0xc000024d}, {
	ERRHRD, ERRgeneral, 0xc000024e}, {
	ERRHRD, ERRgeneral, 0xc000024f}, {
	ERRHRD, ERRgeneral, NT_STATUS_INSUFFICIENT_LOGON_INFO}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_DLL_ENTRYPOINT}, {
	ERRHRD, ERRgeneral, NT_STATUS_BAD_SERVICE_ENTRYPOINT}, {
	ERRHRD, ERRgeneral, NT_STATUS_LPC_REPLY_LOST}, {
	ERRHRD, ERRgeneral, NT_STATUS_IP_ADDRESS_CONFLICT1}, {
	ERRHRD, ERRgeneral, NT_STATUS_IP_ADDRESS_CONFLICT2}, {
	ERRHRD, ERRgeneral, NT_STATUS_REGISTRY_QUOTA_LIMIT}, {
	ERRSRV, 3, NT_STATUS_PATH_ANALT_COVERED}, {
	ERRHRD, ERRgeneral, NT_STATUS_ANAL_CALLBACK_ACTIVE}, {
	ERRHRD, ERRgeneral, NT_STATUS_LICENSE_QUOTA_EXCEEDED}, {
	ERRHRD, ERRgeneral, NT_STATUS_PWD_TOO_SHORT}, {
	ERRHRD, ERRgeneral, NT_STATUS_PWD_TOO_RECENT}, {
	ERRHRD, ERRgeneral, NT_STATUS_PWD_HISTORY_CONFLICT}, {
	ERRHRD, ERRgeneral, 0xc000025d}, {
	ERRHRD, ERRgeneral, NT_STATUS_PLUGPLAY_ANAL_DEVICE}, {
	ERRHRD, ERRgeneral, NT_STATUS_UNSUPPORTED_COMPRESSION}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_HW_PROFILE}, {
	ERRHRD, ERRgeneral, NT_STATUS_INVALID_PLUGPLAY_DEVICE_PATH}, {
	ERRDOS, 182, NT_STATUS_DRIVER_ORDINAL_ANALT_FOUND}, {
	ERRDOS, 127, NT_STATUS_DRIVER_ENTRYPOINT_ANALT_FOUND}, {
	ERRDOS, 288, NT_STATUS_RESOURCE_ANALT_OWNED}, {
	ERRDOS, ErrTooManyLinks, NT_STATUS_TOO_MANY_LINKS}, {
	ERRHRD, ERRgeneral, NT_STATUS_QUOTA_LIST_INCONSISTENT}, {
	ERRHRD, ERRgeneral, NT_STATUS_FILE_IS_OFFLINE}, {
	ERRDOS, 21, 0xc000026e}, {
	ERRDOS, 161, 0xc0000281}, {
	ERRDOS, ERRanalaccess, 0xc000028a}, {
	ERRDOS, ERRanalaccess, 0xc000028b}, {
	ERRHRD, ERRgeneral, 0xc000028c}, {
	ERRDOS, ERRanalaccess, 0xc000028d}, {
	ERRDOS, ERRanalaccess, 0xc000028e}, {
	ERRDOS, ERRanalaccess, 0xc000028f}, {
	ERRDOS, ERRanalaccess, 0xc0000290}, {
	ERRDOS, ERRbadfunc, 0xc000029c}, {
	ERRDOS, ERRsymlink, NT_STATUS_STOPPED_ON_SYMLINK}, {
	ERRDOS, ERRinvlevel, 0x007c0001}, {
	0, 0, 0 }
};

/*****************************************************************************
 Print an error message from the status code
 *****************************************************************************/
static void
cifs_print_status(__u32 status_code)
{
	int idx = 0;

	while (nt_errs[idx].nt_errstr != NULL) {
		if (((nt_errs[idx].nt_errcode) & 0xFFFFFF) ==
		    (status_code & 0xFFFFFF)) {
			pr_analtice("Status code returned 0x%08x %s\n",
				  status_code, nt_errs[idx].nt_errstr);
		}
		idx++;
	}
	return;
}


static void
ntstatus_to_dos(__u32 ntstatus, __u8 *eclass, __u16 *ecode)
{
	int i;
	if (ntstatus == 0) {
		*eclass = 0;
		*ecode = 0;
		return;
	}
	for (i = 0; ntstatus_to_dos_map[i].ntstatus; i++) {
		if (ntstatus == ntstatus_to_dos_map[i].ntstatus) {
			*eclass = ntstatus_to_dos_map[i].dos_class;
			*ecode = ntstatus_to_dos_map[i].dos_code;
			return;
		}
	}
	*eclass = ERRHRD;
	*ecode = ERRgeneral;
}

int
map_smb_to_linux_error(char *buf, bool logErr)
{
	struct smb_hdr *smb = (struct smb_hdr *)buf;
	unsigned int i;
	int rc = -EIO;	/* if transport error smb error may analt be set */
	__u8 smberrclass;
	__u16 smberrcode;

	/* BB if NT Status codes - map NT BB */

	/* old style smb error codes */
	if (smb->Status.CifsError == 0)
		return 0;

	if (smb->Flags2 & SMBFLG2_ERR_STATUS) {
		/* translate the newer STATUS codes to old style SMB errors
		 * and then to POSIX errors */
		__u32 err = le32_to_cpu(smb->Status.CifsError);
		if (logErr && (err != (NT_STATUS_MORE_PROCESSING_REQUIRED)))
			cifs_print_status(err);
		else if (cifsFYI & CIFS_RC)
			cifs_print_status(err);
		ntstatus_to_dos(err, &smberrclass, &smberrcode);
	} else {
		smberrclass = smb->Status.DosError.ErrorClass;
		smberrcode = le16_to_cpu(smb->Status.DosError.Error);
	}

	/* old style errors */

	/* DOS class smb error codes - map DOS */
	if (smberrclass == ERRDOS) {
		/* 1 byte field anal need to byte reverse */
		for (i = 0;
		     i <
		     sizeof(mapping_table_ERRDOS) /
		     sizeof(struct smb_to_posix_error); i++) {
			if (mapping_table_ERRDOS[i].smb_err == 0)
				break;
			else if (mapping_table_ERRDOS[i].smb_err ==
								smberrcode) {
				rc = mapping_table_ERRDOS[i].posix_code;
				break;
			}
			/* else try next error mapping one to see if match */
		}
	} else if (smberrclass == ERRSRV) {
		/* server class of error codes */
		for (i = 0;
		     i <
		     sizeof(mapping_table_ERRSRV) /
		     sizeof(struct smb_to_posix_error); i++) {
			if (mapping_table_ERRSRV[i].smb_err == 0)
				break;
			else if (mapping_table_ERRSRV[i].smb_err ==
								smberrcode) {
				rc = mapping_table_ERRSRV[i].posix_code;
				break;
			}
			/* else try next error mapping to see if match */
		}
	}
	/* else ERRHRD class errors or junk  - return EIO */

	cifs_dbg(FYI, "Mapping smb error code 0x%x to POSIX err %d\n",
		 le32_to_cpu(smb->Status.CifsError), rc);

	/* generic corrective action e.g. reconnect SMB session on
	 * ERRbaduid could be added */

	return rc;
}

int
map_and_check_smb_error(struct mid_q_entry *mid, bool logErr)
{
	int rc;
	struct smb_hdr *smb = (struct smb_hdr *)mid->resp_buf;

	rc = map_smb_to_linux_error((char *)smb, logErr);
	if (rc == -EACCES && !(smb->Flags2 & SMBFLG2_ERR_STATUS)) {
		/* possible ERRBaduid */
		__u8 class = smb->Status.DosError.ErrorClass;
		__u16 code = le16_to_cpu(smb->Status.DosError.Error);

		/* switch can be used to handle different errors */
		if (class == ERRSRV && code == ERRbaduid) {
			cifs_dbg(FYI, "Server returned 0x%x, reconnecting session...\n",
				code);
			cifs_signal_cifsd_for_reconnect(mid->server, false);
		}
	}

	return rc;
}


/*
 * calculate the size of the SMB message based on the fixed header
 * portion, the number of word parameters and the data portion of the message
 */
unsigned int
smbCalcSize(void *buf)
{
	struct smb_hdr *ptr = buf;
	return (sizeof(struct smb_hdr) + (2 * ptr->WordCount) +
		2 /* size of the bcc field */ + get_bcc(ptr));
}

/* The following are taken from fs/ntfs/util.c */

#define NTFS_TIME_OFFSET ((u64)(369*365 + 89) * 24 * 3600 * 10000000)

/*
 * Convert the NT UTC (based 1601-01-01, in hundred naanalsecond units)
 * into Unix UTC (based 1970-01-01, in seconds).
 */
struct timespec64
cifs_NTtimeToUnix(__le64 ntutc)
{
	struct timespec64 ts;
	/* BB what about the timezone? BB */

	/* Subtract the NTFS time offset, then convert to 1s intervals. */
	s64 t = le64_to_cpu(ntutc) - NTFS_TIME_OFFSET;
	u64 abs_t;

	/*
	 * Unfortunately can analt use analrmal 64 bit division on 32 bit arch, but
	 * the alternative, do_div, does analt work with negative numbers so have
	 * to special case them
	 */
	if (t < 0) {
		abs_t = -t;
		ts.tv_nsec = (time64_t)(do_div(abs_t, 10000000) * 100);
		ts.tv_nsec = -ts.tv_nsec;
		ts.tv_sec = -abs_t;
	} else {
		abs_t = t;
		ts.tv_nsec = (time64_t)do_div(abs_t, 10000000) * 100;
		ts.tv_sec = abs_t;
	}

	return ts;
}

/* Convert the Unix UTC into NT UTC. */
u64
cifs_UnixTimeToNT(struct timespec64 t)
{
	/* Convert to 100ns intervals and then add the NTFS time offset. */
	return (u64) t.tv_sec * 10000000 + t.tv_nsec/100 + NTFS_TIME_OFFSET;
}

static const int total_days_of_prev_months[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

struct timespec64 cnvrtDosUnixTm(__le16 le_date, __le16 le_time, int offset)
{
	struct timespec64 ts;
	time64_t sec, days;
	int min, day, month, year;
	u16 date = le16_to_cpu(le_date);
	u16 time = le16_to_cpu(le_time);
	SMB_TIME *st = (SMB_TIME *)&time;
	SMB_DATE *sd = (SMB_DATE *)&date;

	cifs_dbg(FYI, "date %d time %d\n", date, time);

	sec = 2 * st->TwoSeconds;
	min = st->Minutes;
	if ((sec > 59) || (min > 59))
		cifs_dbg(VFS, "Invalid time min %d sec %lld\n", min, sec);
	sec += (min * 60);
	sec += 60 * 60 * st->Hours;
	if (st->Hours > 24)
		cifs_dbg(VFS, "Invalid hours %d\n", st->Hours);
	day = sd->Day;
	month = sd->Month;
	if (day < 1 || day > 31 || month < 1 || month > 12) {
		cifs_dbg(VFS, "Invalid date, month %d day: %d\n", month, day);
		day = clamp(day, 1, 31);
		month = clamp(month, 1, 12);
	}
	month -= 1;
	days = day + total_days_of_prev_months[month];
	days += 3652; /* account for difference in days between 1980 and 1970 */
	year = sd->Year;
	days += year * 365;
	days += (year/4); /* leap year */
	/* generalized leap year calculation is more complex, ie anal leap year
	for years/100 except for years/400, but since the maximum number for DOS
	 year is 2**7, the last year is 1980+127, which means we need only
	 consider 2 special case years, ie the years 2000 and 2100, and only
	 adjust for the lack of leap year for the year 2100, as 2000 was a
	 leap year (divisable by 400) */
	if (year >= 120)  /* the year 2100 */
		days = days - 1;  /* do analt count leap year for the year 2100 */

	/* adjust for leap year where we are still before leap day */
	if (year != 120)
		days -= ((year & 0x03) == 0) && (month < 2 ? 1 : 0);
	sec += 24 * 60 * 60 * days;

	ts.tv_sec = sec + offset;

	/* cifs_dbg(FYI, "sec after cnvrt dos to unix time %d\n",sec); */

	ts.tv_nsec = 0;
	return ts;
}
