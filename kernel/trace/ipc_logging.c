// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <asm/arch_timer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/sched/clock.h>
#include <linux/ipc_logging.h>
#include <soc/qcom/minidump.h>

#include "ipc_logging_private.h"

#define LOG_PAGE_DATA_SIZE	sizeof(((struct ipc_log_page *)0)->data)
#define LOG_PAGE_FLAG (1 << 31)
#define MAX_MINIDUMP_BUFFERS CONFIG_IPC_LOG_MINIDUMP_BUFFERS
/*16th bit is used for minidump feature*/
#define FEATURE_MASK 0x10000

static int minidump_buf_cnt;
static LIST_HEAD(ipc_log_context_list);
static DEFINE_RWLOCK(context_list_lock_lha1);
static void *get_deserialization_func(struct ipc_log_context *ilctxt,
				      int type);

static struct ipc_log_page *get_first_page(struct ipc_log_context *ilctxt)
{
	struct ipc_log_page_header *p_pghdr;
	struct ipc_log_page *pg = NULL;

	if (!ilctxt)
		return NULL;
	p_pghdr = list_first_entry(&ilctxt->page_list,
				   struct ipc_log_page_header, list);
	pg = container_of(p_pghdr, struct ipc_log_page, hdr);
	return pg;
}

/**
 * is_nd_read_empty - Returns true if no data is available to read in log
 *
 * @ilctxt: logging context
 * @returns: > 1 if context is empty; 0 if not empty; <0 for failure
 *
 * This is for the debugfs read pointer which allows for a non-destructive read.
 * There may still be data in the log, but it may have already been read.
 */
static int is_nd_read_empty(struct ipc_log_context *ilctxt)
{
	if (!ilctxt)
		return -EINVAL;

	return ((ilctxt->nd_read_page == ilctxt->write_page) &&
		(ilctxt->nd_read_page->hdr.nd_read_offset ==
		 ilctxt->write_page->hdr.write_offset));
}

/**
 * is_read_empty - Returns true if no data is available in log
 *
 * @ilctxt: logging context
 * @returns: > 1 if context is empty; 0 if not empty; <0 for failure
 *
 * This is for the actual log contents.  If it is empty, then there
 * is no data at all in the log.
 */
static int is_read_empty(struct ipc_log_context *ilctxt)
{
	if (!ilctxt)
		return -EINVAL;

	return ((ilctxt->read_page == ilctxt->write_page) &&
		(ilctxt->read_page->hdr.read_offset ==
		 ilctxt->write_page->hdr.write_offset));
}

/**
 * is_nd_read_equal_read - Return true if the non-destructive read is equal to
 * the destructive read
 *
 * @ilctxt: logging context
 * @returns: true if nd read is equal to read; false otherwise
 */
static bool is_nd_read_equal_read(struct ipc_log_context *ilctxt)
{
	uint16_t read_offset;
	uint16_t nd_read_offset;

	if (ilctxt->nd_read_page == ilctxt->read_page) {
		read_offset = ilctxt->read_page->hdr.read_offset;
		nd_read_offset = ilctxt->nd_read_page->hdr.nd_read_offset;

		if (read_offset == nd_read_offset)
			return true;
	}

	return false;
}


static struct ipc_log_page *get_next_page(struct ipc_log_context *ilctxt,
					  struct ipc_log_page *cur_pg)
{
	struct ipc_log_page_header *p_pghdr;
	struct ipc_log_page *pg = NULL;

	if (!ilctxt || !cur_pg)
		return NULL;

	if (ilctxt->last_page == cur_pg)
		return ilctxt->first_page;

	p_pghdr = list_first_entry(&cur_pg->hdr.list,
			struct ipc_log_page_header, list);
	pg = container_of(p_pghdr, struct ipc_log_page, hdr);

	return pg;
}

