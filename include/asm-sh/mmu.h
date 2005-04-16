#ifndef __MMU_H
#define __MMU_H

#if !defined(CONFIG_MMU)

struct mm_rblock_struct {
	int	size;
	int	refcount;
	void	*kblock;
};

struct mm_tblock_struct {
	struct mm_rblock_struct *rblock;
	struct mm_tblock_struct *next;
};

typedef struct {
	struct mm_tblock_struct tblock;
	unsigned long		end_brk;
} mm_context_t;

#else

/* Default "unsigned long" context */
typedef unsigned long mm_context_t;

#endif /* CONFIG_MMU */
#endif /* __MMH_H */

