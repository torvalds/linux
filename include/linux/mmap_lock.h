#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

static inline void mmap_init_lock(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_sem);
}

static inline void mmap_write_lock(struct mm_struct *mm)
{
	down_write(&mm->mmap_sem);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	return down_write_killable(&mm->mmap_sem);
}

static inline bool mmap_write_trylock(struct mm_struct *mm)
{
	return down_write_trylock(&mm->mmap_sem) != 0;
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	up_write(&mm->mmap_sem);
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
	downgrade_write(&mm->mmap_sem);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_sem);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	return down_read_killable(&mm->mmap_sem);
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	return down_read_trylock(&mm->mmap_sem) != 0;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_sem);
}

#endif /* _LINUX_MMAP_LOCK_H */
