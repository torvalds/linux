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

static const struct nf_logger *nf_loggers[NFPROTO_NUMPROTO] __read_mostly;
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

	if (pf >= ARRAY_SIZE(nf_loggers))
		return -EINVAL;

	mutex_lock(&nf_log_mutex);

	if (pf == NFPROTO_UNSPEC) {
		int i;
		for (i = NFPROTO_UNSPEC; i < NFPROTO_NUMPROTO; i++)
			list_add_tail(&(logger->list[i]), &(nf_loggers_l[i]));
	} else {
		/* register at end of list to honor first register win */
		list_add_tail(&logger->list[pf], &nf_loggers_l[pf]);
		llog = rcu_dereference(nf_loggers[pf]);
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
		c_logger = rcu_dereference(nf_loggers[i]);
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
	__acquires(RCU)
{
	rcu_read_lock();

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
	__releases(RCU)
{
	rcu_read_unlock();
}

static int seq_show(struct seq_file *s, void *v)
{
	loff_t *pos = v;
	const struct nf_logger *logger;

	logger = rcu_dereference(nf_loggers[*pos]);

	if (!logger)
		return seq_printf(s, "%2lld NONE\n", *pos);

	return seq_printf(s, "%2lld %s\n", *pos, logger->name);
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


int __init netfilter_log_init(void)
{
	int i;
#ifdef CONFIG_PROC_FS
	if (!proc_create("nf_log", S_IRUGO,
			 proc_net_netfilter, &nflog_file_ops))
		return -1;
#endif

	for (i = NFPROTO_UNSPEC; i < NFPROTO_NUMPROTO; i++)
		INIT_LIST_HEAD(&(nf_loggers_l[i]));

	return 0;
}
