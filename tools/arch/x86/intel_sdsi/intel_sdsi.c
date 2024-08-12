// SPDX-License-Identifier: GPL-2.0
/*
 * sdsi: Intel On Demand (formerly Software Defined Silicon) tool for
 * provisioning certificates and activation payloads on supported cpus.
 *
 * See https://github.com/intel/intel-sdsi/blob/master/os-interface.rst
 * for register descriptions.
 *
 * Copyright (C) 2022 Intel Corporation. All rights reserved.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define min(x, y) ({                            \
	typeof(x) _min1 = (x);                  \
	typeof(y) _min2 = (y);                  \
	(void) (&_min1 == &_min2);              \
	_min1 < _min2 ? _min1 : _min2; })

#define SDSI_DEV		"intel_vsec.sdsi"
#define AUX_DEV_PATH		"/sys/bus/auxiliary/devices/"
#define SDSI_PATH		(AUX_DEV_DIR SDSI_DEV)
#define GUID_V1			0x6dd191
#define REGS_SIZE_GUID_V1	72
#define GUID_V2			0xF210D9EF
#define REGS_SIZE_GUID_V2	80
#define STATE_CERT_MAX_SIZE	4096
#define METER_CERT_MAX_SIZE	4096
#define STATE_MAX_NUM_LICENSES	16
#define STATE_MAX_NUM_IN_BUNDLE	(uint32_t)8
#define FEAT_LEN		5	/* 4 plus NUL terminator */

#define __round_mask(x, y) ((__typeof__(x))((y) - 1))
#define round_up(x, y) ((((x) - 1) | __round_mask(x, y)) + 1)

struct nvram_content_auth_err_sts {
	uint64_t reserved:3;
	uint64_t sdsi_content_auth_err:1;
	uint64_t reserved1:1;
	uint64_t sdsi_metering_auth_err:1;
	uint64_t reserved2:58;
};

struct enabled_features {
	uint64_t reserved:3;
	uint64_t sdsi:1;
	uint64_t reserved1:8;
	uint64_t attestation:1;
	uint64_t reserved2:13;
	uint64_t metering:1;
	uint64_t reserved3:37;
};

struct key_provision_status {
	uint64_t reserved:1;
	uint64_t license_key_provisioned:1;
	uint64_t reserved2:62;
};

struct auth_fail_count {
	uint64_t key_failure_count:3;
	uint64_t key_failure_threshold:3;
	uint64_t auth_failure_count:3;
	uint64_t auth_failure_threshold:3;
	uint64_t reserved:52;
};

struct availability {
	uint64_t reserved:48;
	uint64_t available:3;
	uint64_t threshold:3;
	uint64_t reserved2:10;
};

struct nvram_update_limit {
	uint64_t reserved:12;
	uint64_t sdsi_50_pct:1;
	uint64_t sdsi_75_pct:1;
	uint64_t sdsi_90_pct:1;
	uint64_t reserved2:49;
};

struct sdsi_regs {
	uint64_t ppin;
	struct nvram_content_auth_err_sts auth_err_sts;
	struct enabled_features en_features;
	struct key_provision_status key_prov_sts;
	struct auth_fail_count auth_fail_count;
	struct availability prov_avail;
	struct nvram_update_limit limits;
	uint64_t pcu_cr3_capid_cfg;
	union {
		struct {
			uint64_t socket_id;
		} v1;
		struct {
			uint64_t reserved;
			uint64_t socket_id;
			uint64_t reserved2;
		} v2;
	} extra;
};
#define CONTENT_TYPE_LK_ENC		0xD
#define CONTENT_TYPE_LK_BLOB_ENC	0xE

struct state_certificate {
	uint32_t content_type;
	uint32_t region_rev_id;
	uint32_t header_size;
	uint32_t total_size;
	uint32_t key_size;
	uint32_t num_licenses;
};

struct license_key_info {
	uint32_t key_rev_id;
	uint64_t key_image_content[6];
} __packed;

