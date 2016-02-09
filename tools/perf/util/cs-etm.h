/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDE__UTIL_PERF_CS_ETM_H__
#define INCLUDE__UTIL_PERF_CS_ETM_H__

/* Versionning header in case things need tro change in the future.  That way
 * decoding of old snapshot is still possible.
 */
enum {
	/* Starting with 0x0 */
	CS_HEADER_VERSION_0,
	/* PMU->type (32 bit), total # of CPUs (32 bit) */
	CS_PMU_TYPE_CPUS,
	CS_ETM_SNAPSHOT,
	CS_HEADER_VERSION_0_MAX,
};

/* Beginning of header common to both ETMv3 and V4 */
enum {
	CS_ETM_MAGIC,
	CS_ETM_CPU,
};

/* ETMv3/PTM metadata */
enum {
	/* Dynamic, configurable parameters */
	CS_ETM_ETMCR = CS_ETM_CPU + 1,
	CS_ETM_ETMTRACEIDR,
	/* RO, taken from sysFS */
	CS_ETM_ETMCCER,
	CS_ETM_ETMIDR,
	CS_ETM_PRIV_MAX,
};

/* ETMv4 metadata */
enum {
	/* Dynamic, configurable parameters */
	CS_ETMV4_TRCCONFIGR = CS_ETM_CPU + 1,
	CS_ETMV4_TRCTRACEIDR,
	/* RO, taken from sysFS */
	CS_ETMV4_TRCIDR0,
	CS_ETMV4_TRCIDR1,
	CS_ETMV4_TRCIDR2,
	CS_ETMV4_TRCIDR8,
	CS_ETMV4_TRCAUTHSTATUS,
	CS_ETMV4_PRIV_MAX,
};

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

#define CS_ETM_HEADER_SIZE (CS_HEADER_VERSION_0_MAX * sizeof(u64))

static const u64 __perf_cs_etmv3_magic   = 0x3030303030303030ULL;
static const u64 __perf_cs_etmv4_magic   = 0x4040404040404040ULL;
#define CS_ETMV3_PRIV_SIZE (CS_ETM_PRIV_MAX * sizeof(u64))
#define CS_ETMV4_PRIV_SIZE (CS_ETMV4_PRIV_MAX * sizeof(u64))

int cs_etm__process_auxtrace_info(union perf_event *event,
                                  struct perf_session *session);

#endif
