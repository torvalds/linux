/* Copyright (C) 2007 Nanoradio AB */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#include "nanoutil.h"

#if (defined CONFIG_KERNEL_LOCK) || LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
#include <linux/smp_lock.h>    /* For (un)lock_kernel */
#else
#include <linux/mutex.h>
static DEFINE_MUTEX(nanofs_mutex);
#endif

static inline int
nrx_file_open(const char *filename, 
	      int flags, 
	      mode_t mode, 
	      struct file **retfile)
{
   struct file *f;

   f = filp_open(filename, flags | O_NOFOLLOW, mode);
   if(f == NULL) {
      KDEBUG(ERROR, "failed to open %s", filename);
      return -ENOENT;
   }
   if(IS_ERR(f)) {
      KDEBUG(ERROR, "error opening %s", filename);
      return PTR_ERR(f);
   }
   if(f->f_op == NULL) {
      filp_close(f, NULL); /* XXX correct? */
      KDEBUG(ERROR, "bad file descriptor");
      return -EINVAL;
   }
   f->f_pos = 0;
   *retfile = f;
   return 0;
}

static inline int
nrx_file_close(struct file *f)
{
   filp_close(f, NULL);
   return 0;
}

static inline ssize_t
nrx_file_read(struct file *f, void *buf, size_t len)
{
   ssize_t l;
   mm_segment_t save;

   if(f->f_op->read == NULL)
      return -EOPNOTSUPP;
   save = get_fs();
   set_fs(KERNEL_DS);
   l = (*f->f_op->read)(f, buf, len, &f->f_pos);
   set_fs(save);
   return l;
}

static inline ssize_t
nrx_file_write(struct file *f, const void *buf, size_t len)
{
   ssize_t l;
   mm_segment_t save;

   if(f->f_op->write == NULL)
      return -EOPNOTSUPP;
   save = get_fs();
   set_fs(KERNEL_DS);
   l = (*f->f_op->write)(f, buf, len, &f->f_pos);
   set_fs(save);
   return l;
}

static inline int
nrx_file_flush(struct file *f)
{
   int ret = 0;
   if(f->f_op->flush != NULL) {
#if (defined CONFIG_KERNEL_LOCK) || LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
      lock_kernel();
#else
      mutex_lock(&nanofs_mutex);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
      ret = (*f->f_op->flush)(f, NULL); /* ok to pass NULL? */
#else
      ret = (*f->f_op->flush)(f);
#endif

#if (defined CONFIG_KERNEL_LOCK) || LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
      unlock_kernel();
#else
      mutex_unlock(&nanofs_mutex);
#endif
   }
   return ret;
}

static inline loff_t
nrx_file_lseek(struct file *f, loff_t offset, int whence)
{
   loff_t retval;

#if (defined CONFIG_KERNEL_LOCK) || LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
      lock_kernel();
#else
      mutex_lock(&nanofs_mutex);
#endif
   if (f->f_op->llseek != NULL)
      retval = (*f->f_op->llseek)(f, offset, whence);
   else
      retval = default_llseek(f, offset, whence);
#if (defined CONFIG_KERNEL_LOCK) || LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
      unlock_kernel();
#else
      mutex_unlock(&nanofs_mutex);
#endif

   return retval;
}

/* ------------------------------------------------------------ */

struct nrx_stream {
   int     (*close)(struct nrx_stream*);
   loff_t  (*lseek)(struct nrx_stream*, loff_t, int);
   ssize_t (*read)(struct nrx_stream*, void*, size_t);
   ssize_t (*write)(struct nrx_stream*, const void*, size_t);
   int     (*flush)(struct nrx_stream*);
};

struct nrx_stream_buf {
   struct nrx_stream stream;
   unsigned char *buf;
   size_t size;
   loff_t pos;
};

struct nrx_stream_file {
   struct nrx_stream stream;
   struct file *file;
};

/*!
 * @brief Closes a previously opened stream.
 *
 * @param stream References the stream.
 *
 * @return Zero on success, or a negative errno number.
 */
int nrx_stream_close(struct nrx_stream *stream)
{
   return (*stream->close)(stream);
}

/*!
 * @brief Seeks to a specified position on a stream.
 *
 * @param stream References the stream.
 * @param offset Offset to seek to relative to position given by whence.
 * @param whence Position to seek from, 
 *   0 = start of stream, 
 *   1 = current position, 
 *   2 = end of stream.
 *
 * @return Position in stream, or a negative errno number.
 */
loff_t nrx_stream_lseek(struct nrx_stream *stream, loff_t offset, int whence)
{
   KDEBUG(TRACE, "offset = %ld, whence = %d", (long)offset, whence);
   return (*stream->lseek)(stream, offset, whence);
}

/*!
 * @brief Reads a specified number of bytes from current position in stream.
 *
 * @param stream References the stream.
 * @param buf The buffer to receive the read data.
 * @param len Number of bytes to read.
 *
 * @return Number of bytes read, or a negative errno number.
 */
