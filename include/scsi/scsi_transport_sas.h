/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SCSI_TRANSPORT_SAS_H
#define SCSI_TRANSPORT_SAS_H

#include <linux/transport_class.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <scsi/sas.h>
#include <linux/bsg-lib.h>

struct scsi_transport_template;
struct sas_rphy;
struct request;

#if !IS_ENABLED(CONFIG_SCSI_SAS_ATTRS)
static inline int scsi_is_sas_rphy(const struct device *sdev)
{
	return 0;
}
#else
extern int scsi_is_sas_rphy(const struct device *);
#endif

static inline int sas_protocol_ata(enum sas_protocol proto)
{
	return ((proto & SAS_PROTOCOL_SATA) ||
		(proto & SAS_PROTOCOL_STP))? 1 : 0;
}

enum sas_linkrate {
	/* These Values are defined in the SAS standard */
	SAS_LINK_RATE_UNKNOWN = 0,
	SAS_PHY_DISABLED = 1,
	SAS_PHY_RESET_PROBLEM = 2,
	SAS_SATA_SPINUP_HOLD = 3,
	SAS_SATA_PORT_SELECTOR = 4,
	SAS_PHY_RESET_IN_PROGRESS = 5,
	SAS_LINK_RATE_1_5_GBPS = 8,
	SAS_LINK_RATE_G1 = SAS_LINK_RATE_1_5_GBPS,
	SAS_LINK_RATE_3_0_GBPS = 9,
	SAS_LINK_RATE_G2 = SAS_LINK_RATE_3_0_GBPS,
	SAS_LINK_RATE_6_0_GBPS = 10,
	SAS_LINK_RATE_12_0_GBPS = 11,
	SAS_LINK_RATE_22_5_GBPS = 12,
	/* These are virtual to the transport class and may never
	 * be signalled normally since the standard defined field
	 * is only 4 bits */
	SAS_LINK_RATE_FAILED = 0x10,
	SAS_PHY_VIRTUAL = 0x11,
};

struct sas_identify {
	enum sas_device_type	device_type;
	enum sas_protocol	initiator_port_protocols;
	enum sas_protocol	target_port_protocols;
	u64			sas_address;
	u8			phy_identifier;
};

struct sas_phy {
	struct device		dev;
	int			number;
	int			enabled;

	/* phy identification */
	struct sas_identify	identify;

	/* phy attributes */
	enum sas_linkrate	negotiated_linkrate;
	enum sas_linkrate	minimum_linkrate_hw;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate_hw;
	enum sas_linkrate	maximum_linkrate;

	/* link error statistics */
	u32			invalid_dword_count;
	u32			running_disparity_error_count;
	u32			loss_of_dword_sync_count;
	u32			phy_reset_problem_count;

	/* for the list of phys belonging to a port */
	struct list_head	port_siblings;

	/* available to the lldd */
	void			*hostdata;
};

#define dev_to_phy(d) \
	container_of((d), struct sas_phy, dev)
#define transport_class_to_phy(dev) \
	dev_to_phy((dev)->parent)
#define phy_to_shost(phy) \
	dev_to_shost((phy)->dev.parent)

struct request_queue;
struct sas_rphy {
	struct device		dev;
	struct sas_identify	identify;
	struct list_head	list;
	struct request_queue	*q;
	u32			scsi_target_id;
};

#define dev_to_rphy(d) \
	container_of((d), struct sas_rphy, dev)
#define transport_class_to_rphy(dev) \
	dev_to_rphy((dev)->parent)
#define rphy_to_shost(rphy) \
	dev_to_shost((rphy)->dev.parent)
#define target_to_rphy(targ) \
	dev_to_rphy((targ)->dev.parent)

struct sas_end_device {
	struct sas_rphy		rphy;
	/* flags */
	unsigned		ready_led_meaning:1;
	unsigned		tlr_supported:1;
	unsigned		tlr_enabled:1;
	/* parameters */
	u16			I_T_nexus_loss_timeout;
	u16			initiator_response_timeout;
};
#define rphy_to_end_device(r) \
	container_of((r), struct sas_end_device, rphy)

struct sas_expander_device {
	int    level;
	int    next_port_id;

	#define SAS_EXPANDER_VENDOR_ID_LEN	8
	char   vendor_id[SAS_EXPANDER_VENDOR_ID_LEN+1];
	#define SAS_EXPANDER_PRODUCT_ID_LEN	16
	char   product_id[SAS_EXPANDER_PRODUCT_ID_LEN+1];
	#define SAS_EXPANDER_PRODUCT_REV_LEN	4
	char   product_rev[SAS_EXPANDER_PRODUCT_REV_LEN+1];
	#define SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN	8
	char   component_vendor_id[SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN+1];
	u16    component_id;
	u8     component_revision_id;

