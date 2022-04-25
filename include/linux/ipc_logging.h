/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2015,2017-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IPC_LOGGING_H
#define _IPC_LOGGING_H

#include <linux/errno.h>
#include <linux/types.h>

#define MAX_MSG_SIZE 255

enum {
	TSV_TYPE_MSG_START = 1,
	TSV_TYPE_SKB = TSV_TYPE_MSG_START,
	TSV_TYPE_STRING,
	TSV_TYPE_MSG_END = TSV_TYPE_STRING,
};

struct tsv_header {
	unsigned char type;
	unsigned char size; /* size of data field */
};

struct encode_context {
	struct tsv_header hdr;
	char buff[MAX_MSG_SIZE];
	int offset;
};

struct decode_context {
	int output_format;      /* 0 = debugfs */
	char *buff;             /* output buffer */
	int size;               /* size of output buffer */
};

#if IS_ENABLED(CONFIG_IPC_LOGGING)
/*
 * ipc_log_context_create: Create a debug log context
 *                         Should not be called from atomic context
 *
 * @max_num_pages: Number of pages of logging space required (max. 10)
 * @mod_name     : Name of the directory entry under DEBUGFS
 * @feature_version : First 16 bit for version number of user-defined message
 *		      formats and next 16 bit for enabling minidump
 *
 * returns context id on success, NULL on failure
 */
void *ipc_log_context_create(int max_num_pages, const char *modname,
		uint32_t feature_version);

/*
 * msg_encode_start: Start encoding a log message
 *
 * @ectxt: Temporary storage to hold the encoded message
 * @type:  Root event type defined by the module which is logging
 */
void msg_encode_start(struct encode_context *ectxt, uint32_t type);

/*
 * tsv_timestamp_write: Writes the current timestamp count
 *
 * @ectxt: Context initialized by calling msg_encode_start()
 */
int tsv_timestamp_write(struct encode_context *ectxt);

/*
 * tsv_qtimer_write: Writes the current QTimer timestamp count
 *
 * @ectxt: Context initialized by calling msg_encode_start()
 */
int tsv_qtimer_write(struct encode_context *ectxt);

/*
 * tsv_pointer_write: Writes a data pointer
 *
 * @ectxt:   Context initialized by calling msg_encode_start()
 * @pointer: Pointer value to write
 */
int tsv_pointer_write(struct encode_context *ectxt, void *pointer);

/*
 * tsv_int32_write: Writes a 32-bit integer value
 *
 * @ectxt: Context initialized by calling msg_encode_start()
 * @n:     Integer to write
 */
int tsv_int32_write(struct encode_context *ectxt, int32_t n);

/*
 * tsv_byte_array_write: Writes a byte array
 *
 * @ectxt:     Context initialized by calling msg_encode_start()
 * @data:      Pointer to byte array
 * @data_size: Size of byte array
 */
int tsv_byte_array_write(struct encode_context *ectxt,
			 void *data, int data_size);

/*
 * msg_encode_end: Complete the message encode process
 *
 * @ectxt: Temporary storage which holds the encoded message
 */
void msg_encode_end(struct encode_context *ectxt);

/*
 * ipc_log_write: Commits message to logging ring buffer
 *
 * @ctxt:  Logging context
 * @ectxt: Temporary storage which holds the encoded message
 */
void ipc_log_write(void *ctxt, struct encode_context *ectxt);

/*
 * ipc_log_string: Helper function to log a string
 *
 * @ilctxt: Debug Log Context created using ipc_log_context_create()
 * @fmt:    Data specified using format specifiers
 */
int ipc_log_string(void *ilctxt, const char *fmt, ...) __printf(2, 3);

/**
 * ipc_log_extract - Reads and deserializes log
 *
 * @ilctxt:  logging context
 * @buff:    buffer to receive the data
 * @size:    size of the buffer
 * @returns: 0 if no data read; >0 number of bytes read; < 0 error
 *
 * If no data is available to be read, then the ilctxt::read_avail
 * completion is reinitialized.  This allows clients to block
 * until new log data is save.
 */
int ipc_log_extract(void *ilctxt, char *buff, int size);

/*
 * Print a string to decode context.
 * @dctxt   Decode context
 * @args   printf args
 */
#define IPC_SPRINTF_DECODE(dctxt, args...) \
do { \
	int i; \
	i = scnprintf(dctxt->buff, dctxt->size, args); \
	dctxt->buff += i; \
	dctxt->size -= i; \
} while (0)