static void register_minidump(u64 vaddr, u64 size,
			      const char *buf_name, int index)
{
	struct md_region md_entry;
	int ret;

	if (msm_minidump_enabled()
	    && (minidump_buf_cnt < MAX_MINIDUMP_BUFFERS)) {
		scnprintf(md_entry.name, sizeof(md_entry.name), "%s_%d",
			  buf_name, index);
		md_entry.virt_addr = vaddr;
		md_entry.phys_addr = virt_to_phys((void *)vaddr);
		md_entry.size = size;

		ret = msm_minidump_add_region(&md_entry);
		if (ret < 0) {
			pr_err(
		 "Failed to register log buffer %s_%d in Minidump ret %d\n",
		  buf_name, index, ret);

			return;
		}
		minidump_buf_cnt++;
	}
}
/**
 * ipc_log_read - do non-destructive read of the log
 *
 * @ilctxt:  Logging context
 * @data:  Data pointer to receive the data
 * @data_size:  Number of bytes to read (must be <= bytes available in log)
 *
 * This read will update a runtime read pointer, but will not affect the actual
 * contents of the log which allows for reading the logs continuously while
 * debugging and if the system crashes, then the full logs can still be
 * extracted.
 */
static void ipc_log_read(struct ipc_log_context *ilctxt,
			 void *data, int data_size)
{
	int bytes_to_read;

	bytes_to_read = MIN(LOG_PAGE_DATA_SIZE
				- ilctxt->nd_read_page->hdr.nd_read_offset,
			      data_size);

	memcpy(data, (ilctxt->nd_read_page->data +
		ilctxt->nd_read_page->hdr.nd_read_offset), bytes_to_read);

	if (bytes_to_read != data_size) {
		/* not enough space, wrap read to next page */
		ilctxt->nd_read_page->hdr.nd_read_offset = 0;
		ilctxt->nd_read_page = get_next_page(ilctxt,
			ilctxt->nd_read_page);
		if (WARN_ON(ilctxt->nd_read_page == NULL))
			return;

		memcpy((data + bytes_to_read),
			   (ilctxt->nd_read_page->data +
			ilctxt->nd_read_page->hdr.nd_read_offset),
			   (data_size - bytes_to_read));
		bytes_to_read = (data_size - bytes_to_read);
	}
	ilctxt->nd_read_page->hdr.nd_read_offset += bytes_to_read;
}

/**
 * ipc_log_drop - do destructive read of the log
 *
 * @ilctxt:  Logging context
 * @data:  Data pointer to receive the data (or NULL)
 * @data_size:  Number of bytes to read (must be <= bytes available in log)
 */
static void ipc_log_drop(struct ipc_log_context *ilctxt, void *data,
		int data_size)
{
	int bytes_to_read;
	bool push_nd_read;

	bytes_to_read = MIN(LOG_PAGE_DATA_SIZE
				- ilctxt->read_page->hdr.read_offset,
			      data_size);
	if (data)
		memcpy(data, (ilctxt->read_page->data +
			ilctxt->read_page->hdr.read_offset), bytes_to_read);

	if (bytes_to_read != data_size) {
		/* not enough space, wrap read to next page */
		push_nd_read = is_nd_read_equal_read(ilctxt);

		ilctxt->read_page->hdr.read_offset = 0;
		if (push_nd_read) {
			ilctxt->read_page->hdr.nd_read_offset = 0;
			ilctxt->read_page = get_next_page(ilctxt,
				ilctxt->read_page);
			if (WARN_ON(ilctxt->read_page == NULL))
				return;
			ilctxt->nd_read_page = ilctxt->read_page;
		} else {
			ilctxt->read_page = get_next_page(ilctxt,
				ilctxt->read_page);
			if (WARN_ON(ilctxt->read_page == NULL))
				return;
		}

		if (data)
			memcpy((data + bytes_to_read),
				   (ilctxt->read_page->data +
				ilctxt->read_page->hdr.read_offset),
				   (data_size - bytes_to_read));

		bytes_to_read = (data_size - bytes_to_read);
	}

	/* update non-destructive read pointer if necessary */
	push_nd_read = is_nd_read_equal_read(ilctxt);
	ilctxt->read_page->hdr.read_offset += bytes_to_read;
	ilctxt->write_avail += data_size;

	if (push_nd_read)
		ilctxt->nd_read_page->hdr.nd_read_offset += bytes_to_read;
}

/**
 * msg_read - Reads a message.
 *
 * If a message is read successfully, then the message context
 * will be set to:
 *     .hdr    message header .size and .type values
 *     .offset beginning of message data
 *
 * @ilctxt	Logging context
 * @ectxt   Message context
 *
 * @returns 0 - no message available; >0 message size; <0 error
 */
