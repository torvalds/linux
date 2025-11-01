// SPDX-License-Identifier: GPL-2.0
/*
 * delaytop.c - system-wide delay monitoring tool.
 *
 * This tool provides real-time monitoring and statistics of
 * system, container, and task-level delays, including CPU,
 * memory, IO, and IRQ. It supports both interactive (top-like),
 * and can output delay information for the whole system, specific
 * containers (cgroups), or individual tasks (PIDs).
 *
 * Key features:
 *	- Collects per-task delay accounting statistics via taskstats.
 *	- Collects system-wide PSI information.
 *	- Supports sorting, filtering.
 *	- Supports both interactive (screen refresh).
 *
 * Copyright (C) Fan Yu, ZTE Corp. 2025
 * Copyright (C) Wang Yaxin, ZTE Corp. 2025
 *
 * Compile with
 *	gcc -I/usr/src/linux/include delaytop.c -o delaytop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <termios.h>
#include <limits.h>
#include <linux/genetlink.h>
#include <linux/taskstats.h>
#include <linux/cgroupstats.h>
#include <stddef.h>

#define PSI_PATH	"/proc/pressure"
#define PSI_CPU_PATH	"/proc/pressure/cpu"
#define PSI_MEMORY_PATH	"/proc/pressure/memory"
#define PSI_IO_PATH	"/proc/pressure/io"
#define PSI_IRQ_PATH	"/proc/pressure/irq"

#define NLA_NEXT(na)			((struct nlattr *)((char *)(na) + NLA_ALIGN((na)->nla_len)))
#define NLA_DATA(na)			((void *)((char *)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)		(len - NLA_HDRLEN)

#define GENLMSG_DATA(glh)		((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)

#define TASK_COMM_LEN	16
#define MAX_MSG_SIZE	1024
#define MAX_TASKS		1000
#define MAX_BUF_LEN		256
#define SET_TASK_STAT(task_count, field) tasks[task_count].field = stats.field
#define BOOL_FPRINT(stream, fmt, ...) \
({ \
	int ret = fprintf(stream, fmt, ##__VA_ARGS__); \
	ret >= 0; \
})
#define TASK_AVG(task, field) average_ms((task).field##_delay_total, (task).field##_count)
#define PSI_LINE_FORMAT "%-12s %6.1f%%/%6.1f%%/%6.1f%%/%8llu(ms)\n"
#define DELAY_FMT_DEFAULT "%8.2f %8.2f %8.2f %8.2f\n"
#define DELAY_FMT_MEMVERBOSE "%8.2f %8.2f %8.2f %8.2f %8.2f %8.2f\n"
#define SORT_FIELD(name, cmd, modes) \
	{#name, #cmd, \
	offsetof(struct task_info, name##_delay_total), \
	offsetof(struct task_info, name##_count), \
	modes}
#define END_FIELD {NULL, 0, 0}

/* Display mode types */
#define MODE_TYPE_ALL	(0xFFFFFFFF)
#define MODE_DEFAULT	(1 << 0)
#define MODE_MEMVERBOSE	(1 << 1)

/* PSI statistics structure */
struct psi_stats {
	double cpu_some_avg10, cpu_some_avg60, cpu_some_avg300;
	unsigned long long cpu_some_total;
	double cpu_full_avg10, cpu_full_avg60, cpu_full_avg300;
	unsigned long long cpu_full_total;
	double memory_some_avg10, memory_some_avg60, memory_some_avg300;
	unsigned long long memory_some_total;
	double memory_full_avg10, memory_full_avg60, memory_full_avg300;
	unsigned long long memory_full_total;
	double io_some_avg10, io_some_avg60, io_some_avg300;
	unsigned long long io_some_total;
	double io_full_avg10, io_full_avg60, io_full_avg300;
	unsigned long long io_full_total;
	double irq_full_avg10, irq_full_avg60, irq_full_avg300;
	unsigned long long irq_full_total;
};

/* Task delay information structure */
struct task_info {
	int pid;
	int tgid;
	char command[TASK_COMM_LEN];
	unsigned long long cpu_count;
	unsigned long long cpu_delay_total;
	unsigned long long blkio_count;
	unsigned long long blkio_delay_total;
	unsigned long long swapin_count;
	unsigned long long swapin_delay_total;
	unsigned long long freepages_count;
	unsigned long long freepages_delay_total;
	unsigned long long thrashing_count;
	unsigned long long thrashing_delay_total;
	unsigned long long compact_count;
	unsigned long long compact_delay_total;
	unsigned long long wpcopy_count;
	unsigned long long wpcopy_delay_total;
	unsigned long long irq_count;
	unsigned long long irq_delay_total;
	unsigned long long mem_count;
	unsigned long long mem_delay_total;
};

