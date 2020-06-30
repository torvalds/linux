// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   fs/cifs_debug.c
 *
 *   Copyright (C) International Business Machines  Corp., 2000,2005
 *
 *   Modified by Steve French (sfrench@us.ibm.com)
 */
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif
#ifdef CONFIG_CIFS_SMB_DIRECT
#include "smbdirect.h"
#endif

void
cifs_dump_mem(char *label, void *data, int length)
{
	pr_debug("%s: dump of %d bytes of data at 0x%p\n", label, length, data);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 4,
		       data, length, true);
}

void cifs_dump_detail(void *buf, struct TCP_Server_Info *server)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct smb_hdr *smb = (struct smb_hdr *)buf;

	cifs_dbg(VFS, "Cmd: %d Err: 0x%x Flags: 0x%x Flgs2: 0x%x Mid: %d Pid: %d\n",
		 smb->Command, smb->Status.CifsError,
		 smb->Flags, smb->Flags2, smb->Mid, smb->Pid);
	cifs_dbg(VFS, "smb buf %p len %u\n", smb,
		 server->ops->calc_smb_size(smb, server));
#endif /* CONFIG_CIFS_DEBUG2 */
}

void cifs_dump_mids(struct TCP_Server_Info *server)
{
#ifdef CONFIG_CIFS_DEBUG2
	struct list_head *tmp;
	struct mid_q_entry *mid_entry;

	if (server == NULL)
		return;

	cifs_dbg(VFS, "Dump pending requests:\n");
	spin_lock(&GlobalMid_Lock);
	list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		cifs_dbg(VFS, "State: %d Cmd: %d Pid: %d Cbdata: %p Mid %llu\n",
			 mid_entry->mid_state,
			 le16_to_cpu(mid_entry->command),
			 mid_entry->pid,
			 mid_entry->callback_data,
			 mid_entry->mid);
#ifdef CONFIG_CIFS_STATS2
		cifs_dbg(VFS, "IsLarge: %d buf: %p time rcv: %ld now: %ld\n",
			 mid_entry->large_buf,
			 mid_entry->resp_buf,
			 mid_entry->when_received,
			 jiffies);
#endif /* STATS2 */
		cifs_dbg(VFS, "IsMult: %d IsEnd: %d\n",
			 mid_entry->multiRsp, mid_entry->multiEnd);
		if (mid_entry->resp_buf) {
			cifs_dump_detail(mid_entry->resp_buf, server);
			cifs_dump_mem("existing buf: ",
				mid_entry->resp_buf, 62);
		}
	}
	spin_unlock(&GlobalMid_Lock);
#endif /* CONFIG_CIFS_DEBUG2 */
}

#ifdef CONFIG_PROC_FS
static void cifs_debug_tcon(struct seq_file *m, struct cifs_tcon *tcon)
{
	__u32 dev_type = le32_to_cpu(tcon->fsDevInfo.DeviceType);

	seq_printf(m, "%s Mounts: %d ", tcon->treeName, tcon->tc_count);
	if (tcon->nativeFileSystem)
		seq_printf(m, "Type: %s ", tcon->nativeFileSystem);
	seq_printf(m, "DevInfo: 0x%x Attributes: 0x%x\n\tPathComponentMax: %d Status: %d",
		   le32_to_cpu(tcon->fsDevInfo.DeviceCharacteristics),
		   le32_to_cpu(tcon->fsAttrInfo.Attributes),
		   le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength),
		   tcon->tidStatus);
	if (dev_type == FILE_DEVICE_DISK)
		seq_puts(m, " type: DISK ");
	else if (dev_type == FILE_DEVICE_CD_ROM)
		seq_puts(m, " type: CDROM ");
	else
		seq_printf(m, " type: %d ", dev_type);

	seq_printf(m, "Serial Number: 0x%x", tcon->vol_serial_number);

	if ((tcon->seal) ||
	    (tcon->ses->session_flags & SMB2_SESSION_FLAG_ENCRYPT_DATA) ||
	    (tcon->share_flags & SHI1005_FLAGS_ENCRYPT_DATA))
		seq_printf(m, " Encrypted");
	if (tcon->nocase)
		seq_printf(m, " nocase");
	if (tcon->unix_ext)
		seq_printf(m, " POSIX Extensions");
	if (tcon->ses->server->ops->dump_share_caps)
		tcon->ses->server->ops->dump_share_caps(m, tcon);

	if (tcon->need_reconnect)
		seq_puts(m, "\tDISCONNECTED ");
	seq_putc(m, '\n');
}

