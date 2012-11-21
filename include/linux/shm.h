#ifndef _LINUX_SHM_H_
#define _LINUX_SHM_H_

#include <asm/page.h>
#include <uapi/linux/shm.h>

#define SHMALL (SHMMAX/PAGE_SIZE*(SHMMNI/16)) /* max shm system wide (pages) */
#include <asm/shmparam.h>
struct shmid_kernel /* private to the kernel */
{	
	struct kern_ipc_perm	shm_perm;
	struct file *		shm_file;
	unsigned long		shm_nattch;
	unsigned long		shm_segsz;
	time_t			shm_atim;
	time_t			shm_dtim;
	time_t			shm_ctim;
	pid_t			shm_cprid;
	pid_t			shm_lprid;
	struct user_struct	*mlock_user;

	/* The task created the shm object.  NULL if the task is dead. */
	struct task_struct	*shm_creator;
};

/* shm_mode upper byte flags */
#define	SHM_DEST	01000	/* segment will be destroyed on last detach */
#define SHM_LOCKED      02000   /* segment will not be swapped */
#define SHM_HUGETLB     04000   /* segment will use huge TLB pages */
#define SHM_NORESERVE   010000  /* don't check for reservations */

#ifdef CONFIG_SYSVIPC
long do_shmat(int shmid, char __user *shmaddr, int shmflg, unsigned long *addr,
	      unsigned long shmlba);
extern int is_file_shm_hugepages(struct file *file);
extern void exit_shm(struct task_struct *task);
#else
static inline long do_shmat(int shmid, char __user *shmaddr,
			    int shmflg, unsigned long *addr,
			    unsigned long shmlba)
{
	return -ENOSYS;
}
static inline int is_file_shm_hugepages(struct file *file)
{
	return 0;
}
static inline void exit_shm(struct task_struct *task)
{
}
#endif

#endif /* _LINUX_SHM_H_ */