#define LICENSE_BLOB_SIZE(l)	(((l) & 0x7fffffff) * 4)
#define LICENSE_VALID(l)	(!!((l) & 0x80000000))

// License Group Types
#define LBT_ONE_TIME_UPGRADE	1
#define LBT_METERED_UPGRADE	2

struct license_blob_content {
	uint32_t type;
	uint64_t id;
	uint64_t ppin;
	uint64_t previous_ppin;
	uint32_t rev_id;
	uint32_t num_bundles;
} __packed;

struct bundle_encoding {
	uint32_t encoding;
	uint32_t encoding_rsvd[7];
};

struct meter_certificate {
	uint32_t signature;
	uint32_t version;
	uint64_t ppin;
	uint32_t counter_unit;
	uint32_t bundle_length;
	uint64_t reserved;
	uint32_t mmrc_encoding;
	uint32_t mmrc_counter;
};

struct bundle_encoding_counter {
	uint32_t encoding;
	uint32_t counter;
};
#define METER_BUNDLE_SIZE sizeof(struct bundle_encoding_counter)
#define BUNDLE_COUNT(length) ((length) / METER_BUNDLE_SIZE)
#define METER_MAX_NUM_BUNDLES							\
		((METER_CERT_MAX_SIZE - sizeof(struct meter_certificate)) /	\
		 sizeof(struct bundle_encoding_counter))

struct sdsi_dev {
	struct sdsi_regs regs;
	struct state_certificate sc;
	char *dev_name;
	char *dev_path;
	uint32_t guid;
};

enum command {
	CMD_SOCKET_INFO,
	CMD_METER_CERT,
	CMD_METER_CURRENT_CERT,
	CMD_STATE_CERT,
	CMD_PROV_AKC,
	CMD_PROV_CAP,
};

static void sdsi_list_devices(void)
{
	struct dirent *entry;
	DIR *aux_dir;
	bool found = false;

	aux_dir = opendir(AUX_DEV_PATH);
	if (!aux_dir) {
		fprintf(stderr, "Cannot open directory %s\n", AUX_DEV_PATH);
		return;
	}

	while ((entry = readdir(aux_dir))) {
		if (!strncmp(SDSI_DEV, entry->d_name, strlen(SDSI_DEV))) {
			found = true;
			printf("%s\n", entry->d_name);
		}
	}

	if (!found)
		fprintf(stderr, "No On Demand devices found.\n");
}

static int sdsi_update_registers(struct sdsi_dev *s)
{
	FILE *regs_ptr;
	int ret;

	memset(&s->regs, 0, sizeof(s->regs));

	/* Open the registers file */
	ret = chdir(s->dev_path);
	if (ret == -1) {
		perror("chdir");
		return ret;
	}

	regs_ptr = fopen("registers", "r");
	if (!regs_ptr) {
		perror("Could not open 'registers' file");
		return -1;
	}

	if (s->guid != GUID_V1 && s->guid != GUID_V2) {
		fprintf(stderr, "Unrecognized guid, 0x%x\n", s->guid);
		fclose(regs_ptr);
		return -1;
	}

	/* Update register info for this guid */
	ret = fread(&s->regs, sizeof(uint8_t), sizeof(s->regs), regs_ptr);
	if ((s->guid == GUID_V1 && ret != REGS_SIZE_GUID_V1) ||
	    (s->guid == GUID_V2 && ret != REGS_SIZE_GUID_V2)) {
		fprintf(stderr, "Could not read 'registers' file\n");
		fclose(regs_ptr);
		return -1;
	}

	fclose(regs_ptr);

	return 0;
}

