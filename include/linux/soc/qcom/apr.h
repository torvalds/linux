/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCOM_APR_H_
#define __QCOM_APR_H_

#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <dt-bindings/soc/qcom,apr.h>

extern struct bus_type aprbus;

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

/* Bits 0 to 15 -- Minor version,  Bits 16 to 31 -- Major version */
#define APR_SVC_MAJOR_VERSION(v)	((v >> 16) & 0xFF)
#define APR_SVC_MINOR_VERSION(v)	(v & 0xFF)

struct packet_router;
struct pkt_router_svc {
	struct device *dev;
	struct packet_router *pr;
	spinlock_t lock;
	int id;
	void *priv;
};

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

#define to_apr_device(d) container_of(d, struct apr_device, dev)
#define svc_to_apr_device(d) container_of(d, struct apr_device, svc)

struct apr_driver {
	int	(*probe)(struct apr_device *sl);
	int	(*remove)(struct apr_device *sl);
	int	(*callback)(struct apr_device *a,
			    struct apr_resp_pkt *d);
	struct device_driver		driver;
	const struct apr_device_id	*id_table;
};

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

int apr_send_pkt(struct apr_device *adev, struct apr_pkt *pkt);

#endif /* __QCOM_APR_H_ */