	struct sas_rphy		rphy;

};
#define rphy_to_expander_device(r) \
	container_of((r), struct sas_expander_device, rphy)

struct sas_port {
	struct device		dev;

	int			port_identifier;
	int			num_phys;
	/* port flags */
	unsigned int		is_backlink:1;

	/* the other end of the link */
	struct sas_rphy		*rphy;

	struct mutex		phy_list_mutex;
	struct list_head	phy_list;
	struct list_head	del_list; /* libsas only */
};

#define dev_to_sas_port(d) \
	container_of((d), struct sas_port, dev)
#define transport_class_to_sas_port(dev) \
	dev_to_sas_port((dev)->parent)

struct sas_phy_linkrates {
	enum sas_linkrate maximum_linkrate;
	enum sas_linkrate minimum_linkrate;
};

/* The functions by which the transport class and the driver communicate */
struct sas_function_template {
	int (*get_linkerrors)(struct sas_phy *);
	int (*get_enclosure_identifier)(struct sas_rphy *, u64 *);
	int (*get_bay_identifier)(struct sas_rphy *);
	int (*phy_reset)(struct sas_phy *, int);
	int (*phy_enable)(struct sas_phy *, int);
	int (*phy_setup)(struct sas_phy *);
	void (*phy_release)(struct sas_phy *);
	int (*set_phy_speed)(struct sas_phy *, struct sas_phy_linkrates *);
	void (*smp_handler)(struct bsg_job *, struct Scsi_Host *,
			struct sas_rphy *);
};


void sas_remove_children(struct device *);
extern void sas_remove_host(struct Scsi_Host *);

extern struct sas_phy *sas_phy_alloc(struct device *, int);
extern void sas_phy_free(struct sas_phy *);
extern int sas_phy_add(struct sas_phy *);
extern void sas_phy_delete(struct sas_phy *);
extern int scsi_is_sas_phy(const struct device *);

u64 sas_get_address(struct scsi_device *);
unsigned int sas_tlr_supported(struct scsi_device *);
unsigned int sas_is_tlr_enabled(struct scsi_device *);
void sas_disable_tlr(struct scsi_device *);
void sas_enable_tlr(struct scsi_device *);

bool sas_ata_ncq_prio_supported(struct scsi_device *sdev);

extern struct sas_rphy *sas_end_device_alloc(struct sas_port *);
extern struct sas_rphy *sas_expander_alloc(struct sas_port *, enum sas_device_type);
void sas_rphy_free(struct sas_rphy *);
extern int sas_rphy_add(struct sas_rphy *);
extern void sas_rphy_remove(struct sas_rphy *);
extern void sas_rphy_delete(struct sas_rphy *);
extern void sas_rphy_unlink(struct sas_rphy *);

struct sas_port *sas_port_alloc(struct device *, int);
struct sas_port *sas_port_alloc_num(struct device *);
int sas_port_add(struct sas_port *);
void sas_port_free(struct sas_port *);
void sas_port_delete(struct sas_port *);
void sas_port_add_phy(struct sas_port *, struct sas_phy *);
void sas_port_delete_phy(struct sas_port *, struct sas_phy *);
void sas_port_mark_backlink(struct sas_port *);
int scsi_is_sas_port(const struct device *);
struct sas_phy *sas_port_get_phy(struct sas_port *port);
static inline void sas_port_put_phy(struct sas_phy *phy)
{
	if (phy)
		put_device(&phy->dev);
}

extern struct scsi_transport_template *
sas_attach_transport(struct sas_function_template *);
extern void sas_release_transport(struct scsi_transport_template *);
int sas_read_port_mode_page(struct scsi_device *);

static inline int
scsi_is_sas_expander_device(struct device *dev)
{
	struct sas_rphy *rphy;
	if (!scsi_is_sas_rphy(dev))
		return 0;
	rphy = dev_to_rphy(dev);
	return rphy->identify.device_type == SAS_FANOUT_EXPANDER_DEVICE ||
		rphy->identify.device_type == SAS_EDGE_EXPANDER_DEVICE;
}

#define scsi_is_sas_phy_local(phy)	scsi_is_host_device((phy)->dev.parent)

#endif /* SCSI_TRANSPORT_SAS_H */
