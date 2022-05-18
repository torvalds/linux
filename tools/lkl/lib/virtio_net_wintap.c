// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual network interface feature based on tap-windows (openvpn)
 * Copyright (c) 2021 Hajime Tazaki
 *
 * Author: Hajime Tazaki <thehajime@gmail.com>
 *
 */
#include <Windows.h>
#include <Winreg.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdint.h>

#include "virtio.h"

//#define DEBUG
#ifdef DEBUG
#define tap_debug_printf lkl_printf
#else
#define tap_debug_printf(fmt, ...) do {} while (0)
#endif

/*
 * =============
 * TAP IOCTLs
 * =============
 */

#define TAP_WIN_CONTROL_CODE(request, method) \
		CTL_CODE(FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

/* Present in 8.1 */
#define TAP_WIN_IOCTL_GET_MAC               TAP_WIN_CONTROL_CODE(1, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_GET_VERSION           TAP_WIN_CONTROL_CODE(2, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_GET_MTU               TAP_WIN_CONTROL_CODE(3, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_GET_INFO              TAP_WIN_CONTROL_CODE(4, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_CONFIG_POINT_TO_POINT TAP_WIN_CONTROL_CODE(5, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_SET_MEDIA_STATUS      TAP_WIN_CONTROL_CODE(6, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_CONFIG_DHCP_MASQ      TAP_WIN_CONTROL_CODE(7, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_GET_LOG_LINE          TAP_WIN_CONTROL_CODE(8, METHOD_BUFFERED)
#define TAP_WIN_IOCTL_CONFIG_DHCP_SET_OPT   TAP_WIN_CONTROL_CODE(9, METHOD_BUFFERED)
/* Added in 8.2 */
/* obsoletes TAP_WIN_IOCTL_CONFIG_POINT_TO_POINT */
#define TAP_WIN_IOCTL_CONFIG_TUN            TAP_WIN_CONTROL_CODE(10, METHOD_BUFFERED)

/*
 * =================
 * Registry keys
 * =================
 */

#define ADAPTER_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define NETWORK_CONNECTIONS_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

/*
 * ======================
 * Filesystem prefixes
 * ======================
 */

#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define SYSDEVICEDIR      "\\Device\\"
#define USERDEVICEDIR     "\\DosDevices\\Global\\"
#define TAP_WIN_SUFFIX    ".tap"

/*
 * Tunnel types
 */
#define DEV_TYPE_UNDEF 0
#define DEV_TYPE_NULL  1
#define DEV_TYPE_TUN   2    /* point-to-point IP tunnel */
#define DEV_TYPE_TAP   3    /* ethernet (802.3) tunnel */

//#define TAP_WIN_COMPONENT_ID  "tap_ovpnconnect"
#define TAP_WIN_COMPONENT_ID  "root\\tap0901"
#define TAP_WIN_MIN_MAJOR     9
#define TAP_WIN_MIN_MINOR     9

/* Extra structs */
struct tap_reg {
	const char *guid;
	struct tap_reg *next;
};

struct panel_reg {
	const char *name;
	const char *guid;
	struct panel_reg *next;
};

/*
 * We try to do all Win32 I/O using overlapped
 * (i.e. asynchronous) I/O for a performance win.
 */
struct overlapped_io {
# define IOSTATE_INITIAL          0
# define IOSTATE_QUEUED           1 /* overlapped I/O has been queued */
# define IOSTATE_IMMEDIATE_RETURN 2 /* I/O function returned immediately without queueing */
	int iostate;
	OVERLAPPED overlapped;
	DWORD size;
	DWORD flags;
	int status;
	int addr_defined;
	uint8_t *buf_init;
	uint32_t buf_init_len;
	uint8_t *buf;
	uint32_t buf_len;
};

struct tuntap {
	int type; /* DEV_TYPE_x as defined in proto.h */
	int ipv6;
	int persistent_if;      /* if existed before, keep on program end */
	char *actual_name; /* actual name of TUN/TAP dev, usually including unit number */
	int post_open_mtu;
	uint8_t mac[6];

	/* Windows stuff */
	DWORD adapter_index; /*adapter index for TAP-Windows adapter, ~0 if undefined */
	HANDLE hand;
	struct overlapped_io reads; /* for overlapped IO */
	struct overlapped_io writes;

};

struct lkl_netdev_wintap {
	struct lkl_netdev dev;

	int statistics_frames_out;
	struct tuntap *tt;
};

/*
 * Private functions
 */

/* Format lasterror */
static wchar_t *GetErrorMessage(void)
{
	static wchar_t buf[256];

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		      NULL,  /* (not used with FORMAT_MESSAGE_FROM_SYSTEM) */
		      GetLastError(),
		      MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		      (LPTSTR)buf,
		      (sizeof(buf) / sizeof(wchar_t)),
		      NULL);
	return buf;
}

