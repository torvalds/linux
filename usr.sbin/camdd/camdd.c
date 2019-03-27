/*-
 * Copyright (c) 1997-2007 Kenneth D. Merry
 * Copyright (c) 2013, 2014, 2015 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Ken Merry           (Spectra Logic Corporation)
 */

/*
 * This is eventually intended to be:
 * - A basic data transfer/copy utility
 * - A simple benchmark utility
 * - An example of how to use the asynchronous pass(4) driver interface.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/sbuf.h>
#include <sys/stat.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <vm/vm.h>
#include <machine/bus.h>
#include <sys/bus.h>
#include <sys/bus_dma.h>
#include <sys/mtio.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <pthread.h>
#include <assert.h>
#include <bsdxml.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/smp_all.h>
#include <camlib.h>
#include <mtlib.h>
#include <zlib.h>

typedef enum {
	CAMDD_CMD_NONE		= 0x00000000,
	CAMDD_CMD_HELP		= 0x00000001,
	CAMDD_CMD_WRITE		= 0x00000002,
	CAMDD_CMD_READ		= 0x00000003
} camdd_cmdmask;

typedef enum {
	CAMDD_ARG_NONE		= 0x00000000,
	CAMDD_ARG_VERBOSE	= 0x00000001,
	CAMDD_ARG_DEVICE	= 0x00000002,
	CAMDD_ARG_BUS		= 0x00000004,
	CAMDD_ARG_TARGET	= 0x00000008,
	CAMDD_ARG_LUN		= 0x00000010,
	CAMDD_ARG_UNIT		= 0x00000020,
	CAMDD_ARG_TIMEOUT	= 0x00000040,
	CAMDD_ARG_ERR_RECOVER	= 0x00000080,
	CAMDD_ARG_RETRIES	= 0x00000100
} camdd_argmask;

typedef enum {
	CAMDD_DEV_NONE		= 0x00,
	CAMDD_DEV_PASS		= 0x01,
	CAMDD_DEV_FILE		= 0x02
} camdd_dev_type;

struct camdd_io_opts {
	camdd_dev_type	dev_type;
	char		*dev_name;
	uint64_t	blocksize;
	uint64_t	queue_depth;
	uint64_t	offset;
	int		min_cmd_size;
	int		write_dev;
	uint64_t	debug;
};

typedef enum {
	CAMDD_BUF_NONE,
	CAMDD_BUF_DATA,
	CAMDD_BUF_INDIRECT
} camdd_buf_type;

struct camdd_buf_indirect {
	/*
	 * Pointer to the source buffer.
	 */
	struct camdd_buf *src_buf;

	/*
	 * Offset into the source buffer, in bytes.
	 */
	uint64_t	  offset;
	/*
	 * Pointer to the starting point in the source buffer.
	 */
	uint8_t		 *start_ptr;

	/*
	 * Length of this chunk in bytes.
	 */
	size_t		  len;
};

struct camdd_buf_data {
	/*
	 * Buffer allocated when we allocate this camdd_buf.  This should
	 * be the size of the blocksize for this device.
	 */
	uint8_t			*buf;

	/*
	 * The amount of backing store allocated in buf.  Generally this
	 * will be the blocksize of the device.
	 */
	uint32_t		 alloc_len;

	/*
	 * The amount of data that was put into the buffer (on reads) or
	 * the amount of data we have put onto the src_list so far (on
	 * writes).
	 */
	uint32_t		 fill_len;

	/*
	 * The amount of data that was not transferred.
	 */
	uint32_t		 resid;

	/*
	 * Starting byte offset on the reader.
	 */
	uint64_t		 src_start_offset;
	
	/*
	 * CCB used for pass(4) device targets.
	 */
	union ccb		 ccb;

	/*
	 * Number of scatter/gather segments.
	 */
	int			 sg_count;

	/*
	 * Set if we had to tack on an extra buffer to round the transfer
	 * up to a sector size.
	 */
	int			 extra_buf;

	/*
	 * Scatter/gather list used generally when we're the writer for a
	 * pass(4) device. 
	 */
	bus_dma_segment_t	*segs;

	/*
	 * Scatter/gather list used generally when we're the writer for a
	 * file or block device;
	 */
	struct iovec		*iovec;
};

union camdd_buf_types {
	struct camdd_buf_indirect	indirect;
	struct camdd_buf_data		data;
};

typedef enum {
	CAMDD_STATUS_NONE,
	CAMDD_STATUS_OK,
	CAMDD_STATUS_SHORT_IO,
	CAMDD_STATUS_EOF,
	CAMDD_STATUS_ERROR
} camdd_buf_status;

struct camdd_buf {
	camdd_buf_type		 buf_type;
	union camdd_buf_types	 buf_type_spec;

	camdd_buf_status	 status;

	uint64_t		 lba;
	size_t			 len;

	/*
	 * A reference count of how many indirect buffers point to this
	 * buffer.
	 */
	int			 refcount;

	/*
	 * A link back to our parent device.
	 */
	struct camdd_dev	*dev;
	STAILQ_ENTRY(camdd_buf)  links;
	STAILQ_ENTRY(camdd_buf)  work_links;

	/*
	 * A count of the buffers on the src_list.
	 */
	int			 src_count;

	/*
	 * List of buffers from our partner thread that are the components
	 * of this buffer for the I/O.  Uses src_links.
	 */
	STAILQ_HEAD(,camdd_buf)	 src_list;
	STAILQ_ENTRY(camdd_buf)  src_links;
};

#define	NUM_DEV_TYPES	2

struct camdd_dev_pass {
	int			 scsi_dev_type;
	int			 protocol;
	struct cam_device	*dev;
	uint64_t		 max_sector;
	uint32_t		 block_len;
	uint32_t		 cpi_maxio;
};

typedef enum {
	CAMDD_FILE_NONE,
	CAMDD_FILE_REG,
	CAMDD_FILE_STD,
	CAMDD_FILE_PIPE,
	CAMDD_FILE_DISK,
	CAMDD_FILE_TAPE,
	CAMDD_FILE_TTY,
	CAMDD_FILE_MEM
} camdd_file_type;

typedef enum {
	CAMDD_FF_NONE 		= 0x00,
	CAMDD_FF_CAN_SEEK	= 0x01
} camdd_file_flags;

struct camdd_dev_file {
	int			 fd;
	struct stat		 sb;
	char			 filename[MAXPATHLEN + 1];
	camdd_file_type		 file_type;
	camdd_file_flags	 file_flags;
	uint8_t			*tmp_buf;
};

struct camdd_dev_block {
	int			 fd;
	uint64_t		 size_bytes;
	uint32_t		 block_len;
};

union camdd_dev_spec {
	struct camdd_dev_pass	pass;
	struct camdd_dev_file	file;
	struct camdd_dev_block	block;
};

typedef enum {
	CAMDD_DEV_FLAG_NONE		= 0x00,
	CAMDD_DEV_FLAG_EOF		= 0x01,
	CAMDD_DEV_FLAG_PEER_EOF		= 0x02,
	CAMDD_DEV_FLAG_ACTIVE		= 0x04,
	CAMDD_DEV_FLAG_EOF_SENT		= 0x08,
	CAMDD_DEV_FLAG_EOF_QUEUED	= 0x10
} camdd_dev_flags;

struct camdd_dev {
	camdd_dev_type		 dev_type;
	union camdd_dev_spec	 dev_spec;
	camdd_dev_flags		 flags;
	char			 device_name[MAXPATHLEN+1];
	uint32_t		 blocksize;
	uint32_t		 sector_size;
	uint64_t		 max_sector;
	uint64_t		 sector_io_limit;
	int			 min_cmd_size;
	int			 write_dev;
	int			 retry_count;
	int			 io_timeout;
	int			 debug;
	uint64_t		 start_offset_bytes;
	uint64_t		 next_io_pos_bytes;
	uint64_t		 next_peer_pos_bytes;
	uint64_t		 next_completion_pos_bytes;
	uint64_t		 peer_bytes_queued;
	uint64_t		 bytes_transferred;
	uint32_t		 target_queue_depth;
	uint32_t		 cur_active_io;
	uint8_t			*extra_buf;
	uint32_t		 extra_buf_len;
	struct camdd_dev	*peer_dev;
	pthread_mutex_t		 mutex;
	pthread_cond_t		 cond;
	int			 kq;

	int			 (*run)(struct camdd_dev *dev);
	int			 (*fetch)(struct camdd_dev *dev);

	/*
	 * Buffers that are available for I/O.  Uses links.
	 */
	STAILQ_HEAD(,camdd_buf)	 free_queue;

	/*
	 * Free indirect buffers.  These are used for breaking a large
	 * buffer into multiple pieces.
	 */
	STAILQ_HEAD(,camdd_buf)	 free_indirect_queue;

	/*
	 * Buffers that have been queued to the kernel.  Uses links.
	 */
	STAILQ_HEAD(,camdd_buf)	 active_queue;

	/*
	 * Will generally contain one of our buffers that is waiting for enough
	 * I/O from our partner thread to be able to execute.  This will
	 * generally happen when our per-I/O-size is larger than the
	 * partner thread's per-I/O-size.  Uses links.
	 */
	STAILQ_HEAD(,camdd_buf)	 pending_queue;

	/*
	 * Number of buffers on the pending queue
	 */
	int			 num_pending_queue;

	/*
	 * Buffers that are filled and ready to execute.  This is used when
	 * our partner (reader) thread sends us blocks that are larger than
	 * our blocksize, and so we have to split them into multiple pieces.
	 */
	STAILQ_HEAD(,camdd_buf)	 run_queue;

	/*
	 * Number of buffers on the run queue.
	 */
	int			 num_run_queue;

	STAILQ_HEAD(,camdd_buf)	 reorder_queue;

	int			 num_reorder_queue;

	/*
	 * Buffers that have been queued to us by our partner thread
	 * (generally the reader thread) to be written out.  Uses
	 * work_links.
	 */
	STAILQ_HEAD(,camdd_buf)	 work_queue;

	/*
	 * Buffers that have been completed by our partner thread.  Uses
	 * work_links.
	 */
	STAILQ_HEAD(,camdd_buf)	 peer_done_queue;

	/*
	 * Number of buffers on the peer done queue.
	 */
	uint32_t		 num_peer_done_queue;

	/*
	 * A list of buffers that we have queued to our peer thread.  Uses
	 * links.
	 */
	STAILQ_HEAD(,camdd_buf)	 peer_work_queue;

	/*
	 * Number of buffers on the peer work queue.
	 */
	uint32_t		 num_peer_work_queue;
};

static sem_t camdd_sem;
static sig_atomic_t need_exit = 0;
static sig_atomic_t error_exit = 0;
static sig_atomic_t need_status = 0;

#ifndef min
#define	min(a, b) (a < b) ? a : b
#endif


/* Generically useful offsets into the peripheral private area */
#define ppriv_ptr0 periph_priv.entries[0].ptr
#define ppriv_ptr1 periph_priv.entries[1].ptr
#define ppriv_field0 periph_priv.entries[0].field
#define ppriv_field1 periph_priv.entries[1].field

#define	ccb_buf	ppriv_ptr0

#define	CAMDD_FILE_DEFAULT_BLOCK	524288
#define	CAMDD_FILE_DEFAULT_DEPTH	1
#define	CAMDD_PASS_MAX_BLOCK		1048576
#define	CAMDD_PASS_DEFAULT_DEPTH	6
#define	CAMDD_PASS_RW_TIMEOUT		60 * 1000

static int parse_btl(char *tstr, int *bus, int *target, int *lun,
		     camdd_argmask *arglst);
void camdd_free_dev(struct camdd_dev *dev);
struct camdd_dev *camdd_alloc_dev(camdd_dev_type dev_type,
				  struct kevent *new_ke, int num_ke,
				  int retry_count, int timeout);
static struct camdd_buf *camdd_alloc_buf(struct camdd_dev *dev,
					 camdd_buf_type buf_type);
void camdd_release_buf(struct camdd_buf *buf);
struct camdd_buf *camdd_get_buf(struct camdd_dev *dev, camdd_buf_type buf_type);
int camdd_buf_sg_create(struct camdd_buf *buf, int iovec,
			uint32_t sector_size, uint32_t *num_sectors_used,
			int *double_buf_needed);
uint32_t camdd_buf_get_len(struct camdd_buf *buf);
void camdd_buf_add_child(struct camdd_buf *buf, struct camdd_buf *child_buf);
int camdd_probe_tape(int fd, char *filename, uint64_t *max_iosize,
		     uint64_t *max_blk, uint64_t *min_blk, uint64_t *blk_gran);
int camdd_probe_pass_scsi(struct cam_device *cam_dev, union ccb *ccb,
         camdd_argmask arglist, int probe_retry_count,
         int probe_timeout, uint64_t *maxsector, uint32_t *block_len);
struct camdd_dev *camdd_probe_file(int fd, struct camdd_io_opts *io_opts,
				   int retry_count, int timeout);
struct camdd_dev *camdd_probe_pass(struct cam_device *cam_dev,
				   struct camdd_io_opts *io_opts,
				   camdd_argmask arglist, int probe_retry_count,
				   int probe_timeout, int io_retry_count,
				   int io_timeout);
void *camdd_file_worker(void *arg);
camdd_buf_status camdd_ccb_status(union ccb *ccb, int protocol);
int camdd_get_cgd(struct cam_device *device, struct ccb_getdev *cgd);
int camdd_queue_peer_buf(struct camdd_dev *dev, struct camdd_buf *buf);
int camdd_complete_peer_buf(struct camdd_dev *dev, struct camdd_buf *peer_buf);
void camdd_peer_done(struct camdd_buf *buf);
void camdd_complete_buf(struct camdd_dev *dev, struct camdd_buf *buf,
			int *error_count);