static int msg_read(struct ipc_log_context *ilctxt,
	     struct encode_context *ectxt)
{
	struct tsv_header hdr;

	if (!ectxt)
		return -EINVAL;

	if (is_nd_read_empty(ilctxt))
		return 0;

	ipc_log_read(ilctxt, &hdr, sizeof(hdr));
	ectxt->hdr.type = hdr.type;
	ectxt->hdr.size = hdr.size;
	ectxt->offset = sizeof(hdr);
	ipc_log_read(ilctxt, (ectxt->buff + ectxt->offset),
			 (int)hdr.size);

	return sizeof(hdr) + (int)hdr.size;
}

/**
 * msg_drop - Drops a message.
 *
 * @ilctxt	Logging context
 */
static void msg_drop(struct ipc_log_context *ilctxt)
{
	struct tsv_header hdr;

	if (!is_read_empty(ilctxt)) {
		ipc_log_drop(ilctxt, &hdr, sizeof(hdr));
		ipc_log_drop(ilctxt, NULL, (int)hdr.size);
	}
}

/*
 * Commits messages to the FIFO.  If the FIFO is full, then enough
 * messages are dropped to create space for the new message.
 */
void ipc_log_write(void *ctxt, struct encode_context *ectxt)
{
	struct ipc_log_context *ilctxt = (struct ipc_log_context *)ctxt;
	int bytes_to_write;
	unsigned long flags;

	if (!ilctxt || !ectxt) {
		pr_err("%s: Invalid ipc_log or encode context\n", __func__);
		return;
	}

	read_lock_irqsave(&context_list_lock_lha1, flags);
	spin_lock(&ilctxt->context_lock_lhb1);
	while (ilctxt->write_avail <= ectxt->offset)
		msg_drop(ilctxt);

	bytes_to_write = MIN(LOG_PAGE_DATA_SIZE
				- ilctxt->write_page->hdr.write_offset,
				ectxt->offset);
	memcpy((ilctxt->write_page->data +
		ilctxt->write_page->hdr.write_offset),
		ectxt->buff, bytes_to_write);

	if (bytes_to_write != ectxt->offset) {
		uint64_t t_now = sched_clock();

		ilctxt->write_page->hdr.write_offset += bytes_to_write;
		ilctxt->write_page->hdr.end_time = t_now;

		ilctxt->write_page = get_next_page(ilctxt, ilctxt->write_page);
		if (WARN_ON(ilctxt->write_page == NULL)) {
			spin_unlock(&ilctxt->context_lock_lhb1);
			read_unlock_irqrestore(&context_list_lock_lha1, flags);
			return;
		}
		ilctxt->write_page->hdr.write_offset = 0;
		ilctxt->write_page->hdr.start_time = t_now;
		memcpy((ilctxt->write_page->data +
			ilctxt->write_page->hdr.write_offset),
		       (ectxt->buff + bytes_to_write),
		       (ectxt->offset - bytes_to_write));
		bytes_to_write = (ectxt->offset - bytes_to_write);
	}
	ilctxt->write_page->hdr.write_offset += bytes_to_write;
	ilctxt->write_avail -= ectxt->offset;
	complete(&ilctxt->read_avail);
	spin_unlock(&ilctxt->context_lock_lhb1);
	read_unlock_irqrestore(&context_list_lock_lha1, flags);
}
EXPORT_SYMBOL(ipc_log_write);

/*
 * Starts a new message after which you can add serialized data and
 * then complete the message by calling msg_encode_end().
 */
void msg_encode_start(struct encode_context *ectxt, uint32_t type)
{
	if (!ectxt) {
		pr_err("%s: Invalid encode context\n", __func__);
		return;
	}

	ectxt->hdr.type = type;
	ectxt->hdr.size = 0;
	ectxt->offset = sizeof(ectxt->hdr);
}
EXPORT_SYMBOL(msg_encode_start);

/*
 * Completes the message
 */
void msg_encode_end(struct encode_context *ectxt)
{
	if (!ectxt) {
		pr_err("%s: Invalid encode context\n", __func__);
		return;
	}

	/* finalize data size */
	ectxt->hdr.size = ectxt->offset - sizeof(ectxt->hdr);
	memcpy(ectxt->buff, &ectxt->hdr, sizeof(ectxt->hdr));
}
EXPORT_SYMBOL(msg_encode_end);