/* Get TAP info from Windows registry */
static const struct tap_reg *get_tap_reg(void)
{
	HKEY adapter_key;
	LONG status;
	DWORD len;
	struct tap_reg *first = NULL;
	struct tap_reg *last = NULL;
	int i = 0;

	status = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,
		ADAPTER_KEY,
		0,
		KEY_READ,
		&adapter_key);

	if (status != ERROR_SUCCESS) {
		lkl_printf("Error opening registry key 0: %s\n", ADAPTER_KEY);
		return NULL;
	}

	while (1) {
		char enum_name[64];
		char unit_string[256];
		HKEY unit_key;
		char component_id_string[] = "ComponentId";
		char component_id[256];
		char net_cfg_instance_id_string[] = "NetCfgInstanceId";
		char net_cfg_instance_id[256];
		DWORD data_type;

		len = sizeof(enum_name);
		status = RegEnumKeyEx(
			adapter_key,
			i,
			enum_name,
			&len,
			NULL,
			NULL,
			NULL,
			NULL);
		if (status == ERROR_NO_MORE_ITEMS)
			break;
		else if (status != ERROR_SUCCESS)
			lkl_printf("Error enumerating registry subkeys of key: %s.\n",
				   ADAPTER_KEY);
		snprintf(unit_string, sizeof(unit_string), "%s\\%s",
			  ADAPTER_KEY, enum_name);

		status = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			unit_string,
			0,
			KEY_READ,
			&unit_key);

		if (status != ERROR_SUCCESS) {
			lkl_printf("Error opening registry key 1: %s\n",
				   unit_string);
			++i;
			continue;
		}

		len = sizeof(component_id);
		status = RegQueryValueEx(
			unit_key,
			component_id_string,
			NULL,
			&data_type,
			(LPBYTE)component_id,
			&len);

		if (status != ERROR_SUCCESS || data_type != REG_SZ) {
			lkl_printf("Error opening registry key 2: %s\\%s\n",
				   unit_string, component_id_string);
			++i;
			continue;
		}

		len = sizeof(net_cfg_instance_id);
		status = RegQueryValueEx(
			unit_key,
			net_cfg_instance_id_string,
			NULL,
			&data_type,
			(LPBYTE)net_cfg_instance_id,
			&len);

		if (status == ERROR_SUCCESS && data_type == REG_SZ) {
			if (!strcmp(component_id, TAP_WIN_COMPONENT_ID)) {
				struct tap_reg *reg;

				reg = calloc(sizeof(struct tap_reg), 1);
				/* ALLOC_OBJ_CLEAR_GC (reg, struct tap_reg, gc); */
				if (!reg)
					return NULL;

				/* reg->guid = string_alloc (net_cfg_instance_id, gc); */
				reg->guid = calloc(strlen(net_cfg_instance_id) + 1, 1);
				if (!(reg->guid)) {
					free(reg);
					return NULL;
				}

				strcpy((char *)reg->guid, net_cfg_instance_id);
				/* link into return list */
				if (!first)
					first = reg;

				if (last)
					last->next = reg;

				last = reg;
			}
		}

		RegCloseKey(unit_key);
		++i;
	}
	RegCloseKey(adapter_key);
	return first;
}

