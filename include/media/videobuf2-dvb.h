#ifndef _VIDEOBUF2_DVB_H_
#define	_VIDEOBUF2_DVB_H_

#include <dvbdev.h>
#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvb_net.h>
#include <dvb_frontend.h>

#include <media/videobuf2-v4l2.h>

/* We don't actually need to include media-device.h here */
struct media_device;

/*
 * TODO: This header file should be replaced with videobuf2-core.h
 * Currently, vb2_thread is not a stuff of videobuf2-core,
 * since vb2_thread has many dependencies on videobuf2-v4l2.
 */

struct vb2_dvb {
	/* filling that the job of the driver */
	char			*name;
	struct dvb_frontend	*frontend;
	struct vb2_queue	dvbq;

	/* video-buf-dvb state info */
	struct mutex		lock;
	int			nfeeds;

	/* vb2_dvb_(un)register manages this */
	struct dvb_demux	demux;
	struct dmxdev		dmxdev;
	struct dmx_frontend	fe_hw;
	struct dmx_frontend	fe_mem;
	struct dvb_net		net;
};

struct vb2_dvb_frontend {
	struct list_head felist;
	int id;
	struct vb2_dvb dvb;
};

struct vb2_dvb_frontends {
	struct list_head felist;
	struct mutex lock;
	struct dvb_adapter adapter;
	int active_fe_id; /* Indicates which frontend in the felist is in use */
	int gate; /* Frontend with gate control 0=!MFE,1=fe0,2=fe1 etc */
};

int vb2_dvb_register_bus(struct vb2_dvb_frontends *f,
			 struct module *module,
			 void *adapter_priv,
			 struct device *device,
			 struct media_device *mdev,
			 short *adapter_nr,
			 int mfe_shared);

void vb2_dvb_unregister_bus(struct vb2_dvb_frontends *f);

struct vb2_dvb_frontend *vb2_dvb_alloc_frontend(struct vb2_dvb_frontends *f, int id);
void vb2_dvb_dealloc_frontends(struct vb2_dvb_frontends *f);

struct vb2_dvb_frontend *vb2_dvb_get_frontend(struct vb2_dvb_frontends *f, int id);
int vb2_dvb_find_frontend(struct vb2_dvb_frontends *f, struct dvb_frontend *p);

#endif			/* _VIDEOBUF2_DVB_H_ */
