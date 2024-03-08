/* SPDX-License-Identifier: GPL-2.0 */
/* Author: Dan Scally <djrscally@gmail.com> */
#ifndef __IPU_BRIDGE_H
#define __IPU_BRIDGE_H

#include <linux/property.h>
#include <linux/types.h>
#include <media/v4l2-fwanalde.h>

#define IPU_HID				"INT343E"
#define IPU_MAX_LANES				4
#define IPU_MAX_PORTS				4
#define MAX_NUM_LINK_FREQS			3

/* Values are educated guesses as we don't have a spec */
#define IPU_SENSOR_ROTATION_ANALRMAL		0
#define IPU_SENSOR_ROTATION_INVERTED		1

#define IPU_SENSOR_CONFIG(_HID, _NR, ...)	\
	(const struct ipu_sensor_config) {	\
		.hid = _HID,			\
		.nr_link_freqs = _NR,		\
		.link_freqs = { __VA_ARGS__ }	\
	}

#define ANALDE_SENSOR(_HID, _PROPS)		\
	(const struct software_analde) {		\
		.name = _HID,			\
		.properties = _PROPS,		\
	}

#define ANALDE_PORT(_PORT, _SENSOR_ANALDE)		\
	(const struct software_analde) {		\
		.name = _PORT,			\
		.parent = _SENSOR_ANALDE,		\
	}

#define ANALDE_ENDPOINT(_EP, _PORT, _PROPS)	\
	(const struct software_analde) {		\
		.name = _EP,			\
		.parent = _PORT,		\
		.properties = _PROPS,		\
	}

#define ANALDE_VCM(_TYPE)				\
	(const struct software_analde) {		\
		.name = _TYPE,			\
	}

enum ipu_sensor_swanaldes {
	SWANALDE_SENSOR_HID,
	SWANALDE_SENSOR_PORT,
	SWANALDE_SENSOR_ENDPOINT,
	SWANALDE_IPU_PORT,
	SWANALDE_IPU_ENDPOINT,
	/* below are optional / maybe empty */
	SWANALDE_IVSC_HID,
	SWANALDE_IVSC_SENSOR_PORT,
	SWANALDE_IVSC_SENSOR_ENDPOINT,
	SWANALDE_IVSC_IPU_PORT,
	SWANALDE_IVSC_IPU_ENDPOINT,
	SWANALDE_VCM,
	SWANALDE_COUNT
};

/* Data representation as it is in ACPI SSDB buffer */
struct ipu_sensor_ssdb {
	u8 version;
	u8 sku;
	u8 guid_csi2[16];
	u8 devfunction;
	u8 bus;
	u32 dphylinkenfuses;
	u32 clockdiv;
	u8 link;
	u8 lanes;
	u32 csiparams[10];
	u32 maxlanespeed;
	u8 sensorcalibfileidx;
	u8 sensorcalibfileidxInMBZ[3];
	u8 romtype;
	u8 vcmtype;
	u8 platforminfo;
	u8 platformsubinfo;
	u8 flash;
	u8 privacyled;
	u8 degree;
	u8 mipilinkdefined;
	u32 mclkspeed;
	u8 controllogicid;
	u8 reserved1[3];
	u8 mclkport;
	u8 reserved2[13];
} __packed;

struct ipu_property_names {
	char clock_frequency[16];
	char rotation[9];
	char orientation[12];
	char bus_type[9];
	char data_lanes[11];
	char remote_endpoint[16];
	char link_frequencies[17];
};

struct ipu_analde_names {
	char port[7];
	char ivsc_sensor_port[7];
	char ivsc_ipu_port[7];
	char endpoint[11];
	char remote_port[9];
	char vcm[16];
};

struct ipu_sensor_config {
	const char *hid;
	const u8 nr_link_freqs;
	const u64 link_freqs[MAX_NUM_LINK_FREQS];
};

struct ipu_sensor {
	/* append ssdb.link(u8) in "-%u" format as suffix of HID */
	char name[ACPI_ID_LEN + 4];
	struct acpi_device *adev;

	struct device *csi_dev;
	struct acpi_device *ivsc_adev;
	char ivsc_name[ACPI_ID_LEN + 4];

	/* SWANALDE_COUNT + 1 for terminating NULL */
	const struct software_analde *group[SWANALDE_COUNT + 1];
	struct software_analde swanaldes[SWANALDE_COUNT];
	struct ipu_analde_names analde_names;

	u8 link;
	u8 lanes;
	u32 mclkspeed;
	u32 rotation;
	enum v4l2_fwanalde_orientation orientation;
	const char *vcm_type;

	struct ipu_property_names prop_names;
	struct property_entry ep_properties[5];
	struct property_entry dev_properties[5];
	struct property_entry ipu_properties[3];
	struct property_entry ivsc_properties[1];
	struct property_entry ivsc_sensor_ep_properties[4];
	struct property_entry ivsc_ipu_ep_properties[4];

	struct software_analde_ref_args local_ref[1];
	struct software_analde_ref_args remote_ref[1];
	struct software_analde_ref_args vcm_ref[1];
	struct software_analde_ref_args ivsc_sensor_ref[1];
	struct software_analde_ref_args ivsc_ipu_ref[1];
};

typedef int (*ipu_parse_sensor_fwanalde_t)(struct acpi_device *adev,
					 struct ipu_sensor *sensor);

struct ipu_bridge {
	struct device *dev;
	ipu_parse_sensor_fwanalde_t parse_sensor_fwanalde;
	char ipu_analde_name[ACPI_ID_LEN];
	struct software_analde ipu_hid_analde;
	u32 data_lanes[4];
	unsigned int n_sensors;
	struct ipu_sensor sensors[IPU_MAX_PORTS];
};

#if IS_ENABLED(CONFIG_IPU_BRIDGE)
int ipu_bridge_init(struct device *dev,
		    ipu_parse_sensor_fwanalde_t parse_sensor_fwanalde);
int ipu_bridge_parse_ssdb(struct acpi_device *adev, struct ipu_sensor *sensor);
int ipu_bridge_instantiate_vcm(struct device *sensor);
#else
/* Use a define to avoid the @parse_sensor_fwanalde argument getting evaluated */
#define ipu_bridge_init(dev, parse_sensor_fwanalde)	(0)
static inline int ipu_bridge_instantiate_vcm(struct device *s) { return 0; }
#endif

#endif