int camdd_pass_fetch(struct camdd_dev *dev);
int camdd_file_run(struct camdd_dev *dev);
int camdd_pass_run(struct camdd_dev *dev);
int camdd_get_next_lba_len(struct camdd_dev *dev, uint64_t *lba, ssize_t *len);
int camdd_queue(struct camdd_dev *dev, struct camdd_buf *read_buf);
void camdd_get_depth(struct camdd_dev *dev, uint32_t *our_depth,
		     uint32_t *peer_depth, uint32_t *our_bytes,
		     uint32_t *peer_bytes);
void *camdd_worker(void *arg);
void camdd_sig_handler(int sig);
void camdd_print_status(struct camdd_dev *camdd_dev,
			struct camdd_dev *other_dev,
			struct timespec *start_time);
int camdd_rw(struct camdd_io_opts *io_opts, int num_io_opts,
	     uint64_t max_io, int retry_count, int timeout);
int camdd_parse_io_opts(char *args, int is_write,
			struct camdd_io_opts *io_opts);
void usage(void);

/*
 * Parse out a bus, or a bus, target and lun in the following
 * format:
 * bus
 * bus:target
 * bus:target:lun
 *
 * Returns the number of parsed components, or 0.
 */
static int
parse_btl(char *tstr, int *bus, int *target, int *lun, camdd_argmask *arglst)
{
	char *tmpstr;
	int convs = 0;

	while (isspace(*tstr) && (*tstr != '\0'))
		tstr++;

	tmpstr = (char *)strtok(tstr, ":");
	if ((tmpstr != NULL) && (*tmpstr != '\0')) {
		*bus = strtol(tmpstr, NULL, 0);
		*arglst |= CAMDD_ARG_BUS;
		convs++;
		tmpstr = (char *)strtok(NULL, ":");
		if ((tmpstr != NULL) && (*tmpstr != '\0')) {
			*target = strtol(tmpstr, NULL, 0);
			*arglst |= CAMDD_ARG_TARGET;
			convs++;
			tmpstr = (char *)strtok(NULL, ":");
			if ((tmpstr != NULL) && (*tmpstr != '\0')) {
				*lun = strtol(tmpstr, NULL, 0);
				*arglst |= CAMDD_ARG_LUN;
				convs++;
			}
		}
	}

	return convs;
}

/*
 * XXX KDM clean up and free all of the buffers on the queue!
 */
void
camdd_free_dev(struct camdd_dev *dev)
{
	if (dev == NULL)
		return;

	switch (dev->dev_type) {
	case CAMDD_DEV_FILE: {
		struct camdd_dev_file *file_dev = &dev->dev_spec.file;

		if (file_dev->fd != -1)
			close(file_dev->fd);
		free(file_dev->tmp_buf);
		break;
	}
	case CAMDD_DEV_PASS: {
		struct camdd_dev_pass *pass_dev = &dev->dev_spec.pass;

		if (pass_dev->dev != NULL)
			cam_close_device(pass_dev->dev);
		break;
	}
	default:
		break;
	}

	free(dev);
}

struct camdd_dev *
camdd_alloc_dev(camdd_dev_type dev_type, struct kevent *new_ke, int num_ke,
		int retry_count, int timeout)
{
	struct camdd_dev *dev = NULL;
	struct kevent *ke;
	size_t ke_size;
	int retval = 0;

	dev = calloc(1, sizeof(*dev));
	if (dev == NULL) {
		warn("%s: unable to malloc %zu bytes", __func__, sizeof(*dev));
		goto bailout;
	}

	dev->dev_type = dev_type;
	dev->io_timeout = timeout;
	dev->retry_count = retry_count;
	STAILQ_INIT(&dev->free_queue);
	STAILQ_INIT(&dev->free_indirect_queue);
	STAILQ_INIT(&dev->active_queue);
	STAILQ_INIT(&dev->pending_queue);
	STAILQ_INIT(&dev->run_queue);
	STAILQ_INIT(&dev->reorder_queue);
	STAILQ_INIT(&dev->work_queue);
	STAILQ_INIT(&dev->peer_done_queue);
	STAILQ_INIT(&dev->peer_work_queue);
	retval = pthread_mutex_init(&dev->mutex, NULL);
	if (retval != 0) {
		warnc(retval, "%s: failed to initialize mutex", __func__);
		goto bailout;
	}

	retval = pthread_cond_init(&dev->cond, NULL);
	if (retval != 0) {
		warnc(retval, "%s: failed to initialize condition variable",
		      __func__);
		goto bailout;
	}

	dev->kq = kqueue();
	if (dev->kq == -1) {
		warn("%s: Unable to create kqueue", __func__);
		goto bailout;
	}

	ke_size = sizeof(struct kevent) * (num_ke + 4);
	ke = calloc(1, ke_size);
	if (ke == NULL) {
		warn("%s: unable to malloc %zu bytes", __func__, ke_size);
		goto bailout;
	}
	if (num_ke > 0)
		bcopy(new_ke, ke, num_ke * sizeof(struct kevent));

	EV_SET(&ke[num_ke++], (uintptr_t)&dev->work_queue, EVFILT_USER,
	       EV_ADD|EV_ENABLE|EV_CLEAR, 0,0, 0);
	EV_SET(&ke[num_ke++], (uintptr_t)&dev->peer_done_queue, EVFILT_USER,
	       EV_ADD|EV_ENABLE|EV_CLEAR, 0,0, 0);
	EV_SET(&ke[num_ke++], SIGINFO, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0,0,0);
	EV_SET(&ke[num_ke++], SIGINT, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0,0,0);

	retval = kevent(dev->kq, ke, num_ke, NULL, 0, NULL);
	if (retval == -1) {
		warn("%s: Unable to register kevents", __func__);
		goto bailout;
	}


	return (dev);

bailout:
	free(dev);

	return (NULL);
}

static struct camdd_buf *
camdd_alloc_buf(struct camdd_dev *dev, camdd_buf_type buf_type)
{
	struct camdd_buf *buf = NULL;
	uint8_t *data_ptr = NULL;

	/*
	 * We only need to allocate data space for data buffers.
	 */
	switch (buf_type) {
	case CAMDD_BUF_DATA:
		data_ptr = malloc(dev->blocksize);
		if (data_ptr == NULL) {
			warn("unable to allocate %u bytes", dev->blocksize);
			goto bailout_error;
		}
		break;
	default:
		break;
	}
	
	buf = calloc(1, sizeof(*buf));
	if (buf == NULL) {
		warn("unable to allocate %zu bytes", sizeof(*buf));
		goto bailout_error;
	}

	buf->buf_type = buf_type;
	buf->dev = dev;
	switch (buf_type) {
	case CAMDD_BUF_DATA: {
		struct camdd_buf_data *data;

		data = &buf->buf_type_spec.data;

		data->alloc_len = dev->blocksize;
		data->buf = data_ptr;
		break;
	}
	case CAMDD_BUF_INDIRECT:
		break;
	default:
		break;
	}
	STAILQ_INIT(&buf->src_list);

	return (buf);

bailout_error:
	free(data_ptr);

	return (NULL);
}

void
camdd_release_buf(struct camdd_buf *buf)
{
	struct camdd_dev *dev;

	dev = buf->dev;

	switch (buf->buf_type) {
	case CAMDD_BUF_DATA: {
		struct camdd_buf_data *data;

		data = &buf->buf_type_spec.data;

		if (data->segs != NULL) {
			if (data->extra_buf != 0) {
				void *extra_buf;

				extra_buf = (void *)
				    data->segs[data->sg_count - 1].ds_addr;
				free(extra_buf);
				data->extra_buf = 0;
			}
			free(data->segs);
			data->segs = NULL;
			data->sg_count = 0;
		} else if (data->iovec != NULL) {
			if (data->extra_buf != 0) {
				free(data->iovec[data->sg_count - 1].iov_base);
				data->extra_buf = 0;
			}
			free(data->iovec);
			data->iovec = NULL;
			data->sg_count = 0;
		}
		STAILQ_INSERT_TAIL(&dev->free_queue, buf, links);
		break;
	}
	case CAMDD_BUF_INDIRECT:
		STAILQ_INSERT_TAIL(&dev->free_indirect_queue, buf, links);
		break;
	default:
		err(1, "%s: Invalid buffer type %d for released buffer",
		    __func__, buf->buf_type);
		break;
	}
}

struct camdd_buf *
camdd_get_buf(struct camdd_dev *dev, camdd_buf_type buf_type)
{
	struct camdd_buf *buf = NULL;

	switch (buf_type) {
	case CAMDD_BUF_DATA:
		buf = STAILQ_FIRST(&dev->free_queue);
		if (buf != NULL) {
			struct camdd_buf_data *data;
			uint8_t *data_ptr;
			uint32_t alloc_len;

			STAILQ_REMOVE_HEAD(&dev->free_queue, links);
			data = &buf->buf_type_spec.data;
			data_ptr = data->buf;
			alloc_len = data->alloc_len;
			bzero(buf, sizeof(*buf));
			data->buf = data_ptr;
			data->alloc_len = alloc_len;
		}
		break;
	case CAMDD_BUF_INDIRECT:
		buf = STAILQ_FIRST(&dev->free_indirect_queue);
		if (buf != NULL) {
			STAILQ_REMOVE_HEAD(&dev->free_indirect_queue, links);

			bzero(buf, sizeof(*buf));
		}
		break;
	default:
		warnx("Unknown buffer type %d requested", buf_type);
		break;
	}


	if (buf == NULL)
		return (camdd_alloc_buf(dev, buf_type));
	else {
		STAILQ_INIT(&buf->src_list);
		buf->dev = dev;
		buf->buf_type = buf_type;

		return (buf);
	}
}

int
camdd_buf_sg_create(struct camdd_buf *buf, int iovec, uint32_t sector_size,
		    uint32_t *num_sectors_used, int *double_buf_needed)
{
	struct camdd_buf *tmp_buf;
	struct camdd_buf_data *data;
	uint8_t *extra_buf = NULL;
	size_t extra_buf_len = 0;
	int extra_buf_attached = 0;
	int i, retval = 0;

	data = &buf->buf_type_spec.data;

	data->sg_count = buf->src_count;
	/*
	 * Compose a scatter/gather list from all of the buffers in the list.
	 * If the length of the buffer isn't a multiple of the sector size,
	 * we'll have to add an extra buffer.  This should only happen
	 * at the end of a transfer.
	 */
	if ((data->fill_len % sector_size) != 0) {
		extra_buf_len = sector_size - (data->fill_len % sector_size);
		extra_buf = calloc(extra_buf_len, 1);
		if (extra_buf == NULL) {
			warn("%s: unable to allocate %zu bytes for extra "
			    "buffer space", __func__, extra_buf_len);
			retval = 1;
			goto bailout;
		}
		data->extra_buf = 1;
		data->sg_count++;
	}
	if (iovec == 0) {
		data->segs = calloc(data->sg_count, sizeof(bus_dma_segment_t));
		if (data->segs == NULL) {
			warn("%s: unable to allocate %zu bytes for S/G list",
			    __func__, sizeof(bus_dma_segment_t) *
			    data->sg_count);
			retval = 1;
			goto bailout;
		}

	} else {
		data->iovec = calloc(data->sg_count, sizeof(struct iovec));
		if (data->iovec == NULL) {
			warn("%s: unable to allocate %zu bytes for S/G list",
			    __func__, sizeof(struct iovec) * data->sg_count);
			retval = 1;
			goto bailout;
		}
	}

	for (i = 0, tmp_buf = STAILQ_FIRST(&buf->src_list);
	     i < buf->src_count && tmp_buf != NULL; i++,
	     tmp_buf = STAILQ_NEXT(tmp_buf, src_links)) {

		if (tmp_buf->buf_type == CAMDD_BUF_DATA) {
			struct camdd_buf_data *tmp_data;

			tmp_data = &tmp_buf->buf_type_spec.data;
			if (iovec == 0) {
				data->segs[i].ds_addr =
				    (bus_addr_t) tmp_data->buf;
				data->segs[i].ds_len = tmp_data->fill_len -
				    tmp_data->resid;
			} else {
				data->iovec[i].iov_base = tmp_data->buf;
				data->iovec[i].iov_len = tmp_data->fill_len -
				    tmp_data->resid;
			}
			if (((tmp_data->fill_len - tmp_data->resid) %
			     sector_size) != 0)
				*double_buf_needed = 1;
		} else {
			struct camdd_buf_indirect *tmp_ind;

			tmp_ind = &tmp_buf->buf_type_spec.indirect;
			if (iovec == 0) {
				data->segs[i].ds_addr =
				    (bus_addr_t)tmp_ind->start_ptr;
				data->segs[i].ds_len = tmp_ind->len;
			} else {
				data->iovec[i].iov_base = tmp_ind->start_ptr;
				data->iovec[i].iov_len = tmp_ind->len;
			}
			if ((tmp_ind->len % sector_size) != 0)
				*double_buf_needed = 1;
		}
	}

	if (extra_buf != NULL) {
		if (iovec == 0) {
			data->segs[i].ds_addr = (bus_addr_t)extra_buf;
			data->segs[i].ds_len = extra_buf_len;
		} else {
			data->iovec[i].iov_base = extra_buf;
			data->iovec[i].iov_len = extra_buf_len;
		}
		extra_buf_attached = 1;
		i++;
	}
	if ((tmp_buf != NULL) || (i != data->sg_count)) {
		warnx("buffer source count does not match "
		      "number of buffers in list!");
		retval = 1;
		goto bailout;
	}

bailout:
	if (retval == 0) {
		*num_sectors_used = (data->fill_len + extra_buf_len) /
		    sector_size;
	} else if (extra_buf_attached == 0) {
		/*
		 * If extra_buf isn't attached yet, we need to free it
		 * to avoid leaking.
		 */
		free(extra_buf);
		data->extra_buf = 0;
		data->sg_count--;
	}
	return (retval);
}

