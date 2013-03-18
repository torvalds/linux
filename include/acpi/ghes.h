#include <acpi/apei.h>
#include <acpi/hed.h>

/*
 * One struct ghes is created for each generic hardware error source.
 * It provides the context for APEI hardware error timer/IRQ/SCI/NMI
 * handler.
 *
 * estatus: memory buffer for error status block, allocated during
 * HEST parsing.
 */
#define GHES_TO_CLEAR		0x0001
#define GHES_EXITING		0x0002

struct ghes {
	struct acpi_hest_generic *generic;
	struct acpi_hest_generic_status *estatus;
	u64 buffer_paddr;
	unsigned long flags;
	union {
		struct list_head list;
		struct timer_list timer;
		unsigned int irq;
	};
};

struct ghes_estatus_node {
	struct llist_node llnode;
	struct acpi_hest_generic *generic;
	struct ghes *ghes;
};

struct ghes_estatus_cache {
	u32 estatus_len;
	atomic_t count;
	struct acpi_hest_generic *generic;
	unsigned long long time_in;
	struct rcu_head rcu;
};

enum {
	GHES_SEV_NO = 0x0,
	GHES_SEV_CORRECTED = 0x1,
	GHES_SEV_RECOVERABLE = 0x2,
	GHES_SEV_PANIC = 0x3,
};

/* From drivers/edac/ghes_edac.c */

#ifdef CONFIG_EDAC_GHES
void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
				struct cper_sec_mem_err *mem_err);

int ghes_edac_register(struct ghes *ghes, struct device *dev);

void ghes_edac_unregister(struct ghes *ghes);

#else
static inline void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
				       struct cper_sec_mem_err *mem_err)
{
}

static inline int ghes_edac_register(struct ghes *ghes, struct device *dev)
{
	return 0;
}

static inline void ghes_edac_unregister(struct ghes *ghes)
{
}
#endif
