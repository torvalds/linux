/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __SMB_COMMON_H__
#define __SMB_COMMON_H__

#include <linux/kernel.h>

#include "glob.h"
#include "nterr.h"
#include "smb2pdu.h"

/* ksmbd's Specific ERRNO */
#define ESHARE			50000

#define SMB1_PROT		0
#define SMB2_PROT		1
#define SMB21_PROT		2
/* multi-protocol negotiate request */
#define SMB2X_PROT		3
#define SMB30_PROT		4
#define SMB302_PROT		5
#define SMB311_PROT		6
#define BAD_PROT		0xFFFF

#define SMB1_VERSION_STRING	"1.0"
#define SMB20_VERSION_STRING	"2.0"
#define SMB21_VERSION_STRING	"2.1"
#define SMB30_VERSION_STRING	"3.0"
#define SMB302_VERSION_STRING	"3.02"
#define SMB311_VERSION_STRING	"3.1.1"

/* Dialects */
#define SMB10_PROT_ID		0x00
#define SMB20_PROT_ID		0x0202
#define SMB21_PROT_ID		0x0210
/* multi-protocol negotiate request */
#define SMB2X_PROT_ID		0x02FF
#define SMB30_PROT_ID		0x0300
#define SMB302_PROT_ID		0x0302
#define SMB311_PROT_ID		0x0311
#define BAD_PROT_ID		0xFFFF

#define SMB_ECHO_INTERVAL	(60 * HZ)

#define CIFS_DEFAULT_IOSIZE	(64 * 1024)
#define MAX_CIFS_SMALL_BUFFER_SIZE 448 /* big enough for most */

/* Responses when opening a file. */
#define F_SUPERSEDED	0
#define F_OPENED	1
#define F_CREATED	2
#define F_OVERWRITTEN	3

/*
 * File Attribute flags
 */
#define ATTR_READONLY			0x0001
#define ATTR_HIDDEN			0x0002
#define ATTR_SYSTEM			0x0004
#define ATTR_VOLUME			0x0008
#define ATTR_DIRECTORY			0x0010
#define ATTR_ARCHIVE			0x0020
#define ATTR_DEVICE			0x0040
#define ATTR_NORMAL			0x0080
#define ATTR_TEMPORARY			0x0100
#define ATTR_SPARSE			0x0200
#define ATTR_REPARSE			0x0400
#define ATTR_COMPRESSED			0x0800
#define ATTR_OFFLINE			0x1000
#define ATTR_NOT_CONTENT_INDEXED	0x2000
#define ATTR_ENCRYPTED			0x4000
#define ATTR_POSIX_SEMANTICS		0x01000000
#define ATTR_BACKUP_SEMANTICS		0x02000000
#define ATTR_DELETE_ON_CLOSE		0x04000000
#define ATTR_SEQUENTIAL_SCAN		0x08000000
#define ATTR_RANDOM_ACCESS		0x10000000
#define ATTR_NO_BUFFERING		0x20000000
#define ATTR_WRITE_THROUGH		0x80000000

#define ATTR_READONLY_LE		cpu_to_le32(ATTR_READONLY)
#define ATTR_HIDDEN_LE			cpu_to_le32(ATTR_HIDDEN)
#define ATTR_SYSTEM_LE			cpu_to_le32(ATTR_SYSTEM)
#define ATTR_DIRECTORY_LE		cpu_to_le32(ATTR_DIRECTORY)
#define ATTR_ARCHIVE_LE			cpu_to_le32(ATTR_ARCHIVE)
#define ATTR_NORMAL_LE			cpu_to_le32(ATTR_NORMAL)
#define ATTR_TEMPORARY_LE		cpu_to_le32(ATTR_TEMPORARY)
#define ATTR_SPARSE_FILE_LE		cpu_to_le32(ATTR_SPARSE)
#define ATTR_REPARSE_POINT_LE		cpu_to_le32(ATTR_REPARSE)
#define ATTR_COMPRESSED_LE		cpu_to_le32(ATTR_COMPRESSED)
#define ATTR_OFFLINE_LE			cpu_to_le32(ATTR_OFFLINE)
#define ATTR_NOT_CONTENT_INDEXED_LE	cpu_to_le32(ATTR_NOT_CONTENT_INDEXED)
#define ATTR_ENCRYPTED_LE		cpu_to_le32(ATTR_ENCRYPTED)
#define ATTR_INTEGRITY_STREAML_LE	cpu_to_le32(0x00008000)
#define ATTR_NO_SCRUB_DATA_LE		cpu_to_le32(0x00020000)
#define ATTR_MASK_LE			cpu_to_le32(0x00007FB7)