/*
 * Helper function used to write data to a message context.
 *
 * @ectxt context initialized by calling msg_encode_start()
 * @data  data to write
 * @size  number of bytes of data to write
 */
static inline int tsv_write_data(struct encode_context *ectxt,
				 void *data, uint32_t size)
{
	if (!ectxt) {
		pr_err("%s: Invalid encode context\n", __func__);
		return -EINVAL;
	}
	if ((ectxt->offset + size) > MAX_MSG_SIZE) {
		pr_err("%s: No space to encode further\n", __func__);
		return -EINVAL;
	}

	memcpy((void *)(ectxt->buff + ectxt->offset), data, size);
	ectxt->offset += size;
	return 0;
}

/*
 * Helper function that writes a type to the context.
 *
 * @ectxt context initialized by calling msg_encode_start()
 * @type  primitive type
 * @size  size of primitive in bytes
 */
static inline int tsv_write_header(struct encode_context *ectxt,
				   uint32_t type, uint32_t size)
{
	struct tsv_header hdr;

	hdr.type = (unsigned char)type;
	hdr.size = (unsigned char)size;
	return tsv_write_data(ectxt, &hdr, sizeof(hdr));
}

/*
 * Writes the current timestamp count.
 *
 * @ectxt   context initialized by calling msg_encode_start()
 */
int tsv_timestamp_write(struct encode_context *ectxt)
{
	int ret;
	uint64_t t_now = sched_clock();

	ret = tsv_write_header(ectxt, TSV_TYPE_TIMESTAMP, sizeof(t_now));
	if (ret)
		return ret;
	return tsv_write_data(ectxt, &t_now, sizeof(t_now));
}
EXPORT_SYMBOL(tsv_timestamp_write);

/*
 * Writes the current QTimer timestamp count.
 *
 * @ectxt   context initialized by calling msg_encode_start()
 */
int tsv_qtimer_write(struct encode_context *ectxt)
{
	int ret;
	uint64_t t_now = __arch_counter_get_cntvct();

	ret = tsv_write_header(ectxt, TSV_TYPE_QTIMER, sizeof(t_now));
	if (ret)
		return ret;
	return tsv_write_data(ectxt, &t_now, sizeof(t_now));
}
EXPORT_SYMBOL(tsv_qtimer_write);

/*
 * Writes a data pointer.
 *
 * @ectxt   context initialized by calling msg_encode_start()
 * @pointer pointer value to write
 */
int tsv_pointer_write(struct encode_context *ectxt, void *pointer)
{
	int ret;

	ret = tsv_write_header(ectxt, TSV_TYPE_POINTER, sizeof(pointer));
	if (ret)
		return ret;
	return tsv_write_data(ectxt, &pointer, sizeof(pointer));
}
EXPORT_SYMBOL(tsv_pointer_write);

/*
 * Writes a 32-bit integer value.
 *
 * @ectxt context initialized by calling msg_encode_start()
 * @n     integer to write
 */
int tsv_int32_write(struct encode_context *ectxt, int32_t n)
{
	int ret;

	ret = tsv_write_header(ectxt, TSV_TYPE_INT32, sizeof(n));
	if (ret)
		return ret;
	return tsv_write_data(ectxt, &n, sizeof(n));
}
EXPORT_SYMBOL(tsv_int32_write);

/*
 * Writes a byte array.
 *
 * @ectxt context initialized by calling msg_write_start()
 * @data  Beginning address of data
 * @data_size Size of data to be written
 */
int tsv_byte_array_write(struct encode_context *ectxt,
			 void *data, int data_size)
{
	int ret;

	ret = tsv_write_header(ectxt, TSV_TYPE_BYTE_ARRAY, data_size);
	if (ret)
		return ret;
	return tsv_write_data(ectxt, data, data_size);
}
EXPORT_SYMBOL(tsv_byte_array_write);

/*
 * Helper function to log a string
 *
 * @ilctxt ipc_log_context created using ipc_log_context_create()
 * @fmt Data specified using format specifiers
 */
