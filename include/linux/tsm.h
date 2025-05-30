/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TSM_H
#define __TSM_H

#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/uuid.h>

#define TSM_REPORT_INBLOB_MAX 64
#define TSM_REPORT_OUTBLOB_MAX SZ_32K

/*
 * Privilege level is a nested permission concept to allow confidential
 * guests to partition address space, 4-levels are supported.
 */
#define TSM_REPORT_PRIVLEVEL_MAX 3

/**
 * struct tsm_report_desc - option descriptor for generating tsm report blobs
 * @privlevel: optional privilege level to associate with @outblob
 * @inblob_len: sizeof @inblob
 * @inblob: arbitrary input data
 * @service_provider: optional name of where to obtain the tsm report blob
 * @service_guid: optional service-provider service guid to attest
 * @service_manifest_version: optional service-provider service manifest version requested
 */
struct tsm_report_desc {
	unsigned int privlevel;
	size_t inblob_len;
	u8 inblob[TSM_REPORT_INBLOB_MAX];
	char *service_provider;
	guid_t service_guid;
	unsigned int service_manifest_version;
};

/**
 * struct tsm_report - track state of report generation relative to options
 * @desc: input parameters to @report_new()
 * @outblob_len: sizeof(@outblob)
 * @outblob: generated evidence to provider to the attestation agent
 * @auxblob_len: sizeof(@auxblob)
 * @auxblob: (optional) auxiliary data to the report (e.g. certificate data)
 * @manifestblob_len: sizeof(@manifestblob)
 * @manifestblob: (optional) manifest data associated with the report
 */
struct tsm_report {
	struct tsm_report_desc desc;
	size_t outblob_len;
	u8 *outblob;
	size_t auxblob_len;
	u8 *auxblob;
	size_t manifestblob_len;
	u8 *manifestblob;
};

/**
 * enum tsm_attr_index - index used to reference report attributes
 * @TSM_REPORT_GENERATION: index of the report generation number attribute
 * @TSM_REPORT_PROVIDER: index of the provider name attribute
 * @TSM_REPORT_PRIVLEVEL: index of the desired privilege level attribute
 * @TSM_REPORT_PRIVLEVEL_FLOOR: index of the minimum allowed privileg level attribute
 * @TSM_REPORT_SERVICE_PROVIDER: index of the service provider identifier attribute
 * @TSM_REPORT_SERVICE_GUID: index of the service GUID attribute
 * @TSM_REPORT_SERVICE_MANIFEST_VER: index of the service manifest version attribute
 */
enum tsm_attr_index {
	TSM_REPORT_GENERATION,
	TSM_REPORT_PROVIDER,
	TSM_REPORT_PRIVLEVEL,
	TSM_REPORT_PRIVLEVEL_FLOOR,
	TSM_REPORT_SERVICE_PROVIDER,
	TSM_REPORT_SERVICE_GUID,
	TSM_REPORT_SERVICE_MANIFEST_VER,
};

/**
 * enum tsm_bin_attr_index - index used to reference binary report attributes
 * @TSM_REPORT_INBLOB: index of the binary report input attribute
 * @TSM_REPORT_OUTBLOB: index of the binary report output attribute
 * @TSM_REPORT_AUXBLOB: index of the binary auxiliary data attribute
 * @TSM_REPORT_MANIFESTBLOB: index of the binary manifest data attribute
 */
enum tsm_bin_attr_index {
	TSM_REPORT_INBLOB,
	TSM_REPORT_OUTBLOB,
	TSM_REPORT_AUXBLOB,
	TSM_REPORT_MANIFESTBLOB,
};

/**
 * struct tsm_report_ops - attributes and operations for tsm_report instances
 * @name: tsm id reflected in /sys/kernel/config/tsm/report/$report/provider
 * @privlevel_floor: convey base privlevel for nested scenarios
 * @report_new: Populate @report with the report blob and auxblob
 * (optional), return 0 on successful population, or -errno otherwise
 * @report_attr_visible: show or hide a report attribute entry
 * @report_bin_attr_visible: show or hide a report binary attribute entry
 *
 * Implementation specific ops, only one is expected to be registered at
 * a time i.e. only one of "sev-guest", "tdx-guest", etc.
 */
struct tsm_report_ops {
	const char *name;
	unsigned int privlevel_floor;
	int (*report_new)(struct tsm_report *report, void *data);
	bool (*report_attr_visible)(int n);
	bool (*report_bin_attr_visible)(int n);
};

int tsm_report_register(const struct tsm_report_ops *ops, void *priv);
int tsm_report_unregister(const struct tsm_report_ops *ops);
#endif /* __TSM_H */
