/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_APR_H_
#define __QCOM_APR_H_

#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <dt-bindings/soc/qcom,apr.h>
#include <dt-bindings/soc/qcom,gpr.h>

extern const struct bus_type aprbus;

#define APR_HDR_LEN(hdr_len) ((hdr_len)/4)

/*
 * HEADER field
 * version:0:3
 * header_size : 4:7
 * message_type : 8:9
 * reserved: 10:15
 */
#define APR_HDR_FIELD(msg_type, hdr_len, ver)\
	(((msg_type & 0x3) << 8) | ((hdr_len & 0xF) << 4) | (ver & 0xF))

#define APR_HDR_SIZE sizeof(struct apr_hdr)
#define APR_SEQ_CMD_HDR_FIELD APR_HDR_FIELD(APR_MSG_TYPE_SEQ_CMD, \
					    APR_HDR_LEN(APR_HDR_SIZE), \
					    APR_PKT_VER)
/* Version */
#define APR_PKT_VER		0x0

/* Command and Response Types */
#define APR_MSG_TYPE_EVENT	0x0
#define APR_MSG_TYPE_CMD_RSP	0x1
#define APR_MSG_TYPE_SEQ_CMD	0x2
#define APR_MSG_TYPE_NSEQ_CMD	0x3
#define APR_MSG_TYPE_MAX	0x04

/* APR Basic Response Message */
#define APR_BASIC_RSP_RESULT 0x000110E8
#define APR_RSP_ACCEPTED     0x000100BE

struct aprv2_ibasic_rsp_result_t {
	uint32_t opcode;
	uint32_t status;
};

/* hdr field Ver [0:3], Size [4:7], Message type [8:10] */
#define APR_HDR_FIELD_VER(h)		(h & 0x000F)
#define APR_HDR_FIELD_SIZE(h)		((h & 0x00F0) >> 4)
#define APR_HDR_FIELD_SIZE_BYTES(h)	(((h & 0x00F0) >> 4) * 4)
#define APR_HDR_FIELD_MT(h)		((h & 0x0300) >> 8)

struct apr_hdr {
	uint16_t hdr_field;
	uint16_t pkt_size;
	uint8_t src_svc;
	uint8_t src_domain;
	uint16_t src_port;
	uint8_t dest_svc;
	uint8_t dest_domain;
	uint16_t dest_port;
	uint32_t token;
	uint32_t opcode;
} __packed;

struct apr_pkt {
	struct apr_hdr hdr;
	uint8_t payload[];
};

struct apr_resp_pkt {
	struct apr_hdr hdr;
	void *payload;
	int payload_size;
};

struct gpr_hdr {
	uint32_t version:4;
	uint32_t hdr_size:4;
	uint32_t pkt_size:24;
	uint32_t dest_domain:8;
	uint32_t src_domain:8;
	uint32_t reserved:16;
	uint32_t src_port;
	uint32_t dest_port;
	uint32_t token;
	uint32_t opcode;
} __packed;

struct gpr_pkt {
	struct gpr_hdr hdr;
	uint32_t payload[];
};

struct gpr_resp_pkt {
	struct gpr_hdr hdr;
	void *payload;
	int payload_size;
};

#define GPR_HDR_SIZE			sizeof(struct gpr_hdr)
#define GPR_PKT_VER			0x0
#define GPR_PKT_HEADER_WORD_SIZE	((sizeof(struct gpr_pkt) + 3) >> 2)
#define GPR_PKT_HEADER_BYTE_SIZE	(GPR_PKT_HEADER_WORD_SIZE << 2)

#define GPR_BASIC_RSP_RESULT		0x02001005

struct gpr_ibasic_rsp_result_t {
	uint32_t opcode;
	uint32_t status;
};

#define GPR_BASIC_EVT_ACCEPTED		0x02001006

struct gpr_ibasic_rsp_accepted_t {
	uint32_t opcode;
};

/* Bits 0 to 15 -- Minor version,  Bits 16 to 31 -- Major version */
#define APR_SVC_MAJOR_VERSION(v)	((v >> 16) & 0xFF)
#define APR_SVC_MINOR_VERSION(v)	(v & 0xFF)

typedef int (*gpr_port_cb) (struct gpr_resp_pkt *d, void *priv, int op);
struct packet_router;
struct pkt_router_svc {
	struct device *dev;
	gpr_port_cb callback;
	struct packet_router *pr;
	spinlock_t lock;
	int id;
	void *priv;
};

typedef struct pkt_router_svc gpr_port_t;

struct apr_device {
	struct device	dev;
	uint16_t	svc_id;
	uint16_t	domain_id;
	uint32_t	version;
	char name[APR_NAME_SIZE];
	const char *service_path;
	struct pkt_router_svc svc;
	struct list_head node;
};

typedef struct apr_device gpr_device_t;

#define to_apr_device(d) container_of(d, struct apr_device, dev)
#define svc_to_apr_device(d) container_of(d, struct apr_device, svc)

struct apr_driver {
	int	(*probe)(struct apr_device *sl);
	void	(*remove)(struct apr_device *sl);
	int	(*callback)(struct apr_device *a,
			    struct apr_resp_pkt *d);
	int	(*gpr_callback)(struct gpr_resp_pkt *d, void *data, int op);
	struct device_driver		driver;
	const struct apr_device_id	*id_table;
};

typedef struct apr_driver gpr_driver_t;
#define to_apr_driver(d) container_of(d, struct apr_driver, driver)

/*
 * use a macro to avoid include chaining to get THIS_MODULE
 */
#define apr_driver_register(drv) __apr_driver_register(drv, THIS_MODULE)

int __apr_driver_register(struct apr_driver *drv, struct module *owner);
void apr_driver_unregister(struct apr_driver *drv);

/**
 * module_apr_driver() - Helper macro for registering a aprbus driver
 * @__apr_driver: apr_driver struct
 *
 * Helper macro for aprbus drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module
 * may only use this macro once, and calling it replaces module_init()
 * and module_exit()
 */
#define module_apr_driver(__apr_driver) \
	module_driver(__apr_driver, apr_driver_register, \
			apr_driver_unregister)
#define module_gpr_driver(__gpr_driver) module_apr_driver(__gpr_driver)

int apr_send_pkt(struct apr_device *adev, struct apr_pkt *pkt);

gpr_port_t *gpr_alloc_port(gpr_device_t *gdev, struct device *dev,
				gpr_port_cb cb, void *priv);
void gpr_free_port(gpr_port_t *port);
int gpr_send_port_pkt(gpr_port_t *port, struct gpr_pkt *pkt);
int gpr_send_pkt(gpr_device_t *gdev, struct gpr_pkt *pkt);

#endif /* __QCOM_APR_H_ */
