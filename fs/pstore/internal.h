#ifndef __PSTORE_INTERNAL_H__
#define __PSTORE_INTERNAL_H__

#include <linux/types.h>
#include <linux/time.h>
#include <linux/pstore.h>

#if NR_CPUS <= 2 && defined(CONFIG_ARM_THUMB)
#define PSTORE_CPU_IN_IP 0x1
#elif NR_CPUS <= 4 && defined(CONFIG_ARM)
#define PSTORE_CPU_IN_IP 0x3
#endif

struct pstore_ftrace_record {
	unsigned long ip;
	unsigned long parent_ip;
#ifndef PSTORE_CPU_IN_IP
	unsigned int cpu;
#endif
};

static inline void
pstore_ftrace_encode_cpu(struct pstore_ftrace_record *rec, unsigned int cpu)
{
#ifndef PSTORE_CPU_IN_IP
	rec->cpu = cpu;
#else
	rec->ip |= cpu;
#endif
}

static inline unsigned int
pstore_ftrace_decode_cpu(struct pstore_ftrace_record *rec)
{
#ifndef PSTORE_CPU_IN_IP
	return rec->cpu;
#else
	return rec->ip & PSTORE_CPU_IN_IP;
#endif
}

#ifdef CONFIG_PSTORE_FTRACE
extern void pstore_register_ftrace(void);
#else
static inline void pstore_register_ftrace(void) {}
#endif

#ifdef CONFIG_PSTORE_PMSG
extern void pstore_register_pmsg(void);
#else
static inline void pstore_register_pmsg(void) {}
#endif

extern struct pstore_info *psinfo;

extern void	pstore_set_kmsg_bytes(int);
extern void	pstore_get_records(int);
extern int	pstore_mkfile(enum pstore_type_id, char *psname, u64 id,
			      int count, char *data, size_t size,
			      struct timespec time, struct pstore_info *psi);
extern int	pstore_is_mounted(void);

#endif
