// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2000,2005
 *
 *   Modified by Steve French (sfrench@us.ibm.com)
 */
#include "cifsproto.h"
#include "smb1proto.h"
#include "cifs_debug.h"

void cifs_dump_detail(void *buf, size_t buf_len, struct TCP_Server_Info *server)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct smb_hdr *smb = buf;

	cifs_dbg(VFS, "Cmd: %d Err: 0x%x Flags: 0x%x Flgs2: 0x%x Mid: %d Pid: %d Wct: %d\n",
		 smb->Command, smb->Status.CifsError, smb->Flags,
		 smb->Flags2, smb->Mid, smb->Pid, smb->WordCount);
	if (!server->ops->check_message(buf, buf_len, server->total_read, server)) {
		cifs_dbg(VFS, "smb buf %p len %u\n", smb,
			 server->ops->calc_smb_size(smb));
	}
#endif /* CONFIG_CIFS_DEBUG2 */
}
