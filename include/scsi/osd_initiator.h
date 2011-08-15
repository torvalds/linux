/*
 * osd_initiator.h - OSD initiator API definition
 *
 * Copyright (C) 2008 Panasas Inc.  All rights reserved.
 *
 * Authors:
 *   Boaz Harrosh <bharrosh@panasas.com>
 *   Benny Halevy <bhalevy@panasas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 *
 */
#ifndef __OSD_INITIATOR_H__
#define __OSD_INITIATOR_H__

#include "osd_protocol.h"
#include "osd_types.h"

#include <linux/blkdev.h>
#include <scsi/scsi_device.h>

/* Note: "NI" in comments below means "Not Implemented yet" */

/* Configure of code:
 * #undef if you *don't* want OSD v1 support in runtime.
 * If #defined the initiator will dynamically configure to encode OSD v1
 * CDB's if the target is detected to be OSD v1 only.
 * OSD v2 only commands, options, and attributes will be ignored if target
 * is v1 only.
 * If #defined will result in bigger/slower code (OK Slower maybe not)
 * Q: Should this be CONFIG_SCSI_OSD_VER1_SUPPORT and set from Kconfig?
 */
#define OSD_VER1_SUPPORT y

enum osd_std_version {
	OSD_VER_NONE = 0,
	OSD_VER1 = 1,
	OSD_VER2 = 2,
};

/*
 * Object-based Storage Device.
 * This object represents an OSD device.
 * It is not a full linux device in any way. It is only
 * a place to hang resources associated with a Linux
 * request Q and some default properties.
 */
struct osd_dev {
	struct scsi_device *scsi_device;
	unsigned def_timeout;

#ifdef OSD_VER1_SUPPORT
	enum osd_std_version version;
#endif
};

/* Unique Identification of an OSD device */
struct osd_dev_info {
	unsigned systemid_len;
	u8 systemid[OSD_SYSTEMID_LEN];
	unsigned osdname_len;
	u8 *osdname;
};

/* Retrieve/return osd_dev(s) for use by Kernel clients
 * Use IS_ERR/ERR_PTR on returned "osd_dev *".
 */
struct osd_dev *osduld_path_lookup(const char *dev_name);
struct osd_dev *osduld_info_lookup(const struct osd_dev_info *odi);
void osduld_put_device(struct osd_dev *od);

const struct osd_dev_info *osduld_device_info(struct osd_dev *od);
bool osduld_device_same(struct osd_dev *od, const struct osd_dev_info *odi);

/* Add/remove test ioctls from external modules */
typedef int (do_test_fn)(struct osd_dev *od, unsigned cmd, unsigned long arg);
int osduld_register_test(unsigned ioctl, do_test_fn *do_test);
void osduld_unregister_test(unsigned ioctl);

/* These are called by uld at probe time */
void osd_dev_init(struct osd_dev *od, struct scsi_device *scsi_device);
void osd_dev_fini(struct osd_dev *od);

/**
 * osd_auto_detect_ver - Detect the OSD version, return Unique Identification
 *
 * @od:     OSD target lun handle
 * @caps:   Capabilities authorizing OSD root read attributes access
 * @odi:    Retrieved information uniquely identifying the osd target lun
 *          Note: odi->osdname must be kfreed by caller.
 *
 * Auto detects the OSD version of the OSD target and sets the @od
 * accordingly. Meanwhile also returns the "system id" and "osd name" root
 * attributes which uniquely identify the OSD target. This member is usually
 * called by the ULD. ULD users should call osduld_device_info().
 * This rutine allocates osd requests and memory at GFP_KERNEL level and might
 * sleep.
 */
int osd_auto_detect_ver(struct osd_dev *od,
	void *caps, struct osd_dev_info *odi);

static inline struct request_queue *osd_request_queue(struct osd_dev *od)
{
	return od->scsi_device->request_queue;
}

/* we might want to use function vector in the future */
static inline void osd_dev_set_ver(struct osd_dev *od, enum osd_std_version v)
{
#ifdef OSD_VER1_SUPPORT
	od->version = v;
#endif
}

static inline bool osd_dev_is_ver1(struct osd_dev *od)
{
#ifdef OSD_VER1_SUPPORT
	return od->version == OSD_VER1;
#else
	return false;
#endif
}

struct osd_request;
typedef void (osd_req_done_fn)(struct osd_request *or, void *private);

struct osd_request {
	struct osd_cdb cdb;
	struct osd_data_out_integrity_info out_data_integ;
	struct osd_data_in_integrity_info in_data_integ;

	struct osd_dev *osd_dev;
	struct request *request;