/* Container statistics structure */
struct container_stats {
	int nr_sleeping;		/* Number of sleeping processes */
	int nr_running;			/* Number of running processes */
	int nr_stopped;			/* Number of stopped processes */
	int nr_uninterruptible; /* Number of uninterruptible processes */
	int nr_io_wait;			/* Number of processes in IO wait */
};

/* Delay field structure */
struct field_desc {
	const char *name;	/* Field name for cmdline argument */
	const char *cmd_char;	/* Interactive command */
	unsigned long total_offset; /* Offset of total delay in task_info */
	unsigned long count_offset; /* Offset of count in task_info */
	size_t supported_modes; /* Supported display modes */
};

/* Program settings structure */
struct config {
	int delay;				/* Update interval in seconds */
	int iterations;			/* Number of iterations, 0 == infinite */
	int max_processes;		/* Maximum number of processes to show */
	int output_one_time;	/* Output once and exit */
	int monitor_pid;		/* Monitor specific PID */
	char *container_path;	/* Path to container cgroup */
	const struct field_desc *sort_field;	/* Current sort field */
	size_t display_mode;	/* Current display mode */
};

/* Global variables */
static struct config cfg;
static struct psi_stats psi;
static struct task_info tasks[MAX_TASKS];
static int task_count;
static int running = 1;
static struct container_stats container_stats;
static const struct field_desc sort_fields[] = {
	SORT_FIELD(cpu,		c,	MODE_DEFAULT),
	SORT_FIELD(blkio,	i,	MODE_DEFAULT),
	SORT_FIELD(irq,		q,	MODE_DEFAULT),
	SORT_FIELD(mem,		m,	MODE_DEFAULT | MODE_MEMVERBOSE),
	SORT_FIELD(swapin,	s,	MODE_MEMVERBOSE),
	SORT_FIELD(freepages,	r,	MODE_MEMVERBOSE),
	SORT_FIELD(thrashing,	t,	MODE_MEMVERBOSE),
	SORT_FIELD(compact,	p,	MODE_MEMVERBOSE),
	SORT_FIELD(wpcopy,	w,	MODE_MEMVERBOSE),
	END_FIELD
};
static int sort_selected;

/* Netlink socket variables */
static int nl_sd = -1;
static int family_id;

