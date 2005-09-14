#ifndef SCSI_TRANSPORT_SAS_H
#define SCSI_TRANSPORT_SAS_H

#include <linux/transport_class.h>
#include <linux/types.h>

struct scsi_transport_template;
struct sas_rphy;


enum sas_device_type {
	SAS_PHY_UNUSED,
	SAS_END_DEVICE,
	SAS_EDGE_EXPANDER_DEVICE,
	SAS_FANOUT_EXPANDER_DEVICE,
};

enum sas_protocol {
	SAS_PROTOCOL_SATA		= 0x01,
	SAS_PROTOCOL_SMP		= 0x02,
	SAS_PROTOCOL_STP		= 0x04,
	SAS_PROTOCOL_SSP		= 0x08,
};

enum sas_linkrate {
	SAS_LINK_RATE_UNKNOWN,
	SAS_PHY_DISABLED,
	SAS_LINK_RATE_FAILED,
	SAS_SATA_SPINUP_HOLD,
	SAS_SATA_PORT_SELECTOR,
	SAS_LINK_RATE_1_5_GBPS,
	SAS_LINK_RATE_3_0_GBPS,
	SAS_LINK_VIRTUAL,
};

struct sas_identify {
	enum sas_device_type	device_type;
	enum sas_protocol	initiator_port_protocols;
	enum sas_protocol	target_port_protocols;
	u64			sas_address;
	u8			phy_identifier;
};

/* The functions by which the transport class and the driver communicate */
struct sas_function_template {
};

struct sas_phy {
	struct device		dev;
	int			number;
	struct sas_identify	identify;
	enum sas_linkrate	negotiated_linkrate;
	enum sas_linkrate	minimum_linkrate_hw;
	enum sas_linkrate	minimum_linkrate;
	enum sas_linkrate	maximum_linkrate_hw;
	enum sas_linkrate	maximum_linkrate;
	u8			port_identifier;
	struct sas_rphy		*rphy;
};

#define dev_to_phy(d) \
	container_of((d), struct sas_phy, dev)
#define transport_class_to_phy(cdev) \
	dev_to_phy((cdev)->dev)
#define phy_to_shost(phy) \
	dev_to_shost((phy)->dev.parent)

struct sas_rphy {
	struct device		dev;
	struct sas_identify	identify;
	struct list_head	list;
	u32			scsi_target_id;
};

#define dev_to_rphy(d) \
	container_of((d), struct sas_rphy, dev)
#define transport_class_to_rphy(cdev) \
	dev_to_rphy((cdev)->dev)
#define rphy_to_shost(rphy) \
	dev_to_shost((rphy)->dev.parent)

extern void sas_remove_host(struct Scsi_Host *);

extern struct sas_phy *sas_phy_alloc(struct device *, int);
extern void sas_phy_free(struct sas_phy *);
extern int sas_phy_add(struct sas_phy *);
extern void sas_phy_delete(struct sas_phy *);
extern int scsi_is_sas_phy(const struct device *);

extern struct sas_rphy *sas_rphy_alloc(struct sas_phy *);
void sas_rphy_free(struct sas_rphy *);
extern int sas_rphy_add(struct sas_rphy *);
extern void sas_rphy_delete(struct sas_rphy *);
extern int scsi_is_sas_rphy(const struct device *);

extern struct scsi_transport_template *
sas_attach_transport(struct sas_function_template *);
extern void sas_release_transport(struct scsi_transport_template *);

#endif /* SCSI_TRANSPORT_SAS_H */