	struct _osd_req_data_segment {
		void *buff;
		unsigned alloc_size; /* 0 here means: don't call kfree */
		unsigned total_bytes;
	} cdb_cont, set_attr, enc_get_attr, get_attr;

	struct _osd_io_info {
		struct bio *bio;
		u64 total_bytes;
		u64 residual;
		struct request *req;
		struct _osd_req_data_segment *last_seg;
		u8 *pad_buff;
	} out, in;

	gfp_t alloc_flags;
	unsigned timeout;
	unsigned retries;
	unsigned sense_len;
	u8 sense[OSD_MAX_SENSE_LEN];
	enum osd_attributes_mode attributes_mode;

	osd_req_done_fn *async_done;
	void *async_private;
	int async_error;
	int req_errors;
};

static inline bool osd_req_is_ver1(struct osd_request *or)
{
	return osd_dev_is_ver1(or->osd_dev);
}

/*
 * How to use the osd library:
 *
 * osd_start_request
 *	Allocates a request.
 *
 * osd_req_*
 *	Call one of, to encode the desired operation.
 *
 * osd_add_{get,set}_attr
 *	Optionally add attributes to the CDB, list or page mode.
 *
 * osd_finalize_request
 *	Computes final data out/in offsets and signs the request,
 *	making it ready for execution.
 *
 * osd_execute_request
 *	May be called to execute it through the block layer. Other wise submit
 *	the associated block request in some other way.
 *
 * After execution:
 * osd_req_decode_sense
 *	Decodes sense information to verify execution results.
 *
 * osd_req_decode_get_attr
 *	Retrieve osd_add_get_attr_list() values if used.
 *
 * osd_end_request
 *	Must be called to deallocate the request.
 */

/**
 * osd_start_request - Allocate and initialize an osd_request
 *
 * @osd_dev:    OSD device that holds the scsi-device and default values
 *              that the request is associated with.
 * @gfp:        The allocation flags to use for request allocation, and all
 *              subsequent allocations. This will be stored at
 *              osd_request->alloc_flags, can be changed by user later
 *
 * Allocate osd_request and initialize all members to the
 * default/initial state.
 */
struct osd_request *osd_start_request(struct osd_dev *od, gfp_t gfp);

enum osd_req_options {
	OSD_REQ_FUA = 0x08,	/* Force Unit Access */
	OSD_REQ_DPO = 0x10,	/* Disable Page Out */

	OSD_REQ_BYPASS_TIMESTAMPS = 0x80,
};

/**
 * osd_finalize_request - Sign request and prepare request for execution
 *
 * @or:		osd_request to prepare
 * @options:	combination of osd_req_options bit flags or 0.
 * @cap:	A Pointer to an OSD_CAP_LEN bytes buffer that is received from
 *              The security manager as capabilities for this cdb.
 * @cap_key:	The cryptographic key used to sign the cdb/data. Can be null
 *              if NOSEC is used.
 *
 * The actual request and bios are only allocated here, so are the get_attr
 * buffers that will receive the returned attributes. Copy's @cap to cdb.
 * Sign the cdb/data with @cap_key.
 */
int osd_finalize_request(struct osd_request *or,
	u8 options, const void *cap, const u8 *cap_key);

/**
 * osd_execute_request - Execute the request synchronously through block-layer
 *
 * @or:		osd_request to Executed
 *
 * Calls blk_execute_rq to q the command and waits for completion.
 */
int osd_execute_request(struct osd_request *or);

/**
 * osd_execute_request_async - Execute the request without waitting.
 *
 * @or:                      - osd_request to Executed
 * @done: (Optional)         - Called at end of execution
 * @private:                 - Will be passed to @done function
 *
 * Calls blk_execute_rq_nowait to queue the command. When execution is done
 * optionally calls @done with @private as parameter. @or->async_error will
 * have the return code
 */
int osd_execute_request_async(struct osd_request *or,
	osd_req_done_fn *done, void *private);

/**
 * osd_req_decode_sense_full - Decode sense information after execution.
 *
 * @or:           - osd_request to examine
 * @osi           - Receives a more detailed error report information (optional).
 * @silent        - Do not print to dmsg (Even if enabled)
 * @bad_obj_list  - Some commands act on multiple objects. Failed objects will
 *                  be received here (optional)
 * @max_obj       - Size of @bad_obj_list.
 * @bad_attr_list - List of failing attributes (optional)
 * @max_attr      - Size of @bad_attr_list.
 *
 * After execution, osd_request results are analyzed using this function. The
 * return code is the final disposition on the error. So it is possible that a
 * CHECK_CONDITION was returned from target but this will return NO_ERROR, for
 * example on recovered errors. All parameters are optional if caller does
 * not need any returned information.
 * Note: This function will also dump the error to dmsg according to settings
 * of the SCSI_OSD_DPRINT_SENSE Kconfig value. Set @silent if you know the
 * command would routinely fail, to not spam the dmsg file.
 */

