#include <linux/config.h>

#ifdef CONFIG_PROC_FS
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h> 
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/atmmpc.h>
#include <linux/atm.h>
#include "mpc.h"
#include "mpoa_caches.h"

/*
 * mpoa_proc.c: Implementation MPOA client's proc
 * file system statistics 
 */

#if 1
#define dprintk printk   /* debug */
#else
#define dprintk(format,args...)
#endif

#define STAT_FILE_NAME "mpc"     /* Our statistic file's name */

extern struct mpoa_client *mpcs;
extern struct proc_dir_entry *atm_proc_root;  /* from proc.c. */

static int proc_mpc_open(struct inode *inode, struct file *file);
static ssize_t proc_mpc_write(struct file *file, const char __user *buff,
                              size_t nbytes, loff_t *ppos);

static int parse_qos(const char *buff);

/*
 *   Define allowed FILE OPERATIONS
 */
static struct file_operations mpc_file_operations = {
	.owner =	THIS_MODULE,
	.open =		proc_mpc_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.write =	proc_mpc_write,
	.release =	seq_release,
};

/*
 * Returns the state of an ingress cache entry as a string
 */
static const char *ingress_state_string(int state){
        switch(state) {
	case INGRESS_RESOLVING:
	        return "resolving  ";
		break;
	case INGRESS_RESOLVED:
                return "resolved   ";
		break;
	case INGRESS_INVALID:
	        return "invalid    ";
		break;
	case INGRESS_REFRESHING:
	        return "refreshing ";
		break;
	default:
	       return "";
	}
}

/*
 * Returns the state of an egress cache entry as a string
 */
static const char *egress_state_string(int state){
        switch(state) {
	case EGRESS_RESOLVED:
	        return "resolved   ";
		break;
	case EGRESS_PURGE:
                return "purge      ";
		break;
	case EGRESS_INVALID:
	        return "invalid    ";
		break;
	default:
	       return "";
	}
}

/*
 * FIXME: mpcs (and per-mpc lists) have no locking whatsoever.
 */

static void *mpc_start(struct seq_file *m, loff_t *pos)
{
	loff_t l = *pos;
	struct mpoa_client *mpc;

	if (!l--)
		return SEQ_START_TOKEN;
	for (mpc = mpcs; mpc; mpc = mpc->next)
		if (!l--)
			return mpc;
	return NULL;
}

static void *mpc_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct mpoa_client *p = v;
	(*pos)++;
	return v == SEQ_START_TOKEN ? mpcs : p->next;
}

static void mpc_stop(struct seq_file *m, void *v)
{
}

/*
 * READING function - called when the /proc/atm/mpoa file is read from.
 */
static int mpc_show(struct seq_file *m, void *v)
{
	struct mpoa_client *mpc = v;
	unsigned char *temp;
	int i;
	in_cache_entry *in_entry;
	eg_cache_entry *eg_entry;
	struct timeval now;
	unsigned char ip_string[16];

	if (v == SEQ_START_TOKEN) {
		atm_mpoa_disp_qos(m);
		return 0;
	}

	seq_printf(m, "\nInterface %d:\n\n", mpc->dev_num);  
	seq_printf(m, "Ingress Entries:\nIP address      State      Holding time  Packets fwded  VPI  VCI\n");
	do_gettimeofday(&now);

	for (in_entry = mpc->in_cache; in_entry; in_entry = in_entry->next) {
		temp = (unsigned char *)&in_entry->ctrl_info.in_dst_ip;
		sprintf(ip_string,"%d.%d.%d.%d", temp[0], temp[1], temp[2], temp[3]);
		seq_printf(m, "%-16s%s%-14lu%-12u",
			      ip_string,
			      ingress_state_string(in_entry->entry_state),
			      in_entry->ctrl_info.holding_time-(now.tv_sec-in_entry->tv.tv_sec),
			      in_entry->packets_fwded);
		if (in_entry->shortcut)
			seq_printf(m, "   %-3d  %-3d",in_entry->shortcut->vpi,in_entry->shortcut->vci);
		seq_printf(m, "\n");
	}

	seq_printf(m, "\n");
	seq_printf(m, "Egress Entries:\nIngress MPC ATM addr\nCache-id        State      Holding time  Packets recvd  Latest IP addr   VPI VCI\n");
	for (eg_entry = mpc->eg_cache; eg_entry; eg_entry = eg_entry->next) {
		unsigned char *p = eg_entry->ctrl_info.in_MPC_data_ATM_addr;
		for(i = 0; i < ATM_ESA_LEN; i++)
			seq_printf(m, "%02x", p[i]);
		seq_printf(m, "\n%-16lu%s%-14lu%-15u",
			   (unsigned long)ntohl(eg_entry->ctrl_info.cache_id),
			   egress_state_string(eg_entry->entry_state),
			   (eg_entry->ctrl_info.holding_time-(now.tv_sec-eg_entry->tv.tv_sec)),
			   eg_entry->packets_rcvd);
		
		/* latest IP address */
		temp = (unsigned char *)&eg_entry->latest_ip_addr;
		sprintf(ip_string, "%d.%d.%d.%d", temp[0], temp[1], temp[2], temp[3]);
		seq_printf(m, "%-16s", ip_string);

		if (eg_entry->shortcut)
			seq_printf(m, " %-3d %-3d",eg_entry->shortcut->vpi,eg_entry->shortcut->vci);
		seq_printf(m, "\n");
	}
	seq_printf(m, "\n");
	return 0;
}