static void
cifs_dump_channel(struct seq_file *m, int i, struct cifs_chan *chan)
{
	struct TCP_Server_Info *server = chan->server;

	seq_printf(m, "\t\tChannel %d Number of credits: %d Dialect 0x%x "
		   "TCP status: %d Instance: %d Local Users To Server: %d "
		   "SecMode: 0x%x Req On Wire: %d In Send: %d "
		   "In MaxReq Wait: %d\n",
		   i+1,
		   server->credits,
		   server->dialect,
		   server->tcpStatus,
		   server->reconnect_instance,
		   server->srv_count,
		   server->sec_mode,
		   in_flight(server),
		   atomic_read(&server->in_send),
		   atomic_read(&server->num_waiters));
}

static void
cifs_dump_iface(struct seq_file *m, struct cifs_server_iface *iface)
{
	struct sockaddr_in *ipv4 = (struct sockaddr_in *)&iface->sockaddr;
	struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&iface->sockaddr;

	seq_printf(m, "\tSpeed: %zu bps\n", iface->speed);
	seq_puts(m, "\t\tCapabilities: ");
	if (iface->rdma_capable)
		seq_puts(m, "rdma ");
	if (iface->rss_capable)
		seq_puts(m, "rss ");
	seq_putc(m, '\n');
	if (iface->sockaddr.ss_family == AF_INET)
		seq_printf(m, "\t\tIPv4: %pI4\n", &ipv4->sin_addr);
	else if (iface->sockaddr.ss_family == AF_INET6)
		seq_printf(m, "\t\tIPv6: %pI6\n", &ipv6->sin6_addr);
}