/* Get Panel info from Windows registry */
const struct panel_reg *get_panel_reg(void)
{
	LONG status;
	HKEY network_connections_key;
	DWORD len;
	struct panel_reg *first = NULL;
	struct panel_reg *last = NULL;
	int i = 0;

	status = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,
		NETWORK_CONNECTIONS_KEY,
		0,
		KEY_READ,
		&network_connections_key);

	if (status != ERROR_SUCCESS) {
		lkl_printf("Error opening registry key 3: %s\n",
			   NETWORK_CONNECTIONS_KEY);
		return NULL;
	}

	while (1) {
		char enum_name[256];
		char connection_string[1024];
		HKEY connection_key;
		WCHAR name_data[256];
		DWORD name_type;

		const WCHAR name_string[] = L"Name";

		len = sizeof(enum_name);
		status = RegEnumKeyEx(
			network_connections_key,
			i,
			enum_name,
			&len,
			NULL,
			NULL,
			NULL,
			NULL);

		if (status == ERROR_NO_MORE_ITEMS)
			break;
		else if (status != ERROR_SUCCESS)
			lkl_printf("Error enumerating registry subkeys of key: %s.\n",
				   NETWORK_CONNECTIONS_KEY);

		snprintf(connection_string, sizeof(connection_string),
			  "%s\\%s\\Connection", NETWORK_CONNECTIONS_KEY, enum_name);

		status = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			connection_string,
			0,
			KEY_READ,
			&connection_key);

		if (status != ERROR_SUCCESS)
			lkl_printf("Error opening registry key 4: %s\n", connection_string);

		len = sizeof(name_data);
		status = RegQueryValueExW(
			connection_key,
			name_string,
			NULL,
			&name_type,
			(LPBYTE) name_data,
			&len);

		if (status != ERROR_SUCCESS || name_type != REG_SZ) {
			lkl_printf("Error opening registry key 5: %s\\%s\\%S\n",
				   NETWORK_CONNECTIONS_KEY, connection_string, name_string);
			++i;
			continue;
		}

		int n;
		LPSTR name;
		struct panel_reg *reg;

		/* ALLOC_OBJ_CLEAR_GC (reg, struct panel_reg, gc); */
		reg = calloc(sizeof(struct panel_reg), 1);
		if (!reg)
			return NULL;

		n = WideCharToMultiByte(CP_UTF8, 0, name_data, -1, NULL, 0, NULL, NULL);
		/* name = gc_malloc (n, false, gc); */
		name = calloc(n, 1);
		if (!name) {
			free(reg);
			return NULL;
		}

		WideCharToMultiByte(CP_UTF8, 0, name_data, -1, name, n, NULL, NULL);
		reg->name = name;
		/* reg->guid = string_alloc (enum_name, gc); */
		reg->guid = calloc(strlen(enum_name) + 1, 1);
		if (!reg->guid) {
			free((void *)reg->name);
			free((void *)reg);
			return NULL;
		}

		strcpy((char *)reg->guid, enum_name);

		/* link into return list */
		if (!first)
			first = reg;

		if (last)
			last->next = reg;

		last = reg;

		RegCloseKey(connection_key);
		++i;
	}
	RegCloseKey(network_connections_key);

	return first;
}


void show_tap_win_adapters(void)
{
	int warn_panel_null = 0;
	int warn_panel_dup = 0;
	int warn_tap_dup = 0;

	int links;

	const struct tap_reg *tr;
	const struct tap_reg *tr1;
	const struct panel_reg *pr;

	const struct tap_reg *tap_reg = get_tap_reg();
	const struct panel_reg *panel_reg = get_panel_reg();

	if (!(tap_reg && panel_reg))
		return;

	tap_debug_printf("Available TAP-WIN32 adapters [name, GUID]:\n");

	/* loop through each TAP-Windows adapter registry entry */
	for (tr = tap_reg; tr != NULL; tr = tr->next) {
		links = 0;

		/* loop through each network connections entry in the control panel */
		for (pr = panel_reg; pr != NULL; pr = pr->next) {
			if (!strcmp(tr->guid, pr->guid)) {
				tap_debug_printf("\t>> '%s' %s\n", pr->name, tr->guid);
				++links;
			}
		}
		if (links > 1)
			warn_panel_dup = 1;
		else if (links == 0) {
			/* a TAP adapter exists without a link from the network
			 * connections control panel
			 */
			warn_panel_null = 1;
			tap_debug_printf("\t>> [NULL] %s\n", tr->guid);
		}
	}

	/* check for TAP-Windows adapter duplicated GUIDs */
	for (tr = tap_reg; tr != NULL; tr = tr->next)
		for (tr1 = tap_reg; tr1 != NULL; tr1 = tr1->next)
			if (tr != tr1 && !strcmp(tr->guid, tr1->guid))
				warn_tap_dup = 1;

	/* warn on registry inconsistencies */
	if (warn_tap_dup)
		tap_debug_printf("WARNING: Some TAP-Windows adapters have duplicate GUIDs\n");

	if (warn_panel_dup)
		tap_debug_printf("WARNING: Some TAP-Windows adapters have duplicates\n");

	if (warn_panel_null)
		tap_debug_printf("WARNING: Some TAP-Windows adapters have no link\n");
}