static int sdsi_read_reg(struct sdsi_dev *s)
{
	int ret;

	ret = sdsi_update_registers(s);
	if (ret)
		return ret;

	/* Print register info for this guid */
	printf("\n");
	printf("Socket information for device %s\n", s->dev_name);
	printf("\n");
	printf("PPIN:                           0x%lx\n", s->regs.ppin);
	printf("NVRAM Content Authorization Error Status\n");
	printf("    SDSi Auth Err Sts:          %s\n", !!s->regs.auth_err_sts.sdsi_content_auth_err ? "Error" : "Okay");

	if (!!s->regs.en_features.metering)
		printf("    Metering Auth Err Sts:      %s\n", !!s->regs.auth_err_sts.sdsi_metering_auth_err ? "Error" : "Okay");

	printf("Enabled Features\n");
	printf("    On Demand:                  %s\n", !!s->regs.en_features.sdsi ? "Enabled" : "Disabled");
	printf("    Attestation:                %s\n", !!s->regs.en_features.attestation ? "Enabled" : "Disabled");
	printf("    On Demand:                  %s\n", !!s->regs.en_features.sdsi ? "Enabled" : "Disabled");
	printf("    Metering:                   %s\n", !!s->regs.en_features.metering ? "Enabled" : "Disabled");
	printf("License Key (AKC) Provisioned:  %s\n", !!s->regs.key_prov_sts.license_key_provisioned ? "Yes" : "No");
	printf("Authorization Failure Count\n");
	printf("    AKC Failure Count:          %d\n", s->regs.auth_fail_count.key_failure_count);
	printf("    AKC Failure Threshold:      %d\n", s->regs.auth_fail_count.key_failure_threshold);
	printf("    CAP Failure Count:          %d\n", s->regs.auth_fail_count.auth_failure_count);
	printf("    CAP Failure Threshold:      %d\n", s->regs.auth_fail_count.auth_failure_threshold);
	printf("Provisioning Availability\n");
	printf("    Updates Available:          %d\n", s->regs.prov_avail.available);
	printf("    Updates Threshold:          %d\n", s->regs.prov_avail.threshold);
	printf("NVRAM Udate Limit\n");
	printf("    50%% Limit Reached:          %s\n", !!s->regs.limits.sdsi_50_pct ? "Yes" : "No");
	printf("    75%% Limit Reached:          %s\n", !!s->regs.limits.sdsi_75_pct ? "Yes" : "No");
	printf("    90%% Limit Reached:          %s\n", !!s->regs.limits.sdsi_90_pct ? "Yes" : "No");
	if (s->guid == GUID_V1)
		printf("Socket ID:                      %ld\n", s->regs.extra.v1.socket_id & 0xF);
	else
		printf("Socket ID:                      %ld\n", s->regs.extra.v2.socket_id & 0xF);

	return 0;
}

static char *license_blob_type(uint32_t type)
{
	switch (type) {
	case LBT_ONE_TIME_UPGRADE:
		return "One time upgrade";
	case LBT_METERED_UPGRADE:
		return "Metered upgrade";
	default:
		return "Unknown license blob type";
	}
}

static char *content_type(uint32_t type)
{
	switch (type) {
	case  CONTENT_TYPE_LK_ENC:
		return "Licencse key encoding";
	case CONTENT_TYPE_LK_BLOB_ENC:
		return "License key + Blob encoding";
	default:
		return "Unknown content type";
	}
}

static void get_feature(uint32_t encoding, char feature[5])
{
	char *name = (char *)&encoding;

	feature[4] = '\0';
	feature[3] = name[0];
	feature[2] = name[1];
	feature[1] = name[2];
	feature[0] = name[3];
}