/* Set terminal to non-canonical mode for q-to-quit */
static struct termios orig_termios;
static void enable_raw_mode(void)
{
	struct termios raw;

	tcgetattr(STDIN_FILENO, &orig_termios);
	raw = orig_termios;
	raw.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
static void disable_raw_mode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

/* Find field descriptor by command line */
static const struct field_desc *get_field_by_cmd_char(char ch)
{
	const struct field_desc *field;

	for (field = sort_fields; field->name != NULL; field++) {
		if (field->cmd_char[0] == ch)
			return field;
	}

	return NULL;
}

/* Find field descriptor by name with string comparison */
static const struct field_desc *get_field_by_name(const char *name)
{
	const struct field_desc *field;
	size_t field_len;

	for (field = sort_fields; field->name != NULL; field++) {
		field_len = strlen(field->name);
		if (field_len != strlen(name))
			continue;
		if (strncmp(field->name, name, field_len) == 0)
			return field;
	}

	return NULL;
}

/* Find display name for a field descriptor */
static const char *get_name_by_field(const struct field_desc *field)
{
	return field ? field->name : "UNKNOWN";
}

/* Generate string of available field names */
static void display_available_fields(size_t mode)
{
	const struct field_desc *field;
	char buf[MAX_BUF_LEN];

	buf[0] = '\0';

	for (field = sort_fields; field->name != NULL; field++) {
		if (!(field->supported_modes & mode))
			continue;
		strncat(buf, "|", MAX_BUF_LEN - strlen(buf) - 1);
		strncat(buf, field->name, MAX_BUF_LEN - strlen(buf) - 1);
		buf[MAX_BUF_LEN - 1] = '\0';
	}

	fprintf(stderr, "Available fields: %s\n", buf);
}

/* Display usage information and command line options */
static void usage(void)
{
	printf("Usage: delaytop [Options]\n"
	"Options:\n"
	"  -h, --help               Show this help message and exit\n"
	"  -d, --delay=SECONDS      Set refresh interval (default: 2 seconds, min: 1)\n"
	"  -n, --iterations=COUNT   Set number of updates (default: 0 = infinite)\n"
	"  -P, --processes=NUMBER   Set maximum number of processes to show (default: 20, max: 1000)\n"
	"  -o, --once               Display once and exit\n"
	"  -p, --pid=PID            Monitor only the specified PID\n"
	"  -C, --container=PATH     Monitor the container at specified cgroup path\n"
	"  -s, --sort=FIELD         Sort by delay field (default: cpu)\n"
	"  -M, --memverbose         Display memory detailed information\n");
	exit(0);
}

/* Parse command line arguments and set configuration */
static void parse_args(int argc, char **argv)
{
	int c;
	const struct field_desc *field;
	struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"delay", required_argument, 0, 'd'},
		{"iterations", required_argument, 0, 'n'},
		{"pid", required_argument, 0, 'p'},
		{"once", no_argument, 0, 'o'},
		{"processes", required_argument, 0, 'P'},
		{"sort", required_argument, 0, 's'},
		{"container", required_argument, 0, 'C'},
		{"memverbose", no_argument, 0, 'M'},
		{0, 0, 0, 0}
	};

	/* Set defaults */
	cfg.delay = 2;
	cfg.iterations = 0;
	cfg.max_processes = 20;
	cfg.sort_field = &sort_fields[0];	/* Default sorted by CPU delay */
	cfg.output_one_time = 0;
	cfg.monitor_pid = 0;	/* 0 means monitor all PIDs */
	cfg.container_path = NULL;
	cfg.display_mode = MODE_DEFAULT;

	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "hd:n:p:oP:C:s:M", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			break;
		case 'd':
			cfg.delay = atoi(optarg);
			if (cfg.delay < 1) {
				fprintf(stderr, "Error: delay must be >= 1.\n");
				exit(1);
			}
			break;
		case 'n':
			cfg.iterations = atoi(optarg);
			if (cfg.iterations < 0) {
				fprintf(stderr, "Error: iterations must be >= 0.\n");
				exit(1);
			}
			break;
		case 'p':
			cfg.monitor_pid = atoi(optarg);
			if (cfg.monitor_pid < 1) {
				fprintf(stderr, "Error: pid must be >= 1.\n");
				exit(1);
			}
			break;
		case 'o':
			cfg.output_one_time = 1;
			break;
		case 'P':
			cfg.max_processes = atoi(optarg);
			if (cfg.max_processes < 1) {
				fprintf(stderr, "Error: processes must be >= 1.\n");
				exit(1);
			}
			if (cfg.max_processes > MAX_TASKS) {
				fprintf(stderr, "Warning: processes capped to %d.\n",
					MAX_TASKS);
				cfg.max_processes = MAX_TASKS;
			}
			break;
		case 'C':
			cfg.container_path = strdup(optarg);
			break;
		case 's':
			if (strlen(optarg) == 0) {
				fprintf(stderr, "Error: empty sort field\n");
				exit(1);
			}

			field = get_field_by_name(optarg);
			/* Show available fields if invalid option provided */
			if (!field) {
				fprintf(stderr, "Error: invalid sort field '%s'\n", optarg);
				display_available_fields(MODE_TYPE_ALL);
				exit(1);
			}

			cfg.sort_field = field;
			break;
		case 'M':
			cfg.display_mode = MODE_MEMVERBOSE;
			cfg.sort_field = get_field_by_name("mem");
			break;
		default:
			fprintf(stderr, "Try 'delaytop --help' for more information.\n");
			exit(1);
		}
	}
}

/* Calculate average delay in milliseconds for overall memory */
static void set_mem_delay_total(struct task_info *t)
{
	t->mem_delay_total = t->swapin_delay_total +
		t->freepages_delay_total +
		t->thrashing_delay_total +
		t->compact_delay_total +
		t->wpcopy_delay_total;
}

static void set_mem_count(struct task_info *t)
{
	t->mem_count = t->swapin_count +
		t->freepages_count +
		t->thrashing_count +
		t->compact_count +
		t->wpcopy_count;
}

/* Create a raw netlink socket and bind */
static int create_nl_socket(void)
{
	int fd;
	struct sockaddr_nl local;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (fd < 0)
		return -1;

	memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;

	if (bind(fd, (struct sockaddr *) &local, sizeof(local)) < 0) {
		fprintf(stderr, "Failed to bind socket when create nl_socket\n");
		close(fd);
		return -1;
	}

	return fd;
}