static int cifs_debug_files_proc_show(struct seq_file *m, void *v)
{
	struct list_head *stmp, *tmp, *tmp1, *tmp2;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cfile;

	seq_puts(m, "# Version:1\n");
	seq_puts(m, "# Format:\n");
	seq_puts(m, "# <tree id> <persistent fid> <flags> <count> <pid> <uid>");
#ifdef CONFIG_CIFS_DEBUG2
	seq_printf(m, " <filename> <mid>\n");
#else
	seq_printf(m, " <filename>\n");
#endif /* CIFS_DEBUG2 */
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(stmp, &cifs_tcp_ses_list) {
		server = list_entry(stmp, struct TCP_Server_Info,
				    tcp_ses_list);
		list_for_each(tmp, &server->smb_ses_list) {
			ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
			list_for_each(tmp1, &ses->tcon_list) {
				tcon = list_entry(tmp1, struct cifs_tcon, tcon_list);
				spin_lock(&tcon->open_file_lock);
				list_for_each(tmp2, &tcon->openFileList) {
					cfile = list_entry(tmp2, struct cifsFileInfo,
						     tlist);
					seq_printf(m,
						"0x%x 0x%llx 0x%x %d %d %d %s",
						tcon->tid,
						cfile->fid.persistent_fid,
						cfile->f_flags,
						cfile->count,
						cfile->pid,
						from_kuid(&init_user_ns, cfile->uid),
						cfile->dentry->d_name.name);
#ifdef CONFIG_CIFS_DEBUG2
					seq_printf(m, " 0x%llx\n", cfile->fid.mid);
#else
					seq_printf(m, "\n");
#endif /* CIFS_DEBUG2 */
				}
				spin_unlock(&tcon->open_file_lock);
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	seq_putc(m, '\n');
	return 0;
}

static int cifs_debug_data_proc_show(struct seq_file *m, void *v)
{
	struct list_head *tmp1, *tmp2, *tmp3;
	struct mid_q_entry *mid_entry;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	int i, j;

	seq_puts(m,
		    "Display Internal CIFS Data Structures for Debugging\n"
		    "---------------------------------------------------\n");
	seq_printf(m, "CIFS Version %s\n", CIFS_VERSION);
	seq_printf(m, "Features:");
#ifdef CONFIG_CIFS_DFS_UPCALL
	seq_printf(m, " DFS");
#endif
#ifdef CONFIG_CIFS_FSCACHE
	seq_printf(m, ",FSCACHE");
#endif
#ifdef CONFIG_CIFS_SMB_DIRECT
	seq_printf(m, ",SMB_DIRECT");
#endif
#ifdef CONFIG_CIFS_STATS2
	seq_printf(m, ",STATS2");
#else
	seq_printf(m, ",STATS");
#endif
#ifdef CONFIG_CIFS_DEBUG2
	seq_printf(m, ",DEBUG2");
#elif defined(CONFIG_CIFS_DEBUG)
	seq_printf(m, ",DEBUG");
#endif
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	seq_printf(m, ",ALLOW_INSECURE_LEGACY");
#endif
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	seq_printf(m, ",WEAK_PW_HASH");
#endif
#ifdef CONFIG_CIFS_POSIX
	seq_printf(m, ",CIFS_POSIX");
#endif
#ifdef CONFIG_CIFS_UPCALL
	seq_printf(m, ",UPCALL(SPNEGO)");
#endif
#ifdef CONFIG_CIFS_XATTR
	seq_printf(m, ",XATTR");
#endif
	seq_printf(m, ",ACL");
	seq_putc(m, '\n');
	seq_printf(m, "CIFSMaxBufSize: %d\n", CIFSMaxBufSize);
	seq_printf(m, "Active VFS Requests: %d\n", GlobalTotalActiveXid);
	seq_printf(m, "Servers:");

	i = 0;
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp1, &cifs_tcp_ses_list) {
		server = list_entry(tmp1, struct TCP_Server_Info,
				    tcp_ses_list);

#ifdef CONFIG_CIFS_SMB_DIRECT
		if (!server->rdma)
			goto skip_rdma;

		if (!server->smbd_conn) {
			seq_printf(m, "\nSMBDirect transport not available");
			goto skip_rdma;
		}

		seq_printf(m, "\nSMBDirect (in hex) protocol version: %x "
			"transport status: %x",
			server->smbd_conn->protocol,
			server->smbd_conn->transport_status);
		seq_printf(m, "\nConn receive_credit_max: %x "
			"send_credit_target: %x max_send_size: %x",
			server->smbd_conn->receive_credit_max,
			server->smbd_conn->send_credit_target,
			server->smbd_conn->max_send_size);
		seq_printf(m, "\nConn max_fragmented_recv_size: %x "
			"max_fragmented_send_size: %x max_receive_size:%x",
			server->smbd_conn->max_fragmented_recv_size,
			server->smbd_conn->max_fragmented_send_size,
			server->smbd_conn->max_receive_size);
		seq_printf(m, "\nConn keep_alive_interval: %x "
			"max_readwrite_size: %x rdma_readwrite_threshold: %x",
			server->smbd_conn->keep_alive_interval,
			server->smbd_conn->max_readwrite_size,
			server->smbd_conn->rdma_readwrite_threshold);
		seq_printf(m, "\nDebug count_get_receive_buffer: %x "
			"count_put_receive_buffer: %x count_send_empty: %x",
			server->smbd_conn->count_get_receive_buffer,
			server->smbd_conn->count_put_receive_buffer,
			server->smbd_conn->count_send_empty);
		seq_printf(m, "\nRead Queue count_reassembly_queue: %x "
			"count_enqueue_reassembly_queue: %x "
			"count_dequeue_reassembly_queue: %x "
			"fragment_reassembly_remaining: %x "
			"reassembly_data_length: %x "
			"reassembly_queue_length: %x",
			server->smbd_conn->count_reassembly_queue,
			server->smbd_conn->count_enqueue_reassembly_queue,
			server->smbd_conn->count_dequeue_reassembly_queue,
			server->smbd_conn->fragment_reassembly_remaining,
			server->smbd_conn->reassembly_data_length,
			server->smbd_conn->reassembly_queue_length);
		seq_printf(m, "\nCurrent Credits send_credits: %x "
			"receive_credits: %x receive_credit_target: %x",
			atomic_read(&server->smbd_conn->send_credits),
			atomic_read(&server->smbd_conn->receive_credits),
			server->smbd_conn->receive_credit_target);
		seq_printf(m, "\nPending send_pending: %x ",
			atomic_read(&server->smbd_conn->send_pending));
		seq_printf(m, "\nReceive buffers count_receive_queue: %x "
			"count_empty_packet_queue: %x",
			server->smbd_conn->count_receive_queue,
			server->smbd_conn->count_empty_packet_queue);
		seq_printf(m, "\nMR responder_resources: %x "
			"max_frmr_depth: %x mr_type: %x",
			server->smbd_conn->responder_resources,
			server->smbd_conn->max_frmr_depth,
			server->smbd_conn->mr_type);
		seq_printf(m, "\nMR mr_ready_count: %x mr_used_count: %x",
			atomic_read(&server->smbd_conn->mr_ready_count),
			atomic_read(&server->smbd_conn->mr_used_count));
skip_rdma:
#endif
		seq_printf(m, "\nNumber of credits: %d Dialect 0x%x",
			server->credits,  server->dialect);
		if (server->compress_algorithm == SMB3_COMPRESS_LZNT1)
			seq_printf(m, " COMPRESS_LZNT1");
		else if (server->compress_algorithm == SMB3_COMPRESS_LZ77)
			seq_printf(m, " COMPRESS_LZ77");
		else if (server->compress_algorithm == SMB3_COMPRESS_LZ77_HUFF)
			seq_printf(m, " COMPRESS_LZ77_HUFF");
		if (server->sign)
			seq_printf(m, " signed");
		if (server->posix_ext_supported)
			seq_printf(m, " posix");

		i++;
		list_for_each(tmp2, &server->smb_ses_list) {
			ses = list_entry(tmp2, struct cifs_ses,
					 smb_ses_list);
			if ((ses->serverDomain == NULL) ||
				(ses->serverOS == NULL) ||
				(ses->serverNOS == NULL)) {
				seq_printf(m, "\n%d) Name: %s Uses: %d Capability: 0x%x\tSession Status: %d ",
					i, ses->serverName, ses->ses_count,
					ses->capabilities, ses->status);
				if (ses->session_flags & SMB2_SESSION_FLAG_IS_GUEST)
					seq_printf(m, "Guest\t");
				else if (ses->session_flags & SMB2_SESSION_FLAG_IS_NULL)
					seq_printf(m, "Anonymous\t");
			} else {
				seq_printf(m,
				    "\n%d) Name: %s  Domain: %s Uses: %d OS:"
				    " %s\n\tNOS: %s\tCapability: 0x%x\n\tSMB"
				    " session status: %d ",
				i, ses->serverName, ses->serverDomain,
				ses->ses_count, ses->serverOS, ses->serverNOS,
				ses->capabilities, ses->status);
			}

			seq_printf(m,"Security type: %s\n",
				get_security_type_str(server->ops->select_sectype(server, ses->sectype)));

			if (server->rdma)
				seq_printf(m, "RDMA\n\t");
			seq_printf(m, "TCP status: %d Instance: %d\n\tLocal Users To "
				   "Server: %d SecMode: 0x%x Req On Wire: %d",
				   server->tcpStatus,
				   server->reconnect_instance,
				   server->srv_count,
				   server->sec_mode, in_flight(server));

			seq_printf(m, " In Send: %d In MaxReq Wait: %d",
				atomic_read(&server->in_send),
				atomic_read(&server->num_waiters));

			/* dump session id helpful for use with network trace */
			seq_printf(m, " SessionId: 0x%llx", ses->Suid);
			if (ses->session_flags & SMB2_SESSION_FLAG_ENCRYPT_DATA)
				seq_puts(m, " encrypted");
			if (ses->sign)
				seq_puts(m, " signed");

			if (ses->chan_count > 1) {
				seq_printf(m, "\n\n\tExtra Channels: %zu\n",
					   ses->chan_count-1);
				for (j = 1; j < ses->chan_count; j++)
					cifs_dump_channel(m, j, &ses->chans[j]);
			}

			seq_puts(m, "\n\tShares:");
			j = 0;

			seq_printf(m, "\n\t%d) IPC: ", j);
			if (ses->tcon_ipc)
				cifs_debug_tcon(m, ses->tcon_ipc);
			else
				seq_puts(m, "none\n");

			list_for_each(tmp3, &ses->tcon_list) {
				tcon = list_entry(tmp3, struct cifs_tcon,
						  tcon_list);
				++j;
				seq_printf(m, "\n\t%d) ", j);
				cifs_debug_tcon(m, tcon);
			}

			seq_puts(m, "\n\tMIDs:\n");

			spin_lock(&GlobalMid_Lock);
			list_for_each(tmp3, &server->pending_mid_q) {
				mid_entry = list_entry(tmp3, struct mid_q_entry,
					qhead);
				seq_printf(m, "\tState: %d com: %d pid:"
					      " %d cbdata: %p mid %llu\n",
					      mid_entry->mid_state,
					      le16_to_cpu(mid_entry->command),
					      mid_entry->pid,
					      mid_entry->callback_data,
					      mid_entry->mid);
			}
			spin_unlock(&GlobalMid_Lock);

			spin_lock(&ses->iface_lock);
			if (ses->iface_count)
				seq_printf(m, "\n\tServer interfaces: %zu\n",
					   ses->iface_count);
			for (j = 0; j < ses->iface_count; j++) {
				struct cifs_server_iface *iface;

				iface = &ses->iface_list[j];
				seq_printf(m, "\t%d)", j);
				cifs_dump_iface(m, iface);
				if (is_ses_using_iface(ses, iface))
					seq_puts(m, "\t\t[CONNECTED]\n");
			}
			spin_unlock(&ses->iface_lock);
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
	seq_putc(m, '\n');

	/* BB add code to dump additional info such as TCP session info now */
	return 0;
}

static ssize_t cifs_stats_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	bool bv;
	int rc;
	struct list_head *tmp1, *tmp2, *tmp3;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	rc = kstrtobool_from_user(buffer, count, &bv);
	if (rc == 0) {
#ifdef CONFIG_CIFS_STATS2
		int i;

		atomic_set(&totBufAllocCount, 0);
		atomic_set(&totSmBufAllocCount, 0);
#endif /* CONFIG_CIFS_STATS2 */
		atomic_set(&tcpSesReconnectCount, 0);
		atomic_set(&tconInfoReconnectCount, 0);

		spin_lock(&GlobalMid_Lock);
		GlobalMaxActiveXid = 0;
		GlobalCurrentXid = 0;
		spin_unlock(&GlobalMid_Lock);
		spin_lock(&cifs_tcp_ses_lock);
		list_for_each(tmp1, &cifs_tcp_ses_list) {
			server = list_entry(tmp1, struct TCP_Server_Info,
					    tcp_ses_list);
			server->max_in_flight = 0;
#ifdef CONFIG_CIFS_STATS2
			for (i = 0; i < NUMBER_OF_SMB2_COMMANDS; i++) {
				atomic_set(&server->num_cmds[i], 0);
				atomic_set(&server->smb2slowcmd[i], 0);
				server->time_per_cmd[i] = 0;
				server->slowest_cmd[i] = 0;
				server->fastest_cmd[0] = 0;
			}
#endif /* CONFIG_CIFS_STATS2 */
			list_for_each(tmp2, &server->smb_ses_list) {
				ses = list_entry(tmp2, struct cifs_ses,
						 smb_ses_list);
				list_for_each(tmp3, &ses->tcon_list) {
					tcon = list_entry(tmp3,
							  struct cifs_tcon,
							  tcon_list);
					atomic_set(&tcon->num_smbs_sent, 0);
					spin_lock(&tcon->stat_lock);
					tcon->bytes_read = 0;
					tcon->bytes_written = 0;
					spin_unlock(&tcon->stat_lock);
					if (server->ops->clear_stats)
						server->ops->clear_stats(tcon);
				}
			}
		}
		spin_unlock(&cifs_tcp_ses_lock);
	} else {
		return rc;
	}

	return count;
}

static int cifs_stats_proc_show(struct seq_file *m, void *v)
{
	int i;
#ifdef CONFIG_CIFS_STATS2
	int j;
#endif /* STATS2 */
	struct list_head *tmp1, *tmp2, *tmp3;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	seq_printf(m, "Resources in use\nCIFS Session: %d\n",
			sesInfoAllocCount.counter);
	seq_printf(m, "Share (unique mount targets): %d\n",
			tconInfoAllocCount.counter);
	seq_printf(m, "SMB Request/Response Buffer: %d Pool size: %d\n",
			bufAllocCount.counter,
			cifs_min_rcv + tcpSesAllocCount.counter);
	seq_printf(m, "SMB Small Req/Resp Buffer: %d Pool size: %d\n",
			smBufAllocCount.counter, cifs_min_small);
#ifdef CONFIG_CIFS_STATS2
	seq_printf(m, "Total Large %d Small %d Allocations\n",
				atomic_read(&totBufAllocCount),
				atomic_read(&totSmBufAllocCount));
#endif /* CONFIG_CIFS_STATS2 */

	seq_printf(m, "Operations (MIDs): %d\n", atomic_read(&midCount));
	seq_printf(m,
		"\n%d session %d share reconnects\n",
		tcpSesReconnectCount.counter, tconInfoReconnectCount.counter);

	seq_printf(m,
		"Total vfs operations: %d maximum at one time: %d\n",
		GlobalCurrentXid, GlobalMaxActiveXid);

	i = 0;
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp1, &cifs_tcp_ses_list) {
		server = list_entry(tmp1, struct TCP_Server_Info,
				    tcp_ses_list);
		seq_printf(m, "\nMax requests in flight: %d", server->max_in_flight);
#ifdef CONFIG_CIFS_STATS2
		seq_puts(m, "\nTotal time spent processing by command. Time ");
		seq_printf(m, "units are jiffies (%d per second)\n", HZ);
		seq_puts(m, "  SMB3 CMD\tNumber\tTotal Time\tFastest\tSlowest\n");
		seq_puts(m, "  --------\t------\t----------\t-------\t-------\n");
		for (j = 0; j < NUMBER_OF_SMB2_COMMANDS; j++)
			seq_printf(m, "  %d\t\t%d\t%llu\t\t%u\t%u\n", j,
				atomic_read(&server->num_cmds[j]),
				server->time_per_cmd[j],
				server->fastest_cmd[j],
				server->slowest_cmd[j]);
		for (j = 0; j < NUMBER_OF_SMB2_COMMANDS; j++)
			if (atomic_read(&server->smb2slowcmd[j]))
				seq_printf(m, "  %d slow responses from %s for command %d\n",
					atomic_read(&server->smb2slowcmd[j]),
					server->hostname, j);
#endif /* STATS2 */
		list_for_each(tmp2, &server->smb_ses_list) {
			ses = list_entry(tmp2, struct cifs_ses,
					 smb_ses_list);
			list_for_each(tmp3, &ses->tcon_list) {
				tcon = list_entry(tmp3,
						  struct cifs_tcon,
						  tcon_list);
				i++;
				seq_printf(m, "\n%d) %s", i, tcon->treeName);
				if (tcon->need_reconnect)
					seq_puts(m, "\tDISCONNECTED ");
				seq_printf(m, "\nSMBs: %d",
					   atomic_read(&tcon->num_smbs_sent));
				if (server->ops->print_stats)
					server->ops->print_stats(m, tcon);
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	seq_putc(m, '\n');
	return 0;
}

static int cifs_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cifs_stats_proc_show, NULL);
}

static const struct proc_ops cifs_stats_proc_ops = {
	.proc_open	= cifs_stats_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= cifs_stats_proc_write,
};

#ifdef CONFIG_CIFS_SMB_DIRECT
#define PROC_FILE_DEFINE(name) \
static ssize_t name##_write(struct file *file, const char __user *buffer, \
	size_t count, loff_t *ppos) \
{ \
	int rc; \
	rc = kstrtoint_from_user(buffer, count, 10, & name); \
	if (rc) \
		return rc; \
	return count; \
} \
static int name##_proc_show(struct seq_file *m, void *v) \
{ \
	seq_printf(m, "%d\n", name ); \
	return 0; \
} \
static int name##_open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, name##_proc_show, NULL); \
} \
\
static const struct proc_ops cifs_##name##_proc_fops = { \
	.proc_open	= name##_open, \
	.proc_read	= seq_read, \
	.proc_lseek	= seq_lseek, \
	.proc_release	= single_release, \
	.proc_write	= name##_write, \
}

