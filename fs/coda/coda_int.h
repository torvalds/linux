#ifndef _CODA_INT_
#define _CODA_INT_

struct dentry;
struct file;

extern struct file_system_type coda_fs_type;
extern unsigned long coda_timeout;
extern int coda_hard;
extern int coda_fake_statfs;

void coda_destroy_inodecache(void);
int coda_init_inodecache(void);
int coda_fsync(struct file *coda_file, int datasync);
void coda_sysctl_init(void);
void coda_sysctl_clean(void);

#endif  /*  _CODA_INT_  */


