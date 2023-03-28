// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2005-2020 IBM Corporation.
 *
 * Includes code from librtas (https://github.com/ibm-power-utilities/librtas/)
 */

#include <byteswap.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include "utils.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define cpu_to_be32(x)		bswap_32(x)
#define be32_to_cpu(x)		bswap_32(x)
#else
#define cpu_to_be32(x)		(x)
#define be32_to_cpu(x)		(x)
#endif

#define RTAS_IO_ASSERT	-1098	/* Unexpected I/O Error */
#define RTAS_UNKNOWN_OP -1099	/* No Firmware Implementation of Function */
#define BLOCK_SIZE 4096
#define PAGE_SIZE 4096
#define MAX_PAGES 64

static const char *ofdt_rtas_path = "/proc/device-tree/rtas";

typedef __be32 uint32_t;
struct rtas_args {
	__be32 token;
	__be32 nargs;
	__be32 nret;
	__be32 args[16];
	__be32 *rets;	  /* Pointer to return values in args[]. */
};

struct region {
	uint64_t addr;
	uint32_t size;
	struct region *next;
};

static int get_property(const char *prop_path, const char *prop_name,
			char **prop_val, size_t *prop_len)
{
	char path[PATH_MAX];

	int len = snprintf(path, sizeof(path), "%s/%s", prop_path, prop_name);
	if (len < 0 || len >= sizeof(path))
		return -ENOMEM;

	return read_file_alloc(path, prop_val, prop_len);
}

int rtas_token(const char *call_name)
{
	char *prop_buf = NULL;
	size_t len;
	int rc;

	rc = get_property(ofdt_rtas_path, call_name, &prop_buf, &len);
	if (rc < 0) {
		rc = RTAS_UNKNOWN_OP;
		goto err;
	}

	rc = be32_to_cpu(*(int *)prop_buf);

err:
	free(prop_buf);
	return rc;
}

static int read_kregion_bounds(struct region *kregion)
{
	char *buf;
	int err;

	err = read_file_alloc("/proc/ppc64/rtas/rmo_buffer", &buf, NULL);
	if (err) {
		perror("Could not open rmo_buffer file");
		return RTAS_IO_ASSERT;
	}

	sscanf(buf, "%" SCNx64 " %x", &kregion->addr, &kregion->size);
	free(buf);

	if (!(kregion->size && kregion->addr) ||
	    (kregion->size > (PAGE_SIZE * MAX_PAGES))) {
		printf("Unexpected kregion bounds\n");
		return RTAS_IO_ASSERT;
	}

	return 0;
}

static int rtas_call(const char *name, int nargs,
		     int nrets, ...)
{
	struct rtas_args args;
	__be32 *rets[16];
	int i, rc, token;
	va_list ap;

	va_start(ap, nrets);

	token = rtas_token(name);
	if (token == RTAS_UNKNOWN_OP) {
		// We don't care if the call doesn't exist
		printf("call '%s' not available, skipping...", name);
		rc = RTAS_UNKNOWN_OP;
		goto err;
	}

	args.token = cpu_to_be32(token);
	args.nargs = cpu_to_be32(nargs);
	args.nret = cpu_to_be32(nrets);

	for (i = 0; i < nargs; i++)
		args.args[i] = (__be32) va_arg(ap, unsigned long);

	for (i = 0; i < nrets; i++)
		rets[i] = (__be32 *) va_arg(ap, unsigned long);

	rc = syscall(__NR_rtas, &args);
	if (rc) {
		rc = -errno;
		goto err;
	}

	if (nrets) {
		*(rets[0]) = be32_to_cpu(args.args[nargs]);

		for (i = 1; i < nrets; i++) {
			*(rets[i]) = args.args[nargs + i];
		}
	}

err:
	va_end(ap);
	return rc;
}

static int test(void)
{
	struct region rmo_region;
	uint32_t rmo_start;
	uint32_t rmo_end;
	__be32 rets[1];
	int rc;

	// Test a legitimate harmless call
	// Expected: call succeeds
	printf("Test a permitted call, no parameters... ");
	rc = rtas_call("get-time-of-day", 0, 1, rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != 0 && rc != RTAS_UNKNOWN_OP);

	// Test a prohibited call
	// Expected: call returns -EINVAL
	printf("Test a prohibited call... ");
	rc = rtas_call("nvram-fetch", 0, 1, rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != -EINVAL && rc != RTAS_UNKNOWN_OP);

	// Get RMO
	rc = read_kregion_bounds(&rmo_region);
	if (rc) {
		printf("Couldn't read RMO region bounds, skipping remaining cases\n");
		return 0;
	}
	rmo_start = rmo_region.addr;
	rmo_end = rmo_start + rmo_region.size - 1;
	printf("RMO range: %08x - %08x\n", rmo_start, rmo_end);

	// Test a permitted call, user-supplied size, buffer inside RMO
	// Expected: call succeeds
	printf("Test a permitted call, user-supplied size, buffer inside RMO... ");
	rc = rtas_call("ibm,get-system-parameter", 3, 1, 0, cpu_to_be32(rmo_start),
		       cpu_to_be32(rmo_end - rmo_start + 1), rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != 0 && rc != RTAS_UNKNOWN_OP);

	// Test a permitted call, user-supplied size, buffer start outside RMO
	// Expected: call returns -EINVAL
	printf("Test a permitted call, user-supplied size, buffer start outside RMO... ");
	rc = rtas_call("ibm,get-system-parameter", 3, 1, 0, cpu_to_be32(rmo_end + 1),
		       cpu_to_be32(4000), rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != -EINVAL && rc != RTAS_UNKNOWN_OP);

	// Test a permitted call, user-supplied size, buffer end outside RMO
	// Expected: call returns -EINVAL
	printf("Test a permitted call, user-supplied size, buffer end outside RMO... ");
	rc = rtas_call("ibm,get-system-parameter", 3, 1, 0, cpu_to_be32(rmo_start),
		       cpu_to_be32(rmo_end - rmo_start + 2), rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != -EINVAL && rc != RTAS_UNKNOWN_OP);

	// Test a permitted call, fixed size, buffer end outside RMO
	// Expected: call returns -EINVAL
	printf("Test a permitted call, fixed size, buffer end outside RMO... ");
	rc = rtas_call("ibm,configure-connector", 2, 1, cpu_to_be32(rmo_end - 4000), 0, rets);
	printf("rc: %d\n", rc);
	FAIL_IF(rc != -EINVAL && rc != RTAS_UNKNOWN_OP);

	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(test, "rtas_filter");
}