PROC_FILE_DEFINE(rdma_readwrite_threshold);
PROC_FILE_DEFINE(smbd_max_frmr_depth);
PROC_FILE_DEFINE(smbd_keep_alive_interval);
PROC_FILE_DEFINE(smbd_max_receive_size);
PROC_FILE_DEFINE(smbd_max_fragmented_recv_size);
PROC_FILE_DEFINE(smbd_max_send_size);
PROC_FILE_DEFINE(smbd_send_credit_target);
PROC_FILE_DEFINE(smbd_receive_credit_max);
#endif

static struct proc_dir_entry *proc_fs_cifs;
static const struct proc_ops cifsFYI_proc_ops;
static const struct proc_ops cifs_lookup_cache_proc_ops;
static const struct proc_ops traceSMB_proc_ops;
static const struct proc_ops cifs_security_flags_proc_ops;
static const struct proc_ops cifs_linux_ext_proc_ops;

void
cifs_proc_init(void)
{
	proc_fs_cifs = proc_mkdir("fs/cifs", NULL);
	if (proc_fs_cifs == NULL)
		return;

	proc_create_single("DebugData", 0, proc_fs_cifs,
			cifs_debug_data_proc_show);

	proc_create_single("open_files", 0400, proc_fs_cifs,
			cifs_debug_files_proc_show);

	proc_create("Stats", 0644, proc_fs_cifs, &cifs_stats_proc_ops);
	proc_create("cifsFYI", 0644, proc_fs_cifs, &cifsFYI_proc_ops);
	proc_create("traceSMB", 0644, proc_fs_cifs, &traceSMB_proc_ops);
	proc_create("LinuxExtensionsEnabled", 0644, proc_fs_cifs,
		    &cifs_linux_ext_proc_ops);
	proc_create("SecurityFlags", 0644, proc_fs_cifs,
		    &cifs_security_flags_proc_ops);
	proc_create("LookupCacheEnabled", 0644, proc_fs_cifs,
		    &cifs_lookup_cache_proc_ops);

#ifdef CONFIG_CIFS_DFS_UPCALL
	proc_create("dfscache", 0644, proc_fs_cifs, &dfscache_proc_ops);
#endif

#ifdef CONFIG_CIFS_SMB_DIRECT
	proc_create("rdma_readwrite_threshold", 0644, proc_fs_cifs,
		&cifs_rdma_readwrite_threshold_proc_fops);
	proc_create("smbd_max_frmr_depth", 0644, proc_fs_cifs,
		&cifs_smbd_max_frmr_depth_proc_fops);
	proc_create("smbd_keep_alive_interval", 0644, proc_fs_cifs,
		&cifs_smbd_keep_alive_interval_proc_fops);
	proc_create("smbd_max_receive_size", 0644, proc_fs_cifs,
		&cifs_smbd_max_receive_size_proc_fops);
	proc_create("smbd_max_fragmented_recv_size", 0644, proc_fs_cifs,
		&cifs_smbd_max_fragmented_recv_size_proc_fops);
	proc_create("smbd_max_send_size", 0644, proc_fs_cifs,
		&cifs_smbd_max_send_size_proc_fops);
	proc_create("smbd_send_credit_target", 0644, proc_fs_cifs,
		&cifs_smbd_send_credit_target_proc_fops);
	proc_create("smbd_receive_credit_max", 0644, proc_fs_cifs,
		&cifs_smbd_receive_credit_max_proc_fops);
#endif
}