int ipc_log_string(void *ilctxt, const char *fmt, ...)
{
	struct encode_context ectxt;
	int avail_size, data_size, hdr_size = sizeof(struct tsv_header);
	va_list arg_list;

	if (!ilctxt)
		return -EINVAL;

	msg_encode_start(&ectxt, TSV_TYPE_STRING);
	tsv_timestamp_write(&ectxt);
	tsv_qtimer_write(&ectxt);
	avail_size = (MAX_MSG_SIZE - (ectxt.offset + hdr_size));
	va_start(arg_list, fmt);
	data_size = vscnprintf((ectxt.buff + ectxt.offset + hdr_size),
				avail_size, fmt, arg_list);
	va_end(arg_list);
	tsv_write_header(&ectxt, TSV_TYPE_BYTE_ARRAY, data_size);
	ectxt.offset += data_size;
	msg_encode_end(&ectxt);
	ipc_log_write(ilctxt, &ectxt);
	return 0;
}
EXPORT_SYMBOL(ipc_log_string);

/**
 * ipc_log_extract - Reads and deserializes log
 *
 * @ctxt:  logging context
 * @buff:    buffer to receive the data
 * @size:    size of the buffer
 * @returns: 0 if no data read; >0 number of bytes read; < 0 error
 *
 * If no data is available to be read, then the ilctxt::read_avail
 * completion is reinitialized.  This allows clients to block
 * until new log data is save.
 */
int ipc_log_extract(void *ctxt, char *buff, int size)
{
	struct encode_context ectxt;
	struct decode_context dctxt;
	void (*deserialize_func)(struct encode_context *ectxt,
				 struct decode_context *dctxt);
	struct ipc_log_context *ilctxt = (struct ipc_log_context *)ctxt;
	unsigned long flags;
	int ret;

	if (size < MAX_MSG_DECODED_SIZE)
		return -EINVAL;

	dctxt.output_format = OUTPUT_DEBUGFS;
	dctxt.buff = buff;
	dctxt.size = size;
	read_lock_irqsave(&context_list_lock_lha1, flags);
	spin_lock(&ilctxt->context_lock_lhb1);
	if (ilctxt->destroyed) {
		ret = -EIO;
		goto done;
	}

	while (dctxt.size >= MAX_MSG_DECODED_SIZE &&
	       !is_nd_read_empty(ilctxt)) {
		msg_read(ilctxt, &ectxt);
		deserialize_func = get_deserialization_func(ilctxt,
							ectxt.hdr.type);
		spin_unlock(&ilctxt->context_lock_lhb1);
		read_unlock_irqrestore(&context_list_lock_lha1, flags);
		if (deserialize_func)
			deserialize_func(&ectxt, &dctxt);
		else
			pr_err("%s: unknown message 0x%x\n",
				__func__, ectxt.hdr.type);
		read_lock_irqsave(&context_list_lock_lha1, flags);
		spin_lock(&ilctxt->context_lock_lhb1);
	}
	ret = size - dctxt.size;
	if (ret == 0) {
		if (!ilctxt->destroyed)
			reinit_completion(&ilctxt->read_avail);
		else
			ret = -EIO;
	}
done:
	spin_unlock(&ilctxt->context_lock_lhb1);
	read_unlock_irqrestore(&context_list_lock_lha1, flags);
	return ret;
}
EXPORT_SYMBOL(ipc_log_extract);

/*
 * Helper function used to read data from a message context.
 *
 * @ectxt  context initialized by calling msg_read()
 * @data  data to read
 * @size  number of bytes of data to read
 */
static void tsv_read_data(struct encode_context *ectxt,
			  void *data, uint32_t size)
{
	if (WARN_ON((ectxt->offset + size) > MAX_MSG_SIZE)) {
		memcpy(data, (ectxt->buff + ectxt->offset),
			MAX_MSG_SIZE - ectxt->offset - 1);
		ectxt->offset += MAX_MSG_SIZE - ectxt->offset - 1;
		return;
	}
	memcpy(data, (ectxt->buff + ectxt->offset), size);
	ectxt->offset += size;
}

/*
 * Helper function that reads a type from the context and updates the
 * context pointers.
 *
 * @ectxt  context initialized by calling msg_read()
 * @hdr   type header
 */