/**
 * osd_err_priority - osd categorized return codes in ascending severity.
 *
 * The categories are borrowed from the pnfs_osd_errno enum.
 * See comments for translated Linux codes returned by osd_req_decode_sense.
 */
enum osd_err_priority {
	OSD_ERR_PRI_NO_ERROR	= 0,
	/* Recoverable, caller should clear_highpage() all pages */
	OSD_ERR_PRI_CLEAR_PAGES = 1, /* -EFAULT */
	OSD_ERR_PRI_RESOURCE	= 2, /* -ENOMEM */
	OSD_ERR_PRI_BAD_CRED	= 3, /* -EINVAL */
	OSD_ERR_PRI_NO_ACCESS	= 4, /* -EACCES */
	OSD_ERR_PRI_UNREACHABLE	= 5, /* any other */
	OSD_ERR_PRI_NOT_FOUND	= 6, /* -ENOENT */
	OSD_ERR_PRI_NO_SPACE	= 7, /* -ENOSPC */
	OSD_ERR_PRI_EIO		= 8, /* -EIO    */
};

struct osd_sense_info {
	enum osd_err_priority osd_err_pri;

	int key;		/* one of enum scsi_sense_keys */
	int additional_code ;	/* enum osd_additional_sense_codes */
	union { /* Sense specific information */
		u16 sense_info;
		u16 cdb_field_offset; 	/* scsi_invalid_field_in_cdb */
	};
	union { /* Command specific information */
		u64 command_info;
	};

	u32 not_initiated_command_functions; /* osd_command_functions_bits */
	u32 completed_command_functions; /* osd_command_functions_bits */
	struct osd_obj_id obj;
	struct osd_attr attr;
};

int osd_req_decode_sense_full(struct osd_request *or,
	struct osd_sense_info *osi, bool silent,
	struct osd_obj_id *bad_obj_list, int max_obj,
	struct osd_attr *bad_attr_list, int max_attr);

static inline int osd_req_decode_sense(struct osd_request *or,
	struct osd_sense_info *osi)
{
	return osd_req_decode_sense_full(or, osi, false, NULL, 0, NULL, 0);
}

/**
 * osd_end_request - return osd_request to free store
 *
 * @or:		osd_request to free
 *
 * Deallocate all osd_request resources (struct req's, BIOs, buffers, etc.)
 */
void osd_end_request(struct osd_request *or);

/*
 * CDB Encoding
 *
 * Note: call only one of the following methods.
 */

/*
 * Device commands
 */
void osd_req_set_master_seed_xchg(struct osd_request *or, ...);/* NI */
void osd_req_set_master_key(struct osd_request *or, ...);/* NI */

void osd_req_format(struct osd_request *or, u64 tot_capacity);

/* list all partitions
 * @list header must be initialized to zero on first run.
 *
 * Call osd_is_obj_list_done() to find if we got the complete list.
 */
int osd_req_list_dev_partitions(struct osd_request *or,
	osd_id initial_id, struct osd_obj_id_list *list, unsigned nelem);

void osd_req_flush_obsd(struct osd_request *or,
	enum osd_options_flush_scope_values);

void osd_req_perform_scsi_command(struct osd_request *or,
	const u8 *cdb, ...);/* NI */
void osd_req_task_management(struct osd_request *or, ...);/* NI */

/*
 * Partition commands
 */
void osd_req_create_partition(struct osd_request *or, osd_id partition);
void osd_req_remove_partition(struct osd_request *or, osd_id partition);

void osd_req_set_partition_key(struct osd_request *or,
	osd_id partition, u8 new_key_id[OSD_CRYPTO_KEYID_SIZE],
	u8 seed[OSD_CRYPTO_SEED_SIZE]);/* NI */

/* list all collections in the partition
 * @list header must be init to zero on first run.
 *
 * Call osd_is_obj_list_done() to find if we got the complete list.
 */
int osd_req_list_partition_collections(struct osd_request *or,
	osd_id partition, osd_id initial_id, struct osd_obj_id_list *list,
	unsigned nelem);

/* list all objects in the partition
 * @list header must be init to zero on first run.
 *
 * Call osd_is_obj_list_done() to find if we got the complete list.
 */
