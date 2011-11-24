/* Procfs helper functionality. */
/* $Id: px.c 19158 2011-05-16 07:56:35Z phth $ */

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#include <nanoutil.h>
#include "wifi_engine.h"
#include "driverenv.h"

#include "px.h"

#include "nrx_stream.h"

#include "m80211_stddefs.h"

#if 0
#define RWLOCK rwlock_t
#define RWLOCK_INIT(X) rwlock_init(X)
#define RWLOCK_RLOCK(X) read_lock(X)
#define RWLOCK_WLOCK(X) write_lock(X)
#define RWLOCK_RUNLOCK(X) read_unlock(X)
#define RWLOCK_WUNLOCK(X) write_unlock(X)
#define RWLOCK_RTRYLOCK(X) read_trylock(X)
#define RWLOCK_WTRYLOCK(X) write_trylock(X)
#elif !defined(CONFIG_PREEMPT) && !defined(CONFIG_PREEMPT_VOLUNTARY)
#define RWLOCK spinlock_t
#define RWLOCK_INIT(X) spin_lock_init(X)
#define RWLOCK_RLOCK(X) spin_lock(X)
#define RWLOCK_WLOCK(X) spin_lock(X)
#define RWLOCK_RUNLOCK(X) spin_unlock(X)
#define RWLOCK_WUNLOCK(X) spin_unlock(X)
#define RWLOCK_RTRYLOCK(X) spin_trylock(X)
#define RWLOCK_WTRYLOCK(X) spin_trylock(X)
#else
#define RWLOCK struct semaphore 
#define RWLOCK_INIT(X) sema_init(X,1)
#define RWLOCK_RLOCK(X) down(X)
#define RWLOCK_WLOCK(X) down(X)
#define RWLOCK_RUNLOCK(X) up(X) 
#define RWLOCK_WUNLOCK(X) up(X)
#define RWLOCK_RTRYLOCK(X) !down_trylock(X)
#define RWLOCK_WTRYLOCK(X) !down_trylock(X) 
#endif

struct nrx_px_softc {
   RWLOCK px_lock; /* lock for member acces */
   WEI_TQ_ENTRY(nrx_px_softc) px_all;
   unsigned int px_open; /* number of open */

   struct proc_dir_entry *px_pde;
   struct proc_dir_entry *px_parent;
   void *px_priv;
   
   unsigned char *px_buf;
   unsigned char px_static[32];
   size_t px_buf_size; /* size of allocated memory */
   size_t px_buf_end;  /* size of data */
   struct nrx_px_entry *px_pe;
   struct inode *px_ino;

   unsigned long px_flags;
#define PX_DIRTY	0
#define PX_DESTROY	1
#define PX_WLOCK	2
};

WEI_TQ_HEAD(, nrx_px_softc) nrx_px_head = WEI_TQ_HEAD_INITIALIZER(nrx_px_head);

void nrx_px_wlock(struct nrx_px_softc *sc)
{
   RWLOCK_WLOCK(&sc->px_lock);
}

void nrx_px_wunlock(struct nrx_px_softc *sc)
{
   RWLOCK_WUNLOCK(&sc->px_lock);
}

int nrx_px_wtrylock(struct nrx_px_softc *sc)
{
   return RWLOCK_WTRYLOCK(&sc->px_lock);
}

void nrx_px_rlock(struct nrx_px_softc *sc)
{
   RWLOCK_RLOCK(&sc->px_lock);
}

void nrx_px_runlock(struct nrx_px_softc *sc)
{
   RWLOCK_RUNLOCK(&sc->px_lock);
}

void *nrx_px_data(struct nrx_px_softc *sc)
{
   return sc->px_buf;
}

size_t nrx_px_size(struct nrx_px_softc *sc)
{
   return sc->px_buf_end;
}

void* nrx_px_priv(struct nrx_px_softc *sc)
{
   return sc->px_priv;
}

