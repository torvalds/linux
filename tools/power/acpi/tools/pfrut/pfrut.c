// SPDX-License-Identifier: GPL-2.0
/*
 * Platform Firmware Runtime Update tool to do Management
 * Mode code injection/driver update and telemetry retrieval.
 *
 * This tool uses the interfaces provided by pfr_update and
 * pfr_telemetry drivers. These interfaces are exposed via
 * /dev/pfr_update and /dev/pfr_telemetry. Write operation
 * on the /dev/pfr_update is to load the EFI capsule into
 * kernel space. Mmap/read operations on /dev/pfr_telemetry
 * could be used to read the telemetry data to user space.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <uuid/uuid.h>
#include PFRUT_HEADER

char *capsule_name;
int action, query_cap, log_type, log_level, log_read, log_getinfo,
	revid, log_revid;
int set_log_level, set_log_type,
	set_revid, set_log_revid;

char *progname;

#define LOG_ERR		0
#define LOG_WARN	1
#define LOG_INFO	2
#define LOG_VERB	4
#define LOG_EXEC_IDX	0
#define LOG_HISTORY_IDX	1
#define REVID_1		1
#define REVID_2		2

static int valid_log_level(int level)
{
	return level == LOG_ERR || level == LOG_WARN ||
	       level == LOG_INFO || level == LOG_VERB;
}

static int valid_log_type(int type)
{
	return type == LOG_EXEC_IDX || type == LOG_HISTORY_IDX;
}

static inline int valid_log_revid(int id)
{
	return id == REVID_1 || id == REVID_2;
}

static void help(void)
{
	fprintf(stderr,
		"usage: %s [OPTIONS]\n"
		" code injection:\n"
		"  -l, --load\n"
		"  -s, --stage\n"
		"  -a, --activate\n"
		"  -u, --update [stage and activate]\n"
		"  -q, --query\n"
		"  -d, --revid update\n"
		" telemetry:\n"
		"  -G, --getloginfo\n"
		"  -T, --type(0:execution, 1:history)\n"
		"  -L, --level(0, 1, 2, 4)\n"
		"  -R, --read\n"
		"  -D, --revid log\n",
		progname);
}

char *option_string = "l:sauqd:GT:L:RD:h";
static struct option long_options[] = {
	{"load", required_argument, 0, 'l'},
	{"stage", no_argument, 0, 's'},
	{"activate", no_argument, 0, 'a'},
	{"update", no_argument, 0, 'u'},
	{"query", no_argument, 0, 'q'},
	{"getloginfo", no_argument, 0, 'G'},
	{"type", required_argument, 0, 'T'},
	{"level", required_argument, 0, 'L'},
	{"read", no_argument, 0, 'R'},
	{"setrev", required_argument, 0, 'd'},
	{"setrevlog", required_argument, 0, 'D'},
	{"help", no_argument, 0, 'h'},
	{}
};

static void parse_options(int argc, char **argv)
{
	int option_index = 0;
	char *pathname;
	int opt;

	pathname = strdup(argv[0]);
	progname = basename(pathname);

	while ((opt = getopt_long_only(argc, argv, option_string,
				       long_options, &option_index)) != -1) {
		switch (opt) {
		case 'l':
			capsule_name = optarg;
			break;
		case 's':
			action = 1;
			break;
		case 'a':
			action = 2;
			break;
		case 'u':
			action = 3;
			break;
		case 'q':
			query_cap = 1;
			break;
		case 'G':
			log_getinfo = 1;
			break;
		case 'T':
			log_type = atoi(optarg);
			set_log_type = 1;
			break;
		case 'L':
			log_level = atoi(optarg);
			set_log_level = 1;
			break;
		case 'R':
			log_read = 1;
			break;
		case 'd':
			revid = atoi(optarg);
			set_revid = 1;
			break;
		case 'D':
			log_revid = atoi(optarg);
			set_log_revid = 1;
			break;
		case 'h':
			help();
			exit(0);
		default:
			break;
		}
	}
}

void print_cap(struct pfru_update_cap_info *cap)
{
	char *uuid;

	uuid = malloc(37);
	if (!uuid) {
		perror("Can not allocate uuid buffer\n");
		exit(1);
	}

	uuid_unparse(cap->code_type, uuid);
	printf("code injection image type:%s\n", uuid);
	printf("fw_version:%d\n", cap->fw_version);
	printf("code_rt_version:%d\n", cap->code_rt_version);

	uuid_unparse(cap->drv_type, uuid);
	printf("driver update image type:%s\n", uuid);
	printf("drv_rt_version:%d\n", cap->drv_rt_version);
	printf("drv_svn:%d\n", cap->drv_svn);

	uuid_unparse(cap->platform_id, uuid);
	printf("platform id:%s\n", uuid);
	uuid_unparse(cap->oem_id, uuid);
	printf("oem id:%s\n", uuid);
	printf("oem information length:%d\n", cap->oem_info_len);

	free(uuid);
}

int main(int argc, char *argv[])
{
	int fd_update, fd_update_log, fd_capsule;
	struct pfrt_log_data_info data_info;
	struct pfrt_log_info info;
	struct pfru_update_cap_info cap;
	void *addr_map_capsule;
	struct stat st;
	char *log_buf;
	int ret;

	if (getuid() != 0) {
		printf("Please run the tool as root - Exiting.\n");
		return 1;
	}

	parse_options(argc, argv);

	fd_update = open("/dev/acpi_pfr_update0", O_RDWR);
	if (fd_update < 0) {
		printf("PFRU device not supported - Quit...\n");
		return 1;
	}

	fd_update_log = open("/dev/acpi_pfr_telemetry0", O_RDWR);
	if (fd_update_log < 0) {
		printf("PFRT device not supported - Quit...\n");
		return 1;
	}

	if (query_cap) {
		ret = ioctl(fd_update, PFRU_IOC_QUERY_CAP, &cap);
		if (ret)
			perror("Query Update Capability info failed.");
		else
			print_cap(&cap);

		close(fd_update);
		close(fd_update_log);

		return ret;
	}

	if (log_getinfo) {
		ret = ioctl(fd_update_log, PFRT_LOG_IOC_GET_DATA_INFO, &data_info);
		if (ret) {
			perror("Get telemetry data info failed.");
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		ret = ioctl(fd_update_log, PFRT_LOG_IOC_GET_INFO, &info);
		if (ret) {
			perror("Get telemetry info failed.");
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		printf("log_level:%d\n", info.log_level);
		printf("log_type:%d\n", info.log_type);
		printf("log_revid:%d\n", info.log_revid);
		printf("max_data_size:%d\n", data_info.max_data_size);
		printf("chunk1_size:%d\n", data_info.chunk1_size);
		printf("chunk2_size:%d\n", data_info.chunk2_size);
		printf("rollover_cnt:%d\n", data_info.rollover_cnt);
		printf("reset_cnt:%d\n", data_info.reset_cnt);

		return 0;
	}

	info.log_level = -1;
	info.log_type = -1;
	info.log_revid = -1;

	if (set_log_level) {
		if (!valid_log_level(log_level)) {
			printf("Invalid log level %d\n",
			       log_level);
		} else {
			info.log_level = log_level;
		}
	}

	if (set_log_type) {
		if (!valid_log_type(log_type)) {
			printf("Invalid log type %d\n",
			       log_type);
		} else {
			info.log_type = log_type;
		}
	}

	if (set_log_revid) {
		if (!valid_log_revid(log_revid)) {
			printf("Invalid log revid %d, unchanged.\n",
			       log_revid);
		} else {
			info.log_revid = log_revid;
		}
	}

	ret = ioctl(fd_update_log, PFRT_LOG_IOC_SET_INFO, &info);
	if (ret) {
		perror("Log information set failed.(log_level, log_type, log_revid)");
		close(fd_update);
		close(fd_update_log);

		return 1;
	}

	if (set_revid) {
		ret = ioctl(fd_update, PFRU_IOC_SET_REV, &revid);
		if (ret) {
			perror("pfru update revid set failed");
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		printf("pfru update revid set to %d\n", revid);
	}

	if (capsule_name) {
		fd_capsule = open(capsule_name, O_RDONLY);
		if (fd_capsule < 0) {
			perror("Can not open capsule file...");
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		if (fstat(fd_capsule, &st) < 0) {
			perror("Can not fstat capsule file...");
			close(fd_capsule);
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		addr_map_capsule = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED,
					fd_capsule, 0);
		if (addr_map_capsule == MAP_FAILED) {
			perror("Failed to mmap capsule file.");
			close(fd_capsule);
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		ret = write(fd_update, (char *)addr_map_capsule, st.st_size);
		printf("Load %d bytes of capsule file into the system\n",
		       ret);

		if (ret == -1) {
			perror("Failed to load capsule file");
			close(fd_capsule);
			close(fd_update);
			close(fd_update_log);

			return 1;
		}

		munmap(addr_map_capsule, st.st_size);
		close(fd_capsule);
		printf("Load done.\n");
	}

	if (action) {
		if (action == 1) {
			ret = ioctl(fd_update, PFRU_IOC_STAGE, NULL);
		} else if (action == 2) {
			ret = ioctl(fd_update, PFRU_IOC_ACTIVATE, NULL);
		} else if (action == 3) {
			ret = ioctl(fd_update, PFRU_IOC_STAGE_ACTIVATE, NULL);
		} else {
			close(fd_update);
			close(fd_update_log);

			return 1;
		}
		printf("Update finished, return %d\n", ret);
	}

	close(fd_update);

	if (log_read) {
		void *p_mmap;
		int max_data_sz;

		ret = ioctl(fd_update_log, PFRT_LOG_IOC_GET_DATA_INFO, &data_info);
		if (ret) {
			perror("Get telemetry data info failed.");
			close(fd_update_log);

			return 1;
		}

		max_data_sz = data_info.max_data_size;
		if (!max_data_sz) {
			printf("No telemetry data available.\n");
			close(fd_update_log);

			return 1;
		}

		log_buf = malloc(max_data_sz + 1);
		if (!log_buf) {
			perror("log_buf allocate failed.");
			close(fd_update_log);

			return 1;
		}

		p_mmap = mmap(NULL, max_data_sz, PROT_READ, MAP_SHARED, fd_update_log, 0);
		if (p_mmap == MAP_FAILED) {
			perror("mmap error.");
			close(fd_update_log);

			return 1;
		}

		memcpy(log_buf, p_mmap, max_data_sz);
		log_buf[max_data_sz] = '\0';
		printf("%s\n", log_buf);
		free(log_buf);

		munmap(p_mmap, max_data_sz);
	}

	close(fd_update_log);

	return 0;
}