/* List of FileSystemAttributes - see 2.5.1 of MS-FSCC */
#define FILE_SUPPORTS_SPARSE_VDL	0x10000000 /* faster nonsparse extend */
#define FILE_SUPPORTS_BLOCK_REFCOUNTING	0x08000000 /* allow ioctl dup extents */
#define FILE_SUPPORT_INTEGRITY_STREAMS	0x04000000
#define FILE_SUPPORTS_USN_JOURNAL	0x02000000
#define FILE_SUPPORTS_OPEN_BY_FILE_ID	0x01000000
#define FILE_SUPPORTS_EXTENDED_ATTRIBUTES 0x00800000
#define FILE_SUPPORTS_HARD_LINKS	0x00400000
#define FILE_SUPPORTS_TRANSACTIONS	0x00200000
#define FILE_SEQUENTIAL_WRITE_ONCE	0x00100000
#define FILE_READ_ONLY_VOLUME		0x00080000
#define FILE_NAMED_STREAMS		0x00040000
#define FILE_SUPPORTS_ENCRYPTION	0x00020000
#define FILE_SUPPORTS_OBJECT_IDS	0x00010000
#define FILE_VOLUME_IS_COMPRESSED	0x00008000
#define FILE_SUPPORTS_REMOTE_STORAGE	0x00000100
#define FILE_SUPPORTS_REPARSE_POINTS	0x00000080
#define FILE_SUPPORTS_SPARSE_FILES	0x00000040
#define FILE_VOLUME_QUOTAS		0x00000020
#define FILE_FILE_COMPRESSION		0x00000010
#define FILE_PERSISTENT_ACLS		0x00000008
#define FILE_UNICODE_ON_DISK		0x00000004
#define FILE_CASE_PRESERVED_NAMES	0x00000002
#define FILE_CASE_SENSITIVE_SEARCH	0x00000001

#define FILE_READ_DATA        0x00000001  /* Data can be read from the file   */
#define FILE_WRITE_DATA       0x00000002  /* Data can be written to the file  */
#define FILE_APPEND_DATA      0x00000004  /* Data can be appended to the file */
#define FILE_READ_EA          0x00000008  /* Extended attributes associated   */
/* with the file can be read        */
#define FILE_WRITE_EA         0x00000010  /* Extended attributes associated   */
/* with the file can be written     */
#define FILE_EXECUTE          0x00000020  /*Data can be read into memory from */
/* the file using system paging I/O */
#define FILE_DELETE_CHILD     0x00000040
#define FILE_READ_ATTRIBUTES  0x00000080  /* Attributes associated with the   */
/* file can be read                 */
#define FILE_WRITE_ATTRIBUTES 0x00000100  /* Attributes associated with the   */
/* file can be written              */
#define DELETE                0x00010000  /* The file can be deleted          */
#define READ_CONTROL          0x00020000  /* The access control list and      */
/* ownership associated with the    */
/* file can be read                 */
#define WRITE_DAC             0x00040000  /* The access control list and      */
/* ownership associated with the    */
/* file can be written.             */
#define WRITE_OWNER           0x00080000  /* Ownership information associated */
/* with the file can be written     */
#define SYNCHRONIZE           0x00100000  /* The file handle can waited on to */
/* synchronize with the completion  */
/* of an input/output request       */
#define GENERIC_ALL           0x10000000
#define GENERIC_EXECUTE       0x20000000
#define GENERIC_WRITE         0x40000000
#define GENERIC_READ          0x80000000
/* In summary - Relevant file       */
/* access flags from CIFS are       */
/* file_read_data, file_write_data  */
/* file_execute, file_read_attributes*/
/* write_dac, and delete.           */

#define FILE_READ_RIGHTS (FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES)
#define FILE_WRITE_RIGHTS (FILE_WRITE_DATA | FILE_APPEND_DATA \
		| FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES)