static struct seq_operations mpc_op = {
	.start =	mpc_start,
	.next =		mpc_next,
	.stop =		mpc_stop,
	.show =		mpc_show
};

static int proc_mpc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &mpc_op);
}

static ssize_t proc_mpc_write(struct file *file, const char __user *buff,
                              size_t nbytes, loff_t *ppos)
{
        char *page, *p;
	unsigned len;

        if (nbytes == 0)
		return 0;

        if (nbytes >= PAGE_SIZE)
		nbytes = PAGE_SIZE-1;

        page = (char *)__get_free_page(GFP_KERNEL);
        if (!page)
		return -ENOMEM;

        for (p = page, len = 0; len < nbytes; p++, len++) {
                if (get_user(*p, buff++)) {
			free_page((unsigned long)page);
			return -EFAULT;
		}
                if (*p == '\0' || *p == '\n')
                        break;
        }

        *p = '\0';

	if (!parse_qos(page))
                printk("mpoa: proc_mpc_write: could not parse '%s'\n", page);

        free_page((unsigned long)page);
        
        return len;
}

static int parse_qos(const char *buff)
{
        /* possible lines look like this
         * add 130.230.54.142 tx=max_pcr,max_sdu rx=max_pcr,max_sdu
         */
        unsigned char ip[4]; 
	int tx_pcr, tx_sdu, rx_pcr, rx_sdu;
        uint32_t ipaddr;
	struct atm_qos qos; 
        
        memset(&qos, 0, sizeof(struct atm_qos));

	if (sscanf(buff, "del %hhu.%hhu.%hhu.%hhu",
			ip, ip+1, ip+2, ip+3) == 4) {
		ipaddr = *(uint32_t *)ip;
		return atm_mpoa_delete_qos(atm_mpoa_search_qos(ipaddr));
	}

	if (sscanf(buff, "add %hhu.%hhu.%hhu.%hhu tx=%d,%d rx=tx",
			ip, ip+1, ip+2, ip+3, &tx_pcr, &tx_sdu) == 6) {
		rx_pcr = tx_pcr;
		rx_sdu = tx_sdu;
	} else if (sscanf(buff, "add %hhu.%hhu.%hhu.%hhu tx=%d,%d rx=%d,%d",
		ip, ip+1, ip+2, ip+3, &tx_pcr, &tx_sdu, &rx_pcr, &rx_sdu) != 8)
		return 0;

        ipaddr = *(uint32_t *)ip;
	qos.txtp.traffic_class = ATM_CBR;
	qos.txtp.max_pcr = tx_pcr;
	qos.txtp.max_sdu = tx_sdu;
	qos.rxtp.traffic_class = ATM_CBR;
	qos.rxtp.max_pcr = rx_pcr;
	qos.rxtp.max_sdu = rx_sdu;
        qos.aal = ATM_AAL5;
	dprintk("mpoa: mpoa_proc.c: parse_qos(): setting qos paramameters to tx=%d,%d rx=%d,%d\n",
		qos.txtp.max_pcr,
		qos.txtp.max_sdu,
		qos.rxtp.max_pcr,
		qos.rxtp.max_sdu
		);

	atm_mpoa_add_qos(ipaddr, &qos);
	return 1;
}

/*
 * INITIALIZATION function - called when module is initialized/loaded.
 */
int mpc_proc_init(void)
{
	struct proc_dir_entry *p;

        p = create_proc_entry(STAT_FILE_NAME, 0, atm_proc_root);
	if (!p) {
                printk(KERN_ERR "Unable to initialize /proc/atm/%s\n", STAT_FILE_NAME);
                return -ENOMEM;
        }
	p->proc_fops = &mpc_file_operations;
	p->owner = THIS_MODULE;
	return 0;
}

/*
 * DELETING function - called when module is removed.
 */
void mpc_proc_clean(void)
{
	remove_proc_entry(STAT_FILE_NAME,atm_proc_root);
}


#endif /* CONFIG_PROC_FS */






