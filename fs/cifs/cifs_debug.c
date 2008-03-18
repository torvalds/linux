/*
 *   fs/cifs_debug.c
 *
 *   Copyright (C) International Business Machines  Corp., 2000,2005
 *
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"

void
cifs_dump_mem(char *label, void *data, int length)
{
	int i, j;
	int *intptr = data;
	char *charptr = data;
	char buf[10], line[80];

	printk(KERN_DEBUG "%s: dump of %d bytes of data at 0x%p\n",
		label, length, data);
	for (i = 0; i < length; i += 16) {
		line[0] = 0;
		for (j = 0; (j < 4) && (i + j * 4 < length); j++) {
			sprintf(buf, " %08x", intptr[i / 4 + j]);
			strcat(line, buf);
		}
		buf[0] = ' ';
		buf[2] = 0;
		for (j = 0; (j < 16) && (i + j < length); j++) {
			buf[1] = isprint(charptr[i + j]) ? charptr[i + j] : '.';
			strcat(line, buf);
		}
		printk(KERN_DEBUG "%s\n", line);
	}
}

#ifdef CONFIG_CIFS_DEBUG2
void cifs_dump_detail(struct smb_hdr *smb)
{
	cERROR(1, ("Cmd: %d Err: 0x%x Flags: 0x%x Flgs2: 0x%x Mid: %d Pid: %d",
		  smb->Command, smb->Status.CifsError,
		  smb->Flags, smb->Flags2, smb->Mid, smb->Pid));
	cERROR(1, ("smb buf %p len %d", smb, smbCalcSize_LE(smb)));
}


void cifs_dump_mids(struct TCP_Server_Info *server)
{
	struct list_head *tmp;
	struct mid_q_entry *mid_entry;

	if (server == NULL)
		return;

	cERROR(1, ("Dump pending requests:"));
	spin_lock(&GlobalMid_Lock);
	list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		if (mid_entry) {
			cERROR(1, ("State: %d Cmd: %d Pid: %d Tsk: %p Mid %d",
				mid_entry->midState,
				(int)mid_entry->command,
				mid_entry->pid,
				mid_entry->tsk,
				mid_entry->mid));
#ifdef CONFIG_CIFS_STATS2
			cERROR(1, ("IsLarge: %d buf: %p time rcv: %ld now: %ld",
				mid_entry->largeBuf,
				mid_entry->resp_buf,
				mid_entry->when_received,
				jiffies));
#endif /* STATS2 */
			cERROR(1, ("IsMult: %d IsEnd: %d", mid_entry->multiRsp,
				  mid_entry->multiEnd));
			if (mid_entry->resp_buf) {
				cifs_dump_detail(mid_entry->resp_buf);
				cifs_dump_mem("existing buf: ",
					mid_entry->resp_buf, 62);
			}
		}
	}
	spin_unlock(&GlobalMid_Lock);
}
#endif /* CONFIG_CIFS_DEBUG2 */