#define FILE_EXEC_RIGHTS (FILE_EXECUTE)

#define SET_FILE_READ_RIGHTS (FILE_READ_DATA | FILE_READ_EA \
		| FILE_READ_ATTRIBUTES \
		| DELETE | READ_CONTROL | WRITE_DAC \
		| WRITE_OWNER | SYNCHRONIZE)
#define SET_FILE_WRITE_RIGHTS (FILE_WRITE_DATA | FILE_APPEND_DATA \
		| FILE_WRITE_EA \
		| FILE_DELETE_CHILD \
		| FILE_WRITE_ATTRIBUTES \
		| DELETE | READ_CONTROL | WRITE_DAC \
		| WRITE_OWNER | SYNCHRONIZE)
#define SET_FILE_EXEC_RIGHTS (FILE_READ_EA | FILE_WRITE_EA | FILE_EXECUTE \
		| FILE_READ_ATTRIBUTES \
		| FILE_WRITE_ATTRIBUTES \
		| DELETE | READ_CONTROL | WRITE_DAC \
		| WRITE_OWNER | SYNCHRONIZE)

#define SET_MINIMUM_RIGHTS (FILE_READ_EA | FILE_READ_ATTRIBUTES \
		| READ_CONTROL | SYNCHRONIZE)

/* generic flags for file open */
#define GENERIC_READ_FLAGS	(READ_CONTROL | FILE_READ_DATA | \
		FILE_READ_ATTRIBUTES | \
		FILE_READ_EA | SYNCHRONIZE)

#define GENERIC_WRITE_FLAGS	(READ_CONTROL | FILE_WRITE_DATA | \
		FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | \
		FILE_APPEND_DATA | SYNCHRONIZE)

#define GENERIC_EXECUTE_FLAGS	(READ_CONTROL | FILE_EXECUTE | \
		FILE_READ_ATTRIBUTES | SYNCHRONIZE)

#define GENERIC_ALL_FLAGS	(DELETE | READ_CONTROL | WRITE_DAC | \
		WRITE_OWNER | SYNCHRONIZE | FILE_READ_DATA | \
		FILE_WRITE_DATA | FILE_APPEND_DATA | \
		FILE_READ_EA | FILE_WRITE_EA | \
		FILE_EXECUTE | FILE_DELETE_CHILD | \
		FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES)

#define SMB1_PROTO_NUMBER		cpu_to_le32(0x424d53ff)
#define SMB_COM_NEGOTIATE		0x72

#define SMB1_CLIENT_GUID_SIZE		(16)
struct smb_hdr {
	__be32 smb_buf_length;
	__u8 Protocol[4];
	__u8 Command;
	union {
		struct {
			__u8 ErrorClass;
			__u8 Reserved;
			__le16 Error;
		} __packed DosError;
		__le32 CifsError;
	} __packed Status;
	__u8 Flags;
	__le16 Flags2;          /* note: le */
	__le16 PidHigh;
	union {
		struct {
			__le32 SequenceNumber;  /* le */
			__u32 Reserved; /* zero */
		} __packed Sequence;
		__u8 SecuritySignature[8];      /* le */
	} __packed Signature;
	__u8 pad[2];
	__le16 Tid;
	__le16 Pid;
	__le16 Uid;
	__le16 Mid;
	__u8 WordCount;
} __packed;

struct smb_negotiate_req {
	struct smb_hdr hdr;     /* wct = 0 */
	__le16 ByteCount;
	unsigned char DialectsArray[1];
} __packed;

struct smb_negotiate_rsp {
	struct smb_hdr hdr;     /* wct = 17 */
	__le16 DialectIndex; /* 0xFFFF = no dialect acceptable */
	__u8 SecurityMode;
	__le16 MaxMpxCount;
	__le16 MaxNumberVcs;
	__le32 MaxBufferSize;
	__le32 MaxRawSize;
	__le32 SessionKey;
	__le32 Capabilities;    /* see below */
	__le32 SystemTimeLow;
	__le32 SystemTimeHigh;
	__le16 ServerTimeZone;
	__u8 EncryptionKeyLength;
	__le16 ByteCount;
	union {
		unsigned char EncryptionKey[8]; /* cap extended security off */
		/* followed by Domain name - if extended security is off */
		/* followed by 16 bytes of server GUID */
		/* then security blob if cap_extended_security negotiated */
		struct {
			unsigned char GUID[SMB1_CLIENT_GUID_SIZE];
			unsigned char SecurityBlob[1];
		} __packed extended_response;
	} __packed u;
} __packed;

