/* 
 *  Parallel SCSI (SPI) transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef SCSI_TRANSPORT_SPI_H
#define SCSI_TRANSPORT_SPI_H

#include <linux/transport_class.h>
#include <linux/mutex.h>

struct scsi_transport_template;
struct scsi_target;
struct scsi_device;
struct Scsi_Host;

struct spi_transport_attrs {
	int period;		/* value in the PPR/SDTR command */
	int min_period;
	int offset;
	int max_offset;
	unsigned int width:1;	/* 0 - narrow, 1 - wide */
	unsigned int max_width:1;
	unsigned int iu:1;	/* Information Units enabled */
	unsigned int dt:1;	/* DT clocking enabled */
	unsigned int qas:1;	/* Quick Arbitration and Selection enabled */
	unsigned int wr_flow:1;	/* Write Flow control enabled */
	unsigned int rd_strm:1;	/* Read streaming enabled */
	unsigned int rti:1;	/* Retain Training Information */
	unsigned int pcomp_en:1;/* Precompensation enabled */
	unsigned int hold_mcs:1;/* Hold Margin Control Settings */
	unsigned int initial_dv:1; /* DV done to this target yet  */
	unsigned long flags;	/* flags field for drivers to use */
	/* Device Properties fields */
	unsigned int support_sync:1; /* synchronous support */
	unsigned int support_wide:1; /* wide support */
	unsigned int support_dt:1; /* allows DT phases */
	unsigned int support_dt_only; /* disallows ST phases */
	unsigned int support_ius; /* support Information Units */
	unsigned int support_qas; /* supports quick arbitration and selection */
	/* Private Fields */
	unsigned int dv_pending:1; /* Internal flag: DV Requested */
	unsigned int dv_in_progress:1;	/* Internal: DV started */
	struct mutex dv_mutex; /* semaphore to serialise dv */
};

enum spi_signal_type {
	SPI_SIGNAL_UNKNOWN = 1,
	SPI_SIGNAL_SE,
	SPI_SIGNAL_LVD,
	SPI_SIGNAL_HVD,
};

struct spi_host_attrs {
	enum spi_signal_type signalling;
};

/* accessor functions */
#define spi_period(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->period)
#define spi_min_period(x) (((struct spi_transport_attrs *)&(x)->starget_data)->min_period)
#define spi_offset(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->offset)
#define spi_max_offset(x) (((struct spi_transport_attrs *)&(x)->starget_data)->max_offset)
#define spi_width(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->width)
#define spi_max_width(x) (((struct spi_transport_attrs *)&(x)->starget_data)->max_width)
#define spi_iu(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->iu)
#define spi_dt(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->dt)
#define spi_qas(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->qas)
#define spi_wr_flow(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->wr_flow)
#define spi_rd_strm(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->rd_strm)
#define spi_rti(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->rti)
#define spi_pcomp_en(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->pcomp_en)
#define spi_hold_mcs(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->hold_mcs)
#define spi_initial_dv(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->initial_dv)
#define spi_dv_pending(x) (((struct spi_transport_attrs *)&(x)->starget_data)->dv_pending)

#define spi_support_sync(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_sync)
#define spi_support_wide(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_wide)
#define spi_support_dt(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_dt)
#define spi_support_dt_only(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_dt_only)
#define spi_support_ius(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_ius)
#define spi_support_qas(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->support_qas)

#define spi_flags(x)	(((struct spi_transport_attrs *)&(x)->starget_data)->flags)
#define spi_signalling(h)	(((struct spi_host_attrs *)(h)->shost_data)->signalling)



/* The functions by which the transport class and the driver communicate */
struct spi_function_template {
	void	(*get_period)(struct scsi_target *);
	void	(*set_period)(struct scsi_target *, int);
	void	(*get_offset)(struct scsi_target *);
	void	(*set_offset)(struct scsi_target *, int);
	void	(*get_width)(struct scsi_target *);
	void	(*set_width)(struct scsi_target *, int);
	void	(*get_iu)(struct scsi_target *);
	void	(*set_iu)(struct scsi_target *, int);
	void	(*get_dt)(struct scsi_target *);
	void	(*set_dt)(struct scsi_target *, int);
	void	(*get_qas)(struct scsi_target *);
	void	(*set_qas)(struct scsi_target *, int);
	void	(*get_wr_flow)(struct scsi_target *);
	void	(*set_wr_flow)(struct scsi_target *, int);
	void	(*get_rd_strm)(struct scsi_target *);
	void	(*set_rd_strm)(struct scsi_target *, int);
	void	(*get_rti)(struct scsi_target *);
	void	(*set_rti)(struct scsi_target *, int);
	void	(*get_pcomp_en)(struct scsi_target *);
	void	(*set_pcomp_en)(struct scsi_target *, int);
	void	(*get_hold_mcs)(struct scsi_target *);
	void	(*set_hold_mcs)(struct scsi_target *, int);
	void	(*get_signalling)(struct Scsi_Host *);
	void	(*set_signalling)(struct Scsi_Host *, enum spi_signal_type);
	int	(*deny_binding)(struct scsi_target *);
	/* The driver sets these to tell the transport class it
	 * wants the attributes displayed in sysfs.  If the show_ flag
	 * is not set, the attribute will be private to the transport
	 * class */
	unsigned long	show_period:1;
	unsigned long	show_offset:1;
	unsigned long	show_width:1;
	unsigned long	show_iu:1;
	unsigned long	show_dt:1;
	unsigned long	show_qas:1;
	unsigned long	show_wr_flow:1;
	unsigned long	show_rd_strm:1;
	unsigned long	show_rti:1;
	unsigned long	show_pcomp_en:1;
	unsigned long	show_hold_mcs:1;
};

struct scsi_transport_template *spi_attach_transport(struct spi_function_template *);
void spi_release_transport(struct scsi_transport_template *);
void spi_schedule_dv_device(struct scsi_device *);
void spi_dv_device(struct scsi_device *);
void spi_display_xfer_agreement(struct scsi_target *);
int spi_print_msg(const unsigned char *);
int spi_populate_width_msg(unsigned char *msg, int width);
int spi_populate_sync_msg(unsigned char *msg, int period, int offset);
int spi_populate_ppr_msg(unsigned char *msg, int period, int offset, int width,
		int options);

#endif /* SCSI_TRANSPORT_SPI_H */