#ifdef CONFIG_PROC_FS
static int
cifs_debug_data_read(char *buf, char **beginBuffer, off_t offset,
		     int count, int *eof, void *data)
{
	struct list_head *tmp;
	struct list_head *tmp1;
	struct mid_q_entry *mid_entry;
	struct cifsSesInfo *ses;
	struct cifsTconInfo *tcon;
	int i;
	int length = 0;
	char *original_buf = buf;

	*beginBuffer = buf + offset;

	length =
	    sprintf(buf,
		    "Display Internal CIFS Data Structures for Debugging\n"
		    "---------------------------------------------------\n");
	buf += length;
	length = sprintf(buf, "CIFS Version %s\n", CIFS_VERSION);
	buf += length;
	length = sprintf(buf,
		"Active VFS Requests: %d\n", GlobalTotalActiveXid);
	buf += length;
	length = sprintf(buf, "Servers:");
	buf += length;

	i = 0;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		i++;
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if ((ses->serverDomain == NULL) || (ses->serverOS == NULL) ||
		   (ses->serverNOS == NULL)) {
			buf += sprintf(buf, "\nentry for %s not fully "
					"displayed\n\t", ses->serverName);
		} else {
			length =
			    sprintf(buf,
				    "\n%d) Name: %s  Domain: %s Mounts: %d OS:"
				    " %s  \n\tNOS: %s\tCapability: 0x%x\n\tSMB"
				    " session status: %d\t",
				i, ses->serverName, ses->serverDomain,
				atomic_read(&ses->inUse),
				ses->serverOS, ses->serverNOS,
				ses->capabilities, ses->status);
			buf += length;
		}
		if (ses->server) {
			buf += sprintf(buf, "TCP status: %d\n\tLocal Users To "
				    "Server: %d SecMode: 0x%x Req On Wire: %d",
				ses->server->tcpStatus,
				atomic_read(&ses->server->socketUseCount),
				ses->server->secMode,
				atomic_read(&ses->server->inFlight));

#ifdef CONFIG_CIFS_STATS2
			buf += sprintf(buf, " In Send: %d In MaxReq Wait: %d",
				atomic_read(&ses->server->inSend),
				atomic_read(&ses->server->num_waiters));
#endif

			length = sprintf(buf, "\nMIDs:\n");
			buf += length;

			spin_lock(&GlobalMid_Lock);
			list_for_each(tmp1, &ses->server->pending_mid_q) {
				mid_entry = list_entry(tmp1, struct
					mid_q_entry,
					qhead);
				if (mid_entry) {
					length = sprintf(buf,
							"State: %d com: %d pid:"
							" %d tsk: %p mid %d\n",
							mid_entry->midState,
							(int)mid_entry->command,
							mid_entry->pid,
							mid_entry->tsk,
							mid_entry->mid);
					buf += length;
				}
			}
			spin_unlock(&GlobalMid_Lock);
		}

	}
	read_unlock(&GlobalSMBSeslock);
	sprintf(buf, "\n");
	buf++;

	length = sprintf(buf, "Shares:");
	buf += length;

	i = 0;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalTreeConnectionList) {
		__u32 dev_type;
		i++;
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		dev_type = le32_to_cpu(tcon->fsDevInfo.DeviceType);
		length = sprintf(buf, "\n%d) %s Uses: %d ", i,
				 tcon->treeName, atomic_read(&tcon->useCount));
		buf += length;
		if (tcon->nativeFileSystem) {
			length = sprintf(buf, "Type: %s ",
					 tcon->nativeFileSystem);
			buf += length;
		}
		length = sprintf(buf, "DevInfo: 0x%x Attributes: 0x%x"
				 "\nPathComponentMax: %d Status: %d",
			    le32_to_cpu(tcon->fsDevInfo.DeviceCharacteristics),
			    le32_to_cpu(tcon->fsAttrInfo.Attributes),
			    le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength),
			    tcon->tidStatus);
		buf += length;
		if (dev_type == FILE_DEVICE_DISK)
			length = sprintf(buf, " type: DISK ");
		else if (dev_type == FILE_DEVICE_CD_ROM)
			length = sprintf(buf, " type: CDROM ");
		else
			length =
			    sprintf(buf, " type: %d ", dev_type);
		buf += length;
		if (tcon->tidStatus == CifsNeedReconnect) {
			buf += sprintf(buf, "\tDISCONNECTED ");
			length += 14;
		}
	}
	read_unlock(&GlobalSMBSeslock);

	length = sprintf(buf, "\n");
	buf += length;

	/* BB add code to dump additional info such as TCP session info now */
	/* Now calculate total size of returned data */
	length = buf - original_buf;

	if (offset + count >= length)
		*eof = 1;
	if (length < offset) {
		*eof = 1;
		return 0;
	} else {
		length = length - offset;
	}
	if (length > count)
		length = count;

	return length;
}

#ifdef CONFIG_CIFS_STATS

static int
cifs_stats_write(struct file *file, const char __user *buffer,
		 unsigned long count, void *data)
{
	char c;
	int rc;
	struct list_head *tmp;
	struct cifsTconInfo *tcon;

	rc = get_user(c, buffer);
	if (rc)
		return rc;

	if (c == '1' || c == 'y' || c == 'Y' || c == '0') {
		read_lock(&GlobalSMBSeslock);
#ifdef CONFIG_CIFS_STATS2
		atomic_set(&totBufAllocCount, 0);
		atomic_set(&totSmBufAllocCount, 0);
#endif /* CONFIG_CIFS_STATS2 */
		list_for_each(tmp, &GlobalTreeConnectionList) {
			tcon = list_entry(tmp, struct cifsTconInfo,
					cifsConnectionList);
			atomic_set(&tcon->num_smbs_sent, 0);
			atomic_set(&tcon->num_writes, 0);
			atomic_set(&tcon->num_reads, 0);
			atomic_set(&tcon->num_oplock_brks, 0);
			atomic_set(&tcon->num_opens, 0);
			atomic_set(&tcon->num_closes, 0);
			atomic_set(&tcon->num_deletes, 0);
			atomic_set(&tcon->num_mkdirs, 0);
			atomic_set(&tcon->num_rmdirs, 0);
			atomic_set(&tcon->num_renames, 0);
			atomic_set(&tcon->num_t2renames, 0);
			atomic_set(&tcon->num_ffirst, 0);
			atomic_set(&tcon->num_fnext, 0);
			atomic_set(&tcon->num_fclose, 0);
			atomic_set(&tcon->num_hardlinks, 0);
			atomic_set(&tcon->num_symlinks, 0);
			atomic_set(&tcon->num_locks, 0);
		}
		read_unlock(&GlobalSMBSeslock);
	}

	return count;
}