static int sdsi_meter_cert_show(struct sdsi_dev *s, bool show_current)
{
	char buf[METER_CERT_MAX_SIZE] = {0};
	struct bundle_encoding_counter *bec;
	struct meter_certificate *mc;
	uint32_t count = 0;
	FILE *cert_ptr;
	char *cert_fname;
	int ret, size;
	char name[FEAT_LEN];

	ret = sdsi_update_registers(s);
	if (ret)
		return ret;

	if (!s->regs.en_features.sdsi) {
		fprintf(stderr, "SDSi feature is present but not enabled.\n");
		return -1;
	}

	if (!s->regs.en_features.metering) {
		fprintf(stderr, "Metering not supporting on this socket.\n");
		return -1;
	}

	ret = chdir(s->dev_path);
	if (ret == -1) {
		perror("chdir");
		return ret;
	}

	cert_fname = show_current ? "meter_current" : "meter_certificate";
	cert_ptr = fopen(cert_fname, "r");

	if (!cert_ptr) {
		fprintf(stderr, "Could not open '%s' file: %s", cert_fname, strerror(errno));
		return -1;
	}

	size = fread(buf, 1, sizeof(buf), cert_ptr);
	if (!size) {
		fprintf(stderr, "Could not read '%s' file\n", cert_fname);
		fclose(cert_ptr);
		return -1;
	}
	fclose(cert_ptr);

	mc = (struct meter_certificate *)buf;

	printf("\n");
	printf("Meter certificate for device %s\n", s->dev_name);
	printf("\n");

	get_feature(mc->signature, name);
	printf("Signature:                    %s\n", name);

	printf("Version:                      %d\n", mc->version);
	printf("Count Unit:                   %dms\n", mc->counter_unit);
	printf("PPIN:                         0x%lx\n", mc->ppin);
	printf("Feature Bundle Length:        %d\n", mc->bundle_length);

	get_feature(mc->mmrc_encoding, name);
	printf("MMRC encoding:                %s\n", name);

	printf("MMRC counter:                 %d\n", mc->mmrc_counter);
	if (mc->bundle_length % METER_BUNDLE_SIZE) {
		fprintf(stderr, "Invalid bundle length\n");
		return -1;
	}

	if (mc->bundle_length > METER_MAX_NUM_BUNDLES * METER_BUNDLE_SIZE)  {
		fprintf(stderr, "More than %ld bundles: actual %ld\n",
			METER_MAX_NUM_BUNDLES, BUNDLE_COUNT(mc->bundle_length));
		return -1;
	}

	bec = (struct bundle_encoding_counter *)(mc + 1);

	printf("Number of Feature Counters:   %ld\n", BUNDLE_COUNT(mc->bundle_length));
	while (count < BUNDLE_COUNT(mc->bundle_length)) {
		char feature[FEAT_LEN];

		get_feature(bec[count].encoding, feature);
		printf("    %s:          %d\n", feature, bec[count].counter);
		++count;
	}

	return 0;
}