/*
 * tsv_timestamp_read: Reads a timestamp
 *
 * @ectxt:  Context retrieved by reading from log space
 * @dctxt:  Temporary storage to hold the decoded message
 * @format: Output format while dumping through DEBUGFS
 */
void tsv_timestamp_read(struct encode_context *ectxt,
			struct decode_context *dctxt, const char *format);

/*
 * tsv_qtimer_read: Reads a QTimer timestamp
 *
 * @ectxt:  Context retrieved by reading from log space
 * @dctxt:  Temporary storage to hold the decoded message
 * @format: Output format while dumping through DEBUGFS
 */
void tsv_qtimer_read(struct encode_context *ectxt,
		     struct decode_context *dctxt, const char *format);

/*
 * tsv_pointer_read: Reads a data pointer
 *
 * @ectxt:  Context retrieved by reading from log space
 * @dctxt:  Temporary storage to hold the decoded message
 * @format: Output format while dumping through DEBUGFS
 */
void tsv_pointer_read(struct encode_context *ectxt,
		      struct decode_context *dctxt, const char *format);

/*
 * tsv_int32_read: Reads a 32-bit integer value
 *
 * @ectxt:  Context retrieved by reading from log space
 * @dctxt:  Temporary storage to hold the decoded message
 * @format: Output format while dumping through DEBUGFS
 */
int32_t tsv_int32_read(struct encode_context *ectxt,
		       struct decode_context *dctxt, const char *format);

/*
 * tsv_byte_array_read: Reads a byte array
 *
 * @ectxt:  Context retrieved by reading from log space
 * @dctxt:  Temporary storage to hold the decoded message
 * @format: Output format while dumping through DEBUGFS
 */
void tsv_byte_array_read(struct encode_context *ectxt,
			 struct decode_context *dctxt, const char *format);

/*
 * add_deserialization_func: Register a deserialization function to
 *                           unpack the subevents of a main event
 *
 * @ctxt: Debug log context to which the deserialization function has
 *        to be registered
 * @type: Main/Root event, defined by the module which is logging, to
 *        which this deserialization function has to be registered.
 * @dfune: Deserialization function to be registered
 *
 * return 0 on success, -ve value on FAILURE
 */
int add_deserialization_func(void *ctxt, int type,
			void (*dfunc)(struct encode_context *,
				      struct decode_context *));

/*
 * ipc_log_context_destroy: Destroy debug log context
 *
 * @ctxt: debug log context created by calling ipc_log_context_create API.
 */
int ipc_log_context_destroy(void *ctxt);

#else

static inline void *ipc_log_context_create(int max_num_pages,
	const char *modname, uint32_t feature_version)
{ return NULL; }

static inline void msg_encode_start(struct encode_context *ectxt,
	uint32_t type) { }

static inline int tsv_timestamp_write(struct encode_context *ectxt)
{ return -EINVAL; }

static inline int tsv_qtimer_write(struct encode_context *ectxt)
{ return -EINVAL; }

static inline int tsv_pointer_write(struct encode_context *ectxt, void *pointer)
{ return -EINVAL; }

static inline int tsv_int32_write(struct encode_context *ectxt, int32_t n)
{ return -EINVAL; }

static inline int tsv_byte_array_write(struct encode_context *ectxt,
			 void *data, int data_size)
{ return -EINVAL; }

static inline void msg_encode_end(struct encode_context *ectxt) { }

static inline void ipc_log_write(void *ctxt, struct encode_context *ectxt) { }

static inline int ipc_log_string(void *ilctxt, const char *fmt, ...)
{ return -EINVAL; }

static inline int ipc_log_extract(void *ilctxt, char *buff, int size)
{ return -EINVAL; }

#define IPC_SPRINTF_DECODE(dctxt, args...) do { } while (0)

static inline void tsv_timestamp_read(struct encode_context *ectxt,
			struct decode_context *dctxt, const char *format) { }

static inline void tsv_qtimer_read(struct encode_context *ectxt,
			struct decode_context *dctxt, const char *format) { }

static inline void tsv_pointer_read(struct encode_context *ectxt,
		      struct decode_context *dctxt, const char *format) { }

static inline int32_t tsv_int32_read(struct encode_context *ectxt,
		       struct decode_context *dctxt, const char *format)
{ return 0; }

static inline void tsv_byte_array_read(struct encode_context *ectxt,
			 struct decode_context *dctxt, const char *format) { }

static inline int add_deserialization_func(void *ctxt, int type,
			void (*dfunc)(struct encode_context *,
				      struct decode_context *))
{ return 0; }

static inline int ipc_log_context_destroy(void *ctxt)
{ return 0; }

#endif

#endif