/* Get the GUID of the first TAP device found */
static const char *get_first_device_guid(const struct tap_reg *tap_reg,
					 const struct panel_reg *panel_reg,
					 char *name)
{
	const struct tap_reg *tr;
	const struct panel_reg *pr;
	/* loop through each TAP-Windows adapter registry entry */
	for (tr = tap_reg; tr != NULL; tr = tr->next) {
		/**
		 * loop through each network connections entry in the control
		 * panel
		 */
		for (pr = panel_reg; pr != NULL; pr = pr->next) {
			if (!strcmp(tr->guid, pr->guid)) {
				tap_debug_printf("Using first TAP device: '%s' %s\n",
					   pr->name, tr->guid);
				if (name)
					strcpy(name, pr->name);

				return tr->guid;
			}
		}
	}
	return NULL;
}

static int open_tun(const char *dev_node, struct lkl_netdev_wintap *nd)
{
	char device_path[256];
	const char *device_guid = NULL;
	DWORD len;

	tap_debug_printf("%s, nd->ipv6=%d\n", __func__, nd->tt->ipv6);
	/*
	 * Lookup the device name in the registry, using the --dev-node
	 * high level name.
	 */
	{
		const struct tap_reg *tap_reg = get_tap_reg();
		const struct panel_reg *panel_reg = get_panel_reg();
		char name[256];

		if (!(tap_reg && panel_reg)) {
			lkl_printf("No TUN/TAP devices found\n");
			return -1;
		}

		/* Get the device GUID for the device specified with --dev-node. */
		device_guid = get_first_device_guid(tap_reg, panel_reg, name);

		if (!device_guid) {
			lkl_printf("TAP-Windows adapter '%s' not found\n",
				   dev_node);
			return -1;
		}

		/* Open Windows TAP-Windows adapter */
		snprintf(device_path, sizeof(device_path), "%s%s%s",
			  USERMODEDEVICEDIR,
			  device_guid,
			  TAP_WIN_SUFFIX);

		nd->tt->hand = CreateFile(
			device_path,
			GENERIC_READ | GENERIC_WRITE,
			0,     /* was: FILE_SHARE_READ */
			0,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
			0
			);

		if (nd->tt->hand == INVALID_HANDLE_VALUE) {
			lkl_printf("%s: CreateFile failed on TAP device: %s\n",
				   __func__, device_path);
			return -1;
		}

		/* translate high-level device name into a device instance
		 * GUID using the registry
		 */
		nd->tt->actual_name = malloc(strlen(name) + 1);
		if (nd->tt->actual_name)
			strcpy(nd->tt->actual_name, name);
	}

	tap_debug_printf("TAP-WIN32 device [%s] opened: %s\n",
		   nd->tt->actual_name, device_path);
	/* TODO TODO TODO */
	/* nd->tt->adapter_index = get_adapter_index (device_guid); */

	/* get driver version info */
	{
		ULONG info[3];
		/* TODO TODO TODO */
		/* CLEAR (info); */
		if (DeviceIoControl(nd->tt->hand, TAP_WIN_IOCTL_GET_VERSION,
				    &info, sizeof(info), &info, sizeof(info), &len, NULL)) {
			tap_debug_printf("TAP-Windows Driver Version %d.%d %s\n",
				   (int)info[0],
				   (int)info[1],
				   (info[2] ? "(DEBUG)" : ""));
		}

		if (!(info[0] == TAP_WIN_MIN_MAJOR && info[1] >= TAP_WIN_MIN_MINOR))
			lkl_printf("ERROR: This requires a TAP-Windows driver version %d.%d.\n",
				      TAP_WIN_MIN_MAJOR,
				      TAP_WIN_MIN_MINOR);
	}

	/* get driver MTU */
	{
		ULONG mtu;

		if (DeviceIoControl(nd->tt->hand, TAP_WIN_IOCTL_GET_MTU,
				    &mtu, sizeof(mtu),
				    &mtu, sizeof(mtu), &len, NULL)) {
			nd->tt->post_open_mtu = (int) mtu;
			tap_debug_printf("TAP-Windows MTU=%d\n", (int) mtu);
		}
	}


	/* get driver MAC */
	{
		uint8_t mac[6] = {
			0, 0, 0, 0, 0, 0
		};
		if (DeviceIoControl(nd->tt->hand, TAP_WIN_IOCTL_GET_MAC,
				    mac, sizeof(mac),
				    mac, sizeof(mac), &len, NULL)) {
			tap_debug_printf("TAP-Windows MAC=[%x,%x,%x,%x,%x,%x]\n",
					 mac[0], mac[1], mac[2],
					 mac[2], mac[4], mac[5]);
			memcpy(nd->dev.mac, mac, sizeof(mac));
		}
	}

	/* set point-to-point mode if TUN device */
	if (nd->tt->type == DEV_TYPE_TUN) {
		lkl_printf("TUN type not supported for now...\n");
		return -1;
	}
	/* TAP DEVICE */
	else if (nd->tt->type == DEV_TYPE_TAP)
		tap_debug_printf("TODO: Set Point-to-point through DeviceIoControl\n");

	/* set driver media status to 'connected' */
	{
		ULONG status = TRUE;

		if (!DeviceIoControl(nd->tt->hand, TAP_WIN_IOCTL_SET_MEDIA_STATUS,
				     &status, sizeof(status),
				     &status, sizeof(status), &len, NULL))
			lkl_printf("WARNING: rejected TAP_WIN_IOCTL_SET_MEDIA_STATUS");
	}

	return 0;
}