uint32_t
camdd_buf_get_len(struct camdd_buf *buf)
{
	uint32_t len = 0;

	if (buf->buf_type != CAMDD_BUF_DATA) {
		struct camdd_buf_indirect *indirect;

		indirect = &buf->buf_type_spec.indirect;
		len = indirect->len;
	} else {
		struct camdd_buf_data *data;

		data = &buf->buf_type_spec.data;
		len = data->fill_len;
	}

	return (len);
}

void
camdd_buf_add_child(struct camdd_buf *buf, struct camdd_buf *child_buf)
{
	struct camdd_buf_data *data;

	assert(buf->buf_type == CAMDD_BUF_DATA);

	data = &buf->buf_type_spec.data;

	STAILQ_INSERT_TAIL(&buf->src_list, child_buf, src_links);
	buf->src_count++;

	data->fill_len += camdd_buf_get_len(child_buf);
}

typedef enum {
	CAMDD_TS_MAX_BLK,
	CAMDD_TS_MIN_BLK,
	CAMDD_TS_BLK_GRAN,
	CAMDD_TS_EFF_IOSIZE
} camdd_status_item_index;

static struct camdd_status_items {
	const char *name;
	struct mt_status_entry *entry;
} req_status_items[] = {
	{ "max_blk", NULL },
	{ "min_blk", NULL },
	{ "blk_gran", NULL },
	{ "max_effective_iosize", NULL }
};

int
camdd_probe_tape(int fd, char *filename, uint64_t *max_iosize,
		 uint64_t *max_blk, uint64_t *min_blk, uint64_t *blk_gran)
{
	struct mt_status_data status_data;
	char *xml_str = NULL;
	unsigned int i;
	int retval = 0;
	
	retval = mt_get_xml_str(fd, MTIOCEXTGET, &xml_str);
	if (retval != 0)
		err(1, "Couldn't get XML string from %s", filename);

	retval = mt_get_status(xml_str, &status_data);
	if (retval != XML_STATUS_OK) {
		warn("couldn't get status for %s", filename);
		retval = 1;
		goto bailout;
	} else
		retval = 0;

	if (status_data.error != 0) {
		warnx("%s", status_data.error_str);
		retval = 1;
		goto bailout;
	}

	for (i = 0; i < nitems(req_status_items); i++) {
                char *name;

		name = __DECONST(char *, req_status_items[i].name);
		req_status_items[i].entry = mt_status_entry_find(&status_data,
		    name);
		if (req_status_items[i].entry == NULL) {
			errx(1, "Cannot find status entry %s",
			    req_status_items[i].name);
		}
	}

	*max_iosize = req_status_items[CAMDD_TS_EFF_IOSIZE].entry->value_unsigned;
	*max_blk= req_status_items[CAMDD_TS_MAX_BLK].entry->value_unsigned;
	*min_blk= req_status_items[CAMDD_TS_MIN_BLK].entry->value_unsigned;
	*blk_gran = req_status_items[CAMDD_TS_BLK_GRAN].entry->value_unsigned;
bailout:

	free(xml_str);
	mt_status_free(&status_data);

	return (retval);
}

struct camdd_dev *
camdd_probe_file(int fd, struct camdd_io_opts *io_opts, int retry_count,
    int timeout)
{
	struct camdd_dev *dev = NULL;
	struct camdd_dev_file *file_dev;
	uint64_t blocksize = io_opts->blocksize;

	dev = camdd_alloc_dev(CAMDD_DEV_FILE, NULL, 0, retry_count, timeout);
	if (dev == NULL)
		goto bailout;

	file_dev = &dev->dev_spec.file;
	file_dev->fd = fd;
	strlcpy(file_dev->filename, io_opts->dev_name,
	    sizeof(file_dev->filename));
	strlcpy(dev->device_name, io_opts->dev_name, sizeof(dev->device_name));
	if (blocksize == 0)
		dev->blocksize = CAMDD_FILE_DEFAULT_BLOCK;
	else
		dev->blocksize = blocksize;

	if ((io_opts->queue_depth != 0)
	 && (io_opts->queue_depth != 1)) {
		warnx("Queue depth %ju for %s ignored, only 1 outstanding "
		    "command supported", (uintmax_t)io_opts->queue_depth,
		    io_opts->dev_name);
	}
	dev->target_queue_depth = CAMDD_FILE_DEFAULT_DEPTH;
	dev->run = camdd_file_run;
	dev->fetch = NULL;

	/*
	 * We can effectively access files on byte boundaries.  We'll reset
	 * this for devices like disks that can be accessed on sector
	 * boundaries.
	 */
	dev->sector_size = 1;

	if ((fd != STDIN_FILENO)
	 && (fd != STDOUT_FILENO)) {
		int retval;

		retval = fstat(fd, &file_dev->sb);
		if (retval != 0) {
			warn("Cannot stat %s", dev->device_name);
			goto bailout_error;
		}
		if (S_ISREG(file_dev->sb.st_mode)) {
			file_dev->file_type = CAMDD_FILE_REG;
		} else if (S_ISCHR(file_dev->sb.st_mode)) {
			int type;

			if (ioctl(fd, FIODTYPE, &type) == -1)
				err(1, "FIODTYPE ioctl failed on %s",
				    dev->device_name);
			else {
				if (type & D_TAPE)
					file_dev->file_type = CAMDD_FILE_TAPE;
				else if (type & D_DISK)
					file_dev->file_type = CAMDD_FILE_DISK;
				else if (type & D_MEM)
					file_dev->file_type = CAMDD_FILE_MEM;
				else if (type & D_TTY)
					file_dev->file_type = CAMDD_FILE_TTY;
			}
		} else if (S_ISDIR(file_dev->sb.st_mode)) {
			errx(1, "cannot operate on directory %s",
			    dev->device_name);
		} else if (S_ISFIFO(file_dev->sb.st_mode)) {
			file_dev->file_type = CAMDD_FILE_PIPE;
		} else
			errx(1, "Cannot determine file type for %s",
			    dev->device_name);

		switch (file_dev->file_type) {
		case CAMDD_FILE_REG:
			if (file_dev->sb.st_size != 0)
				dev->max_sector = file_dev->sb.st_size - 1;
			else
				dev->max_sector = 0;
			file_dev->file_flags |= CAMDD_FF_CAN_SEEK;
			break;
		case CAMDD_FILE_TAPE: {
			uint64_t max_iosize, max_blk, min_blk, blk_gran;
			/*
			 * Check block limits and maximum effective iosize.
			 * Make sure the blocksize is within the block
			 * limits (and a multiple of the minimum blocksize)
			 * and that the blocksize is <= maximum effective
			 * iosize.
			 */
			retval = camdd_probe_tape(fd, dev->device_name,
			    &max_iosize, &max_blk, &min_blk, &blk_gran);
			if (retval != 0)
				errx(1, "Unable to probe tape %s",
				    dev->device_name);

			/*
			 * The blocksize needs to be <= the maximum
			 * effective I/O size of the tape device.  Note
			 * that this also takes into account the maximum
			 * blocksize reported by READ BLOCK LIMITS.
			 */
			if (dev->blocksize > max_iosize) {
				warnx("Blocksize %u too big for %s, limiting "
				    "to %ju", dev->blocksize, dev->device_name,
				    max_iosize);
				dev->blocksize = max_iosize;
			}

			/*
			 * The blocksize needs to be at least min_blk;
			 */
			if (dev->blocksize < min_blk) {
				warnx("Blocksize %u too small for %s, "
				    "increasing to %ju", dev->blocksize,
				    dev->device_name, min_blk);
				dev->blocksize = min_blk;
			}

			/*
			 * And the blocksize needs to be a multiple of
			 * the block granularity.
			 */
			if ((blk_gran != 0)
			 && (dev->blocksize % (1 << blk_gran))) {
				warnx("Blocksize %u for %s not a multiple of "
				    "%d, adjusting to %d", dev->blocksize,
				    dev->device_name, (1 << blk_gran),
				    dev->blocksize & ~((1 << blk_gran) - 1));
				dev->blocksize &= ~((1 << blk_gran) - 1);
			}

			if (dev->blocksize == 0) {
				errx(1, "Unable to derive valid blocksize for "
				    "%s", dev->device_name);
			}

			/*
			 * For tape drives, set the sector size to the
			 * blocksize so that we make sure not to write
			 * less than the blocksize out to the drive.
			 */
			dev->sector_size = dev->blocksize;
			break;
		}
		case CAMDD_FILE_DISK: {
			off_t media_size;
			unsigned int sector_size;

			file_dev->file_flags |= CAMDD_FF_CAN_SEEK;

			if (ioctl(fd, DIOCGSECTORSIZE, &sector_size) == -1) {
				err(1, "DIOCGSECTORSIZE ioctl failed on %s",
				    dev->device_name);
			}

			if (sector_size == 0) {
				errx(1, "DIOCGSECTORSIZE ioctl returned "
				    "invalid sector size %u for %s",
				    sector_size, dev->device_name);
			}

			if (ioctl(fd, DIOCGMEDIASIZE, &media_size) == -1) {
				err(1, "DIOCGMEDIASIZE ioctl failed on %s",
				    dev->device_name);
			}

			if (media_size == 0) {
				errx(1, "DIOCGMEDIASIZE ioctl returned "
				    "invalid media size %ju for %s",
				    (uintmax_t)media_size, dev->device_name);
			}

			if (dev->blocksize % sector_size) {
				errx(1, "%s blocksize %u not a multiple of "
				    "sector size %u", dev->device_name,
				    dev->blocksize, sector_size);
			}

			dev->sector_size = sector_size;
			dev->max_sector = (media_size / sector_size) - 1;
			break;
		}
		case CAMDD_FILE_MEM:
			file_dev->file_flags |= CAMDD_FF_CAN_SEEK;
			break;
		default:
			break;
		}
	}

	if ((io_opts->offset != 0)
	 && ((file_dev->file_flags & CAMDD_FF_CAN_SEEK) == 0)) {
		warnx("Offset %ju specified for %s, but we cannot seek on %s",
		    io_opts->offset, io_opts->dev_name, io_opts->dev_name);
		goto bailout_error;
	}
#if 0
	else if ((io_opts->offset != 0)
		&& ((io_opts->offset % dev->sector_size) != 0)) {
		warnx("Offset %ju for %s is not a multiple of the "
		      "sector size %u", io_opts->offset, 
		      io_opts->dev_name, dev->sector_size);
		goto bailout_error;
	} else {
		dev->start_offset_bytes = io_opts->offset;
	}
#endif

bailout:
	return (dev);

bailout_error:
	camdd_free_dev(dev);
	return (NULL);
}

/*
 * Get a get device CCB for the specified device.
 */
int
camdd_get_cgd(struct cam_device *device, struct ccb_getdev *cgd)
{
        union ccb *ccb;
	int retval = 0;

	ccb = cam_getccb(device);
 
	if (ccb == NULL) {
		warnx("%s: couldn't allocate CCB", __func__);
		return -1;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cgd);

	ccb->ccb_h.func_code = XPT_GDEV_TYPE;
 
	if (cam_send_ccb(device, ccb) < 0) {
		warn("%s: error sending Get Device Information CCB", __func__);
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = -1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = -1;
		goto bailout;
	}

	bcopy(&ccb->cgd, cgd, sizeof(struct ccb_getdev));

bailout:
	cam_freeccb(ccb);
 
	return retval;
}