int nrx_px_dirty(struct nrx_px_softc *sc)
{
   return test_bit(PX_DIRTY, &sc->px_flags);
}

uintptr_t nrx_px_private(struct nrx_px_softc *sc)
{
   return sc->px_pe->private;
}

static void
nrx_px_remove_locked(struct nrx_px_softc *sc)
{
   struct proc_dir_entry *parent = sc->px_parent;
   struct proc_dir_entry *pde = sc->px_pde;
   struct nrx_px_entry *pe = sc->px_pe;
   
   if(pe->list != NULL) {
      WEI_TQ_REMOVE(pe->list, pe, next);
      pe->list = NULL;
   }
   ASSERT(sc->px_open == 0);
   WEI_TQ_REMOVE(&nrx_px_head, sc, px_all);
   if(sc->px_buf != sc->px_static)
      vfree(sc->px_buf);
   sc->px_buf = NULL;
   nrx_px_wunlock(sc);
   kfree(sc);
   pde->data = NULL;
   remove_proc_entry(pde->name, parent);
   if(pe->flags & NRX_PX_DYNAMIC)
      kfree(pe);
}


static inline void
NRX_PX_SETSIZE(struct nrx_px_softc *sc, size_t size)
{
   ASSERT(size <= sc->px_buf_size);

   sc->px_buf_end = size;
   if(sc->px_pde != NULL)
      sc->px_pde->size = size;
   if(sc->px_ino != NULL) 
      i_size_write(sc->px_ino, size);
}

/* make space for total len bytes */
static int
nrx_px_allocate(struct nrx_px_softc *sc, size_t len)
{
   void *new;
   size_t round;

   if(sc->px_buf_size >= len)
      return 0;
   
   if(len <= sizeof(sc->px_static)) {
      new = sc->px_static;
      len = sizeof(sc->px_static);
   } else {
      round = len % sc->px_pe->blocksize;
      if(round > 0)
         len += sc->px_pe->blocksize - round;

      new = vmalloc(len);
      if(new == NULL) {
         KDEBUG(ERROR, "vmalloc %lu", (unsigned long)len);
         return -ENOMEM;
      }
   }

   if(sc->px_buf != NULL && new != sc->px_buf) {
      memcpy(new, sc->px_buf, nrx_px_size(sc)); 
      if(sc->px_buf != sc->px_static)
         vfree(sc->px_buf);
   }
   sc->px_buf = new;
   sc->px_buf_size = len;
   return 0;
}


static ssize_t
nrx_px_fop_write(struct file *file, 
                 const char *buf, 
                 size_t nbytes, 
                 loff_t *ppos)
{
   struct nrx_px_softc *sc = file->private_data;
   size_t copied;

   if (nbytes == 0)
      return 0;

   if(*ppos < 0)
      return -EINVAL;

   nrx_px_wlock(sc);

   ASSERT(nrx_px_allocate(sc, *ppos + nbytes) == 0);
   if(*ppos > nrx_px_size(sc)) {
      /* XXX this extra variable is a fix for
         missing __cmpdi2 on arm with gcc 2.95.3 */
      size_t l = *ppos - nrx_px_size(sc);      
      memset(sc->px_buf + nrx_px_size(sc), 0, l);
   }

   
   copied = copy_from_user(sc->px_buf + *ppos, buf, nbytes);
   if(copied != 0) {
      nrx_px_wunlock(sc);
      return -EFAULT;
   }
   set_bit(PX_DIRTY, &sc->px_flags);
   
   if(nrx_px_size(sc) < *ppos + nbytes)
      NRX_PX_SETSIZE(sc, *ppos + nbytes);
   *ppos += nbytes;

   nrx_px_wunlock(sc);
   return nbytes;
}

