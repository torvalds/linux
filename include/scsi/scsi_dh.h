/*
 * Header file for SCSI device handler infrastruture.
 *
 * Modified version of patches posted by Mike Christie <michaelc@cs.wisc.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2007
 *      Authors:
 *               Chandra Seetharaman <sekharan@us.ibm.com>
 *               Mike Anderson <andmike@linux.vnet.ibm.com>
 */

#include <scsi/scsi_device.h>

enum {
	SCSI_DH_OK = 0,
	/*
	 * device errors
	 */
	SCSI_DH_DEV_FAILED,	/* generic device error */
	SCSI_DH_DEV_TEMP_BUSY,
	SCSI_DH_DEV_UNSUPP,	/* device handler not supported */
	SCSI_DH_DEVICE_MAX,	/* max device blkerr definition */

	/*
	 * transport errors
	 */
	SCSI_DH_NOTCONN = SCSI_DH_DEVICE_MAX + 1,
	SCSI_DH_CONN_FAILURE,
	SCSI_DH_TRANSPORT_MAX,	/* max transport blkerr definition */

	/*
	 * driver and generic errors
	 */
	SCSI_DH_IO = SCSI_DH_TRANSPORT_MAX + 1,	/* generic error */
	SCSI_DH_INVALID_IO,
	SCSI_DH_RETRY,		/* retry the req, but not immediately */
	SCSI_DH_IMM_RETRY,	/* immediately retry the req */
	SCSI_DH_TIMED_OUT,
	SCSI_DH_RES_TEMP_UNAVAIL,
	SCSI_DH_DEV_OFFLINED,
	SCSI_DH_NOMEM,
	SCSI_DH_NOSYS,
	SCSI_DH_DRIVER_MAX,
};

typedef void (*activate_complete)(void *, int);
struct scsi_device_handler {
	/* Used by the infrastructure */
	struct list_head list; /* list of scsi_device_handlers */

	/* Filled by the hardware handler */
	struct module *module;
	const char *name;
	int (*check_sense)(struct scsi_device *, struct scsi_sense_hdr *);
	int (*attach)(struct scsi_device *);
	void (*detach)(struct scsi_device *);
	int (*activate)(struct scsi_device *, activate_complete, void *);
	int (*prep_fn)(struct scsi_device *, struct request *);
	int (*set_params)(struct scsi_device *, const char *);
	void (*rescan)(struct scsi_device *);
};

#ifdef CONFIG_SCSI_DH
extern int scsi_dh_activate(struct request_queue *, activate_complete, void *);
extern int scsi_dh_attach(struct request_queue *, const char *);
extern const char *scsi_dh_attached_handler_name(struct request_queue *, gfp_t);
extern int scsi_dh_set_params(struct request_queue *, const char *);
#else
static inline int scsi_dh_activate(struct request_queue *req,
					activate_complete fn, void *data)
{
	fn(data, 0);
	return 0;
}
static inline int scsi_dh_attach(struct request_queue *req, const char *name)
{
	return SCSI_DH_NOSYS;
}
static inline const char *scsi_dh_attached_handler_name(struct request_queue *q,
							gfp_t gfp)
{
	return NULL;
}
static inline int scsi_dh_set_params(struct request_queue *req, const char *params)
{
	return -SCSI_DH_NOSYS;
}
#endif