static int
cifs_stats_read(char *buf, char **beginBuffer, off_t offset,
		  int count, int *eof, void *data)
{
	int item_length, i, length;
	struct list_head *tmp;
	struct cifsTconInfo *tcon;

	*beginBuffer = buf + offset;

	length = sprintf(buf,
			"Resources in use\nCIFS Session: %d\n",
			sesInfoAllocCount.counter);
	buf += length;
	item_length =
		sprintf(buf, "Share (unique mount targets): %d\n",
			tconInfoAllocCount.counter);
	length += item_length;
	buf += item_length;
	item_length =
		sprintf(buf, "SMB Request/Response Buffer: %d Pool size: %d\n",
			bufAllocCount.counter,
			cifs_min_rcv + tcpSesAllocCount.counter);
	length += item_length;
	buf += item_length;
	item_length =
		sprintf(buf, "SMB Small Req/Resp Buffer: %d Pool size: %d\n",
			smBufAllocCount.counter, cifs_min_small);
	length += item_length;
	buf += item_length;
#ifdef CONFIG_CIFS_STATS2
	item_length = sprintf(buf, "Total Large %d Small %d Allocations\n",
				atomic_read(&totBufAllocCount),
				atomic_read(&totSmBufAllocCount));
	length += item_length;
	buf += item_length;
#endif /* CONFIG_CIFS_STATS2 */

	item_length =
		sprintf(buf, "Operations (MIDs): %d\n",
			midCount.counter);
	length += item_length;
	buf += item_length;
	item_length = sprintf(buf,
		"\n%d session %d share reconnects\n",
		tcpSesReconnectCount.counter, tconInfoReconnectCount.counter);
	length += item_length;
	buf += item_length;

	item_length = sprintf(buf,
		"Total vfs operations: %d maximum at one time: %d\n",
		GlobalCurrentXid, GlobalMaxActiveXid);
	length += item_length;
	buf += item_length;

	i = 0;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalTreeConnectionList) {
		i++;
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		item_length = sprintf(buf, "\n%d) %s", i, tcon->treeName);
		buf += item_length;
		length += item_length;
		if (tcon->tidStatus == CifsNeedReconnect) {
			buf += sprintf(buf, "\tDISCONNECTED ");
			length += 14;
		}
		item_length = sprintf(buf, "\nSMBs: %d Oplock Breaks: %d",
			atomic_read(&tcon->num_smbs_sent),
			atomic_read(&tcon->num_oplock_brks));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf, "\nReads:  %d Bytes: %lld",
			atomic_read(&tcon->num_reads),
			(long long)(tcon->bytes_read));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf, "\nWrites: %d Bytes: %lld",
			atomic_read(&tcon->num_writes),
			(long long)(tcon->bytes_written));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf,
			"\nLocks: %d HardLinks: %d Symlinks: %d",
			atomic_read(&tcon->num_locks),
			atomic_read(&tcon->num_hardlinks),
			atomic_read(&tcon->num_symlinks));
		buf += item_length;
		length += item_length;

		item_length = sprintf(buf, "\nOpens: %d Closes: %d Deletes: %d",
			atomic_read(&tcon->num_opens),
			atomic_read(&tcon->num_closes),
			atomic_read(&tcon->num_deletes));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf, "\nMkdirs: %d Rmdirs: %d",
			atomic_read(&tcon->num_mkdirs),
			atomic_read(&tcon->num_rmdirs));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf, "\nRenames: %d T2 Renames %d",
			atomic_read(&tcon->num_renames),
			atomic_read(&tcon->num_t2renames));
		buf += item_length;
		length += item_length;
		item_length = sprintf(buf, "\nFindFirst: %d FNext %d FClose %d",
			atomic_read(&tcon->num_ffirst),
			atomic_read(&tcon->num_fnext),
			atomic_read(&tcon->num_fclose));
		buf += item_length;
		length += item_length;
	}
	read_unlock(&GlobalSMBSeslock);

	buf += sprintf(buf, "\n");
	length++;

	if (offset + count >= length)
		*eof = 1;
	if (length < offset) {
		*eof = 1;
		return 0;
	} else {
		length = length - offset;
	}
	if (length > count)
		length = count;

	return length;
}
#endif /* STATS */