static ssize_t
nrx_px_fop_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
   struct nrx_px_softc *sc = file->private_data;
   ssize_t read = 0;

   KDEBUG(TRACE, "ENTRY");
   if(count == 0) return read;

   nrx_px_rlock(sc);

   if(*ppos < 0 || *ppos > nrx_px_size(sc)) {
      nrx_px_runlock(sc);
      return -EINVAL;
   }

   if(*ppos < nrx_px_size(sc)) {
      size_t count1 = count;
      if(count1 > nrx_px_size(sc) - *ppos)
         count1 = nrx_px_size(sc) - *ppos;
      if (copy_to_user(buf, sc->px_buf + *ppos, count1)) {
         nrx_px_runlock(sc);
         return -EFAULT;
      }
      *ppos += count1;
      count -= count1;
      read += count1;
      buf += count1;
   }

   nrx_px_runlock(sc);
   return read;
}

static int
nrx_px_fop_open(struct inode *ino, struct file *file)
{
   struct nrx_px_softc *sc;

   KDEBUG(TRACE, "ENTRY");
   if(!capable(CAP_SYS_RAWIO))
      return  -EACCES;
   
   sc = PDE(ino)->data;

   nrx_px_wlock(sc);

   if(nrx_px_readonly(file)) {
      if(test_bit(PX_WLOCK, &sc->px_flags)) {
	 nrx_px_wunlock(sc);
	 return -EWOULDBLOCK;
      }
   } else {
      if(sc->px_open > 0) {
	 nrx_px_wunlock(sc);
	 return -EWOULDBLOCK;
      }
      set_bit(PX_WLOCK, &sc->px_flags);
   }
   sc->px_open++;

   ASSERT(sc->px_open == 1 || sc->px_ino == ino);
   sc->px_ino = ino;
   /* this is crappy, we should get called by proc_get_inode, but
    * we're not */
   NRX_PX_SETSIZE(sc, nrx_px_size(sc));

   if(sc->px_pe->open != NULL)
      (*sc->px_pe->open)(sc, ino, file);

   if(file->f_flags & O_TRUNC) {
      NRX_PX_SETSIZE(sc, 0);
   }
   file->private_data = sc;
   nrx_px_wunlock(sc);

   return 0;
}

static int
nrx_px_fop_release(struct inode *inode, struct file *file)
{
   struct nrx_px_softc *sc = file->private_data;

   KDEBUG(TRACE, "ENTRY");

   nrx_px_wlock(sc);
   if(sc->px_pe->release != NULL)
      (*sc->px_pe->release)(sc, inode, file);
   clear_bit(PX_DIRTY, &sc->px_flags);
   clear_bit(PX_WLOCK, &sc->px_flags);

   sc->px_open--;
   if(sc->px_open == 0)
      sc->px_ino = NULL;

   if(test_bit(PX_DESTROY, &sc->px_flags)) {
      nrx_px_remove_locked(sc);
   } else
      nrx_px_wunlock(sc);

   return 0;
}

#ifndef current_fsuid
#define current_fsuid() (current->fsuid)
#endif

/* this is basically just generic_permission, 
   but w/o the extra capability checks */
static int
nrx_px_iop_permission(struct inode *inode, int mask
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,75) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
                      , struct nameidata *nd
#endif
   )
{
   umode_t mode = inode->i_mode;

   if (current_fsuid() == inode->i_uid)
      mode >>= 6;
   else if (in_group_p(inode->i_gid))
      mode >>= 3;

   mask &= MAY_READ|MAY_WRITE|MAY_EXEC;
   if((mode & mask) == mask)
      return 0;

   return -EACCES;
}