void
cifs_proc_clean(void)
{
	if (proc_fs_cifs == NULL)
		return;

	remove_proc_entry("DebugData", proc_fs_cifs);
	remove_proc_entry("open_files", proc_fs_cifs);
	remove_proc_entry("cifsFYI", proc_fs_cifs);
	remove_proc_entry("traceSMB", proc_fs_cifs);
	remove_proc_entry("Stats", proc_fs_cifs);
	remove_proc_entry("SecurityFlags", proc_fs_cifs);
	remove_proc_entry("LinuxExtensionsEnabled", proc_fs_cifs);
	remove_proc_entry("LookupCacheEnabled", proc_fs_cifs);

#ifdef CONFIG_CIFS_DFS_UPCALL
	remove_proc_entry("dfscache", proc_fs_cifs);
#endif
#ifdef CONFIG_CIFS_SMB_DIRECT
	remove_proc_entry("rdma_readwrite_threshold", proc_fs_cifs);
	remove_proc_entry("smbd_max_frmr_depth", proc_fs_cifs);
	remove_proc_entry("smbd_keep_alive_interval", proc_fs_cifs);
	remove_proc_entry("smbd_max_receive_size", proc_fs_cifs);
	remove_proc_entry("smbd_max_fragmented_recv_size", proc_fs_cifs);
	remove_proc_entry("smbd_max_send_size", proc_fs_cifs);
	remove_proc_entry("smbd_send_credit_target", proc_fs_cifs);
	remove_proc_entry("smbd_receive_credit_max", proc_fs_cifs);
#endif
	remove_proc_entry("fs/cifs", NULL);
}