struct filesystem_attribute_info {
	__le32 Attributes;
	__le32 MaxPathNameComponentLength;
	__le32 FileSystemNameLen;
	__le16 FileSystemName[1]; /* do not have to save this - get subset? */
} __packed;

struct filesystem_device_info {
	__le32 DeviceType;
	__le32 DeviceCharacteristics;
} __packed; /* device info level 0x104 */

struct filesystem_vol_info {
	__le64 VolumeCreationTime;
	__le32 SerialNumber;
	__le32 VolumeLabelSize;
	__le16 Reserved;
	__le16 VolumeLabel[1];
} __packed;

struct filesystem_info {
	__le64 TotalAllocationUnits;
	__le64 FreeAllocationUnits;
	__le32 SectorsPerAllocationUnit;
	__le32 BytesPerSector;
} __packed;     /* size info, level 0x103 */

#define EXTENDED_INFO_MAGIC 0x43667364	/* Cfsd */
#define STRING_LENGTH 28

struct fs_extended_info {
	__le32 magic;
	__le32 version;
	__le32 release;
	__u64 rel_date;
	char    version_string[STRING_LENGTH];
} __packed;

struct object_id_info {
	char objid[16];
	struct fs_extended_info extended_info;
} __packed;

struct file_directory_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	char FileName[1];
} __packed;   /* level 0x101 FF resp data */

struct file_names_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le32 FileNameLength;
	char FileName[1];
} __packed;   /* level 0xc FF resp data */

struct file_full_directory_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize;
	char FileName[1];
} __packed; /* level 0x102 FF resp */

struct file_both_directory_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* length of the xattrs */
	__u8   ShortNameLength;
	__u8   Reserved;
	__u8   ShortName[24];
	char FileName[1];
} __packed; /* level 0x104 FFrsp data */

struct file_id_both_directory_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* length of the xattrs */
	__u8   ShortNameLength;
	__u8   Reserved;
	__u8   ShortName[24];
	__le16 Reserved2;
	__le64 UniqueId;
	char FileName[1];
} __packed;

struct file_id_full_dir_info {
	__le32 NextEntryOffset;
	__u32 FileIndex;
	__le64 CreationTime;
	__le64 LastAccessTime;
	__le64 LastWriteTime;
	__le64 ChangeTime;
	__le64 EndOfFile;
	__le64 AllocationSize;
	__le32 ExtFileAttributes;
	__le32 FileNameLength;
	__le32 EaSize; /* EA size */
	__le32 Reserved;
	__le64 UniqueId; /* inode num - le since Samba puts ino in low 32 bit*/
	char FileName[1];
} __packed; /* level 0x105 FF rsp data */

struct smb_version_values {
	char		*version_string;
	__u16		protocol_id;
	__le16		lock_cmd;
	__u32		capabilities;
	__u32		max_read_size;
	__u32		max_write_size;
	__u32		max_trans_size;
	__u32		large_lock_type;
	__u32		exclusive_lock_type;
	__u32		shared_lock_type;
	__u32		unlock_lock_type;
	size_t		header_size;
	size_t		max_header_size;
	size_t		read_rsp_size;
	unsigned int	cap_unix;
	unsigned int	cap_nt_find;
	unsigned int	cap_large_files;
	__u16		signing_enabled;
	__u16		signing_required;
	size_t		create_lease_size;
	size_t		create_durable_size;
	size_t		create_durable_v2_size;
	size_t		create_mxac_size;
	size_t		create_disk_id_size;
	size_t		create_posix_size;
};

