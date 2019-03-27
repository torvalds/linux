/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-4-Clause) 
 *
 * Copyright (c) 1997, 1998 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * Buffer encoding/decoding routines taken from the original FreeBSD SCSI
 * library and slightly modified.  The original header file had the following
 * copyright:
 */
/* Copyright (c) 1994 HD Associates (hd@world.std.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 * This product includes software developed by HD Associates
 * 4. Neither the name of the HD Associaates nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY HD ASSOCIATES``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL HD ASSOCIATES OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef _CAMLIB_H
#define _CAMLIB_H

#include <sys/cdefs.h>
#include <sys/param.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

#define	CAM_ERRBUF_SIZE 2048	/* CAM library error string size */

/*
 * Right now we hard code the transport layer device, but this will change
 * if we ever get more than one transport layer.
 */
#define	XPT_DEVICE	"/dev/xpt0"


extern char cam_errbuf[];

struct cam_device {
	char 		device_path[MAXPATHLEN];/*
						   * Pathname of the device
						   * given by the user. This
						   * may be null if the
						   * user states the device
						   * name and unit number
						   * separately.
						   */
	char		given_dev_name[DEV_IDLEN+1];/*
						     * Device name given by
						     * the user.
						     */
	u_int32_t	given_unit_number;	    /*
						     * Unit number given by
						     * the user.
						     */
	char		device_name[DEV_IDLEN+1];/*
						  * Name of the device,
						  * e.g. 'pass'
						  */
	u_int32_t	dev_unit_num;	/* Unit number of the passthrough
					 * device associated with this
					 * particular device.
					 */

	char		sim_name[SIM_IDLEN+1]; /* Controller name, e.g. 'ahc' */
	u_int32_t	sim_unit_number; /* Controller unit number */
	u_int32_t	bus_id;		 /* Controller bus number */
	lun_id_t	target_lun;	 /* Logical Unit Number */
	target_id_t	target_id;	 /* Target ID */
	path_id_t	path_id;	 /* System SCSI bus number */
	u_int16_t	pd_type;	 /* type of peripheral device */
	struct scsi_inquiry_data inq_data;  /* SCSI Inquiry data */
	u_int8_t	serial_num[252]; /* device serial number */
	u_int8_t	serial_num_len;  /* length of the serial number */
	u_int8_t	sync_period;	 /* Negotiated sync period */
	u_int8_t	sync_offset;	 /* Negotiated sync offset */
	u_int8_t	bus_width;	 /* Negotiated bus width */
	int		fd;		 /* file descriptor for device */
};

__BEGIN_DECLS
/* Basic utility commands */
struct cam_device *	cam_open_device(const char *path, int flags);
void			cam_close_device(struct cam_device *dev);
void			cam_close_spec_device(struct cam_device *dev);
struct cam_device *	cam_open_spec_device(const char *dev_name,
					     int unit, int flags,
					     struct cam_device *device);
struct cam_device *	cam_open_btl(path_id_t path_id, target_id_t target_id,
				     lun_id_t target_lun, int flags,
				     struct cam_device *device);
struct cam_device *	cam_open_pass(const char *path, int flags,
				      struct cam_device *device);
union ccb *		cam_getccb(struct cam_device *dev);
void			cam_freeccb(union ccb *ccb);
int			cam_send_ccb(struct cam_device *device, union ccb *ccb);
char *			cam_path_string(struct cam_device *dev, char *str,
					int len);
struct cam_device *	cam_device_dup(struct cam_device *device);
void			cam_device_copy(struct cam_device *src,
					struct cam_device *dst);
int			cam_get_device(const char *path, char *dev_name,
				       int devnamelen, int *unit);

/*
 * Buffer encoding/decoding routines, from the old SCSI library.
 */
int csio_decode(struct ccb_scsiio *csio, const char *fmt, ...)
		__printflike(2, 3);
int csio_decode_visit(struct ccb_scsiio *csio, const char *fmt,
		      void (*arg_put)(void *, int, void *, int, char *),
		      void *puthook);
int buff_decode(u_int8_t *buff, size_t len, const char *fmt, ...)
		__printflike(3, 4);
int buff_decode_visit(u_int8_t *buff, size_t len, const char *fmt,
		      void (*arg_put)(void *, int, void *, int, char *),
		      void *puthook);
int csio_build(struct ccb_scsiio *csio, u_int8_t *data_ptr,
	       u_int32_t dxfer_len, u_int32_t flags, int retry_count,
	       int timeout, const char *cmd_spec, ...);
int csio_build_visit(struct ccb_scsiio *csio, u_int8_t *data_ptr,
		     u_int32_t dxfer_len, u_int32_t flags, int retry_count,
		     int timeout, const char *cmd_spec,
		     int (*arg_get)(void *hook, char *field_name),
		     void *gethook);
int csio_encode(struct ccb_scsiio *csio, const char *fmt, ...)
		__printflike(2, 3);
int buff_encode_visit(u_int8_t *buff, size_t len, const char *fmt,
		      int (*arg_get)(void *hook, char *field_name),
		      void *gethook);
int csio_encode_visit(struct ccb_scsiio *csio, const char *fmt,
		      int (*arg_get)(void *hook, char *field_name),
		      void *gethook);
__END_DECLS

#endif /* _CAMLIB_H */