static int cifsFYI_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", cifsFYI);
	return 0;
}

static int cifsFYI_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cifsFYI_proc_show, NULL);
}

static ssize_t cifsFYI_proc_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *ppos)
{
	char c[2] = { '\0' };
	bool bv;
	int rc;

	rc = get_user(c[0], buffer);
	if (rc)
		return rc;
	if (strtobool(c, &bv) == 0)
		cifsFYI = bv;
	else if ((c[0] > '1') && (c[0] <= '9'))
		cifsFYI = (int) (c[0] - '0'); /* see cifs_debug.h for meanings */
	else
		return -EINVAL;

	return count;
}

static const struct proc_ops cifsFYI_proc_ops = {
	.proc_open	= cifsFYI_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= cifsFYI_proc_write,
};

static int cifs_linux_ext_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", linuxExtEnabled);
	return 0;
}

static int cifs_linux_ext_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cifs_linux_ext_proc_show, NULL);
}

static ssize_t cifs_linux_ext_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int rc;

	rc = kstrtobool_from_user(buffer, count, &linuxExtEnabled);
	if (rc)
		return rc;

	return count;
}

static const struct proc_ops cifs_linux_ext_proc_ops = {
	.proc_open	= cifs_linux_ext_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= cifs_linux_ext_proc_write,
};