/* proc_notify_change with extra tests */
static int
nrx_px_iop_setattr(struct dentry *dentry, struct iattr *iattr)
{
   struct inode *inode = dentry->d_inode;
   struct proc_dir_entry *de = PDE(inode);
   int error;

   error = inode_change_ok(inode, iattr);
   if (error)
      goto out;

   /* Make sure a caller can chmod. */
   if (iattr->ia_valid & ATTR_MODE) {
      if((iattr->ia_mode & S_IRWXU) != (inode->i_mode & S_IRWXU))
         return -EPERM;
#define CHECKMODE(X, Y) ((X) & ~(Y))
      if(CHECKMODE((iattr->ia_mode & S_IRWXG) << 3,
                   inode->i_mode & S_IRWXU))
         return -EPERM;
      if(CHECKMODE((iattr->ia_mode & S_IRWXO) << 6,
                   inode->i_mode & S_IRWXU))
         return -EPERM;
   }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
   if ((iattr->ia_valid & ATTR_SIZE) &&
       iattr->ia_size != i_size_read(inode)) {
      error = vmtruncate(inode, iattr->ia_size);
      if (error)
         return error;
   }

   setattr_copy(inode, iattr);
   mark_inode_dirty(inode);
   error = 0;
#else
   error = inode_setattr(inode, iattr);
   if (error)
      goto out;
#endif
	
   de->uid = inode->i_uid;
   de->gid = inode->i_gid;
   de->mode = inode->i_mode;
  out:
   return error;
}

static struct inode_operations nrx_px_iops = {
   .setattr	= nrx_px_iop_setattr,
   .permission  = nrx_px_iop_permission
};

static struct file_operations nrx_px_ops = {
   .owner = THIS_MODULE,
   .open = nrx_px_fop_open,
   .release = nrx_px_fop_release,
   .read = nrx_px_fop_read,
   .write = nrx_px_fop_write,
   .llseek = default_llseek,
};

int
nrx_px_create(struct nrx_px_entry *pe, 
              void *priv, 
              struct proc_dir_entry *parent)
{
   struct nrx_px_softc *sc;

   if(parent == NULL)
      return -EINVAL;

   sc = kmalloc(sizeof(*sc), GFP_KERNEL);
   if(sc == NULL)
      return -ENOMEM;

   sc->px_open = 0;
   sc->px_flags = 0;
   RWLOCK_INIT(&sc->px_lock);

   sc->px_priv = priv;

   sc->px_pe = pe;
   sc->px_ino = NULL;

   sc->px_buf = NULL;
   sc->px_buf_size = 0;

   sc->px_parent = parent;
   
   sc->px_pde = create_proc_entry(pe->name, 
                                  S_IFREG | pe->mode,
                                  sc->px_parent);
   sc->px_pde->data = sc;
   sc->px_pde->proc_fops = &nrx_px_ops; 
   sc->px_pde->proc_iops = &nrx_px_iops; 

   NRX_PX_SETSIZE(sc, 0);

   if(pe->init != NULL)
      (*pe->init)(sc);

   WEI_TQ_INSERT_TAIL(&nrx_px_head, sc, px_all);

   return 0;
}

struct nrx_px_entry *
nrx_px_create_dynamic(struct net_device *dev,
                      const char *name, 
                      const void *priv, 
                      size_t priv_size,
                      int flags,
                      int (*init)(struct nrx_px_softc*),
                      int (*open)(struct nrx_px_softc*, 
                                  struct inode*, 
                                  struct file*),
                      int (*release)(struct nrx_px_softc*, 
                                     struct inode*, 
                                     struct file*),
                      struct proc_dir_entry *parent)
{
   struct nrx_px_entry *e;
   size_t size = sizeof(*e);
   
   if(priv_size > sizeof(e->private))
      size += priv_size;
   e = kmalloc(size, GFP_KERNEL);
      
   if(e == NULL)
      return NULL;
   memset(e, 0, sizeof(*e));
   strlcpy(e->name, name, sizeof(e->name));
   e->mode = (release == NULL) ? S_IRUSR : (S_IRUSR|S_IWUSR);
   e->blocksize = 1024;
   if(priv_size <= sizeof(e->private))
      e->private = (uintptr_t)priv;
   else {
      e->private = (uintptr_t)(e + 1);
      memcpy((void*)e->private, priv, priv_size);
   }
   e->flags = flags | NRX_PX_DYNAMIC;
   e->init = init;
   e->open = open;
   e->release = release;
   
   e->list = NULL;
   
   if(nrx_px_create(e, dev, parent) == 0)
      return e;
   