/* Send a command via netlink */
static int send_cmd(int sd, __u16 nlmsg_type, __u32 nlmsg_pid,
			 __u8 genl_cmd, __u16 nla_type,
			 void *nla_data, int nla_len)
{
	struct sockaddr_nl nladdr;
	struct nlattr *na;
	int r, buflen;
	char *buf;

	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[MAX_MSG_SIZE];
	} msg;

	msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	msg.n.nlmsg_type = nlmsg_type;
	msg.n.nlmsg_flags = NLM_F_REQUEST;
	msg.n.nlmsg_seq = 0;
	msg.n.nlmsg_pid = nlmsg_pid;
	msg.g.cmd = genl_cmd;
	msg.g.version = 0x1;
	na = (struct nlattr *) GENLMSG_DATA(&msg);
	na->nla_type = nla_type;
	na->nla_len = nla_len + NLA_HDRLEN;
	memcpy(NLA_DATA(na), nla_data, nla_len);
	msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	buf = (char *) &msg;
	buflen = msg.n.nlmsg_len;
	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	while ((r = sendto(sd, buf, buflen, 0, (struct sockaddr *) &nladdr,
					sizeof(nladdr))) < buflen) {
		if (r > 0) {
			buf += r;
			buflen -= r;
		} else if (errno != EAGAIN)
			return -1;
	}
	return 0;
}

/* Get family ID for taskstats via netlink */
static int get_family_id(int sd)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[256];
	} ans;

	int id = 0, rc;
	struct nlattr *na;
	int rep_len;
	char name[100];

	strncpy(name, TASKSTATS_GENL_NAME, sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	rc = send_cmd(sd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
			CTRL_ATTR_FAMILY_NAME, (void *)name,
			strlen(TASKSTATS_GENL_NAME)+1);
	if (rc < 0) {
		fprintf(stderr, "Failed to send cmd for family id\n");
		return 0;
	}

	rep_len = recv(sd, &ans, sizeof(ans), 0);
	if (ans.n.nlmsg_type == NLMSG_ERROR ||
		(rep_len < 0) || !NLMSG_OK((&ans.n), rep_len)) {
		fprintf(stderr, "Failed to receive response for family id\n");
		return 0;
	}

	na = (struct nlattr *) GENLMSG_DATA(&ans);
	na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID)
		id = *(__u16 *) NLA_DATA(na);
	return id;
}

static int read_psi_stats(void)
{
	FILE *fp;
	char line[256];
	int ret = 0;
	int error_count = 0;

	/* Check if PSI path exists */
	if (access(PSI_PATH, F_OK) != 0) {
		fprintf(stderr, "Error: PSI interface not found at %s\n", PSI_PATH);
		fprintf(stderr, "Please ensure your kernel supports PSI (Pressure Stall Information)\n");
		return -1;
	}

	/* Zero all fields */
	memset(&psi, 0, sizeof(psi));

	/* CPU pressure */
	fp = fopen(PSI_CPU_PATH, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "some", 4) == 0) {
				ret = sscanf(line, "some avg10=%lf avg60=%lf avg300=%lf total=%llu",
							&psi.cpu_some_avg10, &psi.cpu_some_avg60,
							&psi.cpu_some_avg300, &psi.cpu_some_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse CPU some PSI data\n");
					error_count++;
				}
			} else if (strncmp(line, "full", 4) == 0) {
				ret = sscanf(line, "full avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.cpu_full_avg10, &psi.cpu_full_avg60,
						&psi.cpu_full_avg300, &psi.cpu_full_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse CPU full PSI data\n");
					error_count++;
				}
			}
		}
		fclose(fp);
	} else {
		fprintf(stderr, "Warning: Failed to open %s\n", PSI_CPU_PATH);
		error_count++;
	}

	/* Memory pressure */
	fp = fopen(PSI_MEMORY_PATH, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "some", 4) == 0) {
				ret = sscanf(line, "some avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.memory_some_avg10, &psi.memory_some_avg60,
						&psi.memory_some_avg300, &psi.memory_some_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse Memory some PSI data\n");
					error_count++;
				}
			} else if (strncmp(line, "full", 4) == 0) {
				ret = sscanf(line, "full avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.memory_full_avg10, &psi.memory_full_avg60,
						&psi.memory_full_avg300, &psi.memory_full_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse Memory full PSI data\n");
					error_count++;
				}
			}
		}
		fclose(fp);
	} else {
		fprintf(stderr, "Warning: Failed to open %s\n", PSI_MEMORY_PATH);
		error_count++;
	}

	/* IO pressure */
	fp = fopen(PSI_IO_PATH, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "some", 4) == 0) {
				ret = sscanf(line, "some avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.io_some_avg10, &psi.io_some_avg60,
						&psi.io_some_avg300, &psi.io_some_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse IO some PSI data\n");
					error_count++;
				}
			} else if (strncmp(line, "full", 4) == 0) {
				ret = sscanf(line, "full avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.io_full_avg10, &psi.io_full_avg60,
						&psi.io_full_avg300, &psi.io_full_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse IO full PSI data\n");
					error_count++;
				}
			}
		}
		fclose(fp);
	} else {
		fprintf(stderr, "Warning: Failed to open %s\n", PSI_IO_PATH);
		error_count++;
	}

	/* IRQ pressure (only full) */
	fp = fopen(PSI_IRQ_PATH, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "full", 4) == 0) {
				ret = sscanf(line, "full avg10=%lf avg60=%lf avg300=%lf total=%llu",
						&psi.irq_full_avg10, &psi.irq_full_avg60,
						&psi.irq_full_avg300, &psi.irq_full_total);
				if (ret != 4) {
					fprintf(stderr, "Failed to parse IRQ full PSI data\n");
					error_count++;
				}
			}
		}
		fclose(fp);
	} else {
		fprintf(stderr, "Warning: Failed to open %s\n", PSI_IRQ_PATH);
		error_count++;
	}

	/* Return error count: 0 means success, >0 means warnings, -1 means fatal error */
	if (error_count > 0) {
		fprintf(stderr, "PSI stats reading completed with %d warnings\n", error_count);
		return error_count;
	}

	return 0;
}