int
camdd_probe_pass_scsi(struct cam_device *cam_dev, union ccb *ccb,
		 camdd_argmask arglist, int probe_retry_count,
		 int probe_timeout, uint64_t *maxsector, uint32_t *block_len)
{
	struct scsi_read_capacity_data rcap;
	struct scsi_read_capacity_data_long rcaplong;
	int retval = -1;

	if (ccb == NULL) {
		warnx("%s: error passed ccb is NULL", __func__);
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_read_capacity(&ccb->csio,
			   /*retries*/ probe_retry_count,
			   /*cbfcnp*/ NULL,
			   /*tag_action*/ MSG_SIMPLE_Q_TAG,
			   &rcap,
			   SSD_FULL_SIZE,
			   /*timeout*/ probe_timeout ? probe_timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAMDD_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(cam_dev, ccb) < 0) {
		warn("error sending READ CAPACITY command");

		cam_error_print(cam_dev, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);

		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(cam_dev, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		goto bailout;
	}

	*maxsector = scsi_4btoul(rcap.addr);
	*block_len = scsi_4btoul(rcap.length);

	/*
	 * A last block of 2^32-1 means that the true capacity is over 2TB,
	 * and we need to issue the long READ CAPACITY to get the real
	 * capacity.  Otherwise, we're all set.
	 */
	if (*maxsector != 0xffffffff) {
		retval = 0;
		goto bailout;
	}

	scsi_read_capacity_16(&ccb->csio,
			      /*retries*/ probe_retry_count,
			      /*cbfcnp*/ NULL,
			      /*tag_action*/ MSG_SIMPLE_Q_TAG,
			      /*lba*/ 0,
			      /*reladdr*/ 0,
			      /*pmi*/ 0,
			      (uint8_t *)&rcaplong,
			      sizeof(rcaplong),
			      /*sense_len*/ SSD_FULL_SIZE,
			      /*timeout*/ probe_timeout ? probe_timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAMDD_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(cam_dev, ccb) < 0) {
		warn("error sending READ CAPACITY (16) command");
		cam_error_print(cam_dev, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(cam_dev, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		goto bailout;
	}

	*maxsector = scsi_8btou64(rcaplong.addr);
	*block_len = scsi_4btoul(rcaplong.length);

	retval = 0;

bailout:
	return retval;
}

/*
 * Need to implement this.  Do a basic probe:
 * - Check the inquiry data, make sure we're talking to a device that we
 *   can reasonably expect to talk to -- direct, RBC, CD, WORM.
 * - Send a test unit ready, make sure the device is available.
 * - Get the capacity and block size.
 */
struct camdd_dev *
camdd_probe_pass(struct cam_device *cam_dev, struct camdd_io_opts *io_opts,
		 camdd_argmask arglist, int probe_retry_count,
		 int probe_timeout, int io_retry_count, int io_timeout)
{
	union ccb *ccb;
	uint64_t maxsector = 0;
	uint32_t cpi_maxio, max_iosize, pass_numblocks;
	uint32_t block_len = 0;
	struct camdd_dev *dev = NULL;
	struct camdd_dev_pass *pass_dev;
	struct kevent ke;
	struct ccb_getdev cgd;
	int retval;
	int scsi_dev_type;

	if ((retval = camdd_get_cgd(cam_dev, &cgd)) != 0) {
		warnx("%s: error retrieving CGD", __func__);
		return NULL;
	}

	ccb = cam_getccb(cam_dev);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		goto bailout;
	}

	switch (cgd.protocol) {
	case PROTO_SCSI:
		scsi_dev_type = SID_TYPE(&cam_dev->inq_data);

		/*
		 * For devices that support READ CAPACITY, we'll attempt to get the
		 * capacity.  Otherwise, we really don't support tape or other
		 * devices via SCSI passthrough, so just return an error in that case.
		 */
		switch (scsi_dev_type) {
		case T_DIRECT:
		case T_WORM:
		case T_CDROM:
		case T_OPTICAL:
		case T_RBC:
		case T_ZBC_HM:
			break;
		default:
			errx(1, "Unsupported SCSI device type %d", scsi_dev_type);
			break; /*NOTREACHED*/
		}

		if ((retval = camdd_probe_pass_scsi(cam_dev, ccb, probe_retry_count,
						arglist, probe_timeout, &maxsector,
						&block_len))) {
			goto bailout;
		}
		break;
	default:
		errx(1, "Unsupported PROTO type %d", cgd.protocol);
		break; /*NOTREACHED*/
	}

	if (block_len == 0) {
		warnx("Sector size for %s%u is 0, cannot continue",
		    cam_dev->device_name, cam_dev->dev_unit_num);
		goto bailout_error;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cpi);

	ccb->ccb_h.func_code = XPT_PATH_INQ;
	ccb->ccb_h.flags = CAM_DIR_NONE;
	ccb->ccb_h.retry_count = 1;
	
	if (cam_send_ccb(cam_dev, ccb) < 0) {
		warn("error sending XPT_PATH_INQ CCB");

		cam_error_print(cam_dev, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		goto bailout;
	}

	EV_SET(&ke, cam_dev->fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);

	dev = camdd_alloc_dev(CAMDD_DEV_PASS, &ke, 1, io_retry_count,
			      io_timeout);
	if (dev == NULL)
		goto bailout;

	pass_dev = &dev->dev_spec.pass;
	pass_dev->scsi_dev_type = scsi_dev_type;
	pass_dev->protocol = cgd.protocol;
	pass_dev->dev = cam_dev;
	pass_dev->max_sector = maxsector;
	pass_dev->block_len = block_len;
	pass_dev->cpi_maxio = ccb->cpi.maxio;
	snprintf(dev->device_name, sizeof(dev->device_name), "%s%u",
		 pass_dev->dev->device_name, pass_dev->dev->dev_unit_num);
	dev->sector_size = block_len;
	dev->max_sector = maxsector;
	

	/*
	 * Determine the optimal blocksize to use for this device.
	 */

	/*
	 * If the controller has not specified a maximum I/O size,
	 * just go with 128K as a somewhat conservative value.
	 */
	if (pass_dev->cpi_maxio == 0)
		cpi_maxio = 131072;
	else
		cpi_maxio = pass_dev->cpi_maxio;

	/*
	 * If the controller has a large maximum I/O size, limit it
	 * to something smaller so that the kernel doesn't have trouble
	 * allocating buffers to copy data in and out for us.
	 * XXX KDM this is until we have unmapped I/O support in the kernel.
	 */
	max_iosize = min(cpi_maxio, CAMDD_PASS_MAX_BLOCK);

	/*
	 * If we weren't able to get a block size for some reason,
	 * default to 512 bytes.
	 */
	block_len = pass_dev->block_len;
	if (block_len == 0)
		block_len = 512;

	/*
	 * Figure out how many blocksize chunks will fit in the
	 * maximum I/O size.
	 */
	pass_numblocks = max_iosize / block_len;

	/*
	 * And finally, multiple the number of blocks by the LBA
	 * length to get our maximum block size;
	 */
	dev->blocksize = pass_numblocks * block_len;

	if (io_opts->blocksize != 0) {
		if ((io_opts->blocksize % dev->sector_size) != 0) {
			warnx("Blocksize %ju for %s is not a multiple of "
			      "sector size %u", (uintmax_t)io_opts->blocksize, 
			      dev->device_name, dev->sector_size);
			goto bailout_error;
		}
		dev->blocksize = io_opts->blocksize;
	}
	dev->target_queue_depth = CAMDD_PASS_DEFAULT_DEPTH;
	if (io_opts->queue_depth != 0)
		dev->target_queue_depth = io_opts->queue_depth;

	if (io_opts->offset != 0) {
		if (io_opts->offset > (dev->max_sector * dev->sector_size)) {
			warnx("Offset %ju is past the end of device %s",
			    io_opts->offset, dev->device_name);
			goto bailout_error;
		}
#if 0
		else if ((io_opts->offset % dev->sector_size) != 0) {
			warnx("Offset %ju for %s is not a multiple of the "
			      "sector size %u", io_opts->offset, 
			      dev->device_name, dev->sector_size);
			goto bailout_error;
		}
		dev->start_offset_bytes = io_opts->offset;
#endif
	}

	dev->min_cmd_size = io_opts->min_cmd_size;

	dev->run = camdd_pass_run;
	dev->fetch = camdd_pass_fetch;

bailout:
	cam_freeccb(ccb);

	return (dev);

bailout_error:
	cam_freeccb(ccb);

	camdd_free_dev(dev);

	return (NULL);
}

void *
camdd_worker(void *arg)
{
	struct camdd_dev *dev = arg;
	struct camdd_buf *buf;
	struct timespec ts, *kq_ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	pthread_mutex_lock(&dev->mutex);

	dev->flags |= CAMDD_DEV_FLAG_ACTIVE;

	for (;;) {
		struct kevent ke;
		int retval = 0;

		/*
		 * XXX KDM check the reorder queue depth?
		 */
		if (dev->write_dev == 0) {
			uint32_t our_depth, peer_depth, peer_bytes, our_bytes;
			uint32_t target_depth = dev->target_queue_depth;
			uint32_t peer_target_depth =
			    dev->peer_dev->target_queue_depth;
			uint32_t peer_blocksize = dev->peer_dev->blocksize;

			camdd_get_depth(dev, &our_depth, &peer_depth,
					&our_bytes, &peer_bytes);

#if 0
			while (((our_depth < target_depth)
			     && (peer_depth < peer_target_depth))
			    || ((peer_bytes + our_bytes) <
				 (peer_blocksize * 2))) {
#endif
			while (((our_depth + peer_depth) <
			        (target_depth + peer_target_depth))
			    || ((peer_bytes + our_bytes) <
				(peer_blocksize * 3))) {

				retval = camdd_queue(dev, NULL);
				if (retval == 1)
					break;
				else if (retval != 0) {
					error_exit = 1;
					goto bailout;
				}

				camdd_get_depth(dev, &our_depth, &peer_depth,
						&our_bytes, &peer_bytes);
			}
		}
		/*
		 * See if we have any I/O that is ready to execute.
		 */
		buf = STAILQ_FIRST(&dev->run_queue);
		if (buf != NULL) {
			while (dev->target_queue_depth > dev->cur_active_io) {
				retval = dev->run(dev);
				if (retval == -1) {
					dev->flags |= CAMDD_DEV_FLAG_EOF;
					error_exit = 1;
					break;
				} else if (retval != 0) {
					break;
				}
			}
		}

		/*
		 * We've reached EOF, or our partner has reached EOF.
		 */
		if ((dev->flags & CAMDD_DEV_FLAG_EOF)
		 || (dev->flags & CAMDD_DEV_FLAG_PEER_EOF)) {
			if (dev->write_dev != 0) {
			 	if ((STAILQ_EMPTY(&dev->work_queue))
				 && (dev->num_run_queue == 0)
				 && (dev->cur_active_io == 0)) {
					goto bailout;
				}
			} else {
				/*
				 * If we're the reader, and the writer
				 * got EOF, he is already done.  If we got
				 * the EOF, then we need to wait until
				 * everything is flushed out for the writer.
				 */
				if (dev->flags & CAMDD_DEV_FLAG_PEER_EOF) {
					goto bailout;
				} else if ((dev->num_peer_work_queue == 0)
					&& (dev->num_peer_done_queue == 0)
					&& (dev->cur_active_io == 0)
					&& (dev->num_run_queue == 0)) {
					goto bailout;
				}
			}
			/*
			 * XXX KDM need to do something about the pending
			 * queue and cleanup resources.
			 */
		} 

		if ((dev->write_dev == 0)
		 && (dev->cur_active_io == 0)
		 && (dev->peer_bytes_queued < dev->peer_dev->blocksize))
			kq_ts = &ts;
		else
			kq_ts = NULL;

		/*
		 * Run kevent to see if there are events to process.
		 */
		pthread_mutex_unlock(&dev->mutex);
		retval = kevent(dev->kq, NULL, 0, &ke, 1, kq_ts);
		pthread_mutex_lock(&dev->mutex);
		if (retval == -1) {
			warn("%s: error returned from kevent",__func__);
			goto bailout;
		} else if (retval != 0) {
			switch (ke.filter) {
			case EVFILT_READ:
				if (dev->fetch != NULL) {
					retval = dev->fetch(dev);
					if (retval == -1) {
						error_exit = 1;
						goto bailout;
					}
				}
				break;
			case EVFILT_SIGNAL:
				/*
				 * We register for this so we don't get
				 * an error as a result of a SIGINFO or a
				 * SIGINT.  It will actually get handled
				 * by the signal handler.  If we get a
				 * SIGINT, bail out without printing an
				 * error message.  Any other signals 
				 * will result in the error message above.
				 */
				if (ke.ident == SIGINT)
					goto bailout;
				break;
			case EVFILT_USER:
				retval = 0;
				/*
				 * Check to see if the other thread has
				 * queued any I/O for us to do.  (In this
				 * case we're the writer.)
				 */
				for (buf = STAILQ_FIRST(&dev->work_queue);
				     buf != NULL;
				     buf = STAILQ_FIRST(&dev->work_queue)) {
					STAILQ_REMOVE_HEAD(&dev->work_queue,
							   work_links);
					retval = camdd_queue(dev, buf);
					/*
					 * We keep going unless we get an
					 * actual error.  If we get EOF, we
					 * still want to remove the buffers
					 * from the queue and send the back
					 * to the reader thread.
					 */
					if (retval == -1) {
						error_exit = 1;
						goto bailout;
					} else
						retval = 0;
				}

				/*
				 * Next check to see if the other thread has
				 * queued any completed buffers back to us.
				 * (In this case we're the reader.)
				 */
				for (buf = STAILQ_FIRST(&dev->peer_done_queue);
				     buf != NULL;
				     buf = STAILQ_FIRST(&dev->peer_done_queue)){
					STAILQ_REMOVE_HEAD(
					    &dev->peer_done_queue, work_links);
					dev->num_peer_done_queue--;
					camdd_peer_done(buf);
				}
				break;
			default:
				warnx("%s: unknown kevent filter %d",
				      __func__, ke.filter);
				break;
			}
		}
	}

bailout:

	dev->flags &= ~CAMDD_DEV_FLAG_ACTIVE;

	/* XXX KDM cleanup resources here? */

	pthread_mutex_unlock(&dev->mutex);

	need_exit = 1;
	sem_post(&camdd_sem);

	return (NULL);
}

/*
 * Simplistic translation of CCB status to our local status.
 */
camdd_buf_status
camdd_ccb_status(union ccb *ccb, int protocol)
{
	camdd_buf_status status = CAMDD_STATUS_NONE;
	cam_status ccb_status;

	ccb_status = ccb->ccb_h.status & CAM_STATUS_MASK;

	switch (protocol) {
	case PROTO_SCSI:
		switch (ccb_status) {
		case CAM_REQ_CMP: {
			if (ccb->csio.resid == 0) {
				status = CAMDD_STATUS_OK;
			} else if (ccb->csio.dxfer_len > ccb->csio.resid) {
				status = CAMDD_STATUS_SHORT_IO;
			} else {
				status = CAMDD_STATUS_EOF;
			}
			break;
		}
		case CAM_SCSI_STATUS_ERROR: {
			switch (ccb->csio.scsi_status) {
			case SCSI_STATUS_OK:
			case SCSI_STATUS_COND_MET:
			case SCSI_STATUS_INTERMED:
			case SCSI_STATUS_INTERMED_COND_MET:
				status = CAMDD_STATUS_OK;
				break;
			case SCSI_STATUS_CMD_TERMINATED:
			case SCSI_STATUS_CHECK_COND:
			case SCSI_STATUS_QUEUE_FULL:
			case SCSI_STATUS_BUSY:
			case SCSI_STATUS_RESERV_CONFLICT:
			default:
				status = CAMDD_STATUS_ERROR;
				break;
			}
			break;
		}
		default:
			status = CAMDD_STATUS_ERROR;
			break;
		}
		break;
	default:
		status = CAMDD_STATUS_ERROR;
		break;
	}

	return (status);
}

/*
 * Queue a buffer to our peer's work thread for writing.
 *
 * Returns 0 for success, -1 for failure, 1 if the other thread exited.
 */
int
camdd_queue_peer_buf(struct camdd_dev *dev, struct camdd_buf *buf)
{
	struct kevent ke;
	STAILQ_HEAD(, camdd_buf) local_queue;
	struct camdd_buf *buf1, *buf2;
	struct camdd_buf_data *data = NULL;
	uint64_t peer_bytes_queued = 0;
	int active = 1;
	int retval = 0;

	STAILQ_INIT(&local_queue);

	/*
	 * Since we're the reader, we need to queue our I/O to the writer
	 * in sequential order in order to make sure it gets written out
	 * in sequential order.
	 *
	 * Check the next expected I/O starting offset.  If this doesn't
	 * match, put it on the reorder queue.
	 */
	if ((buf->lba * dev->sector_size) != dev->next_completion_pos_bytes) {

		/*
		 * If there is nothing on the queue, there is no sorting
		 * needed.
		 */
		if (STAILQ_EMPTY(&dev->reorder_queue)) {
			STAILQ_INSERT_TAIL(&dev->reorder_queue, buf, links);
			dev->num_reorder_queue++;
			goto bailout;
		}

		/*
		 * Sort in ascending order by starting LBA.  There should
		 * be no identical LBAs.
		 */
		for (buf1 = STAILQ_FIRST(&dev->reorder_queue); buf1 != NULL;
		     buf1 = buf2) {
			buf2 = STAILQ_NEXT(buf1, links);
			if (buf->lba < buf1->lba) {
				/*
				 * If we're less than the first one, then
				 * we insert at the head of the list
				 * because this has to be the first element
				 * on the list.
				 */
				STAILQ_INSERT_HEAD(&dev->reorder_queue,
						   buf, links);
				dev->num_reorder_queue++;
				break;
			} else if (buf->lba > buf1->lba) {
				if (buf2 == NULL) {
					STAILQ_INSERT_TAIL(&dev->reorder_queue, 
					    buf, links);
					dev->num_reorder_queue++;
					break;
				} else if (buf->lba < buf2->lba) {
					STAILQ_INSERT_AFTER(&dev->reorder_queue,
					    buf1, buf, links);
					dev->num_reorder_queue++;
					break;
				}
			} else {
				errx(1, "Found buffers with duplicate LBA %ju!",
				     buf->lba);
			}
		}
		goto bailout;
	} else {

		/*
		 * We're the next expected I/O completion, so put ourselves
		 * on the local queue to be sent to the writer.  We use
		 * work_links here so that we can queue this to the 
		 * peer_work_queue before taking the buffer off of the
		 * local_queue.
		 */
		dev->next_completion_pos_bytes += buf->len;
		STAILQ_INSERT_TAIL(&local_queue, buf, work_links);

		/*
		 * Go through the reorder queue looking for more sequential
		 * I/O and add it to the local queue.
		 */
		for (buf1 = STAILQ_FIRST(&dev->reorder_queue); buf1 != NULL;
		     buf1 = STAILQ_FIRST(&dev->reorder_queue)) {
			/*
			 * As soon as we see an I/O that is out of sequence,
			 * we're done.
			 */
			if ((buf1->lba * dev->sector_size) !=
			     dev->next_completion_pos_bytes)
				break;

			STAILQ_REMOVE_HEAD(&dev->reorder_queue, links);
			dev->num_reorder_queue--;
			STAILQ_INSERT_TAIL(&local_queue, buf1, work_links);
			dev->next_completion_pos_bytes += buf1->len;
		}
	}

	/*
	 * Setup the event to let the other thread know that it has work
	 * pending.
	 */
	EV_SET(&ke, (uintptr_t)&dev->peer_dev->work_queue, EVFILT_USER, 0,
	       NOTE_TRIGGER, 0, NULL);

	/*
	 * Put this on our shadow queue so that we know what we've queued
	 * to the other thread.
	 */
	STAILQ_FOREACH_SAFE(buf1, &local_queue, work_links, buf2) {
		if (buf1->buf_type != CAMDD_BUF_DATA) {
			errx(1, "%s: should have a data buffer, not an "
			    "indirect buffer", __func__);
		}
		data = &buf1->buf_type_spec.data;

		/*
		 * We only need to send one EOF to the writer, and don't
		 * need to continue sending EOFs after that.
		 */
		if (buf1->status == CAMDD_STATUS_EOF) {
			if (dev->flags & CAMDD_DEV_FLAG_EOF_SENT) {
				STAILQ_REMOVE(&local_queue, buf1, camdd_buf,
				    work_links);
				camdd_release_buf(buf1);
				retval = 1;
				continue;
			}
			dev->flags |= CAMDD_DEV_FLAG_EOF_SENT;
		}


		STAILQ_INSERT_TAIL(&dev->peer_work_queue, buf1, links);
		peer_bytes_queued += (data->fill_len - data->resid);
		dev->peer_bytes_queued += (data->fill_len - data->resid);
		dev->num_peer_work_queue++;
	}

	if (STAILQ_FIRST(&local_queue) == NULL)
		goto bailout;

	/*
	 * Drop our mutex and pick up the other thread's mutex.  We need to
	 * do this to avoid deadlocks.
	 */
	pthread_mutex_unlock(&dev->mutex);
	pthread_mutex_lock(&dev->peer_dev->mutex);

	if (dev->peer_dev->flags & CAMDD_DEV_FLAG_ACTIVE) {
		/*
		 * Put the buffers on the other thread's incoming work queue.
		 */
		for (buf1 = STAILQ_FIRST(&local_queue); buf1 != NULL;
		     buf1 = STAILQ_FIRST(&local_queue)) {
			STAILQ_REMOVE_HEAD(&local_queue, work_links);
			STAILQ_INSERT_TAIL(&dev->peer_dev->work_queue, buf1,
					   work_links);
		}
		/*
		 * Send an event to the other thread's kqueue to let it know
		 * that there is something on the work queue.
		 */
		retval = kevent(dev->peer_dev->kq, &ke, 1, NULL, 0, NULL);
		if (retval == -1)
			warn("%s: unable to add peer work_queue kevent",
			     __func__);
		else
			retval = 0;
	} else
		active = 0;

	pthread_mutex_unlock(&dev->peer_dev->mutex);
	pthread_mutex_lock(&dev->mutex);

	/*
	 * If the other side isn't active, run through the queue and
	 * release all of the buffers.
	 */
	if (active == 0) {
		for (buf1 = STAILQ_FIRST(&local_queue); buf1 != NULL;
		     buf1 = STAILQ_FIRST(&local_queue)) {
			STAILQ_REMOVE_HEAD(&local_queue, work_links);
			STAILQ_REMOVE(&dev->peer_work_queue, buf1, camdd_buf,
				      links);
			dev->num_peer_work_queue--;
			camdd_release_buf(buf1);
		}
		dev->peer_bytes_queued -= peer_bytes_queued;
		retval = 1;
	}

bailout:
	return (retval);
}

/*
 * Return a buffer to the reader thread when we have completed writing it.
 */
int
camdd_complete_peer_buf(struct camdd_dev *dev, struct camdd_buf *peer_buf)
{
	struct kevent ke;
	int retval = 0;

	/*
	 * Setup the event to let the other thread know that we have
	 * completed a buffer.
	 */
	EV_SET(&ke, (uintptr_t)&dev->peer_dev->peer_done_queue, EVFILT_USER, 0,
	       NOTE_TRIGGER, 0, NULL);

	/*
	 * Drop our lock and acquire the other thread's lock before
	 * manipulating 
	 */
	pthread_mutex_unlock(&dev->mutex);
	pthread_mutex_lock(&dev->peer_dev->mutex);

	/*
	 * Put the buffer on the reader thread's peer done queue now that
	 * we have completed it.
	 */
	STAILQ_INSERT_TAIL(&dev->peer_dev->peer_done_queue, peer_buf,
			   work_links);
	dev->peer_dev->num_peer_done_queue++;

	/*
	 * Send an event to the peer thread to let it know that we've added
	 * something to its peer done queue.
	 */
	retval = kevent(dev->peer_dev->kq, &ke, 1, NULL, 0, NULL);
	if (retval == -1)
		warn("%s: unable to add peer_done_queue kevent", __func__);
	else
		retval = 0;

	/*
	 * Drop the other thread's lock and reacquire ours.
	 */
	pthread_mutex_unlock(&dev->peer_dev->mutex);
	pthread_mutex_lock(&dev->mutex);

	return (retval);
}

/*
 * Free a buffer that was written out by the writer thread and returned to
 * the reader thread.
 */
void
camdd_peer_done(struct camdd_buf *buf)
{
	struct camdd_dev *dev;
	struct camdd_buf_data *data;

	dev = buf->dev;
	if (buf->buf_type != CAMDD_BUF_DATA) {
		errx(1, "%s: should have a data buffer, not an "
		    "indirect buffer", __func__);
	}

	data = &buf->buf_type_spec.data;

	STAILQ_REMOVE(&dev->peer_work_queue, buf, camdd_buf, links);
	dev->num_peer_work_queue--;
	dev->peer_bytes_queued -= (data->fill_len - data->resid);

	if (buf->status == CAMDD_STATUS_EOF)
		dev->flags |= CAMDD_DEV_FLAG_PEER_EOF;

	STAILQ_INSERT_TAIL(&dev->free_queue, buf, links);
}

/*
 * Assumes caller holds the lock for this device.
 */
void
camdd_complete_buf(struct camdd_dev *dev, struct camdd_buf *buf,
		   int *error_count)
{
	int retval = 0;

	/*
	 * If we're the reader, we need to send the completed I/O
	 * to the writer.  If we're the writer, we need to just
	 * free up resources, or let the reader know if we've
	 * encountered an error.
	 */
	if (dev->write_dev == 0) {
		retval = camdd_queue_peer_buf(dev, buf);
		if (retval != 0)
			(*error_count)++;
	} else {
		struct camdd_buf *tmp_buf, *next_buf;

		STAILQ_FOREACH_SAFE(tmp_buf, &buf->src_list, src_links,
				    next_buf) {
			struct camdd_buf *src_buf;
			struct camdd_buf_indirect *indirect;

			STAILQ_REMOVE(&buf->src_list, tmp_buf,
				      camdd_buf, src_links);

			tmp_buf->status = buf->status;

			if (tmp_buf->buf_type == CAMDD_BUF_DATA) {
				camdd_complete_peer_buf(dev, tmp_buf);
				continue;
			}

			indirect = &tmp_buf->buf_type_spec.indirect;
			src_buf = indirect->src_buf;
			src_buf->refcount--;
			/*
			 * XXX KDM we probably need to account for
			 * exactly how many bytes we were able to
			 * write.  Allocate the residual to the
			 * first N buffers?  Or just track the
			 * number of bytes written?  Right now the reader
			 * doesn't do anything with a residual.
			 */
			src_buf->status = buf->status;
			if (src_buf->refcount <= 0)
				camdd_complete_peer_buf(dev, src_buf);
			STAILQ_INSERT_TAIL(&dev->free_indirect_queue,
					   tmp_buf, links);
		}

		STAILQ_INSERT_TAIL(&dev->free_queue, buf, links);
	}
}

/*
 * Fetch all completed commands from the pass(4) device.
 *
 * Returns the number of commands received, or -1 if any of the commands
 * completed with an error.  Returns 0 if no commands are available.
 */
int
camdd_pass_fetch(struct camdd_dev *dev)
{
	struct camdd_dev_pass *pass_dev = &dev->dev_spec.pass;
	union ccb ccb;
	int retval = 0, num_fetched = 0, error_count = 0;

	pthread_mutex_unlock(&dev->mutex);
	/*
	 * XXX KDM we don't distinguish between EFAULT and ENOENT.
	 */
	while ((retval = ioctl(pass_dev->dev->fd, CAMIOGET, &ccb)) != -1) {
		struct camdd_buf *buf;
		struct camdd_buf_data *data;
		cam_status ccb_status;
		union ccb *buf_ccb;

		buf = ccb.ccb_h.ccb_buf;
		data = &buf->buf_type_spec.data;
		buf_ccb = &data->ccb;

		num_fetched++;

		/*
		 * Copy the CCB back out so we get status, sense data, etc.
		 */
		bcopy(&ccb, buf_ccb, sizeof(ccb));

		pthread_mutex_lock(&dev->mutex);

		/*
		 * We're now done, so take this off the active queue.
		 */
		STAILQ_REMOVE(&dev->active_queue, buf, camdd_buf, links);
		dev->cur_active_io--;

		ccb_status = ccb.ccb_h.status & CAM_STATUS_MASK;
		if (ccb_status != CAM_REQ_CMP) {
			cam_error_print(pass_dev->dev, &ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		switch (pass_dev->protocol) {
		case PROTO_SCSI:
			data->resid = ccb.csio.resid;
			dev->bytes_transferred += (ccb.csio.dxfer_len - ccb.csio.resid);
			break;
		default:
			return -1;
			break;
		}

		if (buf->status == CAMDD_STATUS_NONE)
			buf->status = camdd_ccb_status(&ccb, pass_dev->protocol);
		if (buf->status == CAMDD_STATUS_ERROR)
			error_count++;
		else if (buf->status == CAMDD_STATUS_EOF) {
			/*
			 * Once we queue this buffer to our partner thread,
			 * he will know that we've hit EOF.
			 */
			dev->flags |= CAMDD_DEV_FLAG_EOF;
		}

		camdd_complete_buf(dev, buf, &error_count);

		/*
		 * Unlock in preparation for the ioctl call.
		 */
		pthread_mutex_unlock(&dev->mutex);
	}

	pthread_mutex_lock(&dev->mutex);

	if (error_count > 0)
		return (-1);
	else
		return (num_fetched);
}

/*
 * Returns -1 for error, 0 for success/continue, and 1 for resource
 * shortage/stop processing.
 */
int
camdd_file_run(struct camdd_dev *dev)
{
	struct camdd_dev_file *file_dev = &dev->dev_spec.file;
	struct camdd_buf_data *data;
	struct camdd_buf *buf;
	off_t io_offset;
	int retval = 0, write_dev = dev->write_dev;
	int error_count = 0, no_resources = 0, double_buf_needed = 0;
	uint32_t num_sectors = 0, db_len = 0;

	buf = STAILQ_FIRST(&dev->run_queue);
	if (buf == NULL) {
		no_resources = 1;
		goto bailout;
	} else if ((dev->write_dev == 0)
		&& (dev->flags & (CAMDD_DEV_FLAG_EOF |
				  CAMDD_DEV_FLAG_EOF_SENT))) {
		STAILQ_REMOVE(&dev->run_queue, buf, camdd_buf, links);
		dev->num_run_queue--;
		buf->status = CAMDD_STATUS_EOF;
		error_count++;
		goto bailout;
	}

	/*
	 * If we're writing, we need to go through the source buffer list
	 * and create an S/G list.
	 */
	if (write_dev != 0) {
		retval = camdd_buf_sg_create(buf, /*iovec*/ 1,
		    dev->sector_size, &num_sectors, &double_buf_needed);
		if (retval != 0) {
			no_resources = 1;
			goto bailout;
		}
	}

	STAILQ_REMOVE(&dev->run_queue, buf, camdd_buf, links);
	dev->num_run_queue--;

	data = &buf->buf_type_spec.data;

	/*
	 * pread(2) and pwrite(2) offsets are byte offsets.
	 */
	io_offset = buf->lba * dev->sector_size;

	/*
	 * Unlock the mutex while we read or write.
	 */
	pthread_mutex_unlock(&dev->mutex);

	/*
	 * Note that we don't need to double buffer if we're the reader
	 * because in that case, we have allocated a single buffer of
	 * sufficient size to do the read.  This copy is necessary on
	 * writes because if one of the components of the S/G list is not
	 * a sector size multiple, the kernel will reject the write.  This
	 * is unfortunate but not surprising.  So this will make sure that
	 * we're using a single buffer that is a multiple of the sector size.
	 */
	if ((double_buf_needed != 0)
	 && (data->sg_count > 1)
	 && (write_dev != 0)) {
		uint32_t cur_offset;
		int i;

		if (file_dev->tmp_buf == NULL)
			file_dev->tmp_buf = calloc(dev->blocksize, 1);
		if (file_dev->tmp_buf == NULL) {
			buf->status = CAMDD_STATUS_ERROR;
			error_count++;
			pthread_mutex_lock(&dev->mutex);
			goto bailout;
		}
		for (i = 0, cur_offset = 0; i < data->sg_count; i++) {
			bcopy(data->iovec[i].iov_base,
			    &file_dev->tmp_buf[cur_offset],
			    data->iovec[i].iov_len);
			cur_offset += data->iovec[i].iov_len;
		}
		db_len = cur_offset;
	}

	if (file_dev->file_flags & CAMDD_FF_CAN_SEEK) {
		if (write_dev == 0) {
			/*
			 * XXX KDM is there any way we would need a S/G
			 * list here?
			 */
			retval = pread(file_dev->fd, data->buf,
			    buf->len, io_offset);
		} else {
			if (double_buf_needed != 0) {
				retval = pwrite(file_dev->fd, file_dev->tmp_buf,
				    db_len, io_offset);
			} else if (data->sg_count == 0) {
				retval = pwrite(file_dev->fd, data->buf,
				    data->fill_len, io_offset);
			} else {
				retval = pwritev(file_dev->fd, data->iovec,
				    data->sg_count, io_offset);
			}
		}
	} else {
		if (write_dev == 0) {
			/*
			 * XXX KDM is there any way we would need a S/G
			 * list here?
			 */
			retval = read(file_dev->fd, data->buf, buf->len);
		} else {
			if (double_buf_needed != 0) {
				retval = write(file_dev->fd, file_dev->tmp_buf,
				    db_len);
			} else if (data->sg_count == 0) {
				retval = write(file_dev->fd, data->buf,
				    data->fill_len);
			} else {
				retval = writev(file_dev->fd, data->iovec,
				    data->sg_count);
			}
		}
	}

	/* We're done, re-acquire the lock */
	pthread_mutex_lock(&dev->mutex);

	if (retval >= (ssize_t)data->fill_len) {
		/*
		 * If the bytes transferred is more than the request size,
		 * that indicates an overrun, which should only happen at
		 * the end of a transfer if we have to round up to a sector
		 * boundary.
		 */
		if (buf->status == CAMDD_STATUS_NONE)
			buf->status = CAMDD_STATUS_OK;
		data->resid = 0;
		dev->bytes_transferred += retval;
	} else if (retval == -1) {
		warn("Error %s %s", (write_dev) ? "writing to" :
		    "reading from", file_dev->filename);

		buf->status = CAMDD_STATUS_ERROR;
		data->resid = data->fill_len;
		error_count++;

		if (dev->debug == 0)
			goto bailout;

		if ((double_buf_needed != 0)
		 && (write_dev != 0)) {
			fprintf(stderr, "%s: fd %d, DB buf %p, len %u lba %ju "
			    "offset %ju\n", __func__, file_dev->fd,
			    file_dev->tmp_buf, db_len, (uintmax_t)buf->lba,
			    (uintmax_t)io_offset);
		} else if (data->sg_count == 0) {
			fprintf(stderr, "%s: fd %d, buf %p, len %u, lba %ju "
			    "offset %ju\n", __func__, file_dev->fd, data->buf,
			    data->fill_len, (uintmax_t)buf->lba,
			    (uintmax_t)io_offset);
		} else {
			int i;

			fprintf(stderr, "%s: fd %d, len %u, lba %ju "
			    "offset %ju\n", __func__, file_dev->fd, 
			    data->fill_len, (uintmax_t)buf->lba,
			    (uintmax_t)io_offset);

			for (i = 0; i < data->sg_count; i++) {
				fprintf(stderr, "index %d ptr %p len %zu\n",
				    i, data->iovec[i].iov_base,
				    data->iovec[i].iov_len);
			}
		}
	} else if (retval == 0) {
		buf->status = CAMDD_STATUS_EOF;
		if (dev->debug != 0)
			printf("%s: got EOF from %s!\n", __func__,
			    file_dev->filename);
		data->resid = data->fill_len;
		error_count++;
	} else if (retval < (ssize_t)data->fill_len) {
		if (buf->status == CAMDD_STATUS_NONE)
			buf->status = CAMDD_STATUS_SHORT_IO;
		data->resid = data->fill_len - retval;
		dev->bytes_transferred += retval;
	}

bailout:
	if (buf != NULL) {
		if (buf->status == CAMDD_STATUS_EOF) {
			struct camdd_buf *buf2;
			dev->flags |= CAMDD_DEV_FLAG_EOF;
			STAILQ_FOREACH(buf2, &dev->run_queue, links)
				buf2->status = CAMDD_STATUS_EOF;
		}

		camdd_complete_buf(dev, buf, &error_count);
	}

	if (error_count != 0)
		return (-1);
	else if (no_resources != 0)
		return (1);
	else
		return (0);
}

/*
 * Execute one command from the run queue.  Returns 0 for success, 1 for
 * stop processing, and -1 for error.
 */
int
camdd_pass_run(struct camdd_dev *dev)
{
	struct camdd_buf *buf = NULL;
	struct camdd_dev_pass *pass_dev = &dev->dev_spec.pass;
	struct camdd_buf_data *data;
	uint32_t num_blocks, sectors_used = 0;
	union ccb *ccb;
	int retval = 0, is_write = dev->write_dev;
	int double_buf_needed = 0;

	buf = STAILQ_FIRST(&dev->run_queue);
	if (buf == NULL) {
		retval = 1;
		goto bailout;
	}

	/*
	 * If we're writing, we need to go through the source buffer list
	 * and create an S/G list.
	 */
	if (is_write != 0) {
		retval = camdd_buf_sg_create(buf, /*iovec*/ 0,dev->sector_size,
		    &sectors_used, &double_buf_needed);
		if (retval != 0) {
			retval = -1;
			goto bailout;
		}
	}

	STAILQ_REMOVE(&dev->run_queue, buf, camdd_buf, links);
	dev->num_run_queue--;

	data = &buf->buf_type_spec.data;

	/*
	 * In almost every case the number of blocks should be the device
	 * block size.  The exception may be at the end of an I/O stream
	 * for a partial block or at the end of a device.
	 */
	if (is_write != 0)
		num_blocks = sectors_used;
	else
		num_blocks = data->fill_len / pass_dev->block_len;

	ccb = &data->ccb;

	switch (pass_dev->protocol) {
	case PROTO_SCSI:
		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

		scsi_read_write(&ccb->csio,
				/*retries*/ dev->retry_count,
				/*cbfcnp*/ NULL,
				/*tag_action*/ MSG_SIMPLE_Q_TAG,
				/*readop*/ (dev->write_dev == 0) ? SCSI_RW_READ :
					   SCSI_RW_WRITE,
				/*byte2*/ 0,
				/*minimum_cmd_size*/ dev->min_cmd_size,
				/*lba*/ buf->lba,
				/*block_count*/ num_blocks,
				/*data_ptr*/ (data->sg_count != 0) ?
					     (uint8_t *)data->segs : data->buf,
				/*dxfer_len*/ (num_blocks * pass_dev->block_len),
				/*sense_len*/ SSD_FULL_SIZE,
				/*timeout*/ dev->io_timeout);

		if (data->sg_count != 0) {
			ccb->csio.sglist_cnt = data->sg_count;
		}
		break;
	default:
		retval = -1;
		goto bailout;
	}

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (dev->retry_count != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (data->sg_count != 0) {
		ccb->ccb_h.flags |= CAM_DATA_SG;
	}

	/*
	 * Store a pointer to the buffer in the CCB.  The kernel will
	 * restore this when we get it back, and we'll use it to identify
	 * the buffer this CCB came from.
	 */
	ccb->ccb_h.ccb_buf = buf;

	/*
	 * Unlock our mutex in preparation for issuing the ioctl.
	 */
	pthread_mutex_unlock(&dev->mutex);
	/*
	 * Queue the CCB to the pass(4) driver.
	 */
	if (ioctl(pass_dev->dev->fd, CAMIOQUEUE, ccb) == -1) {
		pthread_mutex_lock(&dev->mutex);

		warn("%s: error sending CAMIOQUEUE ioctl to %s%u", __func__,
		     pass_dev->dev->device_name, pass_dev->dev->dev_unit_num);
		warn("%s: CCB address is %p", __func__, ccb);
		retval = -1;

		STAILQ_INSERT_TAIL(&dev->free_queue, buf, links);
	} else {
		pthread_mutex_lock(&dev->mutex);

		dev->cur_active_io++;
		STAILQ_INSERT_TAIL(&dev->active_queue, buf, links);
	}

bailout:
	return (retval);
}

int
camdd_get_next_lba_len(struct camdd_dev *dev, uint64_t *lba, ssize_t *len)
{
	struct camdd_dev_pass *pass_dev;
	uint32_t num_blocks;
	int retval = 0;

	pass_dev = &dev->dev_spec.pass;

	*lba = dev->next_io_pos_bytes / dev->sector_size;
	*len = dev->blocksize;
	num_blocks = *len / dev->sector_size;

	/*
	 * If max_sector is 0, then we have no set limit.  This can happen
	 * if we're writing to a file in a filesystem, or reading from
	 * something like /dev/zero.
	 */
	if ((dev->max_sector != 0)
	 || (dev->sector_io_limit != 0)) {
		uint64_t max_sector;

		if ((dev->max_sector != 0)
		 && (dev->sector_io_limit != 0)) 
			max_sector = min(dev->sector_io_limit, dev->max_sector);
		else if (dev->max_sector != 0)
			max_sector = dev->max_sector;
		else
			max_sector = dev->sector_io_limit;


		/*
		 * Check to see whether we're starting off past the end of
		 * the device.  If so, we need to just send an EOF 	
		 * notification to the writer.
		 */
		if (*lba > max_sector) {
			*len = 0;
			retval = 1;
		} else if (((*lba + num_blocks) > max_sector + 1)
			|| ((*lba + num_blocks) < *lba)) {
			/*
			 * If we get here (but pass the first check), we
			 * can trim the request length down to go to the
			 * end of the device.
			 */
			num_blocks = (max_sector + 1) - *lba;
			*len = num_blocks * dev->sector_size;
			retval = 1;
		}
	}

	dev->next_io_pos_bytes += *len;

	return (retval);
}

/*
 * Returns 0 for success, 1 for EOF detected, and -1 for failure.
 */
int
camdd_queue(struct camdd_dev *dev, struct camdd_buf *read_buf)
{
	struct camdd_buf *buf = NULL;
	struct camdd_buf_data *data;
	struct camdd_dev_pass *pass_dev;
	size_t new_len;
	struct camdd_buf_data *rb_data;
	int is_write = dev->write_dev;
	int eof_flush_needed = 0;
	int retval = 0;
	int error;

	pass_dev = &dev->dev_spec.pass;

	/*
	 * If we've gotten EOF or our partner has, we should not continue
	 * queueing I/O.  If we're a writer, though, we should continue
	 * to write any buffers that don't have EOF status.
	 */
	if ((dev->flags & CAMDD_DEV_FLAG_EOF)
	 || ((dev->flags & CAMDD_DEV_FLAG_PEER_EOF)
	  && (is_write == 0))) {
		/*
		 * Tell the worker thread that we have seen EOF.
		 */
		retval = 1;

		/*
		 * If we're the writer, send the buffer back with EOF status.
		 */
		if (is_write) {
			read_buf->status = CAMDD_STATUS_EOF;
			
			error = camdd_complete_peer_buf(dev, read_buf);
		}
		goto bailout;
	}

	if (is_write == 0) {
		buf = camdd_get_buf(dev, CAMDD_BUF_DATA);
		if (buf == NULL) {
			retval = -1;
			goto bailout;
		}
		data = &buf->buf_type_spec.data;

		retval = camdd_get_next_lba_len(dev, &buf->lba, &buf->len);
		if (retval != 0) {
			buf->status = CAMDD_STATUS_EOF;

		 	if ((buf->len == 0)
			 && ((dev->flags & (CAMDD_DEV_FLAG_EOF_SENT |
			     CAMDD_DEV_FLAG_EOF_QUEUED)) != 0)) {
				camdd_release_buf(buf);
				goto bailout;
			}
			dev->flags |= CAMDD_DEV_FLAG_EOF_QUEUED;
		}

		data->fill_len = buf->len;
		data->src_start_offset = buf->lba * dev->sector_size;

		/*
		 * Put this on the run queue.
		 */
		STAILQ_INSERT_TAIL(&dev->run_queue, buf, links);
		dev->num_run_queue++;

		/* We're done. */
		goto bailout;
	}

	/*
	 * Check for new EOF status from the reader.
	 */
	if ((read_buf->status == CAMDD_STATUS_EOF)
	 || (read_buf->status == CAMDD_STATUS_ERROR)) {
		dev->flags |= CAMDD_DEV_FLAG_PEER_EOF;
		if ((STAILQ_FIRST(&dev->pending_queue) == NULL)
		 && (read_buf->len == 0)) {
			camdd_complete_peer_buf(dev, read_buf);
			retval = 1;
			goto bailout;
		} else
			eof_flush_needed = 1;
	}

	/*
	 * See if we have a buffer we're composing with pieces from our
	 * partner thread.
	 */
	buf = STAILQ_FIRST(&dev->pending_queue);
	if (buf == NULL) {
		uint64_t lba;
		ssize_t len;

		retval = camdd_get_next_lba_len(dev, &lba, &len);
		if (retval != 0) {
			read_buf->status = CAMDD_STATUS_EOF;

			if (len == 0) {
				dev->flags |= CAMDD_DEV_FLAG_EOF;
				error = camdd_complete_peer_buf(dev, read_buf);
				goto bailout;
			}
		}

		/*
		 * If we don't have a pending buffer, we need to grab a new
		 * one from the free list or allocate another one.
		 */
		buf = camdd_get_buf(dev, CAMDD_BUF_DATA);
		if (buf == NULL) {
			retval = 1;
			goto bailout;
		}

		buf->lba = lba;
		buf->len = len;

		STAILQ_INSERT_TAIL(&dev->pending_queue, buf, links);
		dev->num_pending_queue++;
	}

	data = &buf->buf_type_spec.data;

	rb_data = &read_buf->buf_type_spec.data;

	if ((rb_data->src_start_offset != dev->next_peer_pos_bytes)
	 && (dev->debug != 0)) {
		printf("%s: WARNING: reader offset %#jx != expected offset "
		    "%#jx\n", __func__, (uintmax_t)rb_data->src_start_offset,
		    (uintmax_t)dev->next_peer_pos_bytes);
	}
	dev->next_peer_pos_bytes = rb_data->src_start_offset +
	    (rb_data->fill_len - rb_data->resid);

	new_len = (rb_data->fill_len - rb_data->resid) + data->fill_len;
	if (new_len < buf->len) {
		/*
		 * There are three cases here:
		 * 1. We need more data to fill up a block, so we put 
		 *    this I/O on the queue and wait for more I/O.
		 * 2. We have a pending buffer in the queue that is
		 *    smaller than our blocksize, but we got an EOF.  So we
		 *    need to go ahead and flush the write out.
		 * 3. We got an error.
		 */

		/*
		 * Increment our fill length.
		 */
		data->fill_len += (rb_data->fill_len - rb_data->resid);

		/*
		 * Add the new read buffer to the list for writing.
		 */
		STAILQ_INSERT_TAIL(&buf->src_list, read_buf, src_links);

		/* Increment the count */
		buf->src_count++;

		if (eof_flush_needed == 0) {
			/*
			 * We need to exit, because we don't have enough
			 * data yet.
			 */
			goto bailout;
		} else {
			/*
			 * Take the buffer off of the pending queue.
			 */
			STAILQ_REMOVE(&dev->pending_queue, buf, camdd_buf,
				      links);
			dev->num_pending_queue--;

			/*
			 * If we need an EOF flush, but there is no data
			 * to flush, go ahead and return this buffer.
			 */
			if (data->fill_len == 0) {
				camdd_complete_buf(dev, buf, /*error_count*/0);
				retval = 1;
				goto bailout;
			}

			/*
			 * Put this on the next queue for execution.
			 */
			STAILQ_INSERT_TAIL(&dev->run_queue, buf, links);
			dev->num_run_queue++;
		}
	} else if (new_len == buf->len) {
		/*
		 * We have enough data to completey fill one block,
		 * so we're ready to issue the I/O.
		 */

		/*
		 * Take the buffer off of the pending queue.
		 */
		STAILQ_REMOVE(&dev->pending_queue, buf, camdd_buf, links);
		dev->num_pending_queue--;

		/*
		 * Add the new read buffer to the list for writing.
		 */
		STAILQ_INSERT_TAIL(&buf->src_list, read_buf, src_links);

		/* Increment the count */
		buf->src_count++;

		/*
		 * Increment our fill length.
		 */
		data->fill_len += (rb_data->fill_len - rb_data->resid);

		/*
		 * Put this on the next queue for execution.
		 */
		STAILQ_INSERT_TAIL(&dev->run_queue, buf, links);
		dev->num_run_queue++;
	} else {
		struct camdd_buf *idb;
		struct camdd_buf_indirect *indirect;
		uint32_t len_to_go, cur_offset;

		
		idb = camdd_get_buf(dev, CAMDD_BUF_INDIRECT);
		if (idb == NULL) {
			retval = 1;
			goto bailout;
		}
		indirect = &idb->buf_type_spec.indirect;
		indirect->src_buf = read_buf;
		read_buf->refcount++;
		indirect->offset = 0;
		indirect->start_ptr = rb_data->buf;
		/*
		 * We've already established that there is more
		 * data in read_buf than we have room for in our
		 * current write request.  So this particular chunk
		 * of the request should just be the remainder
		 * needed to fill up a block.
		 */
		indirect->len = buf->len - (data->fill_len - data->resid);

		camdd_buf_add_child(buf, idb);

		/*
		 * This buffer is ready to execute, so we can take
		 * it off the pending queue and put it on the run
		 * queue.
		 */
		STAILQ_REMOVE(&dev->pending_queue, buf, camdd_buf,
			      links);
		dev->num_pending_queue--;
		STAILQ_INSERT_TAIL(&dev->run_queue, buf, links);
		dev->num_run_queue++;

		cur_offset = indirect->offset + indirect->len;

		/*
		 * The resulting I/O would be too large to fit in
		 * one block.  We need to split this I/O into
		 * multiple pieces.  Allocate as many buffers as needed.
		 */
		for (len_to_go = rb_data->fill_len - rb_data->resid -
		     indirect->len; len_to_go > 0;) {
			struct camdd_buf *new_buf;
			struct camdd_buf_data *new_data;
			uint64_t lba;
			ssize_t len;

			retval = camdd_get_next_lba_len(dev, &lba, &len);
			if ((retval != 0)
			 && (len == 0)) {
				/*
				 * The device has already been marked
				 * as EOF, and there is no space left.
				 */
				goto bailout;
			}

			new_buf = camdd_get_buf(dev, CAMDD_BUF_DATA);
			if (new_buf == NULL) {
				retval = 1;
				goto bailout;
			}

			new_buf->lba = lba;
			new_buf->len = len;

			idb = camdd_get_buf(dev, CAMDD_BUF_INDIRECT);
			if (idb == NULL) {
				retval = 1;
				goto bailout;
			}

			indirect = &idb->buf_type_spec.indirect;

			indirect->src_buf = read_buf;
			read_buf->refcount++;
			indirect->offset = cur_offset;
			indirect->start_ptr = rb_data->buf + cur_offset;
			indirect->len = min(len_to_go, new_buf->len);
#if 0
			if (((indirect->len % dev->sector_size) != 0)
			 || ((indirect->offset % dev->sector_size) != 0)) {
				warnx("offset %ju len %ju not aligned with "
				    "sector size %u", indirect->offset,
				    (uintmax_t)indirect->len, dev->sector_size);
			}
#endif
			cur_offset += indirect->len;
			len_to_go -= indirect->len;

			camdd_buf_add_child(new_buf, idb);

			new_data = &new_buf->buf_type_spec.data;

			if ((new_data->fill_len == new_buf->len)
			 || (eof_flush_needed != 0)) {
				STAILQ_INSERT_TAIL(&dev->run_queue,
						   new_buf, links);
				dev->num_run_queue++;
			} else if (new_data->fill_len < buf->len) {
				STAILQ_INSERT_TAIL(&dev->pending_queue,
					   	new_buf, links);
				dev->num_pending_queue++;
			} else {
				warnx("%s: too much data in new "
				      "buffer!", __func__);
				retval = 1;
				goto bailout;
			}
		}
	}

bailout:
	return (retval);
}

void
camdd_get_depth(struct camdd_dev *dev, uint32_t *our_depth,
		uint32_t *peer_depth, uint32_t *our_bytes, uint32_t *peer_bytes)
{
	*our_depth = dev->cur_active_io + dev->num_run_queue;
	if (dev->num_peer_work_queue >
	    dev->num_peer_done_queue)
		*peer_depth = dev->num_peer_work_queue -
			      dev->num_peer_done_queue;
	else
		*peer_depth = 0;
	*our_bytes = *our_depth * dev->blocksize;
	*peer_bytes = dev->peer_bytes_queued;
}

void
camdd_sig_handler(int sig)
{
	if (sig == SIGINFO)
		need_status = 1;
	else {
		need_exit = 1;
		error_exit = 1;
	}

	sem_post(&camdd_sem);
}

void
camdd_print_status(struct camdd_dev *camdd_dev, struct camdd_dev *other_dev, 
		   struct timespec *start_time)
{
	struct timespec done_time;
	uint64_t total_ns;
	long double mb_sec, total_sec;
	int error = 0;

	error = clock_gettime(CLOCK_MONOTONIC_PRECISE, &done_time);
	if (error != 0) {
		warn("Unable to get done time");
		return;
	}

	timespecsub(&done_time, start_time, &done_time);
	
	total_ns = done_time.tv_nsec + (done_time.tv_sec * 1000000000);
	total_sec = total_ns;
	total_sec /= 1000000000;

	fprintf(stderr, "%ju bytes %s %s\n%ju bytes %s %s\n"
		"%.4Lf seconds elapsed\n",
		(uintmax_t)camdd_dev->bytes_transferred,
		(camdd_dev->write_dev == 0) ?  "read from" : "written to",
		camdd_dev->device_name,
		(uintmax_t)other_dev->bytes_transferred,
		(other_dev->write_dev == 0) ? "read from" : "written to",
		other_dev->device_name, total_sec);

	mb_sec = min(other_dev->bytes_transferred,camdd_dev->bytes_transferred);
	mb_sec /= 1024 * 1024;
	mb_sec *= 1000000000;
	mb_sec /= total_ns;
	fprintf(stderr, "%.2Lf MB/sec\n", mb_sec);
}

int
camdd_rw(struct camdd_io_opts *io_opts, int num_io_opts, uint64_t max_io,
	 int retry_count, int timeout)
{
	struct cam_device *new_cam_dev = NULL;
	struct camdd_dev *devs[2];
	struct timespec start_time;
	pthread_t threads[2];
	int unit = 0;
	int error = 0;
	int i;

	if (num_io_opts != 2) {
		warnx("Must have one input and one output path");
		error = 1;
		goto bailout;
	}

	bzero(devs, sizeof(devs));

	for (i = 0; i < num_io_opts; i++) {
		switch (io_opts[i].dev_type) {
		case CAMDD_DEV_PASS: {
			if (isdigit(io_opts[i].dev_name[0])) {
				camdd_argmask new_arglist = CAMDD_ARG_NONE;
				int bus = 0, target = 0, lun = 0;
				int rv;

				/* device specified as bus:target[:lun] */
				rv = parse_btl(io_opts[i].dev_name, &bus,
				    &target, &lun, &new_arglist);
				if (rv < 2) {
					warnx("numeric device specification "
					     "must be either bus:target, or "
					     "bus:target:lun");
					error = 1;
					goto bailout;
				}
				/* default to 0 if lun was not specified */
				if ((new_arglist & CAMDD_ARG_LUN) == 0) {
					lun = 0;
					new_arglist |= CAMDD_ARG_LUN;
				}
				new_cam_dev = cam_open_btl(bus, target, lun,
				    O_RDWR, NULL);
			} else {
				char name[30];

				if (cam_get_device(io_opts[i].dev_name, name,
						   sizeof name, &unit) == -1) {
					warnx("%s", cam_errbuf);
					error = 1;
					goto bailout;
				}
				new_cam_dev = cam_open_spec_device(name, unit,
				    O_RDWR, NULL);
			}

			if (new_cam_dev == NULL) {
				warnx("%s", cam_errbuf);
				error = 1;
				goto bailout;
			}

			devs[i] = camdd_probe_pass(new_cam_dev,
			    /*io_opts*/ &io_opts[i],
			    CAMDD_ARG_ERR_RECOVER, 
			    /*probe_retry_count*/ 3,
			    /*probe_timeout*/ 5000,
			    /*io_retry_count*/ retry_count,
			    /*io_timeout*/ timeout);
			if (devs[i] == NULL) {
				warn("Unable to probe device %s%u",
				     new_cam_dev->device_name,
				     new_cam_dev->dev_unit_num);
				error = 1;
				goto bailout;
			}
			break;
		}
		case CAMDD_DEV_FILE: {
			int fd = -1;

			if (io_opts[i].dev_name[0] == '-') {
				if (io_opts[i].write_dev != 0)
					fd = STDOUT_FILENO;
				else
					fd = STDIN_FILENO;
			} else {
				if (io_opts[i].write_dev != 0) {
					fd = open(io_opts[i].dev_name,
					    O_RDWR | O_CREAT, S_IWUSR |S_IRUSR);
				} else {
					fd = open(io_opts[i].dev_name,
					    O_RDONLY);
				}
			}
			if (fd == -1) {
				warn("error opening file %s",
				    io_opts[i].dev_name);
				error = 1;
				goto bailout;
			}

			devs[i] = camdd_probe_file(fd, &io_opts[i],
			    retry_count, timeout);
			if (devs[i] == NULL) {
				error = 1;
				goto bailout;
			}

			break;
		}
		default:
			warnx("Unknown device type %d (%s)",
			    io_opts[i].dev_type, io_opts[i].dev_name);
			error = 1;
			goto bailout;
			break; /*NOTREACHED */
		}

		devs[i]->write_dev = io_opts[i].write_dev;

		devs[i]->start_offset_bytes = io_opts[i].offset;

		if (max_io != 0) {
			devs[i]->sector_io_limit =
			    (devs[i]->start_offset_bytes /
			    devs[i]->sector_size) +
			    (max_io / devs[i]->sector_size) - 1;
		}

		devs[i]->next_io_pos_bytes = devs[i]->start_offset_bytes;
		devs[i]->next_completion_pos_bytes =devs[i]->start_offset_bytes;
	}

	devs[0]->peer_dev = devs[1];
	devs[1]->peer_dev = devs[0];
	devs[0]->next_peer_pos_bytes = devs[0]->peer_dev->next_io_pos_bytes;
	devs[1]->next_peer_pos_bytes = devs[1]->peer_dev->next_io_pos_bytes;

	sem_init(&camdd_sem, /*pshared*/ 0, 0);

	signal(SIGINFO, camdd_sig_handler);
	signal(SIGINT, camdd_sig_handler);

	error = clock_gettime(CLOCK_MONOTONIC_PRECISE, &start_time);
	if (error != 0) {
		warn("Unable to get start time");
		goto bailout;
	}

	for (i = 0; i < num_io_opts; i++) {
		error = pthread_create(&threads[i], NULL, camdd_worker,
				       (void *)devs[i]);
		if (error != 0) {
			warnc(error, "pthread_create() failed");
			goto bailout;
		}
	}

	for (;;) {
		if ((sem_wait(&camdd_sem) == -1)
		 || (need_exit != 0)) {
			struct kevent ke;

			for (i = 0; i < num_io_opts; i++) {
				EV_SET(&ke, (uintptr_t)&devs[i]->work_queue,
				    EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);

				devs[i]->flags |= CAMDD_DEV_FLAG_EOF;

				error = kevent(devs[i]->kq, &ke, 1, NULL, 0,
						NULL);
				if (error == -1)
					warn("%s: unable to wake up thread",
					    __func__);
				error = 0;
			}
			break;
		} else if (need_status != 0) {
			camdd_print_status(devs[0], devs[1], &start_time);
			need_status = 0;
		}
	} 
	for (i = 0; i < num_io_opts; i++) {
		pthread_join(threads[i], NULL);
	}

	camdd_print_status(devs[0], devs[1], &start_time);

bailout:

	for (i = 0; i < num_io_opts; i++)
		camdd_free_dev(devs[i]);

	return (error + error_exit);
}

void
usage(void)
{
	fprintf(stderr,
"usage:  camdd <-i|-o pass=pass0,bs=1M,offset=1M,depth=4>\n"
"              <-i|-o file=/tmp/file,bs=512K,offset=1M>\n"
"              <-i|-o file=/dev/da0,bs=512K,offset=1M>\n"
"              <-i|-o file=/dev/nsa0,bs=512K>\n"
"              [-C retry_count][-E][-m max_io_amt][-t timeout_secs][-v][-h]\n"
"Option description\n"
"-i <arg=val>  Specify input device/file and parameters\n"
"-o <arg=val>  Specify output device/file and parameters\n"
"Input and Output parameters\n"
"pass=name     Specify a pass(4) device like pass0 or /dev/pass0\n"
"file=name     Specify a file or device, /tmp/foo, /dev/da0, /dev/null\n"
"              or - for stdin/stdout\n"
"bs=blocksize  Specify blocksize in bytes, or using K, M, G, etc. suffix\n"
"offset=len    Specify starting offset in bytes or using K, M, G suffix\n"
"              NOTE: offset cannot be specified on tapes, pipes, stdin/out\n"
"depth=N       Specify a numeric queue depth.  This only applies to pass(4)\n"
"mcs=N         Specify a minimum cmd size for pass(4) read/write commands\n"
"Optional arguments\n"
"-C retry_cnt  Specify a retry count for pass(4) devices\n"
"-E            Enable CAM error recovery for pass(4) devices\n"
"-m max_io     Specify the maximum amount to be transferred in bytes or\n"
"              using K, G, M, etc. suffixes\n"
"-t timeout    Specify the I/O timeout to use with pass(4) devices\n"
"-v            Enable verbose error recovery\n"
"-h            Print this message\n");
}


int
camdd_parse_io_opts(char *args, int is_write, struct camdd_io_opts *io_opts)
{
	char *tmpstr, *tmpstr2;
	char *orig_tmpstr = NULL;
	int retval = 0;

	io_opts->write_dev = is_write;

	tmpstr = strdup(args);
	if (tmpstr == NULL) {
		warn("strdup failed");
		retval = 1;
		goto bailout;
	}
	orig_tmpstr = tmpstr;
	while ((tmpstr2 = strsep(&tmpstr, ",")) != NULL) {
		char *name, *value;

		/*
		 * If the user creates an empty parameter by putting in two
		 * commas, skip over it and look for the next field.
		 */
		if (*tmpstr2 == '\0')
			continue;

		name = strsep(&tmpstr2, "=");
		if (*name == '\0') {
			warnx("Got empty I/O parameter name");
			retval = 1;
			goto bailout;
		}
		value = strsep(&tmpstr2, "=");
		if ((value == NULL)
		 || (*value == '\0')) {
			warnx("Empty I/O parameter value for %s", name);
			retval = 1;
			goto bailout;
		}
		if (strncasecmp(name, "file", 4) == 0) {
			io_opts->dev_type = CAMDD_DEV_FILE;
			io_opts->dev_name = strdup(value);
			if (io_opts->dev_name == NULL) {
				warn("Error allocating memory");
				retval = 1;
				goto bailout;
			}
		} else if (strncasecmp(name, "pass", 4) == 0) {
			io_opts->dev_type = CAMDD_DEV_PASS;
			io_opts->dev_name = strdup(value);
			if (io_opts->dev_name == NULL) {
				warn("Error allocating memory");
				retval = 1;
				goto bailout;
			}
		} else if ((strncasecmp(name, "bs", 2) == 0)
			|| (strncasecmp(name, "blocksize", 9) == 0)) {
			retval = expand_number(value, &io_opts->blocksize);
			if (retval == -1) {
				warn("expand_number(3) failed on %s=%s", name,
				    value);
				retval = 1;
				goto bailout;
			}
		} else if (strncasecmp(name, "depth", 5) == 0) {
			char *endptr;

			io_opts->queue_depth = strtoull(value, &endptr, 0);
			if (*endptr != '\0') {
				warnx("invalid queue depth %s", value);
				retval = 1;
				goto bailout;
			}
		} else if (strncasecmp(name, "mcs", 3) == 0) {
			char *endptr;

			io_opts->min_cmd_size = strtol(value, &endptr, 0);
			if ((*endptr != '\0')
			 || ((io_opts->min_cmd_size > 16)
			  || (io_opts->min_cmd_size < 0))) {
				warnx("invalid minimum cmd size %s", value);
				retval = 1;
				goto bailout;
			}
		} else if (strncasecmp(name, "offset", 6) == 0) {
			retval = expand_number(value, &io_opts->offset);
			if (retval == -1) {
				warn("expand_number(3) failed on %s=%s", name,
				    value);
				retval = 1;
				goto bailout;
			}
		} else if (strncasecmp(name, "debug", 5) == 0) {
			char *endptr;

			io_opts->debug = strtoull(value, &endptr, 0);
			if (*endptr != '\0') {
				warnx("invalid debug level %s", value);
				retval = 1;
				goto bailout;
			}
		} else {
			warnx("Unrecognized parameter %s=%s", name, value);
		}
	}
bailout:
	free(orig_tmpstr);

	return (retval);
}

int
main(int argc, char **argv)
{
	int c;
	camdd_argmask arglist = CAMDD_ARG_NONE;
	int timeout = 0, retry_count = 1;
	int error = 0;
	uint64_t max_io = 0;
	struct camdd_io_opts *opt_list = NULL;

	if (argc == 1) {
		usage();
		exit(1);
	}

	opt_list = calloc(2, sizeof(struct camdd_io_opts));
	if (opt_list == NULL) {
		warn("Unable to allocate option list");
		error = 1;
		goto bailout;
	}

	while ((c = getopt(argc, argv, "C:Ehi:m:o:t:v")) != -1){
		switch (c) {
		case 'C':
			retry_count = strtol(optarg, NULL, 0);
			if (retry_count < 0)
				errx(1, "retry count %d is < 0",
				     retry_count);
			arglist |= CAMDD_ARG_RETRIES;
			break;
		case 'E':
			arglist |= CAMDD_ARG_ERR_RECOVER;
			break;
		case 'i':
		case 'o':
			if (((c == 'i')
			  && (opt_list[0].dev_type != CAMDD_DEV_NONE))
			 || ((c == 'o')
			  && (opt_list[1].dev_type != CAMDD_DEV_NONE))) {
				errx(1, "Only one input and output path "
				    "allowed");
			}
			error = camdd_parse_io_opts(optarg, (c == 'o') ? 1 : 0,
			    (c == 'o') ? &opt_list[1] : &opt_list[0]);
			if (error != 0)
				goto bailout;
			break;
		case 'm':
			error = expand_number(optarg, &max_io);
			if (error == -1) {
				warn("invalid maximum I/O amount %s", optarg);
				error = 1;
				goto bailout;
			}
			break;
		case 't':
			timeout = strtol(optarg, NULL, 0);
			if (timeout < 0)
				errx(1, "invalid timeout %d", timeout);
			/* Convert the timeout from seconds to ms */
			timeout *= 1000;
			arglist |= CAMDD_ARG_TIMEOUT;
			break;
		case 'v':
			arglist |= CAMDD_ARG_VERBOSE;
			break;
		case 'h':
		default:
			usage();
			exit(1);
			break; /*NOTREACHED*/
		}
	}

	if ((opt_list[0].dev_type == CAMDD_DEV_NONE)
	 || (opt_list[1].dev_type == CAMDD_DEV_NONE))
		errx(1, "Must specify both -i and -o");

	/*
	 * Set the timeout if the user hasn't specified one.
	 */
	if (timeout == 0)
		timeout = CAMDD_PASS_RW_TIMEOUT;

	error = camdd_rw(opt_list, 2, max_io, retry_count, timeout);

bailout:
	free(opt_list);

	exit(error);
}