   kfree(e);
   return NULL;
}

struct proc_dir_entry *
nrx_px_find(const char *name, struct proc_dir_entry *parent)
{
   struct proc_dir_entry **pde;
   size_t len = strlen(name);
   
   for(pde = &parent->subdir; *pde != NULL; pde = &(*pde)->next) {
      if ((*pde)->namelen != len)
         continue;
      if(memcmp(name, (*pde)->name, len) != 0)
         continue;
      return *pde;
   }
   return NULL;
}

struct proc_dir_entry *
nrx_px_find_by_inum(unsigned long inum)
{
   struct nrx_px_softc *sc;
   
   WEI_TQ_FOREACH(sc, &nrx_px_head, px_all) {
      if(sc->px_pde->low_ino == inum)
         return sc->px_pde;
   }
   return NULL;
}

struct nrx_px_softc*
nrx_px_lookup(struct nrx_px_entry *pe, struct proc_dir_entry *parent)
{
   struct proc_dir_entry *pde;

   if(parent == NULL)
      return NULL;
   
   pde = nrx_px_find(pe->name, parent);
   if(pde == NULL)
      return NULL;

   return pde->data;
}


void
nrx_px_remove(struct nrx_px_entry *pe, struct proc_dir_entry *parent)
{
   struct proc_dir_entry *pde;
   struct nrx_px_softc *sc;
   
   if(parent == NULL)
      return;

   pde = nrx_px_find(pe->name, parent);
   if(pde != NULL) {
      sc = pde->data;
      if(!nrx_px_wtrylock(sc)) {
	 set_bit(PX_DESTROY, &sc->px_flags);
	 return;
      }
      nrx_px_remove_locked(sc);
   } else {
      KDEBUG(TRACE, "failed to find pde %s", pe->name);
   }
}

/*
 * Assume that sc is already locked.
 */
int nrx_px_setsize(struct nrx_px_softc *sc, size_t size)
{
   int ret;

   if(size == 0) {
      memset(sc->px_buf, 0, sc->px_buf_size);
      if(sc->px_buf != sc->px_static)
         vfree(sc->px_buf);
      sc->px_buf = NULL;
      sc->px_buf_size = 0;
   } else {
      ret = nrx_px_allocate(sc, size);
      if(ret < 0) {
         return ret;
      }

      if(size > sc->px_buf_end)
         memset(sc->px_buf + sc->px_buf_end, 0, size - sc->px_buf_end);
   }

   NRX_PX_SETSIZE(sc, size);
   
   return 0;
}

/* append data to end of buffer */
/*
 * Assume that sc is already locked.
 */
int nrx_px_pwrite(struct nrx_px_softc *sc, 
               const void *data, 
               size_t len,
               loff_t offset)
{
   int ret;

   if(offset < 0)
      return -EINVAL;

   ret = nrx_px_allocate(sc, offset + len);
   if(ret < 0) {
      return ret;
   }

   memcpy(sc->px_buf + offset, data, len);
   if(offset + len > nrx_px_size(sc))
      NRX_PX_SETSIZE(sc, offset + len);

   return len;
}

/* make sure the buffer ends with a zero byte */
/* Caller must lock */
int nrx_px_zeroterminate(struct nrx_px_softc *sc)
{
   int ret;
   size_t size;
   char *buf;
   char zero = '\0';

   size = nrx_px_size(sc);
   buf = nrx_px_data(sc);
   
   if(size > 0 && buf[size - 1] == '\0') {
      ret = 0;
   } else {
      ret = nrx_px_append(sc, &zero, sizeof(zero));
   }
   return ret;
}