static int read_comm(int pid, char *comm_buf, size_t buf_size)
{
	char path[64];
	int ret = -1;
	size_t len;
	FILE *fp;

	snprintf(path, sizeof(path), "/proc/%d/comm", pid);
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "Failed to open comm file /proc/%d/comm\n", pid);
		return ret;
	}

	if (fgets(comm_buf, buf_size, fp)) {
		len = strlen(comm_buf);
		if (len > 0 && comm_buf[len - 1] == '\n')
			comm_buf[len - 1] = '\0';
		ret = 0;
	}

	fclose(fp);

	return ret;
}

static void fetch_and_fill_task_info(int pid, const char *comm)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[MAX_MSG_SIZE];
	} resp;
	struct taskstats stats;
	struct nlattr *nested;
	struct nlattr *na;
	int nested_len;
	int nl_len;
	int rc;

	/* Send request for task stats */
	if (send_cmd(nl_sd, family_id, getpid(), TASKSTATS_CMD_GET,
				 TASKSTATS_CMD_ATTR_PID, &pid, sizeof(pid)) < 0) {
		fprintf(stderr, "Failed to send request for task stats\n");
		return;
	}

	/* Receive response */
	rc = recv(nl_sd, &resp, sizeof(resp), 0);
	if (rc < 0 || resp.n.nlmsg_type == NLMSG_ERROR) {
		fprintf(stderr, "Failed to receive response for task stats\n");
		return;
	}

	/* Parse response */
	nl_len = GENLMSG_PAYLOAD(&resp.n);
	na = (struct nlattr *) GENLMSG_DATA(&resp);
	while (nl_len > 0) {
		if (na->nla_type == TASKSTATS_TYPE_AGGR_PID) {
			nested = (struct nlattr *) NLA_DATA(na);
			nested_len = NLA_PAYLOAD(na->nla_len);
			while (nested_len > 0) {
				if (nested->nla_type == TASKSTATS_TYPE_STATS) {
					memcpy(&stats, NLA_DATA(nested), sizeof(stats));
					if (task_count < MAX_TASKS) {
						tasks[task_count].pid = pid;
						tasks[task_count].tgid = pid;
						strncpy(tasks[task_count].command, comm,
							TASK_COMM_LEN - 1);
						tasks[task_count].command[TASK_COMM_LEN - 1] = '\0';
						SET_TASK_STAT(task_count, cpu_count);
						SET_TASK_STAT(task_count, cpu_delay_total);
						SET_TASK_STAT(task_count, blkio_count);
						SET_TASK_STAT(task_count, blkio_delay_total);
						SET_TASK_STAT(task_count, swapin_count);
						SET_TASK_STAT(task_count, swapin_delay_total);
						SET_TASK_STAT(task_count, freepages_count);
						SET_TASK_STAT(task_count, freepages_delay_total);
						SET_TASK_STAT(task_count, thrashing_count);
						SET_TASK_STAT(task_count, thrashing_delay_total);
						SET_TASK_STAT(task_count, compact_count);
						SET_TASK_STAT(task_count, compact_delay_total);
						SET_TASK_STAT(task_count, wpcopy_count);
						SET_TASK_STAT(task_count, wpcopy_delay_total);
						SET_TASK_STAT(task_count, irq_count);
						SET_TASK_STAT(task_count, irq_delay_total);
						set_mem_count(&tasks[task_count]);
						set_mem_delay_total(&tasks[task_count]);
						task_count++;
					}
					break;
				}
				nested_len -= NLA_ALIGN(nested->nla_len);
				nested = NLA_NEXT(nested);
			}
		}
		nl_len -= NLA_ALIGN(na->nla_len);
		na = NLA_NEXT(na);
	}
	return;
}

