/* proc extensions */

#ifndef __nrx_px_h__
#define __nrx_px_h__

#include "nanonet.h"
#include "systypes.h" /* XXX uintptr_t */
#include <linux/fs.h>

struct nrx_px_softc;

struct nrx_px_entry;
WEI_TQ_HEAD(nrx_px_entry_head, nrx_px_entry);

struct nrx_px_entry {
   char name[64];
   mode_t mode;
   size_t blocksize;
   uintptr_t private;
   uint32_t flags;
   struct nrx_px_entry_head *list;
   WEI_TQ_ENTRY(nrx_px_entry) next;
#define NRX_PX_REMOVABLE (1 << 0)
#define NRX_PX_DYNAMIC   (1 << 1)
   int (*init)(struct nrx_px_softc*);
   int (*open)(struct nrx_px_softc*, struct inode*, struct file*);
   int (*release)(struct nrx_px_softc*, struct inode*, struct file*);
};

void nrx_px_wlock(struct nrx_px_softc*);
void nrx_px_wunlock(struct nrx_px_softc*);
void nrx_px_rlock(struct nrx_px_softc*);
void nrx_px_runlock(struct nrx_px_softc*);

int nrx_px_create(struct nrx_px_entry*, void*, struct proc_dir_entry*);
void nrx_px_remove(struct nrx_px_entry*, struct proc_dir_entry*);
struct nrx_px_softc* nrx_px_lookup(struct nrx_px_entry*, struct proc_dir_entry*);

void *nrx_px_data(struct nrx_px_softc *sc);
size_t nrx_px_size(struct nrx_px_softc *sc);
int nrx_px_setsize(struct nrx_px_softc *sc, size_t size) WARN_UNUSED;
int nrx_px_pwrite(struct nrx_px_softc*, const void*, size_t, loff_t) WARN_UNUSED;

int nrx_px_dirty(struct nrx_px_softc*);

/* append data to end of buffer */
static inline int nrx_px_append(struct nrx_px_softc *sc, const void *data, size_t len)
{
   return nrx_px_pwrite(sc, data, len, nrx_px_size(sc));
} WARN_UNUSED

/* copy data to buffer */
static inline int nrx_px_copy(struct nrx_px_softc *sc, const void *data, size_t len) 
{
   int ret;
   ret = nrx_px_pwrite(sc, data, len, 0);
   if(ret < 0)
      return ret;

   ret = nrx_px_setsize(sc, len);
   if(ret < 0)
      return ret;

   return len;
} WARN_UNUSED

uintptr_t nrx_px_private(struct nrx_px_softc *sc);

int nrx_px_printf(struct nrx_px_softc *sc, const char *fmt, ...) WARN_UNUSED;
int nrx_px_zeroterminate(struct nrx_px_softc*) WARN_UNUSED;

void *nrx_px_priv(struct nrx_px_softc*);

/* doesn't really belong here, but what the heck */
static inline int nrx_px_readonly(struct file *fp)
{
   return (fp->f_flags & O_ACCMODE) == O_RDONLY;
}

int nrx_px_read_file(struct nrx_px_softc *sc, const char *filename);

struct nrx_px_entry *
nrx_px_create_dynamic(struct net_device*,
                      const char*, 
                      const void*, 
                      size_t,
                      int,
                      int (*)(struct nrx_px_softc*),
                      int (*)(struct nrx_px_softc*,
                              struct inode*,
                              struct file*),
                      int (*)(struct nrx_px_softc*,
                              struct inode*,
                              struct file*),
                      struct proc_dir_entry*);

struct proc_dir_entry* nrx_px_mkdir(const char*, struct proc_dir_entry*);

typedef int (*nrx_px_callout)(struct nrx_px_softc*, int write, void *ptr);

/* === callout handlers === */

int nrx_px_uint_open(struct nrx_px_softc*, struct inode*, struct file*);
int nrx_px_uint_release(struct nrx_px_softc*, struct inode*, struct file*);

struct nrx_px_macaddr {
   unsigned char addr[6];
};

int nrx_px_macaddr_open(struct nrx_px_softc*, struct inode*, struct file*);
int nrx_px_macaddr_release(struct nrx_px_softc*, struct inode*, struct file*);

struct nrx_px_uintvec {
   size_t size;
   unsigned int vals[32];
};

int nrx_px_uintvec_open(struct nrx_px_softc*, struct inode*, struct file*);
int nrx_px_uintvec_release(struct nrx_px_softc*, struct inode*, struct file*);

int nrx_px_string_open(struct nrx_px_softc*, struct inode*, struct file*);
int nrx_px_string_release(struct nrx_px_softc*, struct inode*, struct file*);

int nrx_px_rates_open(struct nrx_px_softc*, struct inode*, struct file*);
int nrx_px_rates_release(struct nrx_px_softc*, struct inode*, struct file*);

#endif /* __nrx_px_h__ */