static int cifs_lookup_cache_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", lookupCacheEnabled);
	return 0;
}

static int cifs_lookup_cache_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cifs_lookup_cache_proc_show, NULL);
}

static ssize_t cifs_lookup_cache_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int rc;

	rc = kstrtobool_from_user(buffer, count, &lookupCacheEnabled);
	if (rc)
		return rc;

	return count;
}

static const struct proc_ops cifs_lookup_cache_proc_ops = {
	.proc_open	= cifs_lookup_cache_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= cifs_lookup_cache_proc_write,
};

static int traceSMB_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", traceSMB);
	return 0;
}

static int traceSMB_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, traceSMB_proc_show, NULL);
}

static ssize_t traceSMB_proc_write(struct file *file, const char __user *buffer,
		size_t count, loff_t *ppos)
{
	int rc;

	rc = kstrtobool_from_user(buffer, count, &traceSMB);
	if (rc)
		return rc;

	return count;
}

static const struct proc_ops traceSMB_proc_ops = {
	.proc_open	= traceSMB_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= traceSMB_proc_write,
};

static int cifs_security_flags_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", global_secflags);
	return 0;
}

static int cifs_security_flags_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cifs_security_flags_proc_show, NULL);
}

/*
 * Ensure that if someone sets a MUST flag, that we disable all other MAY
 * flags except for the ones corresponding to the given MUST flag. If there are
 * multiple MUST flags, then try to prefer more secure ones.
 */