/* format a string at the end of the buffer */
/* Caller locks sc */
int nrx_px_printf(struct nrx_px_softc *sc, const char *fmt, ...)
{
   va_list ap;
   int len;
   char tmp[1];
   int ret;

   va_start(ap, fmt);
   if(sc->px_buf == NULL)
      len = vsnprintf(tmp, sizeof(tmp), fmt, ap);
   else
      len = vsnprintf(sc->px_buf + sc->px_buf_end, 
                      sc->px_buf_size - sc->px_buf_end, 
                      fmt, ap);
   va_end(ap);
   DE_ASSERT(len >= 0); /* kernel vsnprintf always succeeds */
   if((unsigned int)len + 1 > sc->px_buf_size - sc->px_buf_end) {
      ret = nrx_px_allocate(sc, nrx_px_size(sc) + len + 1);
      if(ret < 0)
         return ret;
      va_start(ap, fmt);
      len = vsnprintf(sc->px_buf + sc->px_buf_end, 
                      sc->px_buf_size - sc->px_buf_end, 
                      fmt, ap);
      va_end(ap);
   }
   NRX_PX_SETSIZE(sc, nrx_px_size(sc) + len);

   return len;
}


static int nrx_px_iop_unlink(struct inode *inode, struct dentry *dentry)
{
   struct proc_dir_entry *pde;
   struct nrx_px_softc *sc;

   KDEBUG(TRACE, "dinode = %ld, dentry = %ld",
          inode->i_ino, 
          dentry->d_inode->i_ino);

   pde = nrx_px_find_by_inum(dentry->d_inode->i_ino);
   if(pde == NULL) {
      KDEBUG(TRACE, "entry not found"); 
      return -EIO;
   }
   sc = pde->data;

   if(!(sc->px_pe->flags & NRX_PX_REMOVABLE))
      return -EPERM;

   KDEBUG(TRACE, "parent->low_ino = %u", sc->px_parent->low_ino);
   nrx_px_remove(sc->px_pe, sc->px_parent);
   
   return 0;
}

static struct inode_operations nrx_px_dir_iops;

struct proc_dir_entry*
nrx_px_mkdir(const char *dirname, struct proc_dir_entry *parent)
{
   struct proc_dir_entry *pde;
   pde = create_proc_entry(dirname, 
                           S_IFDIR | S_IRUGO | S_IXUGO,
                           parent);
   pde->uid = 0;
   pde->gid = 0;
   if(nrx_px_dir_iops.unlink == NULL) {
      nrx_px_dir_iops = *pde->proc_iops;
      nrx_px_dir_iops.unlink = nrx_px_iop_unlink;
   }
   pde->proc_iops = &nrx_px_dir_iops; 
   return pde;
}

static int callout_open(struct nrx_px_softc *sc, void *val)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);
   int retcode;

   if((retcode = (*callout)(sc, 0, val)) == 0)
      return 0;

   nrx_px_setsize(sc, 0);
   nrx_px_printf(sc, "unknown\n");
   
   return retcode;
}

int nrx_px_uint_open(struct nrx_px_softc *sc,
                     struct inode *inode,
                     struct file *file)
{
   unsigned int val;
   struct net_device *dev = nrx_px_priv(sc);

   CHECK_UNPLUG(dev);
   
   if(callout_open(sc, &val) == 0) {
      nrx_px_setsize(sc, 0);
      nrx_px_printf(sc, "%u\n", val);
   }
   return 0;
}

int nrx_px_uint_release(struct nrx_px_softc *sc,
                        struct inode *inode,
                        struct file *file)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);
   unsigned int val;
   char *buf, *end;
   struct net_device *dev = nrx_px_priv(sc);

   CHECK_UNPLUG(dev);
   
   if(!nrx_px_dirty(sc))
      return 0;

   nrx_px_zeroterminate(sc);
   buf = nrx_px_data(sc);

   val = simple_strtoul(buf, &end, 0);
   if(end != buf) {
      (*callout)(sc, 1, &val);
   } else {
      KDEBUG(ERROR, "bad value");
   }
   return 0;
}

int nrx_px_macaddr_open(struct nrx_px_softc *sc,
                        struct inode *inode,
                        struct file *file)
{
   struct nrx_px_macaddr val;
   struct net_device *dev = nrx_px_priv(sc);

