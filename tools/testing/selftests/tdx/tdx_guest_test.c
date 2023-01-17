// SPDX-License-Identifier: GPL-2.0
/*
 * Test TDX guest features
 *
 * Copyright (C) 2022 Intel Corporation.
 *
 * Author: Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>
 */

#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>

#include "../kselftest_harness.h"
#include "../../../../include/uapi/linux/tdx-guest.h"

#define TDX_GUEST_DEVNAME "/dev/tdx_guest"
#define HEX_DUMP_SIZE 8
#define DEBUG 0

/**
 * struct tdreport_type - Type header of TDREPORT_STRUCT.
 * @type: Type of the TDREPORT (0 - SGX, 81 - TDX, rest are reserved)
 * @sub_type: Subtype of the TDREPORT (Default value is 0).
 * @version: TDREPORT version (Default value is 0).
 * @reserved: Added for future extension.
 *
 * More details can be found in TDX v1.0 module specification, sec
 * titled "REPORTTYPE".
 */
struct tdreport_type {
	__u8 type;
	__u8 sub_type;
	__u8 version;
	__u8 reserved;
};

/**
 * struct reportmac - TDX guest report data, MAC and TEE hashes.
 * @type: TDREPORT type header.
 * @reserved1: Reserved for future extension.
 * @cpu_svn: CPU security version.
 * @tee_tcb_info_hash: SHA384 hash of TEE TCB INFO.
 * @tee_td_info_hash: SHA384 hash of TDINFO_STRUCT.
 * @reportdata: User defined unique data passed in TDG.MR.REPORT request.
 * @reserved2: Reserved for future extension.
 * @mac: CPU MAC ID.
 *
 * It is MAC-protected and contains hashes of the remainder of the
 * report structure along with user provided report data. More details can
 * be found in TDX v1.0 Module specification, sec titled "REPORTMACSTRUCT"
 */
struct reportmac {
	struct tdreport_type type;
	__u8 reserved1[12];
	__u8 cpu_svn[16];
	__u8 tee_tcb_info_hash[48];
	__u8 tee_td_info_hash[48];
	__u8 reportdata[64];
	__u8 reserved2[32];
	__u8 mac[32];
};

/**
 * struct td_info - TDX guest measurements and configuration.
 * @attr: TDX Guest attributes (like debug, spet_disable, etc).
 * @xfam: Extended features allowed mask.
 * @mrtd: Build time measurement register.
 * @mrconfigid: Software-defined ID for non-owner-defined configuration
 *              of the guest - e.g., run-time or OS configuration.
 * @mrowner: Software-defined ID for the guest owner.
 * @mrownerconfig: Software-defined ID for owner-defined configuration of
 *                 the guest - e.g., specific to the workload.
 * @rtmr: Run time measurement registers.
 * @reserved: Added for future extension.
 *
 * It contains the measurements and initial configuration of the TDX guest
 * that was locked at initialization and a set of measurement registers
 * that are run-time extendable. More details can be found in TDX v1.0
 * Module specification, sec titled "TDINFO_STRUCT".
 */
struct td_info {
	__u8 attr[8];
	__u64 xfam;
	__u64 mrtd[6];
	__u64 mrconfigid[6];
	__u64 mrowner[6];
	__u64 mrownerconfig[6];
	__u64 rtmr[24];
	__u64 reserved[14];
};

/*
 * struct tdreport - Output of TDCALL[TDG.MR.REPORT].
 * @reportmac: Mac protected header of size 256 bytes.
 * @tee_tcb_info: Additional attestable elements in the TCB are not
 *                reflected in the reportmac.
 * @reserved: Added for future extension.
 * @tdinfo: Measurements and configuration data of size 512 bytes.
 *
 * More details can be found in TDX v1.0 Module specification, sec
 * titled "TDREPORT_STRUCT".
 */
struct tdreport {
	struct reportmac reportmac;
	__u8 tee_tcb_info[239];
	__u8 reserved[17];
	struct td_info tdinfo;
};

static void print_array_hex(const char *title, const char *prefix_str,
			    const void *buf, int len)
{
	int i, j, line_len, rowsize = HEX_DUMP_SIZE;
	const __u8 *ptr = buf;

	printf("\t\t%s", title);

	for (j = 0; j < len; j += rowsize) {
		line_len = rowsize < (len - j) ? rowsize : (len - j);
		printf("%s%.8x:", prefix_str, j);
		for (i = 0; i < line_len; i++)
			printf(" %.2x", ptr[j + i]);
		printf("\n");
	}

	printf("\n");
}

TEST(verify_report)
{
	struct tdx_report_req req;
	struct tdreport *tdreport;
	int devfd, i;

	devfd = open(TDX_GUEST_DEVNAME, O_RDWR | O_SYNC);
	ASSERT_LT(0, devfd);

	/* Generate sample report data */
	for (i = 0; i < TDX_REPORTDATA_LEN; i++)
		req.reportdata[i] = i;

	/* Get TDREPORT */
	ASSERT_EQ(0, ioctl(devfd, TDX_CMD_GET_REPORT0, &req));

	if (DEBUG) {
		print_array_hex("\n\t\tTDX report data\n", "",
				req.reportdata, sizeof(req.reportdata));

		print_array_hex("\n\t\tTDX tdreport data\n", "",
				req.tdreport, sizeof(req.tdreport));
	}

	/* Make sure TDREPORT data includes the REPORTDATA passed */
	tdreport = (struct tdreport *)req.tdreport;
	ASSERT_EQ(0, memcmp(&tdreport->reportmac.reportdata[0],
			    req.reportdata, sizeof(req.reportdata)));

	ASSERT_EQ(0, close(devfd));
}

TEST_HARNESS_MAIN