static void overlapped_io_init(struct overlapped_io *o, int event_state)
{
	memset(o, 0, sizeof(*o));

	/* manual reset event, initially set according to event_state */
	o->overlapped.hEvent = CreateEvent(NULL, TRUE, event_state,
					   event_state ? "write" : NULL);
	if (o->overlapped.hEvent == NULL)
		lkl_printf("Error: %s: CreateEvent failed(%s)", __func__,
			   GetErrorMessage());

	/* allocate buffer for overlapped I/O */
	o->buf_init = malloc(1500); /* XXX: MTU */
	o->buf_init_len = 1500; /* XXX: MTU */
	if (!(o->buf_init))
		lkl_printf("buffer alloc failed!\n"); /* XXX: return -1 or so? */
	else
		tap_debug_printf("%s:_io_init buffer allocated!\n", __func__);
}

static void overlapped_io_exit(struct overlapped_io *o)
{
	CloseHandle(o->overlapped.hEvent);
	free(o->buf_init);
}


/* returns the state */
int tun_read_queue(struct tuntap *tt, uint8_t *buffer, int maxsize)
{
	if (tt->reads.iostate == IOSTATE_INITIAL) {
		DWORD len = 1500;
		BOOL status;
		int err;

		/* reset buf to its initial state */
		tt->reads.buf = tt->reads.buf_init;
		tt->reads.buf_len = tt->reads.buf_init_len;

		len = maxsize ? (DWORD)maxsize : (tt->reads.buf_len);
		if (len > (tt->reads.buf_len)) /* clip to buffer len */
			len = tt->reads.buf_len;

		/* the overlapped read will signal this event on I/O completion */
		if (!ResetEvent(tt->reads.overlapped.hEvent))
			lkl_printf("ResetEvent failed\n");

		status = ReadFile(
			tt->hand,
			buffer,
			len,
			&tt->reads.size,
			&tt->reads.overlapped
			);

		if (status) { /* operation completed immediately? */
			/*
			 * since we got an immediate return, we must signal
			 * the event object ourselves
			 */
			if (!SetEvent(tt->reads.overlapped.hEvent))
				lkl_printf("SetEvent failed\n");

			tt->reads.iostate = IOSTATE_IMMEDIATE_RETURN;
			tt->reads.status = 0;

			tap_debug_printf("WIN32 I/O: TAP Read immediate return [%d,%d]\n",
				       (int) len,
				       (int) tt->reads.size);
		} else {
			err = GetLastError();
			if (err == ERROR_IO_PENDING) { /* operation queued? */
				tt->reads.iostate = IOSTATE_QUEUED;
				tt->reads.status = err;
				tap_debug_printf("WIN32 I/O: TAP Read queued [%d]\n", (int) len);
			} else {
				if (!SetEvent(tt->reads.overlapped.hEvent))
					lkl_printf("SetEvent failed\n");

				tt->reads.iostate = IOSTATE_IMMEDIATE_RETURN;
				tt->reads.status = err;
				lkl_printf("WIN32: TAP Read error [%d] : %d\n",
					   (int)len, (int)err);
			}
		}
	}

	return tt->reads.iostate;
}