static int sdsi_state_cert_show(struct sdsi_dev *s)
{
	char buf[STATE_CERT_MAX_SIZE] = {0};
	struct state_certificate *sc;
	struct license_key_info *lki;
	uint32_t offset = 0;
	uint32_t count = 0;
	FILE *cert_ptr;
	int ret, size;

	ret = sdsi_update_registers(s);
	if (ret)
		return ret;

	if (!s->regs.en_features.sdsi) {
		fprintf(stderr, "On Demand feature is present but not enabled.");
		fprintf(stderr, " Unable to read state certificate");
		return -1;
	}

	ret = chdir(s->dev_path);
	if (ret == -1) {
		perror("chdir");
		return ret;
	}

	cert_ptr = fopen("state_certificate", "r");
	if (!cert_ptr) {
		perror("Could not open 'state_certificate' file");
		return -1;
	}

	size = fread(buf, 1, sizeof(buf), cert_ptr);
	if (!size) {
		fprintf(stderr, "Could not read 'state_certificate' file\n");
		fclose(cert_ptr);
		return -1;
	}
	fclose(cert_ptr);

	sc = (struct state_certificate *)buf;

	/* Print register info for this guid */
	printf("\n");
	printf("State certificate for device %s\n", s->dev_name);
	printf("\n");
	printf("Content Type:          %s\n", content_type(sc->content_type));
	printf("Region Revision ID:    %d\n", sc->region_rev_id);
	printf("Header Size:           %d\n", sc->header_size * 4);
	printf("Total Size:            %d\n", sc->total_size);
	printf("OEM Key Size:          %d\n", sc->key_size * 4);
	printf("Number of Licenses:    %d\n", sc->num_licenses);

	/* Skip over the license sizes 4 bytes per license) to get the license key info */
	lki = (void *)sc + sizeof(*sc) + (4 * sc->num_licenses);

	printf("License blob Info:\n");
	printf("    License Key Revision ID:    0x%x\n", lki->key_rev_id);
	printf("    License Key Image Content:  0x%lx%lx%lx%lx%lx%lx\n",
	       lki->key_image_content[5], lki->key_image_content[4],
	       lki->key_image_content[3], lki->key_image_content[2],
	       lki->key_image_content[1], lki->key_image_content[0]);

	while (count++ < sc->num_licenses) {
		uint32_t blob_size_field = *(uint32_t *)(buf + 0x14 + count * 4);
		uint32_t blob_size = LICENSE_BLOB_SIZE(blob_size_field);
		bool license_valid = LICENSE_VALID(blob_size_field);
		struct license_blob_content *lbc =
			(void *)(sc) +			// start of the state certificate
			sizeof(*sc) +			// size of the state certificate
			(4 * sc->num_licenses) +	// total size of the blob size blocks
			sizeof(*lki) +			// size of the license key info
			offset;				// offset to this blob content
		struct bundle_encoding *bundle = (void *)(lbc) + sizeof(*lbc);
		char feature[FEAT_LEN];
		uint32_t i;

		printf("     Blob %d:\n", count - 1);
		printf("        License blob size:          %u\n", blob_size);
		printf("        License is valid:           %s\n", license_valid ? "Yes" : "No");
		printf("        License blob type:          %s\n", license_blob_type(lbc->type));
		printf("        License blob ID:            0x%lx\n", lbc->id);
		printf("        PPIN:                       0x%lx\n", lbc->ppin);
		printf("        Previous PPIN:              0x%lx\n", lbc->previous_ppin);
		printf("        Blob revision ID:           %u\n", lbc->rev_id);
		printf("        Number of Features:         %u\n", lbc->num_bundles);

		for (i = 0; i < min(lbc->num_bundles, STATE_MAX_NUM_IN_BUNDLE); i++) {
			get_feature(bundle[i].encoding, feature);
			printf("                 Feature %d:         %s\n", i, feature);
		}

		if (lbc->num_bundles > STATE_MAX_NUM_IN_BUNDLE)
			fprintf(stderr, "        Warning: %d > %d licenses in bundle reported.\n",
				lbc->num_bundles, STATE_MAX_NUM_IN_BUNDLE);

		offset += blob_size;
	};

	return 0;
}

static int sdsi_provision(struct sdsi_dev *s, char *bin_file, enum command command)
{
	int bin_fd, prov_fd, size, ret;
	char buf[STATE_CERT_MAX_SIZE] = { 0 };
	char cap[] = "provision_cap";
	char akc[] = "provision_akc";
	char *prov_file;

	if (!bin_file) {
		fprintf(stderr, "No binary file provided\n");
		return -1;
	}

	/* Open the binary */
	bin_fd = open(bin_file, O_RDONLY);
	if (bin_fd == -1) {
		fprintf(stderr, "Could not open file %s: %s\n", bin_file, strerror(errno));
		return bin_fd;
	}

	prov_file = (command == CMD_PROV_AKC) ? akc : cap;

	ret = chdir(s->dev_path);
	if (ret == -1) {
		perror("chdir");
		close(bin_fd);
		return ret;
	}

	/* Open the provision file */
	prov_fd = open(prov_file, O_WRONLY);
	if (prov_fd == -1) {
		fprintf(stderr, "Could not open file %s: %s\n", prov_file, strerror(errno));
		close(bin_fd);
		return prov_fd;
	}

	/* Read the binary file into the buffer */
	size = read(bin_fd, buf, STATE_CERT_MAX_SIZE);
	if (size == -1) {
		close(bin_fd);
		close(prov_fd);
		return -1;
	}

	ret = write(prov_fd, buf, size);
	if (ret == -1) {
		close(bin_fd);
		close(prov_fd);
		perror("Provisioning failed");
		return ret;
	}

	printf("Provisioned %s file %s successfully\n", prov_file, bin_file);

	close(bin_fd);
	close(prov_fd);

	return 0;
}

