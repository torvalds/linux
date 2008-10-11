#include <dvbdev.h>
#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvb_net.h>
#include <dvb_frontend.h>

struct videobuf_dvb {
	/* filling that the job of the driver */
	char                       *name;
	struct dvb_frontend        *frontend;
	struct videobuf_queue      dvbq;

	/* video-buf-dvb state info */
	struct mutex               lock;
	struct task_struct         *thread;
	int                        nfeeds;

	/* videobuf_dvb_(un)register manges this */
	struct dvb_adapter         adapter;
	struct dvb_demux           demux;
	struct dmxdev              dmxdev;
	struct dmx_frontend        fe_hw;
	struct dmx_frontend        fe_mem;
	struct dvb_net             net;
};

struct videobuf_dvb_frontend {
	void *dev;
	struct list_head felist;
	int id;
	struct videobuf_dvb dvb;
};

struct videobuf_dvb_frontends {
	struct mutex lock;
	struct dvb_adapter adapter;
	int active_fe_id; /* Indicates which frontend in the felist is in use */
	struct videobuf_dvb_frontend frontend;
	int gate; /* Frontend with gate control 0=!MFE,1=fe0,2=fe1 etc */
};

int videobuf_dvb_register_bus(struct videobuf_dvb_frontends *f,
			  struct module *module,
			  void *adapter_priv,
			  struct device *device,
			  short *adapter_nr);   //NEW

void videobuf_dvb_unregister_bus(struct videobuf_dvb_frontends *f);

int videobuf_dvb_register_adapter(struct videobuf_dvb_frontends *f,
			  struct module *module,
			  void *adapter_priv,
			  struct device *device,
			  char *adapter_name,
			  short *adapter_nr);   //NEW

int videobuf_dvb_register_frontend(struct dvb_adapter *adapter, struct videobuf_dvb *dvb);

struct videobuf_dvb_frontend * videobuf_dvb_alloc_frontend(void *private, struct videobuf_dvb_frontends *f, int id);

struct videobuf_dvb_frontend * videobuf_dvb_get_frontend(struct videobuf_dvb_frontends *f, int id);
int videobuf_dvb_find_frontend(struct videobuf_dvb_frontends *f, struct dvb_frontend *p);


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