/* Finalize any pending overlapped IO's */
int tun_finalize(HANDLE h, struct overlapped_io *io, uint8_t **buf, uint32_t *buf_len)
{
	int ret = -1;
	BOOL status;

	switch (io->iostate) {
	case IOSTATE_QUEUED:
		status = GetOverlappedResult(
			h,
			&io->overlapped,
			&io->size,
			0u
			);
		if (status) {
			/* successful return for a queued operation */
			if (buf) {
				*buf = io->buf;
				*buf_len = io->buf_len;
			}

			ret = io->size;
			io->iostate = IOSTATE_INITIAL;

			if (!ResetEvent(io->overlapped.hEvent))
				lkl_printf("ResetEvent in finalize failed!\n");

			tap_debug_printf("WIN32 I/O: TAP Completion success: QUEUED! [%d]\n", ret);
		} else {
			/* error during a queued operation */
			/* error, or just not completed? */
			ret = 0;
			if (GetLastError() != ERROR_IO_INCOMPLETE) {
				/* if no error (i.e. just not finished yet),
				 * then DON'T execute this code
				 */
				io->iostate = IOSTATE_INITIAL;
				if (!ResetEvent(io->overlapped.hEvent))
					lkl_printf("ResetEvent in finalize failed!\n");

				lkl_printf("WIN32 I/O: TAP Completion error\n");
				ret = -1;     /* There actually was an error */
			}
		}

		break;
	case IOSTATE_IMMEDIATE_RETURN:
		io->iostate = IOSTATE_INITIAL;
		if (!ResetEvent(io->overlapped.hEvent))
			lkl_printf("ResetEvent in finalize failed!\n");

		if (io->status) {
			/* error return for a non-queued operation */
			SetLastError(io->status);
			ret = -1;
			lkl_printf("WIN32 I/O: TAP Completion non-queued error\n");
		} else {
			/* successful return for a non-queued operation */
			if (buf)
				*buf = io->buf;

			ret = io->size;
		}

		break;
	case IOSTATE_INITIAL:     /* were we called without proper queueing? */
		SetLastError(ERROR_INVALID_FUNCTION);
		ret = -1;
		lkl_printf("WIN32 I/O: TAP Completion BAD STATE\n");
		break;

	default:
		lkl_printf("Some weird case happened..\n");
	}

	if (buf)
		*buf_len = ret;

	return ret;
}



/* returns the amount of bytes written */
int tun_write_queue(struct tuntap *tt, uint8_t *buf, uint32_t buf_len)
{
	/* workaround for threaded hEvent */
	if (!tt->writes.overlapped.hEvent)
		tt->writes.overlapped.hEvent =
			OpenEvent(EVENT_ALL_ACCESS, FALSE, "write");


	if (tt->writes.iostate == IOSTATE_INITIAL) {
		BOOL status;
		int err;

		/* make a private copy of buf */
		tt->writes.buf = tt->writes.buf_init;
		tt->writes.buf_len = buf_len;
		memcpy(tt->writes.buf, buf, buf_len);

		/* the overlapped write will signal this event on I/O completion */
		if (!ResetEvent(tt->writes.overlapped.hEvent))
			lkl_printf("ResetEvent in write_queue failed!\n");

		status = WriteFile(
			tt->hand,
			tt->writes.buf,
			tt->writes.buf_len,
			&tt->writes.size,
			&tt->writes.overlapped
			);

		if (status) { /* operation completed immediately? */
			tt->writes.iostate = IOSTATE_IMMEDIATE_RETURN;

			/* since we got an immediate return,
			 * we must signal the event object ourselves
			 */
			if (!SetEvent(tt->writes.overlapped.hEvent))
				lkl_printf("SetEvent in write_queue failed!\n");

			tt->writes.status = 0;

			tap_debug_printf("WIN32 I/O: TAP Write immediate return [%d,%d]\n",
				       (int)(tt->writes.buf_len),
				       (int)tt->writes.size);
		} else {
			err = GetLastError();
			if (err == ERROR_IO_PENDING) { /* operation queued? */
				tt->writes.iostate = IOSTATE_QUEUED;
				tt->writes.status = err;
				tap_debug_printf("WIN32 I/O: TAP Write queued [%d]\n",
					      (tt->writes.buf_len));
			} else {
				if (!SetEvent(tt->writes.overlapped.hEvent))
					lkl_printf("SetEvent in write_queue failed!\n");

				tt->writes.iostate = IOSTATE_IMMEDIATE_RETURN;
				tt->writes.status = err;
				lkl_printf("WIN32 I/O: TAP Write error [%ld] : %d\n",
					   &tt->writes.buf_len, (int)err);
			}
		}
	}

	return tt->writes.iostate;
}