static void get_task_delays(void)
{
	char comm[TASK_COMM_LEN];
	struct dirent *entry;
	DIR *dir;
	int pid;

	task_count = 0;
	if (cfg.monitor_pid > 0) {
		if (read_comm(cfg.monitor_pid, comm, sizeof(comm)) == 0)
			fetch_and_fill_task_info(cfg.monitor_pid, comm);
		return;
	}

	dir = opendir("/proc");
	if (!dir) {
		fprintf(stderr, "Error opening /proc directory\n");
		return;
	}

	while ((entry = readdir(dir)) != NULL && task_count < MAX_TASKS) {
		if (!isdigit(entry->d_name[0]))
			continue;
		pid = atoi(entry->d_name);
		if (pid == 0)
			continue;
		if (read_comm(pid, comm, sizeof(comm)) != 0)
			continue;
		fetch_and_fill_task_info(pid, comm);
	}
	closedir(dir);
}

/* Calculate average delay in milliseconds */
static double average_ms(unsigned long long total, unsigned long long count)
{
	if (count == 0)
		return 0;
	return (double)total / 1000000.0 / count;
}

/* Comparison function for sorting tasks */
static int compare_tasks(const void *a, const void *b)
{
	const struct task_info *t1 = (const struct task_info *)a;
	const struct task_info *t2 = (const struct task_info *)b;
	unsigned long long total1;
	unsigned long long total2;
	unsigned long count1;
	unsigned long count2;
	double avg1, avg2;

	total1 = *(unsigned long long *)((char *)t1 + cfg.sort_field->total_offset);
	total2 = *(unsigned long long *)((char *)t2 + cfg.sort_field->total_offset);
	count1 = *(unsigned long *)((char *)t1 + cfg.sort_field->count_offset);
	count2 = *(unsigned long *)((char *)t2 + cfg.sort_field->count_offset);

	avg1 = average_ms(total1, count1);
	avg2 = average_ms(total2, count2);
	if (avg1 != avg2)
		return avg2 > avg1 ? 1 : -1;

	return 0;
}

/* Sort tasks by selected field */
static void sort_tasks(void)
{
	if (task_count > 0)
		qsort(tasks, task_count, sizeof(struct task_info), compare_tasks);
}

/* Get container statistics via cgroupstats */
static void get_container_stats(void)
{
	int rc, cfd;
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[MAX_MSG_SIZE];
	} req, resp;
	struct nlattr *na;
	int nl_len;
	struct cgroupstats stats;

	/* Check if container path is set */
	if (!cfg.container_path)
		return;

	/* Open container cgroup */
	cfd = open(cfg.container_path, O_RDONLY);
	if (cfd < 0) {
		fprintf(stderr, "Error opening container path: %s\n", cfg.container_path);
		return;
	}

	/* Send request for container stats */
	if (send_cmd(nl_sd, family_id, getpid(), CGROUPSTATS_CMD_GET,
				CGROUPSTATS_CMD_ATTR_FD, &cfd, sizeof(__u32)) < 0) {
		fprintf(stderr, "Failed to send request for container stats\n");
		close(cfd);
		return;
	}

	/* Receive response */
	rc = recv(nl_sd, &resp, sizeof(resp), 0);
	if (rc < 0 || resp.n.nlmsg_type == NLMSG_ERROR) {
		fprintf(stderr, "Failed to receive response for container stats\n");
		close(cfd);
		return;
	}

	/* Parse response */
	nl_len = GENLMSG_PAYLOAD(&resp.n);
	na = (struct nlattr *) GENLMSG_DATA(&resp);
	while (nl_len > 0) {
		if (na->nla_type == CGROUPSTATS_TYPE_CGROUP_STATS) {
			/* Get the cgroupstats structure */
			memcpy(&stats, NLA_DATA(na), sizeof(stats));

			/* Fill container stats */
			container_stats.nr_sleeping = stats.nr_sleeping;
			container_stats.nr_running = stats.nr_running;
			container_stats.nr_stopped = stats.nr_stopped;
			container_stats.nr_uninterruptible = stats.nr_uninterruptible;
			container_stats.nr_io_wait = stats.nr_io_wait;
			break;
		}
		nl_len -= NLA_ALIGN(na->nla_len);
		na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
	}

	close(cfd);
}