   CHECK_UNPLUG(dev);
   
   if(callout_open(sc, &val) == 0) {
      nrx_px_setsize(sc, 0);
      nrx_px_printf(sc, "%02x:%02x:%02x:%02x:%02x:%02x\n", 
                    (unsigned char)val.addr[0],
                    (unsigned char)val.addr[1],
                    (unsigned char)val.addr[2],
                    (unsigned char)val.addr[3],
                    (unsigned char)val.addr[4],
                    (unsigned char)val.addr[5]);
   }
   return 0;
}

int nrx_px_macaddr_release(struct nrx_px_softc *sc,
                           struct inode *inode,
                           struct file *file)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);
   struct nrx_px_macaddr val;
   char *buf;
   unsigned int t[sizeof(val.addr)];
   size_t i;
   struct net_device *dev = nrx_px_priv(sc);

   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(!nrx_px_dirty(sc))
      return 0;

   nrx_px_zeroterminate(sc);
   buf = nrx_px_data(sc);

   if(sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", 
             &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) != 6) {
      KDEBUG(TRACE, "bad value %s", buf);
      return 0;
   }
   
   for(i = 0; i < sizeof(val.addr); i++) {
      if(t[i] > 255) {
         KDEBUG(TRACE, "bad value %s", buf);
         return 0;
      }
      val.addr[i] = t[i];
   }
   
   (*callout)(sc, 1, &val);
   
   return 0;
}

int
nrx_px_uintvec_open(struct nrx_px_softc *sc, 
                    struct inode *inode, 
                    struct file *file)
{
   struct nrx_px_uintvec val;
   size_t i;
   struct net_device *dev = nrx_px_priv(sc);

   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(callout_open(sc, &val) == 0) {
      nrx_px_setsize(sc, 0);
   
      for(i = 0; i < val.size; i++) {
         if(i > 0)
            nrx_px_printf(sc, " ");
         nrx_px_printf(sc, "%u", val.vals[i]);
      }
      nrx_px_printf(sc, "\n");
   }
   return 0;
}

int
nrx_px_uintvec_release(struct nrx_px_softc *sc, 
                       struct inode *inode, 
                       struct file *file)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);

   char *buf, *p;
   struct nrx_px_uintvec val;
   struct net_device *dev = nrx_px_priv(sc);
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(!nrx_px_dirty(sc))
      return 0;
   
   nrx_px_zeroterminate(sc);
   
   buf = nrx_px_data(sc);
   p = buf;
   val.size = 0;
   while(1) {
      int v, pos;
      
      if(sscanf(p, " %u%n", &v, &pos) != 1)
         break;
      p += pos;
      if(val.size < ARRAY_SIZE(val.vals))
         val.vals[val.size++] = v;
   }

   (*callout)(sc, 1, &val);
   
   return 0;
}

int
nrx_px_rates_open(struct nrx_px_softc *sc, 
                  struct inode *inode, 
                  struct file *file)
{
   we_ratemask_t val;
   int i;
   int f = 0;
   struct net_device *dev = nrx_px_priv(sc);
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   if(callout_open(sc, &val) == 0) {
      nrx_px_setsize(sc, 0);
   
      WE_RATEMASK_FOREACH(i, val) {
         unsigned int rate, a, b;
         rate = WiFiEngine_rate_native2bps(i);
         if(f)
            nrx_px_printf(sc, " ");
         rate /= 10000;
         a = rate / 100;
         b = rate % 100;
         switch(b) {
            case 0:
               nrx_px_printf(sc, "%u", a);
               break;
            case 25:
            case 75:
               nrx_px_printf(sc, "%u.%u", a, b);
               break;
            case 50:
               nrx_px_printf(sc, "%u.5", a);
               break;
         }
         f = 1;
      }
      nrx_px_printf(sc, "\n");
   }
   
   return 0;
}

