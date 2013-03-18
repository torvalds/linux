/*
 * An implementation of key value pair (KVP) functionality for Linux.
 *
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/utsname.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <linux/connector.h>
#include <linux/hyperv.h>
#include <linux/netlink.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <net/if.h>

/*
 * KVP protocol: The user mode component first registers with the
 * the kernel component. Subsequently, the kernel component requests, data
 * for the specified keys. In response to this message the user mode component
 * fills in the value corresponding to the specified key. We overload the
 * sequence field in the cn_msg header to define our KVP message types.
 *
 * We use this infrastructure for also supporting queries from user mode
 * application for state that may be maintained in the KVP kernel component.
 *
 */


enum key_index {
	FullyQualifiedDomainName = 0,
	IntegrationServicesVersion, /*This key is serviced in the kernel*/
	NetworkAddressIPv4,
	NetworkAddressIPv6,
	OSBuildNumber,
	OSName,
	OSMajorVersion,
	OSMinorVersion,
	OSVersion,
	ProcessorArchitecture
};


enum {
	IPADDR = 0,
	NETMASK,
	GATEWAY,
	DNS
};

static char kvp_send_buffer[4096];
static char kvp_recv_buffer[4096 * 2];
static struct sockaddr_nl addr;
static int in_hand_shake = 1;

static char *os_name = "";
static char *os_major = "";
static char *os_minor = "";
static char *processor_arch;
static char *os_build;
static char *os_version;
static char *lic_version = "Unknown version";
static struct utsname uts_buf;

/*
 * The location of the interface configuration file.
 */

#define KVP_CONFIG_LOC	"/var/lib/hyperv"

#define MAX_FILE_NAME 100
#define ENTRIES_PER_BLOCK 50

struct kvp_record {
	char key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
	char value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE];
};

struct kvp_file_state {
	int fd;
	int num_blocks;
	struct kvp_record *records;
	int num_records;
	char fname[MAX_FILE_NAME];
};

static struct kvp_file_state kvp_file_info[KVP_POOL_COUNT];

static void kvp_acquire_lock(int pool)
{
	struct flock fl = {F_WRLCK, SEEK_SET, 0, 0, 0};
	fl.l_pid = getpid();

	if (fcntl(kvp_file_info[pool].fd, F_SETLKW, &fl) == -1) {
		syslog(LOG_ERR, "Failed to acquire the lock pool: %d", pool);
		exit(EXIT_FAILURE);
	}
}

static void kvp_release_lock(int pool)
{
	struct flock fl = {F_UNLCK, SEEK_SET, 0, 0, 0};
	fl.l_pid = getpid();

	if (fcntl(kvp_file_info[pool].fd, F_SETLK, &fl) == -1) {
		perror("fcntl");
		syslog(LOG_ERR, "Failed to release the lock pool: %d", pool);
		exit(EXIT_FAILURE);
	}
}

static void kvp_update_file(int pool)
{
	FILE *filep;
	size_t bytes_written;

	/*
	 * We are going to write our in-memory registry out to
	 * disk; acquire the lock first.
	 */
	kvp_acquire_lock(pool);

	filep = fopen(kvp_file_info[pool].fname, "we");
	if (!filep) {
		kvp_release_lock(pool);
		syslog(LOG_ERR, "Failed to open file, pool: %d", pool);
		exit(EXIT_FAILURE);
	}

	bytes_written = fwrite(kvp_file_info[pool].records,
				sizeof(struct kvp_record),
				kvp_file_info[pool].num_records, filep);

	if (ferror(filep) || fclose(filep)) {
		kvp_release_lock(pool);
		syslog(LOG_ERR, "Failed to write file, pool: %d", pool);
		exit(EXIT_FAILURE);
	}

	kvp_release_lock(pool);
}