/* Display results to stdout or log file */
static void display_results(int psi_ret)
{
	time_t now = time(NULL);
	struct tm *tm_now = localtime(&now);
	FILE *out = stdout;
	char timestamp[32];
	bool suc = true;
	int i, count;

	/* Clear terminal screen */
	suc &= BOOL_FPRINT(out, "\033[H\033[J");

	/* PSI output (one-line, no cat style) */
	suc &= BOOL_FPRINT(out, "System Pressure Information: (avg10/avg60vg300/total)\n");
	if (psi_ret) {
		suc &= BOOL_FPRINT(out, "  PSI not found: check if psi=1 enabled in cmdline\n");
	} else {
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"CPU some:",
			psi.cpu_some_avg10,
			psi.cpu_some_avg60,
			psi.cpu_some_avg300,
			psi.cpu_some_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"CPU full:",
			psi.cpu_full_avg10,
			psi.cpu_full_avg60,
			psi.cpu_full_avg300,
			psi.cpu_full_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"Memory full:",
			psi.memory_full_avg10,
			psi.memory_full_avg60,
			psi.memory_full_avg300,
			psi.memory_full_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"Memory some:",
			psi.memory_some_avg10,
			psi.memory_some_avg60,
			psi.memory_some_avg300,
			psi.memory_some_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"IO full:",
			psi.io_full_avg10,
			psi.io_full_avg60,
			psi.io_full_avg300,
			psi.io_full_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"IO some:",
			psi.io_some_avg10,
			psi.io_some_avg60,
			psi.io_some_avg300,
			psi.io_some_total / 1000);
		suc &= BOOL_FPRINT(out, PSI_LINE_FORMAT,
			"IRQ full:",
			psi.irq_full_avg10,
			psi.irq_full_avg60,
			psi.irq_full_avg300,
			psi.irq_full_total / 1000);
	}

	if (cfg.container_path) {
		suc &= BOOL_FPRINT(out, "Container Information (%s):\n", cfg.container_path);
		suc &= BOOL_FPRINT(out, "Processes: running=%d, sleeping=%d, ",
			container_stats.nr_running, container_stats.nr_sleeping);
		suc &= BOOL_FPRINT(out, "stopped=%d, uninterruptible=%d, io_wait=%d\n\n",
			container_stats.nr_stopped, container_stats.nr_uninterruptible,
			container_stats.nr_io_wait);
	}

	/* Interacive command */
	suc &= BOOL_FPRINT(out, "[o]sort [M]memverbose [q]quit\n");
	if (sort_selected) {
		if (cfg.display_mode == MODE_MEMVERBOSE)
			suc &= BOOL_FPRINT(out,
				"sort selection: [m]MEM [r]RCL [t]THR [p]CMP [w]WP\n");
		else
			suc &= BOOL_FPRINT(out,
				"sort selection: [c]CPU [i]IO [m]MEM [q]IRQ\n");
	}

	/* Task delay output */
	suc &= BOOL_FPRINT(out, "Top %d processes (sorted by %s delay):\n",
			cfg.max_processes, get_name_by_field(cfg.sort_field));

	suc &= BOOL_FPRINT(out, "%8s  %8s  %-17s", "PID", "TGID", "COMMAND");
	if (cfg.display_mode == MODE_MEMVERBOSE) {
		suc &= BOOL_FPRINT(out, "%8s %8s %8s %8s %8s %8s\n",
			"MEM(ms)", "SWAP(ms)", "RCL(ms)",
			"THR(ms)", "CMP(ms)", "WP(ms)");
		suc &= BOOL_FPRINT(out, "-----------------------");
		suc &= BOOL_FPRINT(out, "-----------------------");
		suc &= BOOL_FPRINT(out, "-----------------------");
		suc &= BOOL_FPRINT(out, "---------------------\n");
	} else {
		suc &= BOOL_FPRINT(out, "%8s %8s %8s %8s\n",
			"CPU(ms)", "IO(ms)", "IRQ(ms)", "MEM(ms)");
		suc &= BOOL_FPRINT(out, "-----------------------");
		suc &= BOOL_FPRINT(out, "-----------------------");
		suc &= BOOL_FPRINT(out, "--------------------------\n");
	}

	count = task_count < cfg.max_processes ? task_count : cfg.max_processes;

	for (i = 0; i < count; i++) {
		suc &= BOOL_FPRINT(out, "%8d  %8d  %-15s",
			tasks[i].pid, tasks[i].tgid, tasks[i].command);
		if (cfg.display_mode == MODE_MEMVERBOSE) {
			suc &= BOOL_FPRINT(out, DELAY_FMT_MEMVERBOSE,
				TASK_AVG(tasks[i], mem),
				TASK_AVG(tasks[i], swapin),
				TASK_AVG(tasks[i], freepages),
				TASK_AVG(tasks[i], thrashing),
				TASK_AVG(tasks[i], compact),
				TASK_AVG(tasks[i], wpcopy));
		} else {
			suc &= BOOL_FPRINT(out, DELAY_FMT_DEFAULT,
				TASK_AVG(tasks[i], cpu),
				TASK_AVG(tasks[i], blkio),
				TASK_AVG(tasks[i], irq),
				TASK_AVG(tasks[i], mem));
		}
	}

	suc &= BOOL_FPRINT(out, "\n");

	if (!suc)
		perror("Error writing to output");
}