static inline int overlapped_io_active(struct overlapped_io *o)
{
	return o->iostate == IOSTATE_QUEUED || o->iostate == IOSTATE_IMMEDIATE_RETURN;
}

/* if >= 0: returns the amount of bytes read, otherwise error! */
static int tun_write_win32(struct tuntap *tt, uint8_t *buf, uint32_t buf_len)
{
	int err = 0;
	int status = 0;

	if (overlapped_io_active(&tt->writes)) {
		status = tun_finalize(tt->hand, &tt->writes, NULL, 0);
		if (status == 0) {
			/* busy, just wait, do not schedule a new write */
			return 0;
		}

		if (status < 0)
			err = GetLastError();
	}

	/* the overlapped IO is done, now we can schedule a new write */
	tun_write_queue(tt, buf, buf_len);
	if (status < 0) {
		SetLastError(err);
		return status;
	} else
		return buf_len;
}


/* if >= 0: returns the amount of bytes read, otherwise error! */
static int tun_read_win32(struct tuntap *tt, uint8_t *buf, uint32_t buf_len)
{
	int err = 0;
	int status = 0;


	/* First, finish possible pending IOs */
	if (overlapped_io_active(&tt->reads)) {
		status = tun_finalize(tt->hand, &tt->reads, &buf, &buf_len);
		if (status == 0) {
			/* busy, just wait, do not schedule a new read */
			return 0;
		}

		if (status < 0) {
			tap_debug_printf("tun_finalize status < 0: %d\n", status);
			err = GetLastError();
		}

		if (status > 0)
			return buf_len;
	}

	/* If no pending IOs, schedule a new read */
	/* queue, or immediate return */
	if (tun_read_queue(tt, buf, buf_len) == IOSTATE_IMMEDIATE_RETURN)
		return tt->reads.size;

	/* If the pending IOs gave an error, report it */
	if (status < 0) {
		SetLastError(err);
		return status;
	}

	/* no errors, but the newly scheduled read is now pending */
	return 0;
}


static int read_tun_buffered(struct tuntap *tt, uint8_t *buf, uint32_t buf_len)
{
	return tun_read_win32(tt, buf, buf_len);
}

static int write_tun_buffered(struct tuntap *tt, uint8_t *buf, uint32_t buf_len)
{
	return tun_write_win32(tt, buf, buf_len);
}

static int wintap_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	int bytes_sent = 0;
	struct lkl_netdev_wintap *nd_wintap =
		container_of(nd, struct lkl_netdev_wintap, dev);
	/* Increase the statistic count */
	nd_wintap->statistics_frames_out++;
	int i;

	for (i = 0; i < cnt; i++) {
		bytes_sent = write_tun_buffered(nd_wintap->tt, iov[i].iov_base,
						 iov[i].iov_len);
		if (bytes_sent < 0)
			return -1;
	}
	tap_debug_printf("TX> sent %d bytes\n", bytes_sent);

	/* Discard the frame content silently. */
	return bytes_sent;
}

uint8_t recv_buffer[1500];