static int sdsi_provision_akc(struct sdsi_dev *s, char *bin_file)
{
	int ret;

	ret = sdsi_update_registers(s);
	if (ret)
		return ret;

	if (!s->regs.en_features.sdsi) {
		fprintf(stderr, "On Demand feature is present but not enabled. Unable to provision");
		return -1;
	}

	if (!s->regs.prov_avail.available) {
		fprintf(stderr, "Maximum number of updates (%d) has been reached.\n",
			s->regs.prov_avail.threshold);
		return -1;
	}

	if (s->regs.auth_fail_count.key_failure_count ==
	    s->regs.auth_fail_count.key_failure_threshold) {
		fprintf(stderr, "Maximum number of AKC provision failures (%d) has been reached.\n",
			s->regs.auth_fail_count.key_failure_threshold);
		fprintf(stderr, "Power cycle the system to reset the counter\n");
		return -1;
	}

	return sdsi_provision(s, bin_file, CMD_PROV_AKC);
}

static int sdsi_provision_cap(struct sdsi_dev *s, char *bin_file)
{
	int ret;

	ret = sdsi_update_registers(s);
	if (ret)
		return ret;

	if (!s->regs.en_features.sdsi) {
		fprintf(stderr, "On Demand feature is present but not enabled. Unable to provision");
		return -1;
	}

	if (!s->regs.prov_avail.available) {
		fprintf(stderr, "Maximum number of updates (%d) has been reached.\n",
			s->regs.prov_avail.threshold);
		return -1;
	}

	if (s->regs.auth_fail_count.auth_failure_count ==
	    s->regs.auth_fail_count.auth_failure_threshold) {
		fprintf(stderr, "Maximum number of CAP provision failures (%d) has been reached.\n",
			s->regs.auth_fail_count.auth_failure_threshold);
		fprintf(stderr, "Power cycle the system to reset the counter\n");
		return -1;
	}

	return sdsi_provision(s, bin_file, CMD_PROV_CAP);
}

