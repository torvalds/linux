/* Public domain. */

#ifndef _ASM_IOSF_MBI_H
#define _ASM_IOSF_MBI_H

#include "iosf.h"

#if NIOSF > 0
#include <dev/ic/iosfvar.h>
#else

static inline void
iosf_mbi_assert_punit_acquired(void)
{
}

static inline void
iosf_mbi_punit_acquire(void)
{
}

static inline void
iosf_mbi_punit_release(void)
{
}

#endif /* NIOSF */

struct notifier_block;

#define MBI_PMIC_BUS_ACCESS_BEGIN	1
#define MBI_PMIC_BUS_ACCESS_END		2

static inline int
iosf_mbi_register_pmic_bus_access_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int
iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(struct notifier_block *nb)
{
	return 0;
}

#endif