static void
cifs_security_flags_handle_must_flags(unsigned int *flags)
{
	unsigned int signflags = *flags & CIFSSEC_MUST_SIGN;

	if ((*flags & CIFSSEC_MUST_KRB5) == CIFSSEC_MUST_KRB5)
		*flags = CIFSSEC_MUST_KRB5;
	else if ((*flags & CIFSSEC_MUST_NTLMSSP) == CIFSSEC_MUST_NTLMSSP)
		*flags = CIFSSEC_MUST_NTLMSSP;
	else if ((*flags & CIFSSEC_MUST_NTLMV2) == CIFSSEC_MUST_NTLMV2)
		*flags = CIFSSEC_MUST_NTLMV2;
	else if ((*flags & CIFSSEC_MUST_NTLM) == CIFSSEC_MUST_NTLM)
		*flags = CIFSSEC_MUST_NTLM;
	else if (CIFSSEC_MUST_LANMAN &&
		 (*flags & CIFSSEC_MUST_LANMAN) == CIFSSEC_MUST_LANMAN)
		*flags = CIFSSEC_MUST_LANMAN;
	else if (CIFSSEC_MUST_PLNTXT &&
		 (*flags & CIFSSEC_MUST_PLNTXT) == CIFSSEC_MUST_PLNTXT)
		*flags = CIFSSEC_MUST_PLNTXT;

	*flags |= signflags;
}

static ssize_t cifs_security_flags_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	int rc;
	unsigned int flags;
	char flags_string[12];
	bool bv;

	if ((count < 1) || (count > 11))
		return -EINVAL;

	memset(flags_string, 0, 12);

	if (copy_from_user(flags_string, buffer, count))
		return -EFAULT;

	if (count < 3) {
		/* single char or single char followed by null */
		if (strtobool(flags_string, &bv) == 0) {
			global_secflags = bv ? CIFSSEC_MAX : CIFSSEC_DEF;
			return count;
		} else if (!isdigit(flags_string[0])) {
			cifs_dbg(VFS, "Invalid SecurityFlags: %s\n",
					flags_string);
			return -EINVAL;
		}
	}

	/* else we have a number */
	rc = kstrtouint(flags_string, 0, &flags);
	if (rc) {
		cifs_dbg(VFS, "Invalid SecurityFlags: %s\n",
				flags_string);
		return rc;
	}

	cifs_dbg(FYI, "sec flags 0x%x\n", flags);

	if (flags == 0)  {
		cifs_dbg(VFS, "Invalid SecurityFlags: %s\n", flags_string);
		return -EINVAL;
	}

	if (flags & ~CIFSSEC_MASK) {
		cifs_dbg(VFS, "Unsupported security flags: 0x%x\n",
			 flags & ~CIFSSEC_MASK);
		return -EINVAL;
	}

	cifs_security_flags_handle_must_flags(&flags);

	/* flags look ok - update the global security flags for cifs module */
	global_secflags = flags;
	if (global_secflags & CIFSSEC_MUST_SIGN) {
		/* requiring signing implies signing is allowed */
		global_secflags |= CIFSSEC_MAY_SIGN;
		cifs_dbg(FYI, "packet signing now required\n");
	} else if ((global_secflags & CIFSSEC_MAY_SIGN) == 0) {
		cifs_dbg(FYI, "packet signing disabled\n");
	}
	/* BB should we turn on MAY flags for other MUST options? */
	return count;
}

static const struct proc_ops cifs_security_flags_proc_ops = {
	.proc_open	= cifs_security_flags_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= cifs_security_flags_proc_write,
};
#else
inline void cifs_proc_init(void)
{
}

inline void cifs_proc_clean(void)
{
}
#endif /* PROC_FS */