static void kvp_update_mem_state(int pool)
{
	FILE *filep;
	size_t records_read = 0;
	struct kvp_record *record = kvp_file_info[pool].records;
	struct kvp_record *readp;
	int num_blocks = kvp_file_info[pool].num_blocks;
	int alloc_unit = sizeof(struct kvp_record) * ENTRIES_PER_BLOCK;

	kvp_acquire_lock(pool);

	filep = fopen(kvp_file_info[pool].fname, "re");
	if (!filep) {
		kvp_release_lock(pool);
		syslog(LOG_ERR, "Failed to open file, pool: %d", pool);
		exit(EXIT_FAILURE);
	}
	for (;;) {
		readp = &record[records_read];
		records_read += fread(readp, sizeof(struct kvp_record),
					ENTRIES_PER_BLOCK * num_blocks,
					filep);

		if (ferror(filep)) {
			syslog(LOG_ERR, "Failed to read file, pool: %d", pool);
			exit(EXIT_FAILURE);
		}

		if (!feof(filep)) {
			/*
			 * We have more data to read.
			 */
			num_blocks++;
			record = realloc(record, alloc_unit * num_blocks);

			if (record == NULL) {
				syslog(LOG_ERR, "malloc failed");
				exit(EXIT_FAILURE);
			}
			continue;
		}
		break;
	}

	kvp_file_info[pool].num_blocks = num_blocks;
	kvp_file_info[pool].records = record;
	kvp_file_info[pool].num_records = records_read;

	fclose(filep);
	kvp_release_lock(pool);
}
static int kvp_file_init(void)
{
	int  fd;
	FILE *filep;
	size_t records_read;
	char *fname;
	struct kvp_record *record;
	struct kvp_record *readp;
	int num_blocks;
	int i;
	int alloc_unit = sizeof(struct kvp_record) * ENTRIES_PER_BLOCK;

	if (access(KVP_CONFIG_LOC, F_OK)) {
		if (mkdir(KVP_CONFIG_LOC, 0755 /* rwxr-xr-x */)) {
			syslog(LOG_ERR, " Failed to create %s", KVP_CONFIG_LOC);
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < KVP_POOL_COUNT; i++) {
		fname = kvp_file_info[i].fname;
		records_read = 0;
		num_blocks = 1;
		sprintf(fname, "%s/.kvp_pool_%d", KVP_CONFIG_LOC, i);
		fd = open(fname, O_RDWR | O_CREAT | O_CLOEXEC, 0644 /* rw-r--r-- */);

		if (fd == -1)
			return 1;


		filep = fopen(fname, "re");
		if (!filep)
			return 1;

		record = malloc(alloc_unit * num_blocks);
		if (record == NULL) {
			fclose(filep);
			return 1;
		}
		for (;;) {
			readp = &record[records_read];
			records_read += fread(readp, sizeof(struct kvp_record),
					ENTRIES_PER_BLOCK,
					filep);

			if (ferror(filep)) {
				syslog(LOG_ERR, "Failed to read file, pool: %d",
				       i);
				exit(EXIT_FAILURE);
			}

			if (!feof(filep)) {
				/*
				 * We have more data to read.
				 */
				num_blocks++;
				record = realloc(record, alloc_unit *
						num_blocks);
				if (record == NULL) {
					fclose(filep);
					return 1;
				}
				continue;
			}
			break;
		}
		kvp_file_info[i].fd = fd;
		kvp_file_info[i].num_blocks = num_blocks;
		kvp_file_info[i].records = record;
		kvp_file_info[i].num_records = records_read;
		fclose(filep);

	}

	return 0;
}

static int kvp_key_delete(int pool, const char *key, int key_size)
{
	int i;
	int j, k;
	int num_records;
	struct kvp_record *record;

	/*
	 * First update the in-memory state.
	 */
	kvp_update_mem_state(pool);

	num_records = kvp_file_info[pool].num_records;
	record = kvp_file_info[pool].records;

	for (i = 0; i < num_records; i++) {
		if (memcmp(key, record[i].key, key_size))
			continue;
		/*
		 * Found a match; just move the remaining
		 * entries up.
		 */
		if (i == num_records) {
			kvp_file_info[pool].num_records--;
			kvp_update_file(pool);
			return 0;
		}

		j = i;
		k = j + 1;
		for (; k < num_records; k++) {
			strcpy(record[j].key, record[k].key);
			strcpy(record[j].value, record[k].value);
			j++;
		}

		kvp_file_info[pool].num_records--;
		kvp_update_file(pool);
		return 0;
	}
	return 1;
}

static int kvp_key_add_or_modify(int pool, const char *key, int key_size, const char *value,
			int value_size)
{
	int i;
	int num_records;
	struct kvp_record *record;
	int num_blocks;

	if ((key_size > HV_KVP_EXCHANGE_MAX_KEY_SIZE) ||
		(value_size > HV_KVP_EXCHANGE_MAX_VALUE_SIZE))
		return 1;

	/*
	 * First update the in-memory state.
	 */
	kvp_update_mem_state(pool);

	num_records = kvp_file_info[pool].num_records;
	record = kvp_file_info[pool].records;
	num_blocks = kvp_file_info[pool].num_blocks;

	for (i = 0; i < num_records; i++) {
		if (memcmp(key, record[i].key, key_size))
			continue;
		/*
		 * Found a match; just update the value -
		 * this is the modify case.
		 */
		memcpy(record[i].value, value, value_size);
		kvp_update_file(pool);
		return 0;
	}

	/*
	 * Need to add a new entry;
	 */
	if (num_records == (ENTRIES_PER_BLOCK * num_blocks)) {
		/* Need to allocate a larger array for reg entries. */
		record = realloc(record, sizeof(struct kvp_record) *
			 ENTRIES_PER_BLOCK * (num_blocks + 1));

		if (record == NULL)
			return 1;
		kvp_file_info[pool].num_blocks++;

	}
	memcpy(record[i].value, value, value_size);
	memcpy(record[i].key, key, key_size);
	kvp_file_info[pool].records = record;
	kvp_file_info[pool].num_records++;
	kvp_update_file(pool);
	return 0;
}

static int kvp_get_value(int pool, const char *key, int key_size, char *value,
			int value_size)
{
	int i;
	int num_records;
	struct kvp_record *record;

	if ((key_size > HV_KVP_EXCHANGE_MAX_KEY_SIZE) ||
		(value_size > HV_KVP_EXCHANGE_MAX_VALUE_SIZE))
		return 1;

	/*
	 * First update the in-memory state.
	 */
	kvp_update_mem_state(pool);

	num_records = kvp_file_info[pool].num_records;
	record = kvp_file_info[pool].records;

	for (i = 0; i < num_records; i++) {
		if (memcmp(key, record[i].key, key_size))
			continue;
		/*
		 * Found a match; just copy the value out.
		 */
		memcpy(value, record[i].value, value_size);
		return 0;
	}

	return 1;
}

static int kvp_pool_enumerate(int pool, int index, char *key, int key_size,
				char *value, int value_size)
{
	struct kvp_record *record;

	/*
	 * First update our in-memory database.
	 */
	kvp_update_mem_state(pool);
	record = kvp_file_info[pool].records;

	if (index >= kvp_file_info[pool].num_records) {
		return 1;
	}

	memcpy(key, record[index].key, key_size);
	memcpy(value, record[index].value, value_size);
	return 0;
}


void kvp_get_os_info(void)
{
	FILE	*file;
	char	*p, buf[512];

	uname(&uts_buf);
	os_version = uts_buf.release;
	os_build = strdup(uts_buf.release);

	os_name = uts_buf.sysname;
	processor_arch = uts_buf.machine;

	/*
	 * The current windows host (win7) expects the build
	 * string to be of the form: x.y.z
	 * Strip additional information we may have.
	 */
	p = strchr(os_version, '-');
	if (p)
		*p = '\0';

	/*
	 * Parse the /etc/os-release file if present:
	 * http://www.freedesktop.org/software/systemd/man/os-release.html
	 */
	file = fopen("/etc/os-release", "r");
	if (file != NULL) {
		while (fgets(buf, sizeof(buf), file)) {
			char *value, *q;

			/* Ignore comments */
			if (buf[0] == '#')
				continue;

			/* Split into name=value */
			p = strchr(buf, '=');
			if (!p)
				continue;
			*p++ = 0;

			/* Remove quotes and newline; un-escape */
			value = p;
			q = p;
			while (*p) {
				if (*p == '\\') {
					++p;
					if (!*p)
						break;
					*q++ = *p++;
				} else if (*p == '\'' || *p == '"' ||
					   *p == '\n') {
					++p;
				} else {
					*q++ = *p++;
				}
			}
			*q = 0;

			if (!strcmp(buf, "NAME")) {
				p = strdup(value);
				if (!p)
					break;
				os_name = p;
			} else if (!strcmp(buf, "VERSION_ID")) {
				p = strdup(value);
				if (!p)
					break;
				os_major = p;
			}
		}
		fclose(file);
		return;
	}

	/* Fallback for older RH/SUSE releases */
	file = fopen("/etc/SuSE-release", "r");
	if (file != NULL)
		goto kvp_osinfo_found;
	file  = fopen("/etc/redhat-release", "r");
	if (file != NULL)
		goto kvp_osinfo_found;

	/*
	 * We don't have information about the os.
	 */
	return;

kvp_osinfo_found:
	/* up to three lines */
	p = fgets(buf, sizeof(buf), file);
	if (p) {
		p = strchr(buf, '\n');
		if (p)
			*p = '\0';
		p = strdup(buf);
		if (!p)
			goto done;
		os_name = p;

		/* second line */
		p = fgets(buf, sizeof(buf), file);
		if (p) {
			p = strchr(buf, '\n');
			if (p)
				*p = '\0';
			p = strdup(buf);
			if (!p)
				goto done;
			os_major = p;

			/* third line */
			p = fgets(buf, sizeof(buf), file);
			if (p)  {
				p = strchr(buf, '\n');
				if (p)
					*p = '\0';
				p = strdup(buf);
				if (p)
					os_minor = p;
			}
		}
	}

done:
	fclose(file);
	return;
}



/*
 * Retrieve an interface name corresponding to the specified guid.
 * If there is a match, the function returns a pointer
 * to the interface name and if not, a NULL is returned.
 * If a match is found, the caller is responsible for
 * freeing the memory.
 */

static char *kvp_get_if_name(char *guid)
{
	DIR *dir;
	struct dirent *entry;
	FILE    *file;
	char    *p, *q, *x;
	char    *if_name = NULL;
	char    buf[256];
	char *kvp_net_dir = "/sys/class/net/";
	char dev_id[256];

	dir = opendir(kvp_net_dir);
	if (dir == NULL)
		return NULL;

	snprintf(dev_id, sizeof(dev_id), "%s", kvp_net_dir);
	q = dev_id + strlen(kvp_net_dir);

	while ((entry = readdir(dir)) != NULL) {
		/*
		 * Set the state for the next pass.
		 */
		*q = '\0';
		strcat(dev_id, entry->d_name);
		strcat(dev_id, "/device/device_id");

		file = fopen(dev_id, "r");
		if (file == NULL)
			continue;

		p = fgets(buf, sizeof(buf), file);
		if (p) {
			x = strchr(p, '\n');
			if (x)
				*x = '\0';

			if (!strcmp(p, guid)) {
				/*
				 * Found the guid match; return the interface
				 * name. The caller will free the memory.
				 */
				if_name = strdup(entry->d_name);
				fclose(file);
				break;
			}
		}
		fclose(file);
	}

	closedir(dir);
	return if_name;
}

/*
 * Retrieve the MAC address given the interface name.
 */

static char *kvp_if_name_to_mac(char *if_name)
{
	FILE    *file;
	char    *p, *x;
	char    buf[256];
	char addr_file[256];
	int i;
	char *mac_addr = NULL;

	snprintf(addr_file, sizeof(addr_file), "%s%s%s", "/sys/class/net/",
		if_name, "/address");

	file = fopen(addr_file, "r");
	if (file == NULL)
		return NULL;

	p = fgets(buf, sizeof(buf), file);
	if (p) {
		x = strchr(p, '\n');
		if (x)
			*x = '\0';
		for (i = 0; i < strlen(p); i++)
			p[i] = toupper(p[i]);
		mac_addr = strdup(p);
	}

	fclose(file);
	return mac_addr;
}


/*
 * Retrieve the interface name given tha MAC address.
 */

static char *kvp_mac_to_if_name(char *mac)
{
	DIR *dir;
	struct dirent *entry;
	FILE    *file;
	char    *p, *q, *x;
	char    *if_name = NULL;
	char    buf[256];
	char *kvp_net_dir = "/sys/class/net/";
	char dev_id[256];
	int i;

	dir = opendir(kvp_net_dir);
	if (dir == NULL)
		return NULL;

	snprintf(dev_id, sizeof(dev_id), kvp_net_dir);
	q = dev_id + strlen(kvp_net_dir);

	while ((entry = readdir(dir)) != NULL) {
		/*
		 * Set the state for the next pass.
		 */
		*q = '\0';

		strcat(dev_id, entry->d_name);
		strcat(dev_id, "/address");

		file = fopen(dev_id, "r");
		if (file == NULL)
			continue;

		p = fgets(buf, sizeof(buf), file);
		if (p) {
			x = strchr(p, '\n');
			if (x)
				*x = '\0';

			for (i = 0; i < strlen(p); i++)
				p[i] = toupper(p[i]);

			if (!strcmp(p, mac)) {
				/*
				 * Found the MAC match; return the interface
				 * name. The caller will free the memory.
				 */
				if_name = strdup(entry->d_name);
				fclose(file);
				break;
			}
		}
		fclose(file);
	}

	closedir(dir);
	return if_name;
}


static void kvp_process_ipconfig_file(char *cmd,
					char *config_buf, int len,
					int element_size, int offset)
{
	char buf[256];
	char *p;
	char *x;
	FILE *file;

	/*
	 * First execute the command.
	 */
	file = popen(cmd, "r");
	if (file == NULL)
		return;

	if (offset == 0)
		memset(config_buf, 0, len);
	while ((p = fgets(buf, sizeof(buf), file)) != NULL) {
		if ((len - strlen(config_buf)) < (element_size + 1))
			break;

		x = strchr(p, '\n');
		*x = '\0';
		strcat(config_buf, p);
		strcat(config_buf, ";");
	}
	pclose(file);
}

static void kvp_get_ipconfig_info(char *if_name,
				 struct hv_kvp_ipaddr_value *buffer)
{
	char cmd[512];
	char dhcp_info[128];
	char *p;
	FILE *file;

	/*
	 * Get the address of default gateway (ipv4).
	 */
	sprintf(cmd, "%s %s", "ip route show dev", if_name);
	strcat(cmd, " | awk '/default/ {print $3 }'");

	/*
	 * Execute the command to gather gateway info.
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->gate_way,
				(MAX_GATEWAY_SIZE * 2), INET_ADDRSTRLEN, 0);

	/*
	 * Get the address of default gateway (ipv6).
	 */
	sprintf(cmd, "%s %s", "ip -f inet6  route show dev", if_name);
	strcat(cmd, " | awk '/default/ {print $3 }'");

	/*
	 * Execute the command to gather gateway info (ipv6).
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->gate_way,
				(MAX_GATEWAY_SIZE * 2), INET6_ADDRSTRLEN, 1);


	/*
	 * Gather the DNS  state.
	 * Since there is no standard way to get this information
	 * across various distributions of interest; we just invoke
	 * an external script that needs to be ported across distros
	 * of interest.
	 *
	 * Following is the expected format of the information from the script:
	 *
	 * ipaddr1 (nameserver1)
	 * ipaddr2 (nameserver2)
	 * .
	 * .
	 */

	sprintf(cmd, "%s",  "hv_get_dns_info");

	/*
	 * Execute the command to gather DNS info.
	 */
	kvp_process_ipconfig_file(cmd, (char *)buffer->dns_addr,
				(MAX_IP_ADDR_SIZE * 2), INET_ADDRSTRLEN, 0);

	/*
	 * Gather the DHCP state.
	 * We will gather this state by invoking an external script.
	 * The parameter to the script is the interface name.
	 * Here is the expected output:
	 *
	 * Enabled: DHCP enabled.
	 */

	sprintf(cmd, "%s %s", "hv_get_dhcp_info", if_name);

	file = popen(cmd, "r");
	if (file == NULL)
		return;

	p = fgets(dhcp_info, sizeof(dhcp_info), file);
	if (p == NULL) {
		pclose(file);
		return;
	}

	if (!strncmp(p, "Enabled", 7))
		buffer->dhcp_enabled = 1;
	else
		buffer->dhcp_enabled = 0;

	pclose(file);
}


static unsigned int hweight32(unsigned int *w)
{
	unsigned int res = *w - ((*w >> 1) & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
	res = (res + (res >> 4)) & 0x0F0F0F0F;
	res = res + (res >> 8);
	return (res + (res >> 16)) & 0x000000FF;
}

static int kvp_process_ip_address(void *addrp,
				int family, char *buffer,
				int length,  int *offset)
{
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	int addr_length;
	char tmp[50];
	const char *str;

	if (family == AF_INET) {
		addr = (struct sockaddr_in *)addrp;
		str = inet_ntop(family, &addr->sin_addr, tmp, 50);
		addr_length = INET_ADDRSTRLEN;
	} else {
		addr6 = (struct sockaddr_in6 *)addrp;
		str = inet_ntop(family, &addr6->sin6_addr.s6_addr, tmp, 50);
		addr_length = INET6_ADDRSTRLEN;
	}

	if ((length - *offset) < addr_length + 2)
		return HV_E_FAIL;
	if (str == NULL) {
		strcpy(buffer, "inet_ntop failed\n");
		return HV_E_FAIL;
	}
	if (*offset == 0)
		strcpy(buffer, tmp);
	else {
		strcat(buffer, ";");
		strcat(buffer, tmp);
	}

	*offset += strlen(str) + 1;

	return 0;
}

static int
kvp_get_ip_info(int family, char *if_name, int op,
		 void  *out_buffer, int length)
{
	struct ifaddrs *ifap;
	struct ifaddrs *curp;
	int offset = 0;
	int sn_offset = 0;
	int error = 0;
	char *buffer;
	struct hv_kvp_ipaddr_value *ip_buffer;
	char cidr_mask[5]; /* /xyz */
	int weight;
	int i;
	unsigned int *w;
	char *sn_str;
	struct sockaddr_in6 *addr6;

	if (op == KVP_OP_ENUMERATE) {
		buffer = out_buffer;
	} else {
		ip_buffer = out_buffer;
		buffer = (char *)ip_buffer->ip_addr;
		ip_buffer->addr_family = 0;
	}
	/*
	 * On entry into this function, the buffer is capable of holding the
	 * maximum key value.
	 */

	if (getifaddrs(&ifap)) {
		strcpy(buffer, "getifaddrs failed\n");
		return HV_E_FAIL;
	}

	curp = ifap;
	while (curp != NULL) {
		if (curp->ifa_addr == NULL) {
			curp = curp->ifa_next;
			continue;
		}

		if ((if_name != NULL) &&
			(strncmp(curp->ifa_name, if_name, strlen(if_name)))) {
			/*
			 * We want info about a specific interface;
			 * just continue.
			 */
			curp = curp->ifa_next;
			continue;
		}

		/*
		 * We only support two address families: AF_INET and AF_INET6.
		 * If a family value of 0 is specified, we collect both
		 * supported address families; if not we gather info on
		 * the specified address family.
		 */
		if ((((family != 0) &&
			 (curp->ifa_addr->sa_family != family))) ||
			 (curp->ifa_flags & IFF_LOOPBACK)) {
			curp = curp->ifa_next;
			continue;
		}
		if ((curp->ifa_addr->sa_family != AF_INET) &&
			(curp->ifa_addr->sa_family != AF_INET6)) {
			curp = curp->ifa_next;
			continue;
		}

		if (op == KVP_OP_GET_IP_INFO) {
			/*
			 * Gather info other than the IP address.
			 * IP address info will be gathered later.
			 */
			if (curp->ifa_addr->sa_family == AF_INET) {
				ip_buffer->addr_family |= ADDR_FAMILY_IPV4;
				/*
				 * Get subnet info.
				 */
				error = kvp_process_ip_address(
							     curp->ifa_netmask,
							     AF_INET,
							     (char *)
							     ip_buffer->sub_net,
							     length,
							     &sn_offset);
				if (error)
					goto gather_ipaddr;
			} else {
				ip_buffer->addr_family |= ADDR_FAMILY_IPV6;

				/*
				 * Get subnet info in CIDR format.
				 */
				weight = 0;
				sn_str = (char *)ip_buffer->sub_net;
				addr6 = (struct sockaddr_in6 *)
					curp->ifa_netmask;
				w = addr6->sin6_addr.s6_addr32;

				for (i = 0; i < 4; i++)
					weight += hweight32(&w[i]);

				sprintf(cidr_mask, "/%d", weight);
				if ((length - sn_offset) <
					(strlen(cidr_mask) + 1))
					goto gather_ipaddr;

				if (sn_offset == 0)
					strcpy(sn_str, cidr_mask);
				else
					strcat(sn_str, cidr_mask);
				strcat((char *)ip_buffer->sub_net, ";");
				sn_offset += strlen(sn_str) + 1;
			}

			/*
			 * Collect other ip related configuration info.
			 */

			kvp_get_ipconfig_info(if_name, ip_buffer);
		}

gather_ipaddr:
		error = kvp_process_ip_address(curp->ifa_addr,
						curp->ifa_addr->sa_family,
						buffer,
						length, &offset);
		if (error)
			goto getaddr_done;

		curp = curp->ifa_next;
	}

getaddr_done:
	freeifaddrs(ifap);
	return error;
}


static int expand_ipv6(char *addr, int type)
{
	int ret;
	struct in6_addr v6_addr;

	ret = inet_pton(AF_INET6, addr, &v6_addr);

	if (ret != 1) {
		if (type == NETMASK)
			return 1;
		return 0;
	}

	sprintf(addr, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
		"%02x%02x:%02x%02x:%02x%02x",
		(int)v6_addr.s6_addr[0], (int)v6_addr.s6_addr[1],
		(int)v6_addr.s6_addr[2], (int)v6_addr.s6_addr[3],
		(int)v6_addr.s6_addr[4], (int)v6_addr.s6_addr[5],
		(int)v6_addr.s6_addr[6], (int)v6_addr.s6_addr[7],
		(int)v6_addr.s6_addr[8], (int)v6_addr.s6_addr[9],
		(int)v6_addr.s6_addr[10], (int)v6_addr.s6_addr[11],
		(int)v6_addr.s6_addr[12], (int)v6_addr.s6_addr[13],
		(int)v6_addr.s6_addr[14], (int)v6_addr.s6_addr[15]);

	return 1;

}

static int is_ipv4(char *addr)
{
	int ret;
	struct in_addr ipv4_addr;

	ret = inet_pton(AF_INET, addr, &ipv4_addr);

	if (ret == 1)
		return 1;
	return 0;
}

static int parse_ip_val_buffer(char *in_buf, int *offset,
				char *out_buf, int out_len)
{
	char *x;
	char *start;

	/*
	 * in_buf has sequence of characters that are seperated by
	 * the character ';'. The last sequence does not have the
	 * terminating ";" character.
	 */
	start = in_buf + *offset;

	x = strchr(start, ';');
	if (x)
		*x = 0;
	else
		x = start + strlen(start);

	if (strlen(start) != 0) {
		int i = 0;
		/*
		 * Get rid of leading spaces.
		 */
		while (start[i] == ' ')
			i++;

		if ((x - start) <= out_len) {
			strcpy(out_buf, (start + i));
			*offset += (x - start) + 1;
			return 1;
		}
	}
	return 0;
}

static int kvp_write_file(FILE *f, char *s1, char *s2, char *s3)
{
	int ret;

	ret = fprintf(f, "%s%s%s%s\n", s1, s2, "=", s3);

	if (ret < 0)
		return HV_E_FAIL;

	return 0;
}


static int process_ip_string(FILE *f, char *ip_string, int type)
{
	int error = 0;
	char addr[INET6_ADDRSTRLEN];
	int i = 0;
	int j = 0;
	char str[256];
	char sub_str[10];
	int offset = 0;

	memset(addr, 0, sizeof(addr));

	while (parse_ip_val_buffer(ip_string, &offset, addr,
					(MAX_IP_ADDR_SIZE * 2))) {

		sub_str[0] = 0;
		if (is_ipv4(addr)) {
			switch (type) {
			case IPADDR:
				snprintf(str, sizeof(str), "%s", "IPADDR");
				break;
			case NETMASK:
				snprintf(str, sizeof(str), "%s", "NETMASK");
				break;
			case GATEWAY:
				snprintf(str, sizeof(str), "%s", "GATEWAY");
				break;
			case DNS:
				snprintf(str, sizeof(str), "%s", "DNS");
				break;
			}

			if (type == DNS) {
				snprintf(sub_str, sizeof(sub_str), "%d", ++i);
			} else if (type == GATEWAY && i == 0) {
				++i;
			} else {
				snprintf(sub_str, sizeof(sub_str), "%d", i++);
			}


		} else if (expand_ipv6(addr, type)) {
			switch (type) {
			case IPADDR:
				snprintf(str, sizeof(str), "%s", "IPV6ADDR");
				break;
			case NETMASK:
				snprintf(str, sizeof(str), "%s", "IPV6NETMASK");
				break;
			case GATEWAY:
				snprintf(str, sizeof(str), "%s",
					"IPV6_DEFAULTGW");
				break;
			case DNS:
				snprintf(str, sizeof(str), "%s",  "DNS");
				break;
			}

			if (type == DNS) {
				snprintf(sub_str, sizeof(sub_str), "%d", ++i);
			} else if (j == 0) {
				++j;
			} else {
				snprintf(sub_str, sizeof(sub_str), "_%d", j++);
			}
		} else {
			return  HV_INVALIDARG;
		}

		error = kvp_write_file(f, str, sub_str, addr);
		if (error)
			return error;
		memset(addr, 0, sizeof(addr));
	}

	return 0;
}

static int kvp_set_ip_info(char *if_name, struct hv_kvp_ipaddr_value *new_val)
{
	int error = 0;
	char if_file[128];
	FILE *file;
	char cmd[512];
	char *mac_addr;

	/*
	 * Set the configuration for the specified interface with
	 * the information provided. Since there is no standard
	 * way to configure an interface, we will have an external
	 * script that does the job of configuring the interface and
	 * flushing the configuration.
	 *
	 * The parameters passed to this external script are:
	 * 1. A configuration file that has the specified configuration.
	 *
	 * We will embed the name of the interface in the configuration
	 * file: ifcfg-ethx (where ethx is the interface name).
	 *
	 * The information provided here may be more than what is needed
	 * in a given distro to configure the interface and so are free
	 * ignore information that may not be relevant.
	 *
	 * Here is the format of the ip configuration file:
	 *
	 * HWADDR=macaddr
	 * DEVICE=interface name
	 * BOOTPROTO=<protocol> (where <protocol> is "dhcp" if DHCP is configured
	 *                       or "none" if no boot-time protocol should be used)
	 *
	 * IPADDR0=ipaddr1
	 * IPADDR1=ipaddr2
	 * IPADDRx=ipaddry (where y = x + 1)
	 *
	 * NETMASK0=netmask1
	 * NETMASKx=netmasky (where y = x + 1)
	 *
	 * GATEWAY=ipaddr1
	 * GATEWAYx=ipaddry (where y = x + 1)
	 *
	 * DNSx=ipaddrx (where first DNS address is tagged as DNS1 etc)
	 *
	 * IPV6 addresses will be tagged as IPV6ADDR, IPV6 gateway will be
	 * tagged as IPV6_DEFAULTGW and IPV6 NETMASK will be tagged as
	 * IPV6NETMASK.
	 *
	 * The host can specify multiple ipv4 and ipv6 addresses to be
	 * configured for the interface. Furthermore, the configuration
	 * needs to be persistent. A subsequent GET call on the interface
	 * is expected to return the configuration that is set via the SET
	 * call.
	 */

	snprintf(if_file, sizeof(if_file), "%s%s%s", KVP_CONFIG_LOC,
		"/ifcfg-", if_name);

	file = fopen(if_file, "w");

	if (file == NULL) {
		syslog(LOG_ERR, "Failed to open config file");
		return HV_E_FAIL;
	}

	/*
	 * First write out the MAC address.
	 */

	mac_addr = kvp_if_name_to_mac(if_name);
	if (mac_addr == NULL) {
		error = HV_E_FAIL;
		goto setval_error;
	}

	error = kvp_write_file(file, "HWADDR", "", mac_addr);
	if (error)
		goto setval_error;

	error = kvp_write_file(file, "DEVICE", "", if_name);
	if (error)
		goto setval_error;

	if (new_val->dhcp_enabled) {
		error = kvp_write_file(file, "BOOTPROTO", "", "dhcp");
		if (error)
			goto setval_error;

		/*
		 * We are done!.
		 */
		goto setval_done;

	} else {
		error = kvp_write_file(file, "BOOTPROTO", "", "none");
		if (error)
			goto setval_error;
	}

	/*
	 * Write the configuration for ipaddress, netmask, gateway and
	 * name servers.
	 */

	error = process_ip_string(file, (char *)new_val->ip_addr, IPADDR);
	if (error)
		goto setval_error;

	error = process_ip_string(file, (char *)new_val->sub_net, NETMASK);
	if (error)
		goto setval_error;

	error = process_ip_string(file, (char *)new_val->gate_way, GATEWAY);
	if (error)
		goto setval_error;

	error = process_ip_string(file, (char *)new_val->dns_addr, DNS);
	if (error)
		goto setval_error;

setval_done:
	free(mac_addr);
	fclose(file);

	/*
	 * Now that we have populated the configuration file,
	 * invoke the external script to do its magic.
	 */

	snprintf(cmd, sizeof(cmd), "%s %s", "hv_set_ifconfig", if_file);
	system(cmd);
	return 0;

setval_error:
	syslog(LOG_ERR, "Failed to write config file");
	free(mac_addr);
	fclose(file);
	return error;
}


static int
kvp_get_domain_name(char *buffer, int length)
{
	struct addrinfo	hints, *info ;
	int error = 0;

	gethostname(buffer, length);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; /*Get only ipv4 addrinfo. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	error = getaddrinfo(buffer, NULL, &hints, &info);
	if (error != 0) {
		strcpy(buffer, "getaddrinfo failed\n");
		return error;
	}
	strcpy(buffer, info->ai_canonname);
	freeaddrinfo(info);
	return error;
}

static int
netlink_send(int fd, struct cn_msg *msg)
{
	struct nlmsghdr *nlh;
	unsigned int size;
	struct msghdr message;
	char buffer[64];
	struct iovec iov[2];

	size = NLMSG_SPACE(sizeof(struct cn_msg) + msg->len);

	nlh = (struct nlmsghdr *)buffer;
	nlh->nlmsg_seq = 0;
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_len = NLMSG_LENGTH(size - sizeof(*nlh));
	nlh->nlmsg_flags = 0;

	iov[0].iov_base = nlh;
	iov[0].iov_len = sizeof(*nlh);

	iov[1].iov_base = msg;
	iov[1].iov_len = size;

	memset(&message, 0, sizeof(message));
	message.msg_name = &addr;
	message.msg_namelen = sizeof(addr);
	message.msg_iov = iov;
	message.msg_iovlen = 2;

	return sendmsg(fd, &message, 0);
}

int main(void)
{
	int fd, len, sock_opt;
	int error;
	struct cn_msg *message;
	struct pollfd pfd;
	struct nlmsghdr *incoming_msg;
	struct cn_msg	*incoming_cn_msg;
	struct hv_kvp_msg *hv_msg;
	char	*p;
	char	*key_value;
	char	*key_name;
	int	op;
	int	pool;
	char	*if_name;
	struct hv_kvp_ipaddr_value *kvp_ip_val;

	daemon(1, 0);
	openlog("KVP", 0, LOG_USER);
	syslog(LOG_INFO, "KVP starting; pid is:%d", getpid());
	/*
	 * Retrieve OS release information.
	 */
	kvp_get_os_info();

	if (kvp_file_init()) {
		syslog(LOG_ERR, "Failed to initialize the pools");
		exit(EXIT_FAILURE);
	}

	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (fd < 0) {
		syslog(LOG_ERR, "netlink socket creation failed; error:%d", fd);
		exit(EXIT_FAILURE);
	}
	addr.nl_family = AF_NETLINK;
	addr.nl_pad = 0;
	addr.nl_pid = 0;
	addr.nl_groups = CN_KVP_IDX;


	error = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (error < 0) {
		syslog(LOG_ERR, "bind failed; error:%d", error);
		close(fd);
		exit(EXIT_FAILURE);
	}
	sock_opt = addr.nl_groups;
	setsockopt(fd, 270, 1, &sock_opt, sizeof(sock_opt));
	/*
	 * Register ourselves with the kernel.
	 */
	message = (struct cn_msg *)kvp_send_buffer;
	message->id.idx = CN_KVP_IDX;
	message->id.val = CN_KVP_VAL;

	hv_msg = (struct hv_kvp_msg *)message->data;
	hv_msg->kvp_hdr.operation = KVP_OP_REGISTER1;
	message->ack = 0;
	message->len = sizeof(struct hv_kvp_msg);

	len = netlink_send(fd, message);
	if (len < 0) {
		syslog(LOG_ERR, "netlink_send failed; error:%d", len);
		close(fd);
		exit(EXIT_FAILURE);
	}

	pfd.fd = fd;

	while (1) {
		struct sockaddr *addr_p = (struct sockaddr *) &addr;
		socklen_t addr_l = sizeof(addr);
		pfd.events = POLLIN;
		pfd.revents = 0;
		poll(&pfd, 1, -1);

		len = recvfrom(fd, kvp_recv_buffer, sizeof(kvp_recv_buffer), 0,
				addr_p, &addr_l);

		if (len < 0) {
			syslog(LOG_ERR, "recvfrom failed; pid:%u error:%d %s",
					addr.nl_pid, errno, strerror(errno));
			close(fd);
			return -1;
		}

		if (addr.nl_pid) {
			syslog(LOG_WARNING, "Received packet from untrusted pid:%u",
					addr.nl_pid);
			continue;
		}

		incoming_msg = (struct nlmsghdr *)kvp_recv_buffer;
		incoming_cn_msg = (struct cn_msg *)NLMSG_DATA(incoming_msg);
		hv_msg = (struct hv_kvp_msg *)incoming_cn_msg->data;

		/*
		 * We will use the KVP header information to pass back
		 * the error from this daemon. So, first copy the state
		 * and set the error code to success.
		 */
		op = hv_msg->kvp_hdr.operation;
		pool = hv_msg->kvp_hdr.pool;
		hv_msg->error = HV_S_OK;

		if ((in_hand_shake) && (op == KVP_OP_REGISTER1)) {
			/*
			 * Driver is registering with us; stash away the version
			 * information.
			 */
			in_hand_shake = 0;
			p = (char *)hv_msg->body.kvp_register.version;
			lic_version = malloc(strlen(p) + 1);
			if (lic_version) {
				strcpy(lic_version, p);
				syslog(LOG_INFO, "KVP LIC Version: %s",
					lic_version);
			} else {
				syslog(LOG_ERR, "malloc failed");
			}
			continue;
		}

		switch (op) {
		case KVP_OP_GET_IP_INFO:
			kvp_ip_val = &hv_msg->body.kvp_ip_val;
			if_name =
			kvp_mac_to_if_name((char *)kvp_ip_val->adapter_id);

			if (if_name == NULL) {
				/*
				 * We could not map the mac address to an
				 * interface name; return error.
				 */
				hv_msg->error = HV_E_FAIL;
				break;
			}
			error = kvp_get_ip_info(
						0, if_name, KVP_OP_GET_IP_INFO,
						kvp_ip_val,
						(MAX_IP_ADDR_SIZE * 2));

			if (error)
				hv_msg->error = error;

			free(if_name);
			break;

		case KVP_OP_SET_IP_INFO:
			kvp_ip_val = &hv_msg->body.kvp_ip_val;
			if_name = kvp_get_if_name(
					(char *)kvp_ip_val->adapter_id);
			if (if_name == NULL) {
				/*
				 * We could not map the guid to an
				 * interface name; return error.
				 */
				hv_msg->error = HV_GUID_NOTFOUND;
				break;
			}
			error = kvp_set_ip_info(if_name, kvp_ip_val);
			if (error)
				hv_msg->error = error;

			free(if_name);
			break;

		case KVP_OP_SET:
			if (kvp_key_add_or_modify(pool,
					hv_msg->body.kvp_set.data.key,
					hv_msg->body.kvp_set.data.key_size,
					hv_msg->body.kvp_set.data.value,
					hv_msg->body.kvp_set.data.value_size))
					hv_msg->error = HV_S_CONT;
			break;

		case KVP_OP_GET:
			if (kvp_get_value(pool,
					hv_msg->body.kvp_set.data.key,
					hv_msg->body.kvp_set.data.key_size,
					hv_msg->body.kvp_set.data.value,
					hv_msg->body.kvp_set.data.value_size))
					hv_msg->error = HV_S_CONT;
			break;

		case KVP_OP_DELETE:
			if (kvp_key_delete(pool,
					hv_msg->body.kvp_delete.key,
					hv_msg->body.kvp_delete.key_size))
					hv_msg->error = HV_S_CONT;
			break;

		default:
			break;
		}

		if (op != KVP_OP_ENUMERATE)
			goto kvp_done;

		/*
		 * If the pool is KVP_POOL_AUTO, dynamically generate
		 * both the key and the value; if not read from the
		 * appropriate pool.
		 */
		if (pool != KVP_POOL_AUTO) {
			if (kvp_pool_enumerate(pool,
					hv_msg->body.kvp_enum_data.index,
					hv_msg->body.kvp_enum_data.data.key,
					HV_KVP_EXCHANGE_MAX_KEY_SIZE,
					hv_msg->body.kvp_enum_data.data.value,
					HV_KVP_EXCHANGE_MAX_VALUE_SIZE))
					hv_msg->error = HV_S_CONT;
			goto kvp_done;
		}

		hv_msg = (struct hv_kvp_msg *)incoming_cn_msg->data;
		key_name = (char *)hv_msg->body.kvp_enum_data.data.key;
		key_value = (char *)hv_msg->body.kvp_enum_data.data.value;

		switch (hv_msg->body.kvp_enum_data.index) {
		case FullyQualifiedDomainName:
			kvp_get_domain_name(key_value,
					HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
			strcpy(key_name, "FullyQualifiedDomainName");
			break;
		case IntegrationServicesVersion:
			strcpy(key_name, "IntegrationServicesVersion");
			strcpy(key_value, lic_version);
			break;
		case NetworkAddressIPv4:
			kvp_get_ip_info(AF_INET, NULL, KVP_OP_ENUMERATE,
				key_value, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
			strcpy(key_name, "NetworkAddressIPv4");
			break;
		case NetworkAddressIPv6:
			kvp_get_ip_info(AF_INET6, NULL, KVP_OP_ENUMERATE,
				key_value, HV_KVP_EXCHANGE_MAX_VALUE_SIZE);
			strcpy(key_name, "NetworkAddressIPv6");
			break;
		case OSBuildNumber:
			strcpy(key_value, os_build);
			strcpy(key_name, "OSBuildNumber");
			break;
		case OSName:
			strcpy(key_value, os_name);
			strcpy(key_name, "OSName");
			break;
		case OSMajorVersion:
			strcpy(key_value, os_major);
			strcpy(key_name, "OSMajorVersion");
			break;
		case OSMinorVersion:
			strcpy(key_value, os_minor);
			strcpy(key_name, "OSMinorVersion");
			break;
		case OSVersion:
			strcpy(key_value, os_version);
			strcpy(key_name, "OSVersion");
			break;
		case ProcessorArchitecture:
			strcpy(key_value, processor_arch);
			strcpy(key_name, "ProcessorArchitecture");
			break;
		default:
			hv_msg->error = HV_S_CONT;
			break;
		}
		/*
		 * Send the value back to the kernel. The response is
		 * already in the receive buffer. Update the cn_msg header to
		 * reflect the key value that has been added to the message
		 */
kvp_done:

		incoming_cn_msg->id.idx = CN_KVP_IDX;
		incoming_cn_msg->id.val = CN_KVP_VAL;
		incoming_cn_msg->ack = 0;
		incoming_cn_msg->len = sizeof(struct hv_kvp_msg);

		len = netlink_send(fd, incoming_cn_msg);
		if (len < 0) {
			syslog(LOG_ERR, "net_link send failed; error:%d", len);
			exit(EXIT_FAILURE);
		}
	}

}
