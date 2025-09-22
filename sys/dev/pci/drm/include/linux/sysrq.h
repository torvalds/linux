/* Public domain. */

#ifndef _LINUX_SYSRQ_H
#define _LINUX_SYSRQ_H

struct sysrq_key_op {
};

static inline int
register_sysrq_key(int k, const struct sysrq_key_op *op)
{
	return 0;
}

static inline int
unregister_sysrq_key(int k, const struct sysrq_key_op *op)
{
	return 0;
}

#endif