static void tsv_read_header(struct encode_context *ectxt,
			    struct tsv_header *hdr)
{
	if (WARN_ON((ectxt->offset + sizeof(*hdr)) > MAX_MSG_SIZE)) {
		memcpy(hdr, (ectxt->buff + ectxt->offset),
			MAX_MSG_SIZE - ectxt->offset - 1);
		ectxt->offset += MAX_MSG_SIZE - ectxt->offset - 1;
		return;
	}
	memcpy(hdr, (ectxt->buff + ectxt->offset), sizeof(*hdr));
	ectxt->offset += sizeof(*hdr);
}

/*
 * Reads a timestamp.
 *
 * @ectxt   context initialized by calling msg_read()
 * @dctxt   deserialization context
 * @format output format (appended to %6u.09u timestamp format)
 */
void tsv_timestamp_read(struct encode_context *ectxt,
			struct decode_context *dctxt, const char *format)
{
	struct tsv_header hdr;
	uint64_t val;
	unsigned long nanosec_rem;

	tsv_read_header(ectxt, &hdr);
	if (WARN_ON(hdr.type != TSV_TYPE_TIMESTAMP))
		return;
	tsv_read_data(ectxt, &val, sizeof(val));
	nanosec_rem = do_div(val, 1000000000U);
	IPC_SPRINTF_DECODE(dctxt, "[%6u.%09lu%s/",
			(unsigned int)val, nanosec_rem, format);
}
EXPORT_SYMBOL(tsv_timestamp_read);

/*
 * Reads a QTimer timestamp.
 *
 * @ectxt   context initialized by calling msg_read()
 * @dctxt   deserialization context
 * @format output format (appended to %#18llx timestamp format)
 */
void tsv_qtimer_read(struct encode_context *ectxt,
		     struct decode_context *dctxt, const char *format)
{
	struct tsv_header hdr;
	uint64_t val;

	tsv_read_header(ectxt, &hdr);
	if (WARN_ON(hdr.type != TSV_TYPE_QTIMER))
		return;
	tsv_read_data(ectxt, &val, sizeof(val));

	/*
	 * This gives 16 hex digits of output. The # prefix prepends
	 * a 0x, and these characters count as part of the number.
	 */
	IPC_SPRINTF_DECODE(dctxt, "%#18llx]%s", val, format);
}
EXPORT_SYMBOL(tsv_qtimer_read);

/*
 * Reads a data pointer.
 *
 * @ectxt   context initialized by calling msg_read()
 * @dctxt   deserialization context
 * @format output format
 */
void tsv_pointer_read(struct encode_context *ectxt,
		      struct decode_context *dctxt, const char *format)
{
	struct tsv_header hdr;
	void *val;

	tsv_read_header(ectxt, &hdr);
	if (WARN_ON(hdr.type != TSV_TYPE_POINTER))
		return;
	tsv_read_data(ectxt, &val, sizeof(val));

	IPC_SPRINTF_DECODE(dctxt, format, val);
}
EXPORT_SYMBOL(tsv_pointer_read);

/*
 * Reads a 32-bit integer value.
 *
 * @ectxt   context initialized by calling msg_read()
 * @dctxt   deserialization context
 * @format output format
 */
int32_t tsv_int32_read(struct encode_context *ectxt,
		       struct decode_context *dctxt, const char *format)
{
	struct tsv_header hdr;
	int32_t val;

	tsv_read_header(ectxt, &hdr);
	if (WARN_ON(hdr.type != TSV_TYPE_INT32))
		return -EINVAL;
	tsv_read_data(ectxt, &val, sizeof(val));

	IPC_SPRINTF_DECODE(dctxt, format, val);
	return val;
}
EXPORT_SYMBOL(tsv_int32_read);

/*
 * Reads a byte array/string.
 *
 * @ectxt   context initialized by calling msg_read()
 * @dctxt   deserialization context
 * @format output format
 */
void tsv_byte_array_read(struct encode_context *ectxt,
			 struct decode_context *dctxt, const char *format)
{
	struct tsv_header hdr;

	tsv_read_header(ectxt, &hdr);
	if (WARN_ON(hdr.type != TSV_TYPE_BYTE_ARRAY))
		return;
	tsv_read_data(ectxt, dctxt->buff, hdr.size);
	dctxt->buff += hdr.size;
	dctxt->size -= hdr.size;
}
EXPORT_SYMBOL(tsv_byte_array_read);

