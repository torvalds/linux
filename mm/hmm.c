/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: JÃ©rÃ´me Glisse <jglisse@redhat.com>
 */
/*
 * Refer to include/linux/hmm.h for information about heterogeneous memory
 * management or HMM for short.
 */
#include <linux/mm.h>
#include <linux/hmm.h>
#include <linux/slab.h>
#include <linux/sched.h>


#ifdef CONFIG_HMM
/*
 * struct hmm - HMM per mm struct
 *
 * @mm: mm struct this HMM struct is bound to
 */
struct hmm {
	struct mm_struct	*mm;
};

/*
 * hmm_register - register HMM against an mm (HMM internal)
 *
 * @mm: mm struct to attach to
 *
 * This is not intended to be used directly by device drivers. It allocates an
 * HMM struct if mm does not have one, and initializes it.
 */
static struct hmm *hmm_register(struct mm_struct *mm)
{
	if (!mm->hmm) {
		struct hmm *hmm = NULL;

		hmm = kmalloc(sizeof(*hmm), GFP_KERNEL);
		if (!hmm)
			return NULL;
		hmm->mm = mm;

		spin_lock(&mm->page_table_lock);
		if (!mm->hmm)
			mm->hmm = hmm;
		else
			kfree(hmm);
		spin_unlock(&mm->page_table_lock);
	}

	/*
	 * The hmm struct can only be freed once the mm_struct goes away,
	 * hence we should always have pre-allocated an new hmm struct
	 * above.
	 */
	return mm->hmm;
}

void hmm_mm_destroy(struct mm_struct *mm)
{
	kfree(mm->hmm);
}
#endif /* CONFIG_HMM */