static struct proc_dir_entry *proc_fs_cifs;
read_proc_t cifs_txanchor_read;
static read_proc_t cifsFYI_read;
static write_proc_t cifsFYI_write;
static read_proc_t oplockEnabled_read;
static write_proc_t oplockEnabled_write;
static read_proc_t lookupFlag_read;
static write_proc_t lookupFlag_write;
static read_proc_t traceSMB_read;
static write_proc_t traceSMB_write;
static read_proc_t multiuser_mount_read;
static write_proc_t multiuser_mount_write;
static read_proc_t security_flags_read;
static write_proc_t security_flags_write;
/* static read_proc_t ntlmv2_enabled_read;
static write_proc_t ntlmv2_enabled_write;
static read_proc_t packet_signing_enabled_read;
static write_proc_t packet_signing_enabled_write;*/
static read_proc_t experimEnabled_read;
static write_proc_t experimEnabled_write;
static read_proc_t linuxExtensionsEnabled_read;
static write_proc_t linuxExtensionsEnabled_write;

void
cifs_proc_init(void)
{
	struct proc_dir_entry *pde;

	proc_fs_cifs = proc_mkdir("cifs", proc_root_fs);
	if (proc_fs_cifs == NULL)
		return;

	proc_fs_cifs->owner = THIS_MODULE;
	create_proc_read_entry("DebugData", 0, proc_fs_cifs,
				cifs_debug_data_read, NULL);

#ifdef CONFIG_CIFS_STATS
	pde = create_proc_read_entry("Stats", 0, proc_fs_cifs,
				cifs_stats_read, NULL);
	if (pde)
		pde->write_proc = cifs_stats_write;
#endif /* STATS */
	pde = create_proc_read_entry("cifsFYI", 0, proc_fs_cifs,
				cifsFYI_read, NULL);
	if (pde)
		pde->write_proc = cifsFYI_write;

	pde =
	    create_proc_read_entry("traceSMB", 0, proc_fs_cifs,
				traceSMB_read, NULL);
	if (pde)
		pde->write_proc = traceSMB_write;

	pde = create_proc_read_entry("OplockEnabled", 0, proc_fs_cifs,
				oplockEnabled_read, NULL);
	if (pde)
		pde->write_proc = oplockEnabled_write;

	pde = create_proc_read_entry("Experimental", 0, proc_fs_cifs,
				experimEnabled_read, NULL);
	if (pde)
		pde->write_proc = experimEnabled_write;

	pde = create_proc_read_entry("LinuxExtensionsEnabled", 0, proc_fs_cifs,
				linuxExtensionsEnabled_read, NULL);
	if (pde)
		pde->write_proc = linuxExtensionsEnabled_write;

	pde =
	    create_proc_read_entry("MultiuserMount", 0, proc_fs_cifs,
				multiuser_mount_read, NULL);
	if (pde)
		pde->write_proc = multiuser_mount_write;

	pde =
	    create_proc_read_entry("SecurityFlags", 0, proc_fs_cifs,
				security_flags_read, NULL);
	if (pde)
		pde->write_proc = security_flags_write;

	pde =
	create_proc_read_entry("LookupCacheEnabled", 0, proc_fs_cifs,
				lookupFlag_read, NULL);
	if (pde)
		pde->write_proc = lookupFlag_write;

/*	pde =
	    create_proc_read_entry("NTLMV2Enabled", 0, proc_fs_cifs,
				ntlmv2_enabled_read, NULL);
	if (pde)
		pde->write_proc = ntlmv2_enabled_write;

	pde =
	    create_proc_read_entry("PacketSigningEnabled", 0, proc_fs_cifs,
				packet_signing_enabled_read, NULL);
	if (pde)
		pde->write_proc = packet_signing_enabled_write;*/
}