static int wintap_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	struct lkl_netdev_wintap *nd_wintap =
		container_of(nd, struct lkl_netdev_wintap, dev);
	int i, bytes_read = 0, ret;

	for (i = 0; i < cnt; i++) {
		ret = read_tun_buffered(nd_wintap->tt, recv_buffer, 1500);
		if (ret > 0) {
			uint8_t src[6], dst[6];

			memcpy(iov[i].iov_base, recv_buffer, ret);
			tap_debug_printf("RX< recvd: %d bytes\n", ret);
			/* break; */
			bytes_read += ret;

			memcpy(dst, iov[i].iov_base, 6);
			memcpy(src, iov[i].iov_base + 6, 6);
			tap_debug_printf("%s: rx dst=%02x:%02x:%02x:%02x:%02x:%02x",
					 __func__,
					 dst[0],
					 dst[1],
					 dst[2],
					 dst[3],
					 dst[4],
					 dst[5]);
			tap_debug_printf(", src=%02x:%02x:%02x:%02x:%02x:%02x\n",
					 src[0],
					 src[1],
					 src[2],
					 src[3],
					 src[4],
					 src[5]);
		} else
			return -1;
	}
	return bytes_read;
}

static int ishup;
static int wintap_poll(struct lkl_netdev *nd)
{
	struct lkl_netdev_wintap *nd_wintap =
		container_of(nd, struct lkl_netdev_wintap, dev);
	int ret = 0;

	if (WaitForSingleObject(nd_wintap->tt->writes.overlapped.hEvent, 0) == WAIT_OBJECT_0)
		ret |= LKL_DEV_NET_POLL_TX;
	if (WaitForSingleObject(nd_wintap->tt->reads.overlapped.hEvent, 0) == WAIT_OBJECT_0) {
		tap_debug_printf("%s: read ready\n", __func__);
		ret |= LKL_DEV_NET_POLL_RX;
	}

	if (ishup)
		ret |= LKL_DEV_NET_POLL_HUP;

	return ret;
}

static void wintap_poll_hup(struct lkl_netdev *nd)
{
	ishup = 1;
}

static void wintap_free(struct lkl_netdev *nd)
{
	struct lkl_netdev_wintap *nd_wintap =
		container_of(nd, struct lkl_netdev_wintap, dev);

	/* stopping wintap */
	if (!CancelIo(nd_wintap->tt->hand))
		lkl_printf("Warning: CancelIO failed on TAP-Windows adapter");

	overlapped_io_exit(&nd_wintap->tt->reads);
	overlapped_io_exit(&nd_wintap->tt->writes);

	if (!CloseHandle(nd_wintap->tt->hand))
		lkl_printf("Warning: CancelIO failed on TAP-Windows adapter");

	free(nd_wintap->tt->actual_name);
	/* XXX: if we free this, Win raises SIGABRT */
	//free(nd_wintap);
}

struct lkl_dev_net_ops wintap_ops =  {
	.tx = wintap_tx,
	.rx = wintap_rx,
	.poll = wintap_poll,
	.poll_hup = wintap_poll_hup,
	.free = wintap_free,
};

struct lkl_netdev *lkl_netdev_wintap_create(const char *ifparams)
{
	struct lkl_netdev_wintap *nd;
	struct tuntap *tt;

	nd = malloc(sizeof(*nd));
	if (!nd) {
		fprintf(stderr, "wintap: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}
	memset(nd, 0, sizeof(*nd));

	show_tap_win_adapters();

	tt = malloc(sizeof(struct tuntap));
	if (!tt) {
		fprintf(stderr, "wintap: failed to allocate memory\n");
		/* TODO: propagate the error state, maybe use errno for that? */
		return NULL;
	}
	memset(tt, 0, sizeof(*tt));
	nd->tt = tt;

	nd->tt->type = DEV_TYPE_TAP;
	if (open_tun(ifparams, nd)) {
		lkl_printf("Failed to create TAP device!\n");
		free(nd);
		return NULL;
	}

	/* init overlapped io */
	overlapped_io_init(&nd->tt->reads, FALSE);
	overlapped_io_init(&nd->tt->writes, TRUE);
	nd->tt->writes.overlapped.hEvent = NULL;

	lkl_printf("Device %s created.\n", ifparams);
	nd->dev.ops = &wintap_ops;
	return &nd->dev;
}
