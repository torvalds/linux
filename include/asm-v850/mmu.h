/* Copyright (C) 2002, David McCullough <davidm@snapgear.com> */

#ifndef __V850_MMU_H__
#define __V850_MMU_H__

struct mm_rblock_struct {
	int		size;
	int		refcount;
	void	*kblock;
};

struct mm_tblock_struct {
	struct mm_rblock_struct	*rblock;
	struct mm_tblock_struct	*next;
};

typedef struct {
	struct mm_tblock_struct	tblock;
	unsigned long			end_brk;
} mm_context_t;

#endif /* __V850_MMU_H__ */