int add_deserialization_func(void *ctxt, int type,
			void (*dfunc)(struct encode_context *,
				      struct decode_context *))
{
	struct ipc_log_context *ilctxt = (struct ipc_log_context *)ctxt;
	struct dfunc_info *df_info;
	unsigned long flags;

	if (!ilctxt || !dfunc)
		return -EINVAL;

	df_info = kmalloc(sizeof(struct dfunc_info), GFP_KERNEL);
	if (!df_info)
		return -ENOSPC;

	read_lock_irqsave(&context_list_lock_lha1, flags);
	spin_lock(&ilctxt->context_lock_lhb1);
	df_info->type = type;
	df_info->dfunc = dfunc;
	list_add_tail(&df_info->list, &ilctxt->dfunc_info_list);
	spin_unlock(&ilctxt->context_lock_lhb1);
	read_unlock_irqrestore(&context_list_lock_lha1, flags);
	return 0;
}
EXPORT_SYMBOL(add_deserialization_func);

static void *get_deserialization_func(struct ipc_log_context *ilctxt,
				      int type)
{
	struct dfunc_info *df_info = NULL;

	if (!ilctxt)
		return NULL;

	list_for_each_entry(df_info, &ilctxt->dfunc_info_list, list) {
		if (df_info->type == type)
			return df_info->dfunc;
	}
	return NULL;
}

/**
 * ipc_log_context_create: Create a debug log context if context does not exist.
 *                         Should not be called from atomic context
 *
 * @max_num_pages: Number of pages of logging space required (max. 10)
 * @mod_name     : Name of the directory entry under DEBUGFS
 * @feature_version : First 16 bit for version number of user-defined message
 *		      formats and next 16 bit for enabling minidump
 *
 * returns context id on success, NULL on failure
 */
void *ipc_log_context_create(int max_num_pages,
			     const char *mod_name, uint32_t feature_version)
{
	struct ipc_log_context *ctxt = NULL, *tmp;
	struct ipc_log_page *pg = NULL;
	int page_cnt;
	unsigned long flags;
	int enable_minidump;

	/* check if ipc ctxt already exists */
	read_lock_irq(&context_list_lock_lha1);
	list_for_each_entry(tmp, &ipc_log_context_list, list)
		if (!strcmp(tmp->name, mod_name)) {
			ctxt = tmp;
			break;
		}
	read_unlock_irq(&context_list_lock_lha1);

	if (ctxt)
		return ctxt;

	ctxt = kzalloc(sizeof(struct ipc_log_context), GFP_KERNEL);
	if (!ctxt)
		return 0;

	init_completion(&ctxt->read_avail);
	INIT_LIST_HEAD(&ctxt->page_list);
	INIT_LIST_HEAD(&ctxt->dfunc_info_list);
	spin_lock_init(&ctxt->context_lock_lhb1);

	enable_minidump = feature_version & FEATURE_MASK;

	spin_lock_irqsave(&ctxt->context_lock_lhb1, flags);
	if (enable_minidump) {
		register_minidump((u64)ctxt, sizeof(struct ipc_log_context),
				  "ipc_ctxt", minidump_buf_cnt);
	}
	spin_unlock_irqrestore(&ctxt->context_lock_lhb1, flags);

	for (page_cnt = 0; page_cnt < max_num_pages; page_cnt++) {
		pg = kzalloc(sizeof(struct ipc_log_page), GFP_KERNEL);
		if (!pg)
			goto release_ipc_log_context;
		pg->hdr.log_id = (uint64_t)(uintptr_t)ctxt;
		pg->hdr.page_num = LOG_PAGE_FLAG | page_cnt;
		pg->hdr.ctx_offset = (int64_t)((uint64_t)(uintptr_t)ctxt -
			(uint64_t)(uintptr_t)&pg->hdr);

		/* set magic last to signal that page init is complete */
		pg->hdr.magic = IPC_LOGGING_MAGIC_NUM;
		pg->hdr.nmagic = ~(IPC_LOGGING_MAGIC_NUM);

		spin_lock_irqsave(&ctxt->context_lock_lhb1, flags);
		list_add_tail(&pg->hdr.list, &ctxt->page_list);

		if (enable_minidump) {
			register_minidump((u64)pg, sizeof(struct ipc_log_page),
					  mod_name, minidump_buf_cnt);
		}
		spin_unlock_irqrestore(&ctxt->context_lock_lhb1, flags);
	}

	ctxt->log_id = (uint64_t)(uintptr_t)ctxt;
	ctxt->version = IPC_LOG_VERSION;
	strscpy(ctxt->name, mod_name, IPC_LOG_MAX_CONTEXT_NAME_LEN);
	ctxt->user_version = feature_version & 0xffff;
	ctxt->first_page = get_first_page(ctxt);
	ctxt->last_page = pg;
	ctxt->write_page = ctxt->first_page;
	ctxt->read_page = ctxt->first_page;
	ctxt->nd_read_page = ctxt->first_page;
	ctxt->write_avail = max_num_pages * LOG_PAGE_DATA_SIZE;
	ctxt->header_size = sizeof(struct ipc_log_page_header);
	kref_init(&ctxt->refcount);
	ctxt->destroyed = false;
	create_ctx_debugfs(ctxt, mod_name);
	ipc_log_cdev_create(ctxt, mod_name);
	/* set magic last to signal context init is complete */
	ctxt->magic = IPC_LOG_CONTEXT_MAGIC_NUM;
	ctxt->nmagic = ~(IPC_LOG_CONTEXT_MAGIC_NUM);

	write_lock_irqsave(&context_list_lock_lha1, flags);
	if (enable_minidump  && (minidump_buf_cnt < MAX_MINIDUMP_BUFFERS))
		list_add(&ctxt->list, &ipc_log_context_list);
	else
		list_add_tail(&ctxt->list, &ipc_log_context_list);
	write_unlock_irqrestore(&context_list_lock_lha1, flags);

	return (void *)ctxt;

release_ipc_log_context:
	while (page_cnt-- > 0) {
		pg = get_first_page(ctxt);
		list_del(&pg->hdr.list);
		kfree(pg);
	}
	kfree(ctxt);
	return 0;
}
EXPORT_SYMBOL(ipc_log_context_create);