int osd_req_list_partition_objects(struct osd_request *or,
	osd_id partition, osd_id initial_id, struct osd_obj_id_list *list,
	unsigned nelem);

void osd_req_flush_partition(struct osd_request *or,
	osd_id partition, enum osd_options_flush_scope_values);

/*
 * Collection commands
 */
void osd_req_create_collection(struct osd_request *or,
	const struct osd_obj_id *);/* NI */
void osd_req_remove_collection(struct osd_request *or,
	const struct osd_obj_id *);/* NI */

/* list all objects in the collection */
int osd_req_list_collection_objects(struct osd_request *or,
	const struct osd_obj_id *, osd_id initial_id,
	struct osd_obj_id_list *list, unsigned nelem);

/* V2 only filtered list of objects in the collection */
void osd_req_query(struct osd_request *or, ...);/* NI */

void osd_req_flush_collection(struct osd_request *or,
	const struct osd_obj_id *, enum osd_options_flush_scope_values);

void osd_req_get_member_attrs(struct osd_request *or, ...);/* V2-only NI */
void osd_req_set_member_attrs(struct osd_request *or, ...);/* V2-only NI */

/*
 * Object commands
 */
void osd_req_create_object(struct osd_request *or, struct osd_obj_id *);
void osd_req_remove_object(struct osd_request *or, struct osd_obj_id *);

void osd_req_write(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, struct bio *bio, u64 len);
int osd_req_write_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void *buff, u64 len);
void osd_req_append(struct osd_request *or,
	const struct osd_obj_id *, struct bio *data_out);/* NI */
void osd_req_create_write(struct osd_request *or,
	const struct osd_obj_id *, struct bio *data_out, u64 offset);/* NI */
void osd_req_clear(struct osd_request *or,
	const struct osd_obj_id *, u64 offset, u64 len);/* NI */
void osd_req_punch(struct osd_request *or,
	const struct osd_obj_id *, u64 offset, u64 len);/* V2-only NI */

void osd_req_flush_object(struct osd_request *or,
	const struct osd_obj_id *, enum osd_options_flush_scope_values,
	/*V2*/ u64 offset, /*V2*/ u64 len);

void osd_req_read(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, struct bio *bio, u64 len);
int osd_req_read_kern(struct osd_request *or,
	const struct osd_obj_id *obj, u64 offset, void *buff, u64 len);

/* Scatter/Gather write/read commands */
int osd_req_write_sg(struct osd_request *or,
	const struct osd_obj_id *obj, struct bio *bio,
	const struct osd_sg_entry *sglist, unsigned numentries);
int osd_req_read_sg(struct osd_request *or,
	const struct osd_obj_id *obj, struct bio *bio,
	const struct osd_sg_entry *sglist, unsigned numentries);
int osd_req_write_sg_kern(struct osd_request *or,
	const struct osd_obj_id *obj, void **buff,
	const struct osd_sg_entry *sglist, unsigned numentries);
int osd_req_read_sg_kern(struct osd_request *or,
	const struct osd_obj_id *obj, void **buff,
	const struct osd_sg_entry *sglist, unsigned numentries);

/*
 * Root/Partition/Collection/Object Attributes commands
 */

/* get before set */
void osd_req_get_attributes(struct osd_request *or, const struct osd_obj_id *);

/* set before get */
void osd_req_set_attributes(struct osd_request *or, const struct osd_obj_id *);

/*
 * Attributes appended to most commands
 */

/* Attributes List mode (or V2 CDB) */
  /*
   * TODO: In ver2 if at finalize time only one attr was set and no gets,
   * then the Attributes CDB mode is used automatically to save IO.
   */

/* set a list of attributes. */
int osd_req_add_set_attr_list(struct osd_request *or,
	const struct osd_attr *, unsigned nelem);

/* get a list of attributes */
int osd_req_add_get_attr_list(struct osd_request *or,
	const struct osd_attr *, unsigned nelem);

/*
 * Attributes list decoding
 * Must be called after osd_request.request was executed
 * It is called in a loop to decode the returned get_attr
 * (see osd_add_get_attr)
 */
int osd_req_decode_get_attr_list(struct osd_request *or,
	struct osd_attr *, int *nelem, void **iterator);

/* Attributes Page mode */

/*
 * Read an attribute page and optionally set one attribute
 *
 * Retrieves the attribute page directly to a user buffer.
 * @attr_page_data shall stay valid until end of execution.
 * See osd_attributes.h for common page structures
 */
int osd_req_add_get_attr_page(struct osd_request *or,
	u32 page_id, void *attr_page_data, unsigned max_page_len,
	const struct osd_attr *set_one);

#endif /* __OSD_LIB_H__ */