/* Check for keyboard input with timeout based on cfg.delay */
static char check_for_keypress(void)
{
	struct timeval tv = {cfg.delay, 0};
	fd_set readfds;
	char ch = 0;

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);
	int r = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

	if (r > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
		read(STDIN_FILENO, &ch, 1);
		return ch;
	}

	return 0;
}

#define MAX_MODE_SIZE 2
static void toggle_display_mode(void)
{
	static const size_t modes[MAX_MODE_SIZE] = {MODE_DEFAULT, MODE_MEMVERBOSE};
	static size_t cur_index;

	cur_index = (cur_index + 1) % MAX_MODE_SIZE;
	cfg.display_mode = modes[cur_index];
}

/* Handle keyboard input: sorting selection, mode toggle, or quit */
static void handle_keypress(char ch, int *running)
{
	const struct field_desc *field;

	/* Change sort field */
	if (sort_selected) {
		field = get_field_by_cmd_char(ch);
		if (field && (field->supported_modes & cfg.display_mode))
			cfg.sort_field = field;

		sort_selected = 0;
	/* Handle mode changes or quit */
	} else {
		switch (ch) {
		case 'o':
			sort_selected = 1;
			break;
		case 'M':
			toggle_display_mode();
			for (field = sort_fields; field->name != NULL; field++) {
				if (field->supported_modes & cfg.display_mode) {
					cfg.sort_field = field;
					break;
				}
			}
			break;
		case 'q':
		case 'Q':
			*running = 0;
			break;
		default:
			break;
		}
	}
}

/* Main function */
int main(int argc, char **argv)
{
	const struct field_desc *field;
	int iterations = 0;
	int psi_ret = 0;
	char keypress;

	/* Parse command line arguments */
	parse_args(argc, argv);

	/* Setup netlink socket */
	nl_sd = create_nl_socket();
	if (nl_sd < 0) {
		fprintf(stderr, "Error creating netlink socket\n");
		exit(1);
	}

	/* Get family ID for taskstats via netlink */
	family_id = get_family_id(nl_sd);
	if (!family_id) {
		fprintf(stderr, "Error getting taskstats family ID\n");
		close(nl_sd);
		exit(1);
	}

	/* Set terminal to non-canonical mode for interaction */
	enable_raw_mode();

	/* Main loop */
	while (running) {
		/* Auto-switch sort field when not matching display mode */
		if (!(cfg.sort_field->supported_modes & cfg.display_mode)) {
			for (field = sort_fields; field->name != NULL; field++) {
				if (field->supported_modes & cfg.display_mode) {
					cfg.sort_field = field;
					printf("Auto-switched sort field to: %s\n", field->name);
					break;
				}
			}
		}

		/* Read PSI statistics */
		psi_ret = read_psi_stats();

		/* Get container stats if container path provided */
		if (cfg.container_path)
			get_container_stats();

		/* Get task delays */
		get_task_delays();

		/* Sort tasks */
		sort_tasks();

		/* Display results to stdout or log file */
		display_results(psi_ret);

		/* Check for iterations */
		if (cfg.iterations > 0 && ++iterations >= cfg.iterations)
			break;

		/* Exit if output_one_time is set */
		if (cfg.output_one_time)
			break;

		/* Keypress for interactive usage */
		keypress = check_for_keypress();
		if (keypress)
			handle_keypress(keypress, &running);
	}

	/* Restore terminal mode */
	disable_raw_mode();

	/* Cleanup */
	close(nl_sd);
	if (cfg.container_path)
		free(cfg.container_path);

	return 0;
}