void ipc_log_context_free(struct kref *kref)
{
	struct ipc_log_context *ilctxt = container_of(kref,
				struct ipc_log_context, refcount);
	struct ipc_log_page *pg = NULL;

	while (!list_empty(&ilctxt->page_list)) {
		pg = get_first_page(ilctxt);
		list_del(&pg->hdr.list);
		kfree(pg);
	}

	kfree(ilctxt);
}

/*
 * Destroy debug log context
 *
 * @ctxt: debug log context created by calling ipc_log_context_create API.
 */
int ipc_log_context_destroy(void *ctxt)
{
	struct ipc_log_context *ilctxt = (struct ipc_log_context *)ctxt;
	struct dfunc_info *df_info = NULL, *tmp = NULL;
	unsigned long flags;

	if (!ilctxt)
		return 0;

	debugfs_remove_recursive(ilctxt->dent);
	ipc_log_cdev_remove(ilctxt);
	spin_lock(&ilctxt->context_lock_lhb1);
	ilctxt->destroyed = true;
	complete_all(&ilctxt->read_avail);
	list_for_each_entry_safe(df_info, tmp, &ilctxt->dfunc_info_list, list) {
		list_del(&df_info->list);
		kfree(df_info);
	}
	spin_unlock(&ilctxt->context_lock_lhb1);

	write_lock_irqsave(&context_list_lock_lha1, flags);
	list_del(&ilctxt->list);
	write_unlock_irqrestore(&context_list_lock_lha1, flags);

	ipc_log_context_put(ilctxt);

	return 0;
}
EXPORT_SYMBOL(ipc_log_context_destroy);

static int __init ipc_logging_init(void)
{
	check_and_create_debugfs();
	ipc_log_cdev_init();
	register_minidump((u64)&ipc_log_context_list, sizeof(struct list_head),
			  "ipc_log_ctxt_list", minidump_buf_cnt);

	return 0;
}

module_init(ipc_logging_init);

MODULE_DESCRIPTION("ipc logging");
MODULE_LICENSE("GPL");