static we_xmit_rate_t
string2rate(char *s, char **end)
{
   unsigned int a, b;
   int n;
   
   if(sscanf(s, "%u.%u%n", &a, &b, &n) == 2) {
   } else if(sscanf(s, "%u%n", &a, &n) == 1) {
      b = 0;
   } else {
      *end = s;
      return WE_XMIT_RATE_INVALID;
   }

   *end = s + n;

   switch(b) {
      case 0:
         break;
      case 25:
      case 250:
      case 2500:
      case 25000:
      case 250000:
      case 2500000:
      case 25000000:
      case 250000000:
      case 2500000000U:
         b = 25;
         break;
      case 5:
      case 50:
      case 500:
      case 5000:
      case 50000:
      case 500000:
      case 5000000:
      case 50000000:
      case 500000000:
         b = 50;
         break;
      case 75:
      case 750:
      case 7500:
      case 75000:
      case 750000:
      case 7500000:
      case 75000000:
      case 750000000:
         b = 75;
         break;
   }
   return WiFiEngine_rate_bps2native(1000000 * a + 10000 * b);
}

int
nrx_px_rates_release(struct nrx_px_softc *sc, 
                       struct inode *inode, 
                       struct file *file)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);

   char *buf, *end;
   we_ratemask_t val;
   we_xmit_rate_t r;
   struct net_device *dev = nrx_px_priv(sc);
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);
   
   if(!nrx_px_dirty(sc))
      return 0;

   nrx_px_zeroterminate(sc);
   
   buf = nrx_px_data(sc);
   WE_RATEMASK_CLEAR(val);
   while(1) {
      r = string2rate(buf, &end);
      if(r == WE_XMIT_RATE_INVALID)
         break;
      WE_RATEMASK_SETRATE(val, r);
      buf = end;
   }
   
   (*callout)(sc, 1, &val);
   
   return 0;
}

int nrx_px_string_open(struct nrx_px_softc *sc,
                       struct inode *inode,
                       struct file *file)
{
   struct net_device *dev = nrx_px_priv(sc);
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);

   callout_open(sc, NULL);

   return 0;
}

int nrx_px_string_release(struct nrx_px_softc *sc,
                          struct inode *inode,
                          struct file *file)
{
   nrx_px_callout callout = (nrx_px_callout)nrx_px_private(sc);
   struct net_device *dev = nrx_px_priv(sc);
   
   KDEBUG(TRACE, "ENTRY");
   CHECK_UNPLUG(dev);
   
   if(!nrx_px_dirty(sc))
      return 0;

   nrx_px_zeroterminate(sc);

   (*callout)(sc, 1, NULL);
   
   return 0;
}

int
nrx_px_read_file(struct nrx_px_softc *sc, 
		 const char *filename)
{
   struct nrx_stream *fd;
   int status;
   loff_t off;
   ssize_t n;

   if((status = nrx_stream_open_file(filename, O_RDONLY, 0600, &fd)) < 0)
      return status;
   off = nrx_stream_lseek(fd, 0, 2);
   if(off < 0) {
      KDEBUG(ERROR, "EXIT %d", (int)off);
      nrx_stream_close(fd);
      return off;
   }
   if((status = nrx_px_allocate(sc, off)) < 0) {
      KDEBUG(ERROR, "EXIT %d", status);
      nrx_stream_close(fd);
      return status;
   }
   off = nrx_stream_lseek(fd, 0, 0);
   if(off < 0) {
      KDEBUG(ERROR, "EXIT %d", (int)off);
      nrx_stream_close(fd);
      return off;
   }
   
   off = 0;
   n = 0;
   while(off < sc->px_buf_size) {
      n = nrx_stream_read(fd, sc->px_buf + off, sc->px_buf_size - off);
      KDEBUG(TRACE, "read = %zd", n);
      if(n <= 0)
	 break;
      off += n;
   }
   nrx_stream_close(fd);
   NRX_PX_SETSIZE(sc, off);
   KDEBUG(TRACE, "EXIT %d", (int)n);
   return n;
}