void
cifs_proc_clean(void)
{
	if (proc_fs_cifs == NULL)
		return;

	remove_proc_entry("DebugData", proc_fs_cifs);
	remove_proc_entry("cifsFYI", proc_fs_cifs);
	remove_proc_entry("traceSMB", proc_fs_cifs);
#ifdef CONFIG_CIFS_STATS
	remove_proc_entry("Stats", proc_fs_cifs);
#endif
	remove_proc_entry("MultiuserMount", proc_fs_cifs);
	remove_proc_entry("OplockEnabled", proc_fs_cifs);
/*	remove_proc_entry("NTLMV2Enabled",proc_fs_cifs); */
	remove_proc_entry("SecurityFlags", proc_fs_cifs);
/*	remove_proc_entry("PacketSigningEnabled", proc_fs_cifs); */
	remove_proc_entry("LinuxExtensionsEnabled", proc_fs_cifs);
	remove_proc_entry("Experimental", proc_fs_cifs);
	remove_proc_entry("LookupCacheEnabled", proc_fs_cifs);
	remove_proc_entry("cifs", proc_root_fs);
}

static int
cifsFYI_read(char *page, char **start, off_t off, int count,
	     int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", cifsFYI);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
cifsFYI_write(struct file *file, const char __user *buffer,
	      unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		cifsFYI = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		cifsFYI = 1;
	else if ((c > '1') && (c <= '9'))
		cifsFYI = (int) (c - '0'); /* see cifs_debug.h for meanings */

	return count;
}

static int
oplockEnabled_read(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", oplockEnabled);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
oplockEnabled_write(struct file *file, const char __user *buffer,
		    unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		oplockEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		oplockEnabled = 1;

	return count;
}

static int
experimEnabled_read(char *page, char **start, off_t off,
		    int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", experimEnabled);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
experimEnabled_write(struct file *file, const char __user *buffer,
		     unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		experimEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		experimEnabled = 1;
	else if (c == '2')
		experimEnabled = 2;

	return count;
}

static int
linuxExtensionsEnabled_read(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", linuxExtEnabled);
	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
linuxExtensionsEnabled_write(struct file *file, const char __user *buffer,
			     unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		linuxExtEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		linuxExtEnabled = 1;

	return count;
}


static int
lookupFlag_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", lookupCacheEnabled);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
lookupFlag_write(struct file *file, const char __user *buffer,
		    unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		lookupCacheEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		lookupCacheEnabled = 1;

	return count;
}
static int
traceSMB_read(char *page, char **start, off_t off, int count,
	      int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", traceSMB);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
traceSMB_write(struct file *file, const char __user *buffer,
	       unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		traceSMB = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		traceSMB = 1;

	return count;
}

static int
multiuser_mount_read(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", multiuser_mount);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
multiuser_mount_write(struct file *file, const char __user *buffer,
		      unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		multiuser_mount = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		multiuser_mount = 1;

	return count;
}

static int
security_flags_read(char *page, char **start, off_t off,
		       int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "0x%x\n", extended_security);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
security_flags_write(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
{
	unsigned int flags;
	char flags_string[12];
	char c;

	if ((count < 1) || (count > 11))
		return -EINVAL;

	memset(flags_string, 0, 12);

	if (copy_from_user(flags_string, buffer, count))
		return -EFAULT;

	if (count < 3) {
		/* single char or single char followed by null */
		c = flags_string[0];
		if (c == '0' || c == 'n' || c == 'N') {
			extended_security = CIFSSEC_DEF; /* default */
			return count;
		} else if (c == '1' || c == 'y' || c == 'Y') {
			extended_security = CIFSSEC_MAX;
			return count;
		} else if (!isdigit(c)) {
			cERROR(1, ("invalid flag %c", c));
			return -EINVAL;
		}
	}
	/* else we have a number */

	flags = simple_strtoul(flags_string, NULL, 0);

	cFYI(1, ("sec flags 0x%x", flags));

	if (flags <= 0)  {
		cERROR(1, ("invalid security flags %s", flags_string));
		return -EINVAL;
	}

	if (flags & ~CIFSSEC_MASK) {
		cERROR(1, ("attempt to set unsupported security flags 0x%x",
			flags & ~CIFSSEC_MASK));
		return -EINVAL;
	}
	/* flags look ok - update the global security flags for cifs module */
	extended_security = flags;
	if (extended_security & CIFSSEC_MUST_SIGN) {
		/* requiring signing implies signing is allowed */
		extended_security |= CIFSSEC_MAY_SIGN;
		cFYI(1, ("packet signing now required"));
	} else if ((extended_security & CIFSSEC_MAY_SIGN) == 0) {
		cFYI(1, ("packet signing disabled"));
	}
	/* BB should we turn on MAY flags for other MUST options? */
	return count;
}
#else
inline void cifs_proc_init(void)
{
}

inline void cifs_proc_clean(void)
{
}
#endif /* PROC_FS */