ssize_t nrx_stream_read(struct nrx_stream *stream, void *buf, size_t len)
{
   KDEBUG(TRACE, "len = %lu", (unsigned long)len);
   return (*stream->read)(stream, buf, len);
}

/*!
 * @brief Writes a specified number of bytes to current position in stream.
 *
 * @param stream References the stream.
 * @param buf The buffer that holds the data to write.
 * @param len Size of buf.
 *
 * @return Number of bytes written, or a negative errno number.
 */
ssize_t nrx_stream_write(struct nrx_stream *stream, const void *buf, size_t len)
{
   KDEBUG(TRACE, "len = %lu", (unsigned long)len);
   if(stream->write == NULL)
      return -EOPNOTSUPP;
   return (*stream->write)(stream, buf, len);
}

/*!
 * @brief Flushes written data to medium.
 *
 * @param stream References the stream.
 *
 * @return Zero on success, or a negative errno number.
 */
int nrx_stream_flush(struct nrx_stream *stream)
{
   if(stream->flush == NULL)
      return 0;
   return (*stream->flush)(stream);
}

static loff_t
stream_buf_lseek(struct nrx_stream *stream, loff_t offset, int whence)
{
   struct nrx_stream_buf *dstream = (void*)stream;

   switch(whence) {
   case 0: /* SET */
      dstream->pos = offset;
      break;
   case 1: /* CUR */
      dstream->pos += offset;
      break;
   case 2: /* END */
      dstream->pos = dstream->size + offset;
      break;
   default:
      return -EINVAL;
   }
   return dstream->pos;
}

static ssize_t
stream_buf_read(struct nrx_stream *stream, void *buf, size_t len)
{
   struct nrx_stream_buf *dstream = (void*)stream;

   if(dstream->pos > dstream->size)
      return 0;
   if(dstream->pos + len > dstream->size)
      len = dstream->size - dstream->pos;
   memcpy(buf, dstream->buf + dstream->pos, len);
   dstream->pos += len;
   return len;
}

static int
stream_buf_close(struct nrx_stream *s)
{
   kfree(s);
   return 0;
}

/*!
 * @brief Opens a fixed size buffer as a stream.
 *
 * @param buf Points to the buffer.
 * @param len Size of buf in bytes.
 * @param stream Returns a stream object.
 *
 * @return Zero on success, or -ENOMEM if there was a memory error.
 */
int
nrx_stream_open_buf(void *buf, size_t len, struct nrx_stream **stream)
{
   struct nrx_stream_buf *s = kmalloc(sizeof(*s), GFP_KERNEL);
   if(s == NULL)
      return -ENOMEM;
   s->buf = buf;
   s->size = len;
   s->pos = 0;
   s->stream.close = stream_buf_close;
   s->stream.read = stream_buf_read;
   s->stream.lseek = stream_buf_lseek;
   s->stream.write = NULL;
   s->stream.flush = NULL;
   *stream = &s->stream;
   return 0;
}

static int
stream_file_close(struct nrx_stream *s)
{
   struct nrx_stream_file *fs = (void*)s;
   nrx_file_close(fs->file);
   kfree(s);
   return 0;
}

static ssize_t
stream_file_read(struct nrx_stream *s, void *buf, size_t len)
{
   struct nrx_stream_file *fs = (void*)s;
   return nrx_file_read(fs->file, buf, len);
}

static ssize_t
stream_file_write(struct nrx_stream *s, const void *buf, size_t len)
{
   struct nrx_stream_file *fs = (void*)s;
   return nrx_file_write(fs->file, buf, len);
}

static loff_t
stream_file_lseek(struct nrx_stream *s, loff_t offset, int whence)
{
   struct nrx_stream_file *fs = (void*)s;
   return nrx_file_lseek(fs->file, offset, whence);
}

static int
stream_file_flush(struct nrx_stream *s)
{
   struct nrx_stream_file *fs = (void*)s;
   return nrx_file_flush(fs->file);
}

/*!
 * @brief Opens a file as a stream.
 *
 * @param filename The path to the file to open.
 * @param flags Flags to pass to open (O_RDONLY etc).
 * @param mode Mode of the file to open, only meaningful 
 *             if flags include O_CREAT.
 * @param stream Returns a stream object.
 *
 * @return Zero on success, or a negative errno number.
 */
int
nrx_stream_open_file(const char *filename, 
		     int flags, 
		     mode_t mode, 
		     struct nrx_stream **stream)
{
   int ret;
   struct nrx_stream_file *s = kmalloc(sizeof(*s), GFP_KERNEL);
   if(s == NULL)
      return -ENOMEM;
   ret = nrx_file_open(filename, flags, mode, &s->file);
   if(ret < 0) {
      kfree(s);
      return ret;
   }
   s->stream.close = stream_file_close;
   s->stream.read = stream_file_read;
   s->stream.write = stream_file_write;
   s->stream.lseek = stream_file_lseek;
   s->stream.flush = stream_file_flush;
   *stream = &s->stream;
   return 0;
}
