#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/seq_file.h>
#include <net/protocol.h>
#include <net/netfilter/nf_log.h>

#include "nf_internals.h"

/* Internal logging interface, which relies on the real
   LOG target modules */

#define NF_LOG_PREFIXLEN		128
#define NFLOGGER_NAME_LEN		64

static const struct nf_logger __rcu *nf_loggers[NFPROTO_NUMPROTO] __read_mostly;
static struct list_head nf_loggers_l[NFPROTO_NUMPROTO] __read_mostly;
static DEFINE_MUTEX(nf_log_mutex);

static struct nf_logger *__find_logger(int pf, const char *str_logger)
{
	struct nf_logger *t;

	list_for_each_entry(t, &nf_loggers_l[pf], list[pf]) {
		if (!strnicmp(str_logger, t->name, strlen(t->name)))
			return t;
	}

	return NULL;
}

/* return EEXIST if the same logger is registred, 0 on success. */
int nf_log_register(u_int8_t pf, struct nf_logger *logger)
{
	const struct nf_logger *llog;
	int i;

	if (pf >= ARRAY_SIZE(nf_loggers))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(logger->list); i++)
		INIT_LIST_HEAD(&logger->list[i]);

	mutex_lock(&nf_log_mutex);

	if (pf == NFPROTO_UNSPEC) {
		for (i = NFPROTO_UNSPEC; i < NFPROTO_NUMPROTO; i++)
			list_add_tail(&(logger->list[i]), &(nf_loggers_l[i]));
	} else {
		/* register at end of list to honor first register win */
		list_add_tail(&logger->list[pf], &nf_loggers_l[pf]);
		llog = rcu_dereference_protected(nf_loggers[pf],
						 lockdep_is_held(&nf_log_mutex));
		if (llog == NULL)
			rcu_assign_pointer(nf_loggers[pf], logger);
	}

	mutex_unlock(&nf_log_mutex);

	return 0;
}
EXPORT_SYMBOL(nf_log_register);

void nf_log_unregister(struct nf_logger *logger)
{
	const struct nf_logger *c_logger;
	int i;

	mutex_lock(&nf_log_mutex);
	for (i = 0; i < ARRAY_SIZE(nf_loggers); i++) {
		c_logger = rcu_dereference_protected(nf_loggers[i],
						     lockdep_is_held(&nf_log_mutex));
		if (c_logger == logger)
			rcu_assign_pointer(nf_loggers[i], NULL);
		list_del(&logger->list[i]);
	}
	mutex_unlock(&nf_log_mutex);

	synchronize_rcu();
}
EXPORT_SYMBOL(nf_log_unregister);

int nf_log_bind_pf(u_int8_t pf, const struct nf_logger *logger)
{
	if (pf >= ARRAY_SIZE(nf_loggers))
		return -EINVAL;
	mutex_lock(&nf_log_mutex);
	if (__find_logger(pf, logger->name) == NULL) {
		mutex_unlock(&nf_log_mutex);
		return -ENOENT;
	}
	rcu_assign_pointer(nf_loggers[pf], logger);
	mutex_unlock(&nf_log_mutex);
	return 0;
}
EXPORT_SYMBOL(nf_log_bind_pf);

void nf_log_unbind_pf(u_int8_t pf)
{
	if (pf >= ARRAY_SIZE(nf_loggers))
		return;
	mutex_lock(&nf_log_mutex);
	rcu_assign_pointer(nf_loggers[pf], NULL);
	mutex_unlock(&nf_log_mutex);
}
EXPORT_SYMBOL(nf_log_unbind_pf);

void nf_log_packet(u_int8_t pf,
		   unsigned int hooknum,
		   const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   const struct nf_loginfo *loginfo,
		   const char *fmt, ...)
{
	va_list args;
	char prefix[NF_LOG_PREFIXLEN];
	const struct nf_logger *logger;

	rcu_read_lock();
	logger = rcu_dereference(nf_loggers[pf]);
	if (logger) {
		va_start(args, fmt);
		vsnprintf(prefix, sizeof(prefix), fmt, args);
		va_end(args);
		logger->logfn(pf, hooknum, skb, in, out, loginfo, prefix);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(nf_log_packet);

#ifdef CONFIG_PROC_FS
static void *seq_start(struct seq_file *seq, loff_t *pos)
{
	mutex_lock(&nf_log_mutex);

	if (*pos >= ARRAY_SIZE(nf_loggers))
		return NULL;

	return pos;
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;

	if (*pos >= ARRAY_SIZE(nf_loggers))
		return NULL;

	return pos;
}

static void seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&nf_log_mutex);
}