static int read_sysfs_data(const char *file, int *value)
{
	char buff[16];
	FILE *fp;

	fp = fopen(file, "r");
	if (!fp) {
		perror(file);
		return -1;
	}

	if (!fgets(buff, 16, fp)) {
		fprintf(stderr, "Failed to read file '%s'", file);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	*value = strtol(buff, NULL, 0);

	return 0;
}

static struct sdsi_dev *sdsi_create_dev(char *dev_no)
{
	int dev_name_len = sizeof(SDSI_DEV) + strlen(dev_no) + 1;
	struct sdsi_dev *s;
	int guid;
	DIR *dir;

	s = (struct sdsi_dev *)malloc(sizeof(*s));
	if (!s) {
		perror("malloc");
		return NULL;
	}

	s->dev_name = (char *)malloc(sizeof(SDSI_DEV) + strlen(dev_no) + 1);
	if (!s->dev_name) {
		perror("malloc");
		free(s);
		return NULL;
	}

	snprintf(s->dev_name, dev_name_len, "%s.%s", SDSI_DEV, dev_no);

	s->dev_path = (char *)malloc(sizeof(AUX_DEV_PATH) + dev_name_len);
	if (!s->dev_path) {
		perror("malloc");
		free(s->dev_name);
		free(s);
		return NULL;
	}

	snprintf(s->dev_path, sizeof(AUX_DEV_PATH) + dev_name_len, "%s%s", AUX_DEV_PATH,
		 s->dev_name);
	dir = opendir(s->dev_path);
	if (!dir) {
		fprintf(stderr, "Could not open directory '%s': %s\n", s->dev_path,
			strerror(errno));
		free(s->dev_path);
		free(s->dev_name);
		free(s);
		return NULL;
	}

	if (chdir(s->dev_path) == -1) {
		perror("chdir");
		free(s->dev_path);
		free(s->dev_name);
		free(s);
		return NULL;
	}

	if (read_sysfs_data("guid", &guid)) {
		free(s->dev_path);
		free(s->dev_name);
		free(s);
		return NULL;
	}

	s->guid = guid;

	return s;
}

static void sdsi_free_dev(struct sdsi_dev *s)
{
	free(s->dev_path);
	free(s->dev_name);
	free(s);
}

static void usage(char *prog)
{
	printf("Usage: %s [-l] [-d DEVNO [-i] [-s] [-m | -C] [-a FILE] [-c FILE]\n", prog);
}

static void show_help(void)
{
	printf("Commands:\n");
	printf("  %-18s\t%s\n", "-l, --list",           "list available On Demand devices");
	printf("  %-18s\t%s\n", "-d, --devno DEVNO",    "On Demand device number");
	printf("  %-18s\t%s\n", "-i, --info",           "show socket information");
	printf("  %-18s\t%s\n", "-s, --state",          "show state certificate data");
	printf("  %-18s\t%s\n", "-m, --meter",          "show meter certificate data");
	printf("  %-18s\t%s\n", "-C, --meter_current",  "show live unattested meter data");
	printf("  %-18s\t%s\n", "-a, --akc FILE",       "provision socket with AKC FILE");
	printf("  %-18s\t%s\n", "-c, --cap FILE>",      "provision socket with CAP FILE");
}

int main(int argc, char *argv[])
{
	char bin_file[PATH_MAX], *dev_no = NULL;
	bool device_selected = false;
	char *progname;
	enum command command = -1;
	struct sdsi_dev *s;
	int ret = 0, opt;
	int option_index = 0;

	static struct option long_options[] = {
		{"akc",			required_argument,	0, 'a'},
		{"cap",			required_argument,	0, 'c'},
		{"devno",		required_argument,	0, 'd'},
		{"help",		no_argument,		0, 'h'},
		{"info",		no_argument,		0, 'i'},
		{"list",		no_argument,		0, 'l'},
		{"meter",		no_argument,		0, 'm'},
		{"meter_current",	no_argument,		0, 'C'},
		{"state",		no_argument,		0, 's'},
		{0,			0,			0, 0 }
	};


	progname = argv[0];

	while ((opt = getopt_long_only(argc, argv, "+a:c:d:hilmCs", long_options,
			&option_index)) != -1) {
		switch (opt) {
		case 'd':
			dev_no = optarg;
			device_selected = true;
			break;
		case 'l':
			sdsi_list_devices();
			return 0;
		case 'i':
			command = CMD_SOCKET_INFO;
			break;
		case 'm':
			command = CMD_METER_CERT;
			break;
		case 'C':
			command = CMD_METER_CURRENT_CERT;
			break;
		case 's':
			command = CMD_STATE_CERT;
			break;
		case 'a':
		case 'c':
			if (!access(optarg, F_OK) == 0) {
				fprintf(stderr, "Could not open file '%s': %s\n", optarg,
					strerror(errno));
				return -1;
			}

			if (!realpath(optarg, bin_file)) {
				perror("realpath");
				return -1;
			}

			command = (opt == 'a') ? CMD_PROV_AKC : CMD_PROV_CAP;
			break;
		case 'h':
			usage(progname);
			show_help();
			return 0;
		default:
			usage(progname);
			return -1;
		}
	}

	if (device_selected) {
		s = sdsi_create_dev(dev_no);
		if (!s)
			return -1;

		switch (command) {
		case CMD_SOCKET_INFO:
			ret = sdsi_read_reg(s);
			break;
		case CMD_METER_CERT:
			ret = sdsi_meter_cert_show(s, false);
			break;
		case CMD_METER_CURRENT_CERT:
			ret = sdsi_meter_cert_show(s, true);
			break;
		case CMD_STATE_CERT:
			ret = sdsi_state_cert_show(s);
			break;
		case CMD_PROV_AKC:
			ret = sdsi_provision_akc(s, bin_file);
			break;
		case CMD_PROV_CAP:
			ret = sdsi_provision_cap(s, bin_file);
			break;
		default:
			fprintf(stderr, "No command specified\n");
			return -1;
		}

		sdsi_free_dev(s);

	} else {
		fprintf(stderr, "No device specified\n");
		return -1;
	}

	return ret;
}