struct filesystem_posix_info {
	/* For undefined recommended transfer size return -1 in that field */
	__le32 OptimalTransferSize;  /* bsize on some os, iosize on other os */
	__le32 BlockSize;
	/* The next three fields are in terms of the block size.
	 * (above). If block size is unknown, 4096 would be a
	 * reasonable block size for a server to report.
	 * Note that returning the blocks/blocksavail removes need
	 * to make a second call (to QFSInfo level 0x103 to get this info.
	 * UserBlockAvail is typically less than or equal to BlocksAvail,
	 * if no distinction is made return the same value in each
	 */
	__le64 TotalBlocks;
	__le64 BlocksAvail;       /* bfree */
	__le64 UserBlocksAvail;   /* bavail */
	/* For undefined Node fields or FSID return -1 */
	__le64 TotalFileNodes;
	__le64 FreeFileNodes;
	__le64 FileSysIdentifier;   /* fsid */
	/* NB Namelen comes from FILE_SYSTEM_ATTRIBUTE_INFO call */
	/* NB flags can come from FILE_SYSTEM_DEVICE_INFO call   */
} __packed;

struct smb_version_ops {
	u16 (*get_cmd_val)(struct ksmbd_work *swork);
	int (*init_rsp_hdr)(struct ksmbd_work *swork);
	void (*set_rsp_status)(struct ksmbd_work *swork, __le32 err);
	int (*allocate_rsp_buf)(struct ksmbd_work *work);
	int (*set_rsp_credits)(struct ksmbd_work *work);
	int (*check_user_session)(struct ksmbd_work *work);
	int (*get_ksmbd_tcon)(struct ksmbd_work *work);
	bool (*is_sign_req)(struct ksmbd_work *work, unsigned int command);
	int (*check_sign_req)(struct ksmbd_work *work);
	void (*set_sign_rsp)(struct ksmbd_work *work);
	int (*generate_signingkey)(struct ksmbd_session *sess, struct ksmbd_conn *conn);
	int (*generate_encryptionkey)(struct ksmbd_session *sess);
	bool (*is_transform_hdr)(void *buf);
	int (*decrypt_req)(struct ksmbd_work *work);
	int (*encrypt_resp)(struct ksmbd_work *work);
};

struct smb_version_cmds {
	int (*proc)(struct ksmbd_work *swork);
};

static inline size_t
smb2_hdr_size_no_buflen(struct smb_version_values *vals)
{
	return vals->header_size - 4;
}

int ksmbd_min_protocol(void);
int ksmbd_max_protocol(void);

int ksmbd_lookup_protocol_idx(char *str);

int ksmbd_verify_smb_message(struct ksmbd_work *work);
bool ksmbd_smb_request(struct ksmbd_conn *conn);

int ksmbd_lookup_dialect_by_id(__le16 *cli_dialects, __le16 dialects_count);

int ksmbd_init_smb_server(struct ksmbd_work *work);

bool ksmbd_pdu_size_has_room(unsigned int pdu);

struct ksmbd_kstat;
int ksmbd_populate_dot_dotdot_entries(struct ksmbd_work *work,
				      int info_level,
				      struct ksmbd_file *dir,
				      struct ksmbd_dir_info *d_info,
				      char *search_pattern,
				      int (*fn)(struct ksmbd_conn *,
						int,
						struct ksmbd_dir_info *,
						struct ksmbd_kstat *));

int ksmbd_extract_shortname(struct ksmbd_conn *conn,
			    const char *longname,
			    char *shortname);

int ksmbd_smb_negotiate_common(struct ksmbd_work *work, unsigned int command);

int ksmbd_smb_check_shared_mode(struct file *filp, struct ksmbd_file *curr_fp);
int ksmbd_override_fsids(struct ksmbd_work *work);
void ksmbd_revert_fsids(struct ksmbd_work *work);

unsigned int ksmbd_server_side_copy_max_chunk_count(void);
unsigned int ksmbd_server_side_copy_max_chunk_size(void);
unsigned int ksmbd_server_side_copy_max_total_size(void);
bool is_asterisk(char *p);
__le32 smb_map_generic_desired_access(__le32 daccess);

static inline unsigned int get_rfc1002_len(void *buf)
{
	return be32_to_cpu(*((__be32 *)buf)) & 0xffffff;
}

static inline void inc_rfc1001_len(void *buf, int count)
{
	be32_add_cpu((__be32 *)buf, count);
}
#endif /* __SMB_COMMON_H__ */