static int seq_show(struct seq_file *s, void *v)
{
	loff_t *pos = v;
	const struct nf_logger *logger;
	struct nf_logger *t;
	int ret;

	logger = nf_loggers[*pos];

	if (!logger)
		ret = seq_printf(s, "%2lld NONE (", *pos);
	else
		ret = seq_printf(s, "%2lld %s (", *pos, logger->name);

	if (ret < 0)
		return ret;

	list_for_each_entry(t, &nf_loggers_l[*pos], list[*pos]) {
		ret = seq_printf(s, "%s", t->name);
		if (ret < 0)
			return ret;
		if (&t->list[*pos] != nf_loggers_l[*pos].prev) {
			ret = seq_printf(s, ",");
			if (ret < 0)
				return ret;
		}
	}

	return seq_printf(s, ")\n");
}

static const struct seq_operations nflog_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nflog_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nflog_seq_ops);
}

static const struct file_operations nflog_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nflog_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};


#endif /* PROC_FS */

#ifdef CONFIG_SYSCTL
static struct ctl_path nf_log_sysctl_path[] = {
	{ .procname = "net", },
	{ .procname = "netfilter", },
	{ .procname = "nf_log", },
	{ }
};

static char nf_log_sysctl_fnames[NFPROTO_NUMPROTO-NFPROTO_UNSPEC][3];
static struct ctl_table nf_log_sysctl_table[NFPROTO_NUMPROTO+1];
static struct ctl_table_header *nf_log_dir_header;

static int nf_log_proc_dostring(ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	const struct nf_logger *logger;
	char buf[NFLOGGER_NAME_LEN];
	size_t size = *lenp;
	int r = 0;
	int tindex = (unsigned long)table->extra1;

	if (write) {
		if (size > sizeof(buf))
			size = sizeof(buf);
		if (copy_from_user(buf, buffer, size))
			return -EFAULT;

		if (!strcmp(buf, "NONE")) {
			nf_log_unbind_pf(tindex);
			return 0;
		}
		mutex_lock(&nf_log_mutex);
		logger = __find_logger(tindex, buf);
		if (logger == NULL) {
			mutex_unlock(&nf_log_mutex);
			return -ENOENT;
		}
		rcu_assign_pointer(nf_loggers[tindex], logger);
		mutex_unlock(&nf_log_mutex);
	} else {
		mutex_lock(&nf_log_mutex);
		logger = nf_loggers[tindex];
		if (!logger)
			table->data = "NONE";
		else
			table->data = logger->name;
		r = proc_dostring(table, write, buffer, lenp, ppos);
		mutex_unlock(&nf_log_mutex);
	}

	return r;
}

static __init int netfilter_log_sysctl_init(void)
{
	int i;

	for (i = NFPROTO_UNSPEC; i < NFPROTO_NUMPROTO; i++) {
		snprintf(nf_log_sysctl_fnames[i-NFPROTO_UNSPEC], 3, "%d", i);
		nf_log_sysctl_table[i].procname	=
			nf_log_sysctl_fnames[i-NFPROTO_UNSPEC];
		nf_log_sysctl_table[i].data = NULL;
		nf_log_sysctl_table[i].maxlen =
			NFLOGGER_NAME_LEN * sizeof(char);
		nf_log_sysctl_table[i].mode = 0644;
		nf_log_sysctl_table[i].proc_handler = nf_log_proc_dostring;
		nf_log_sysctl_table[i].extra1 = (void *)(unsigned long) i;
	}

	nf_log_dir_header = register_sysctl_paths(nf_log_sysctl_path,
				       nf_log_sysctl_table);
	if (!nf_log_dir_header)
		return -ENOMEM;

	return 0;
}
#else
static __init int netfilter_log_sysctl_init(void)
{
	return 0;
}
#endif /* CONFIG_SYSCTL */

int __init netfilter_log_init(void)
{
	int i, r;
#ifdef CONFIG_PROC_FS
	if (!proc_create("nf_log", S_IRUGO,
			 proc_net_netfilter, &nflog_file_ops))
		return -1;
#endif

	/* Errors will trigger panic, unroll on error is unnecessary. */
	r = netfilter_log_sysctl_init();
	if (r < 0)
		return r;

	for (i = NFPROTO_UNSPEC; i < NFPROTO_NUMPROTO; i++)
		INIT_LIST_HEAD(&(nf_loggers_l[i]));

	return 0;
}
