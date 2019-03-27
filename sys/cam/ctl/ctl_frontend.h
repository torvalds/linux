/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
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
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_frontend.h#2 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer front end registration hooks
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_FRONTEND_H_
#define	_CTL_FRONTEND_H_

#include <cam/ctl/ctl_ioctl.h>
#include <sys/nv.h>

typedef enum {
	CTL_PORT_STATUS_NONE		= 0x00,
	CTL_PORT_STATUS_ONLINE		= 0x01,
	CTL_PORT_STATUS_HA_SHARED	= 0x02
} ctl_port_status;

typedef int (*fe_init_t)(void);
typedef int (*fe_shutdown_t)(void);
typedef void (*port_func_t)(void *onoff_arg);
typedef int (*port_info_func_t)(void *onoff_arg, struct sbuf *sb);
typedef	int (*lun_func_t)(void *arg, int lun_id);
typedef int (*fe_ioctl_t)(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
			  struct thread *td);

#define CTL_FRONTEND_DECLARE(name, driver) \
	static int name ## _modevent(module_t mod, int type, void *data) \
	{ \
		switch (type) { \
		case MOD_LOAD: \
			return (ctl_frontend_register( \
				(struct ctl_frontend *)data)); \
			break; \
		case MOD_UNLOAD: \
			return (ctl_frontend_deregister( \
				(struct ctl_frontend *)data)); \
			break; \
		default: \
			return EOPNOTSUPP; \
		} \
		return 0; \
	} \
	static moduledata_t name ## _mod = { \
		#name, \
		name ## _modevent, \
		(void *)&driver \
	}; \
	DECLARE_MODULE(name, name ## _mod, SI_SUB_CONFIGURE, SI_ORDER_FOURTH); \
	MODULE_DEPEND(name, ctl, 1, 1, 1); \
	MODULE_DEPEND(name, cam, 1, 1, 1)

struct ctl_wwpn_iid {
	int in_use;
	time_t last_use;
	uint64_t wwpn;
	char *name;
};

/*
 * The ctl_frontend structure is the registration mechanism between a FETD
 * (Front End Target Driver) and the CTL layer.  Here is a description of
 * the fields:
 *
 * port_type:		  This field tells CTL what kind of front end it is
 *                        dealing with.  This field serves two purposes.
 *                        The first is to let CTL know whether the frontend
 *                        in question is inside the main CTL module (i.e.
 *                        the ioctl front end), and therefore its module
 *                        reference count shouldn't be incremented.  The
 *                        CTL ioctl front end should continue to use the
 *                        CTL_PORT_IOCTL argument as long as it is part of
 *                        the main CTL module.  The second is to let CTL
 *                        know what kind of front end it is dealing with, so
 *                        it can return the proper inquiry data for that
 *                        particular port.
 *
 * num_requested_ctl_io:  This is the number of ctl_io structures that the
 *			  front end needs for its pool.  This should
 * 			  generally be the maximum number of outstanding
 *			  transactions that the FETD can handle.  The CTL
 *			  layer will add a few to this to account for
 *			  ctl_io buffers queued for pending sense data.
 *			  (Pending sense only gets queued if the FETD
 * 			  doesn't support autosense.  e.g. non-packetized
 * 			  parallel SCSI doesn't support autosense.)
 *
 * port_name:		  A string describing the FETD.  e.g. "LSI 1030T U320"
 *			  or whatever you want to use to describe the driver.
 *
 * physical_port:	  This is the physical port number of this
 * 			  particular port within the driver/hardware.  This
 * 			  number is hardware/driver specific.
 * virtual_port:	  This is the virtual port number of this
 * 			  particular port.  This is for things like NP-IV.
 * 
 * port_online():	  This function is called, with onoff_arg as its
 *			  argument, by the CTL layer when it wants the FETD
 *			  to start responding to selections on the specified
 * 			  target ID.
 *
 * port_offline():	  This function is called, with onoff_arg as its
 *			  argument, by the CTL layer when it wants the FETD
 * 			  to stop responding to selection on the specified
 * 			  target ID.
 *
 * onoff_arg:		  This is supplied as an argument to port_online()
 *			  and port_offline().  This is specified by the
 *			  FETD.
 *
 * lun_enable():	  This function is called, with targ_lun_arg, a target
 *			  ID and a LUN ID as its arguments, by CTL when it
 *			  wants the FETD to enable a particular LUN.  If the
 *			  FETD doesn't really know about LUNs, it should
 *			  just ignore this call and return 0.  If the FETD
 *			  cannot enable the requested LUN for some reason, the
 *			  FETD should return non-zero status.
 *
 * lun_disable():	  This function is called, with targ_lun_arg, a target
 *			  ID and LUN ID as its arguments, by CTL when it
 *			  wants the FETD to disable a particular LUN.  If the
 *			  FETD doesn't really know about LUNs, it should just
 *			  ignore this call and return 0.  If the FETD cannot
 *			  disable the requested LUN for some reason, the
 *			  FETD should return non-zero status.
 *
 * targ_lun_arg:	  This is supplied as an argument to the targ/lun
 *			  enable/disable() functions.  This is specified by
 *			  the FETD.
 *
 * fe_datamove():	  This function is called one or more times per I/O
 *			  by the CTL layer to tell the FETD to initiate a
 *			  DMA to or from the data buffer(s) specified by
 * 			  the passed-in ctl_io structure.
 *
 * fe_done():	  	  This function is called by the CTL layer when a
 *			  particular SCSI I/O or task management command has
 * 			  completed.  For SCSI I/O requests (CTL_IO_SCSI),
 *			  sense data is always supplied if the status is
 *			  CTL_SCSI_ERROR and the SCSI status byte is
 *			  SCSI_STATUS_CHECK_COND.  If the FETD doesn't
 *			  support autosense, the sense should be queued
 *			  back to the CTL layer via ctl_queue_sense().
 *
 * fe_dump():		  This function, if it exists, is called by CTL
 *			  to request a dump of any debugging information or
 *			  state to the console.
 *
 * targ_port:		  The CTL layer assigns a "port number" to every
 *			  FETD.  This port number should be passed back in
 *			  in the header of every ctl_io that is queued to
 *			  the CTL layer.  This enables us to determine
 *			  which bus the command came in on.
 *
 * ctl_pool_ref:	  Memory pool reference used by the FETD in calls to
 * 			  ctl_alloc_io().
 *
 * max_initiators:	  Maximum number of initiators that the FETD is
 *			  allowed to have.  Initiators should be numbered
 *			  from 0 to max_initiators - 1.  This value will
 * 			  typically be 16, and thus not a problem for
 * 			  parallel SCSI.  This may present issues for Fibre
 *			  Channel.
 *
 * wwnn			  World Wide Node Name to be used by the FETD.
 *			  Note that this is set *after* registration.  It
 * 			  will be set prior to the online function getting
 * 			  called.
 *
 * wwpn			  World Wide Port Name to be used by the FETD.
 *			  Note that this is set *after* registration.  It
 * 			  will be set prior to the online function getting
 * 			  called.
 *
 * status:		  Used by CTL to keep track of per-FETD state.
 *
 * links:		  Linked list pointers, used by CTL.  The FETD
 *			  shouldn't touch this field.
 */
struct ctl_port {
	struct ctl_softc *ctl_softc;
	struct ctl_frontend *frontend;
	ctl_port_type	port_type;		/* passed to CTL */
	int		num_requested_ctl_io;	/* passed to CTL */
	char		*port_name;		/* passed to CTL */
	int		physical_port;		/* passed to CTL */
	int		virtual_port;		/* passed to CTL */
	port_func_t	port_online;		/* passed to CTL */
	port_func_t	port_offline;		/* passed to CTL */
	port_info_func_t port_info;		/* passed to CTL */
	void		*onoff_arg;		/* passed to CTL */
	lun_func_t	lun_enable;		/* passed to CTL */
	lun_func_t	lun_disable;		/* passed to CTL */
	int		lun_map_size;		/* passed to CTL */
	uint32_t	*lun_map;		/* passed to CTL */
	void		*targ_lun_arg;		/* passed to CTL */
	void		(*fe_datamove)(union ctl_io *io); /* passed to CTL */
	void		(*fe_done)(union ctl_io *io); /* passed to CTL */
	int32_t		targ_port;		/* passed back to FETD */
	void		*ctl_pool_ref;		/* passed back to FETD */
	uint32_t	max_initiators;		/* passed back to FETD */
	struct ctl_wwpn_iid *wwpn_iid;		/* used by CTL */
	uint64_t	wwnn;			/* set by CTL before online */
	uint64_t	wwpn;			/* set by CTL before online */
	ctl_port_status	status;			/* used by CTL */
	nvlist_t	*options;		/* passed to CTL */
	struct ctl_devid *port_devid;		/* passed to CTL */
	struct ctl_devid *target_devid;		/* passed to CTL */
	struct ctl_devid *init_devid;		/* passed to CTL */
	struct ctl_io_stats stats;		/* used by CTL */
	struct mtx	port_lock;		/* used by CTL */
	STAILQ_ENTRY(ctl_port) fe_links;	/* used by CTL */
	STAILQ_ENTRY(ctl_port) links;		/* used by CTL */
};

struct ctl_frontend {
	char		name[CTL_DRIVER_NAME_LEN];	/* passed to CTL */
	fe_init_t	init;			/* passed to CTL */
	fe_ioctl_t	ioctl;			/* passed to CTL */
	void		(*fe_dump)(void);	/* passed to CTL */
	fe_shutdown_t	shutdown;		/* passed to CTL */
	STAILQ_HEAD(, ctl_port) port_list;	/* used by CTL */
	STAILQ_ENTRY(ctl_frontend) links;	/* used by CTL */
};

/*
 * This may block until resources are allocated.  Called at FETD module load
 * time. Returns 0 for success, non-zero for failure.
 */
int ctl_frontend_register(struct ctl_frontend *fe);

/*
 * Called at FETD module unload time.
 * Returns 0 for success, non-zero for failure.
 */
int ctl_frontend_deregister(struct ctl_frontend *fe);

/*
 * Find the frontend by its name. Returns NULL if not found.
 */
struct ctl_frontend * ctl_frontend_find(char *frontend_name);

/*
 * This may block until resources are allocated.  Called at FETD module load
 * time. Returns 0 for success, non-zero for failure.
 */
int ctl_port_register(struct ctl_port *port);

/*
 * Called at FETD module unload time.
 * Returns 0 for success, non-zero for failure.
 */
int ctl_port_deregister(struct ctl_port *port);

/*
 * Called to set the WWNN and WWPN for a particular frontend.
 */
void ctl_port_set_wwns(struct ctl_port *port, int wwnn_valid,
			   uint64_t wwnn, int wwpn_valid, uint64_t wwpn);

/*
 * Called to bring a particular frontend online.
 */
void ctl_port_online(struct ctl_port *fe);

/*
 * Called to take a particular frontend offline.
 */
void ctl_port_offline(struct ctl_port *fe);

/*
 * This routine queues I/O and task management requests from the FETD to the
 * CTL layer.  Returns immediately.  Returns 0 for success, non-zero for
 * failure.
 */
int ctl_queue(union ctl_io *io);

/*
 * This routine is used if the front end interface doesn't support
 * autosense (e.g. non-packetized parallel SCSI).  This will queue the
 * scsiio structure back to a per-lun pending sense queue.  This MUST be
 * called BEFORE any request sense can get queued to the CTL layer -- I
 * need it in the queue in order to service the request.  The scsiio
 * structure passed in here will be freed by the CTL layer when sense is
 * retrieved by the initiator.  Returns 0 for success, non-zero for failure.
 */
int ctl_queue_sense(union ctl_io *io);

/*
 * This routine adds an initiator to CTL's port database.
 * The iid field should be the same as the iid passed in the nexus of each
 * ctl_io from this initiator.
 * The WWPN should be the FC WWPN, if available.
 */
int ctl_add_initiator(struct ctl_port *port, int iid, uint64_t wwpn, char *name);

/*
 * This routine will remove an initiator from CTL's port database.
 * The iid field should be the same as the iid passed in the nexus of each
 * ctl_io from this initiator.
 */
int ctl_remove_initiator(struct ctl_port *port, int iid);

#endif	/* _CTL_FRONTEND_H_ */
