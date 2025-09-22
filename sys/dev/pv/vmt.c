/*	$OpenBSD: vmt.c,v 1.32 2024/05/24 10:05:55 jsg Exp $ */

/*
 * Copyright (c) 2007 David Crawshaw <david@zentus.com>
 * Copyright (c) 2008 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if !defined(__i386__) && !defined(__amd64__)
#error vmt(4) is only supported on i386 and amd64
#endif

/*
 * Protocol reverse engineered by Ken Kato:
 * https://sites.google.com/site/chitchatvmback/backdoor
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/timeout.h>
#include <sys/syslog.h>
#include <sys/mount.h>
#include <sys/task.h>
#include <sys/sensors.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/rtable.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pv/pvvar.h>

/* "The" magic number, always occupies the EAX register. */
#define VM_MAGIC			0x564D5868

/* Port numbers, passed on EDX.LOW . */
#define VM_PORT_CMD			0x5658
#define VM_PORT_RPC			0x5659

/* Commands, passed on ECX.LOW. */
#define VM_CMD_GET_SPEED		0x01
#define VM_CMD_APM			0x02
#define VM_CMD_GET_MOUSEPOS		0x04
#define VM_CMD_SET_MOUSEPOS		0x05
#define VM_CMD_GET_CLIPBOARD_LEN	0x06
#define VM_CMD_GET_CLIPBOARD		0x07
#define VM_CMD_SET_CLIPBOARD_LEN	0x08
#define VM_CMD_SET_CLIPBOARD		0x09
#define VM_CMD_GET_VERSION		0x0a
#define  VM_VERSION_UNMANAGED			0x7fffffff
#define VM_CMD_GET_DEVINFO		0x0b
#define VM_CMD_DEV_ADDREMOVE		0x0c
#define VM_CMD_GET_GUI_OPTIONS		0x0d
#define VM_CMD_SET_GUI_OPTIONS		0x0e
#define VM_CMD_GET_SCREEN_SIZE		0x0f
#define VM_CMD_GET_HWVER		0x11
#define VM_CMD_POPUP_OSNOTFOUND		0x12
#define VM_CMD_GET_BIOS_UUID		0x13
#define VM_CMD_GET_MEM_SIZE		0x14
/*#define VM_CMD_GET_TIME		0x17 */	/* deprecated */
#define VM_CMD_RPC			0x1e
#define VM_CMD_GET_TIME_FULL		0x2e

/* RPC sub-commands, passed on ECX.HIGH. */
#define VM_RPC_OPEN			0x00
#define VM_RPC_SET_LENGTH		0x01
#define VM_RPC_SET_DATA			0x02
#define VM_RPC_GET_LENGTH		0x03
#define VM_RPC_GET_DATA			0x04
#define VM_RPC_GET_END			0x05
#define VM_RPC_CLOSE			0x06

/* RPC magic numbers, passed on EBX. */
#define VM_RPC_OPEN_RPCI	0x49435052UL /* with VM_RPC_OPEN. */
#define VM_RPC_OPEN_TCLO	0x4F4C4354UL /* with VP_RPC_OPEN. */
#define VM_RPC_ENH_DATA		0x00010000UL /* with enhanced RPC data calls. */

#define VM_RPC_FLAG_COOKIE	0x80000000UL

/* RPC reply flags */
#define VM_RPC_REPLY_SUCCESS	0x0001
#define VM_RPC_REPLY_DORECV	0x0002		/* incoming message available */
#define VM_RPC_REPLY_CLOSED	0x0004		/* RPC channel is closed */
#define VM_RPC_REPLY_UNSENT	0x0008		/* incoming message was removed? */
#define VM_RPC_REPLY_CHECKPOINT	0x0010		/* checkpoint occurred -> retry */
#define VM_RPC_REPLY_POWEROFF	0x0020		/* underlying device is powering off */
#define VM_RPC_REPLY_TIMEOUT	0x0040
#define VM_RPC_REPLY_HB		0x0080		/* high-bandwidth tx/rx available */

/* VM state change IDs */
#define VM_STATE_CHANGE_HALT	1
#define VM_STATE_CHANGE_REBOOT	2
#define VM_STATE_CHANGE_POWERON 3
#define VM_STATE_CHANGE_RESUME  4
#define VM_STATE_CHANGE_SUSPEND 5

/* VM guest info keys */
#define VM_GUEST_INFO_DNS_NAME		1
#define VM_GUEST_INFO_IP_ADDRESS	2
#define VM_GUEST_INFO_DISK_FREE_SPACE	3
#define VM_GUEST_INFO_BUILD_NUMBER	4
#define VM_GUEST_INFO_OS_NAME_FULL	5
#define VM_GUEST_INFO_OS_NAME		6
#define VM_GUEST_INFO_UPTIME		7
#define VM_GUEST_INFO_MEMORY		8
#define VM_GUEST_INFO_IP_ADDRESS_V2	9
#define VM_GUEST_INFO_IP_ADDRESS_V3	10

/* RPC responses */
#define VM_RPC_REPLY_OK			"OK "
#define VM_RPC_RESET_REPLY		"OK ATR toolbox"
#define VM_RPC_REPLY_ERROR		"ERROR Unknown command"
#define VM_RPC_REPLY_ERROR_IP_ADDR	"ERROR Unable to find guest IP address"

/* VM backup error codes */
#define VM_BACKUP_SUCCESS		0
#define VM_BACKUP_SYNC_ERROR		3
#define VM_BACKUP_REMOTE_ABORT		4

#define VM_BACKUP_TIMEOUT		30 /* seconds */

/* NIC/IP address stuff */
#define VM_NICINFO_VERSION		3

#define VM_NICINFO_IP_LEN		64
#define VM_NICINFO_MAX_NICS		16
#define VM_NICINFO_MAX_ADDRS		2048
#define VM_NICINFO_MAC_LEN		20

#define VM_NICINFO_ADDR_IPV4		1
#define VM_NICINFO_ADDR_IPV6		2

struct vm_nicinfo_addr_v4 {
	uint32_t	v4_addr_type;
	uint32_t	v4_addr_len;
	struct in_addr	v4_addr;
	uint32_t	v4_prefix_len;
	uint32_t	v4_origin;
	uint32_t	v4_status;
};

struct vm_nicinfo_addr_v6 {
	uint32_t	v6_addr_type;
	uint32_t	v6_addr_len;
	struct in6_addr v6_addr;
	uint32_t	v6_prefix_len;
	uint32_t	v6_origin;
	uint32_t	v6_status;
};

struct vm_nicinfo_nic {
	uint32_t	ni_mac_len;
	char		ni_mac[VM_NICINFO_MAC_LEN];
	uint32_t	ni_num_addrs;
};

struct vm_nicinfo_nic_nomac {
	uint32_t	nn_mac_len;
	uint32_t	nn_num_addrs;
};

struct vm_nicinfo_nic_post {
	uint32_t	np_dns_config;
	uint32_t	np_wins_config;
	uint32_t	np_dhcpv4_config;
	uint32_t	np_dhcpv6_config;
};

struct vm_nicinfo_nic_list {
	uint32_t	nl_version;
	uint32_t	nl_nic_list;
	uint32_t	nl_num_nics;
};

struct vm_nicinfo_nic_list_post {
	uint32_t	nl_num_routes;
	uint32_t	nl_dns_config;
	uint32_t	nl_wins_config;
	uint32_t	nl_dhcpv4_config;
	uint32_t	nl_dhcpv6_config;
};

#define VM_NICINFO_CMD			"SetGuestInfo  10 "

/* A register. */
union vm_reg {
	struct {
		uint16_t low;
		uint16_t high;
	} part;
	uint32_t word;
#ifdef __amd64__
	struct {
		uint32_t low;
		uint32_t high;
	} words;
	uint64_t quad;
#endif
} __packed;

/* A register frame. */
struct vm_backdoor {
	union vm_reg eax;
	union vm_reg ebx;
	union vm_reg ecx;
	union vm_reg edx;
	union vm_reg esi;
	union vm_reg edi;
	union vm_reg ebp;
} __packed;

/* RPC context. */
struct vm_rpc {
	uint16_t channel;
	uint32_t cookie1;
	uint32_t cookie2;
};

struct vmt_softc {
	struct device		sc_dev;

	struct vm_rpc		sc_tclo_rpc;
	char			*sc_rpc_buf;
	int			sc_rpc_error;
	int			sc_tclo_ping;
	int			sc_set_guest_os;
	int			sc_quiesce;
	struct task		sc_quiesce_task;
	struct task		sc_nicinfo_task;
#define VMT_RPC_BUFLEN		4096

	struct timeout		sc_tick;
	struct timeout		sc_tclo_tick;
	struct ksensordev	sc_sensordev;
	struct ksensor		sc_sensor;

	char			sc_hostname[MAXHOSTNAMELEN];
	size_t			sc_nic_info_size;
	char			*sc_nic_info;
};

#ifdef VMT_DEBUG
#define DPRINTF(_arg...)	printf(_arg)
#else
#define DPRINTF(_arg...)	do {} while(0)
#endif
#define DEVNAME(_s)		((_s)->sc_dev.dv_xname)

void	 vm_cmd(struct vm_backdoor *);
void	 vm_ins(struct vm_backdoor *);
void	 vm_outs(struct vm_backdoor *);

/* Functions for communicating with the VM Host. */
int	 vm_rpc_open(struct vm_rpc *, uint32_t);
int	 vm_rpc_close(struct vm_rpc *);
int	 vm_rpc_send(const struct vm_rpc *, const uint8_t *, uint32_t);
int	 vm_rpc_send_str(const struct vm_rpc *, const uint8_t *);
int	 vm_rpc_get_length(const struct vm_rpc *, uint32_t *, uint16_t *);
int	 vm_rpc_get_data(const struct vm_rpc *, char *, uint32_t, uint16_t);
int	 vm_rpc_send_rpci_tx_buf(struct vmt_softc *, const uint8_t *, uint32_t);
int	 vm_rpc_send_rpci_tx(struct vmt_softc *, const char *, ...)
	    __attribute__((__format__(__kprintf__,2,3)));
int	 vm_rpci_response_successful(struct vmt_softc *);

int	 vmt_kvop(void *, int, char *, char *, size_t);

void	 vmt_probe_cmd(struct vm_backdoor *, uint16_t);
void	 vmt_tclo_state_change_success(struct vmt_softc *, int, char);
void	 vmt_do_reboot(struct vmt_softc *);
void	 vmt_do_shutdown(struct vmt_softc *);
void	 vmt_shutdown(void *);

void	 vmt_clear_guest_info(struct vmt_softc *);
void	 vmt_update_guest_info(struct vmt_softc *);
void	 vmt_update_guest_uptime(struct vmt_softc *);

void	 vmt_tick_hook(struct device *self);
void	 vmt_tick(void *);
void	 vmt_resume(void);

int	 vmt_match(struct device *, void *, void *);
void	 vmt_attach(struct device *, struct device *, void *);
int	 vmt_activate(struct device *, int);

void	 vmt_tclo_tick(void *);
int	 vmt_tclo_process(struct vmt_softc *, const char *);
void	 vmt_tclo_reset(struct vmt_softc *);
void	 vmt_tclo_ping(struct vmt_softc *);
void	 vmt_tclo_halt(struct vmt_softc *);
void	 vmt_tclo_reboot(struct vmt_softc *);
void	 vmt_tclo_poweron(struct vmt_softc *);
void	 vmt_tclo_suspend(struct vmt_softc *);
void	 vmt_tclo_resume(struct vmt_softc *);
void	 vmt_tclo_capreg(struct vmt_softc *);
void	 vmt_tclo_broadcastip(struct vmt_softc *);

void	 vmt_set_backup_status(struct vmt_softc *, const char *, int,
	    const char *);
void	 vmt_quiesce_task(void *);
void	 vmt_quiesce_done_task(void *);
void	 vmt_tclo_abortbackup(struct vmt_softc *);
void	 vmt_tclo_startbackup(struct vmt_softc *);
void	 vmt_tclo_backupdone(struct vmt_softc *);

size_t	 vmt_xdr_ifaddr(struct ifaddr *, char *);
size_t	 vmt_xdr_nic_entry(struct ifnet *, char *);
size_t	 vmt_xdr_nic_info(char *);
void	 vmt_nicinfo_task(void *);

int	 vmt_probe(void);

struct vmt_tclo_rpc {
	const char	*name;
	void		(*cb)(struct vmt_softc *);
} vmt_tclo_rpc[] = {
	/* Keep sorted by name (case-sensitive) */
        { "Capabilities_Register",      vmt_tclo_capreg },
        { "OS_Halt",                    vmt_tclo_halt },
        { "OS_PowerOn",                 vmt_tclo_poweron },
        { "OS_Reboot",                  vmt_tclo_reboot },
        { "OS_Resume",                  vmt_tclo_resume },
        { "OS_Suspend",                 vmt_tclo_suspend },
        { "Set_Option broadcastIP 1",   vmt_tclo_broadcastip },
        { "ping",                       vmt_tclo_ping },
        { "reset",                      vmt_tclo_reset },
        { "vmbackup.abort",		vmt_tclo_abortbackup },
        { "vmbackup.snapshotDone",	vmt_tclo_backupdone },
        { "vmbackup.start 1",		vmt_tclo_startbackup },
        { NULL },
#if 0
	/* Various unsupported commands */
	{ "Set_Option autohide 0" },
	{ "Set_Option copypaste 1" },
	{ "Set_Option enableDnD 1" },
	{ "Set_Option enableMessageBusTunnel 0" },
	{ "Set_Option linkRootHgfsShare 0" },
	{ "Set_Option mapRootHgfsShare 0" },
	{ "Set_Option synctime 1" },
	{ "Set_Option synctime.period 0" },
	{ "Set_Option time.synchronize.tools.enable 1" },
	{ "Set_Option time.synchronize.tools.percentCorrection 0" },
	{ "Set_Option time.synchronize.tools.slewCorrection 1" },
	{ "Set_Option time.synchronize.tools.startup 1" },
	{ "Set_Option toolScripts.afterPowerOn 1" },
	{ "Set_Option toolScripts.afterResume 1" },
	{ "Set_Option toolScripts.beforePowerOff 1" },
	{ "Set_Option toolScripts.beforeSuspend 1" },
	{ "Time_Synchronize 0" },
	{ "Vix_1_Relayed_Command \"38cdcae40e075d66\"" },
#endif
};

const struct cfattach vmt_ca = {
	sizeof(struct vmt_softc),
	vmt_match,
	vmt_attach,
	NULL,
	vmt_activate
};

struct cfdriver vmt_cd = {
	NULL,
	"vmt",
	DV_DULL
};

extern char hostname[MAXHOSTNAMELEN];

void
vmt_probe_cmd(struct vm_backdoor *frame, uint16_t cmd)
{
	bzero(frame, sizeof(*frame));

	(frame->eax).word = VM_MAGIC;
	(frame->ebx).word = ~VM_MAGIC;
	(frame->ecx).part.low = cmd;
	(frame->ecx).part.high = 0xffff;
	(frame->edx).part.low  = VM_PORT_CMD;
	(frame->edx).part.high = 0;

	vm_cmd(frame);
}

int
vmt_probe(void)
{
	struct vm_backdoor frame;

	vmt_probe_cmd(&frame, VM_CMD_GET_VERSION);
	if (frame.eax.word == 0xffffffff ||
	    frame.ebx.word != VM_MAGIC)
		return (0);

	vmt_probe_cmd(&frame, VM_CMD_GET_SPEED);
	if (frame.eax.word == VM_MAGIC)
		return (0);

	return (1);
}

int
vmt_match(struct device *parent, void *match, void *aux)
{
	struct pv_attach_args	*pva = aux;
	struct pvbus_hv		*hv = &pva->pva_hv[PVBUS_VMWARE];

	if (hv->hv_base == 0)
		return (0);
	if (!vmt_probe())
		return (0);

	return (1);
}

void
vmt_attach(struct device *parent, struct device *self, void *aux)
{
	struct vmt_softc *sc = (struct vmt_softc *)self;
	struct pv_attach_args	*pva = aux;
	struct pvbus_hv		*hv = &pva->pva_hv[PVBUS_VMWARE];

	printf("\n");
	sc->sc_rpc_buf = malloc(VMT_RPC_BUFLEN, M_DEVBUF, M_NOWAIT);
	if (sc->sc_rpc_buf == NULL) {
		printf("%s: unable to allocate buffer for RPC\n",
		    DEVNAME(sc));
		return;
	}

	if (vm_rpc_open(&sc->sc_tclo_rpc, VM_RPC_OPEN_TCLO) != 0) {
		printf("%s: failed to open backdoor RPC channel "
		    "(TCLO protocol)\n", DEVNAME(sc));
		goto free;
	}

	/* don't know if this is important at all yet */
	if (vm_rpc_send_rpci_tx(sc,
	    "tools.capability.hgfs_server toolbox 1") != 0) {
		printf(": failed to set HGFS server capability\n");
		goto free;
	}

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TIMEDELTA;
	sc->sc_sensor.status = SENSOR_S_UNKNOWN;

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);

	config_mountroot(self, vmt_tick_hook);

	timeout_set_proc(&sc->sc_tclo_tick, vmt_tclo_tick, sc);
	timeout_add_sec(&sc->sc_tclo_tick, 1);
	sc->sc_tclo_ping = 1;

	task_set(&sc->sc_nicinfo_task, vmt_nicinfo_task, sc);

	/* pvbus(4) key/value interface */
	hv->hv_kvop = vmt_kvop;
	hv->hv_arg = sc;

	return;

free:
	free(sc->sc_rpc_buf, M_DEVBUF, VMT_RPC_BUFLEN);
}

int
vmt_kvop(void *arg, int op, char *key, char *value, size_t valuelen)
{
	struct vmt_softc *sc = arg;
	struct vm_rpc rpci;
	char *buf = NULL;
	size_t bufsz;
	int error = 0;
	uint32_t rlen;
	uint16_t ack;

	bufsz = VMT_RPC_BUFLEN;
	buf = malloc(bufsz, M_TEMP, M_WAITOK | M_ZERO);

	switch (op) {
	case PVBUS_KVWRITE:
		if ((size_t)snprintf(buf, bufsz, "info-set %s %s",
		    key, value) >= bufsz) {
			DPRINTF("%s: write command too long", DEVNAME(sc));
			error = EINVAL;
			goto done;
		}
		break;
	case PVBUS_KVREAD:
		if ((size_t)snprintf(buf, bufsz, "info-get %s",
		    key) >= bufsz) {
			DPRINTF("%s: read command too long", DEVNAME(sc));
			error = EINVAL;
			goto done;
		}
		break;
	default:
		error = EOPNOTSUPP;
		goto done;
	}

	if (vm_rpc_open(&rpci, VM_RPC_OPEN_RPCI) != 0) {
		DPRINTF("%s: rpci channel open failed\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
		error = EIO;
		goto done;
	}

	if (vm_rpc_send(&rpci, buf, bufsz) != 0) {
		DPRINTF("%s: unable to send rpci command\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
		error = EIO;
		goto close;
	}

	if (vm_rpc_get_length(&rpci, &rlen, &ack) != 0) {
		DPRINTF("%s: failed to get length of rpci response data\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
		error = EIO;
		goto close;
	}

	if (rlen > 0) {
		if (rlen + 1 > valuelen) {
			error = ERANGE;
			goto close;
		}

		if (vm_rpc_get_data(&rpci, value, rlen, ack) != 0) {
			DPRINTF("%s: failed to get rpci response data\n",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
			error = EIO;
			goto close;
		}
		/* test if response success  */
		if (rlen < 2 || value[0] != '1' || value[1] != ' ') {
			DPRINTF("%s: host rejected command: %s\n", DEVNAME(sc),
			    buf);
			error = EINVAL;
			goto close;
		}
		/* skip response that was tested */
		bcopy(value + 2, value, valuelen - 2);
		value[rlen - 2] = '\0';
	}

 close:
	if (vm_rpc_close(&rpci) != 0)
		DPRINTF("%s: unable to close rpci channel\n", DEVNAME(sc));
 done:
	free(buf, M_TEMP, bufsz);
	return (error);
}

void
vmt_resume(void)
{
	struct vm_backdoor frame;
	extern void rdrand(void *);

	bzero(&frame, sizeof(frame));
	frame.eax.word = VM_MAGIC;
	frame.ecx.part.low = VM_CMD_GET_TIME_FULL;
	frame.edx.part.low  = VM_PORT_CMD;
	vm_cmd(&frame);

	rdrand(NULL);
	enqueue_randomness(frame.eax.word);
	enqueue_randomness(frame.esi.word);
	enqueue_randomness(frame.edx.word);
	enqueue_randomness(frame.ebx.word);
	resume_randomness(NULL, 0);
}

int
vmt_activate(struct device *self, int act)
{
	int rv = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		vmt_shutdown(self);
		break;
	case DVACT_RESUME:
		vmt_resume();
		break;
	}
	return (rv);
}


void
vmt_update_guest_uptime(struct vmt_softc *sc)
{
	/* host wants uptime in hundredths of a second */
	if (vm_rpc_send_rpci_tx(sc, "SetGuestInfo  %d %lld00",
	    VM_GUEST_INFO_UPTIME, (long long)getuptime()) != 0) {
		DPRINTF("%s: unable to set guest uptime", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_clear_guest_info(struct vmt_softc *sc)
{
	if (sc->sc_nic_info_size != 0) {
		free(sc->sc_nic_info, M_DEVBUF, sc->sc_nic_info_size);
		sc->sc_nic_info = NULL;
		sc->sc_nic_info_size = 0;
	}
	sc->sc_hostname[0] = '\0';
	sc->sc_set_guest_os = 0;
}

void
vmt_update_guest_info(struct vmt_softc *sc)
{
	if (strncmp(sc->sc_hostname, hostname, sizeof(sc->sc_hostname)) != 0) {
		strlcpy(sc->sc_hostname, hostname, sizeof(sc->sc_hostname));

		if (vm_rpc_send_rpci_tx(sc, "SetGuestInfo  %d %s",
		    VM_GUEST_INFO_DNS_NAME, sc->sc_hostname) != 0) {
			DPRINTF("%s: unable to set hostname", DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}
	}

	if (sc->sc_set_guest_os == 0) {
		if (vm_rpc_send_rpci_tx(sc, "SetGuestInfo  %d %s %s %s",
		    VM_GUEST_INFO_OS_NAME_FULL,
		    ostype, osrelease, osversion) != 0) {
			DPRINTF("%s: unable to set full guest OS", DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}

		/*
		 * Host doesn't like it if we send an OS name it doesn't
		 * recognise, so use the closest match, which happens
		 * to be FreeBSD.
		 */

		if (vm_rpc_send_rpci_tx(sc, "SetGuestInfo  %d %s",
		    VM_GUEST_INFO_OS_NAME, "FreeBSD") != 0) {
			DPRINTF("%s: unable to set guest OS", DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}

		sc->sc_set_guest_os = 1;
	}

	task_add(systq, &sc->sc_nicinfo_task);
}

void
vmt_tick_hook(struct device *self)
{
	struct vmt_softc *sc = (struct vmt_softc *)self;

	timeout_set(&sc->sc_tick, vmt_tick, sc);
	vmt_tick(sc);
}

void
vmt_tick(void *xarg)
{
	struct vmt_softc *sc = xarg;
	struct vm_backdoor frame;
	struct timeval *guest = &sc->sc_sensor.tv;
	struct timeval host, diff;

	microtime(guest);

	bzero(&frame, sizeof(frame));
	frame.eax.word = VM_MAGIC;
	frame.ecx.part.low = VM_CMD_GET_TIME_FULL;
	frame.edx.part.low  = VM_PORT_CMD;
	vm_cmd(&frame);

	if (frame.eax.word != 0xffffffff) {
		host.tv_sec = ((uint64_t)frame.esi.word << 32) | frame.edx.word;
		host.tv_usec = frame.ebx.word;

		timersub(guest, &host, &diff);

		sc->sc_sensor.value = (u_int64_t)diff.tv_sec * 1000000000LL +
		    (u_int64_t)diff.tv_usec * 1000LL;
		sc->sc_sensor.status = SENSOR_S_OK;
	} else {
		sc->sc_sensor.status = SENSOR_S_UNKNOWN;
	}

	vmt_update_guest_info(sc);
	vmt_update_guest_uptime(sc);

	timeout_add_sec(&sc->sc_tick, 15);
}

void
vmt_tclo_state_change_success(struct vmt_softc *sc, int success, char state)
{
	if (vm_rpc_send_rpci_tx(sc, "tools.os.statechange.status %d %d",
	    success, state) != 0) {
		DPRINTF("%s: unable to send state change result\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_do_shutdown(struct vmt_softc *sc)
{
	vmt_tclo_state_change_success(sc, 1, VM_STATE_CHANGE_HALT);
	vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK);
	pvbus_shutdown(&sc->sc_dev);
}

void
vmt_do_reboot(struct vmt_softc *sc)
{
	vmt_tclo_state_change_success(sc, 1, VM_STATE_CHANGE_REBOOT);
	vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK);
	pvbus_reboot(&sc->sc_dev);
}

void
vmt_shutdown(void *arg)
{
	struct vmt_softc *sc = arg;

	if (vm_rpc_send_rpci_tx(sc,
	    "tools.capability.hgfs_server toolbox 0") != 0) {
		DPRINTF("%s: failed to disable hgfs server capability\n",
		    DEVNAME(sc));
	}

	if (vm_rpc_send(&sc->sc_tclo_rpc, NULL, 0) != 0) {
		DPRINTF("%s: failed to send shutdown ping\n", DEVNAME(sc));
	}

	vm_rpc_close(&sc->sc_tclo_rpc);
}

void
vmt_tclo_reset(struct vmt_softc *sc)
{
	if (sc->sc_rpc_error != 0) {
		DPRINTF("%s: resetting rpc\n", DEVNAME(sc));
		vm_rpc_close(&sc->sc_tclo_rpc);

		/* reopen and send the reset reply next time around */
		sc->sc_rpc_error = 1;
		return;
	}

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_RESET_REPLY) != 0) {
		DPRINTF("%s: failed to send reset reply\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_ping(struct vmt_softc *sc)
{
	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK) != 0) {
		DPRINTF("%s: error sending ping response\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_halt(struct vmt_softc *sc)
{
	vmt_do_shutdown(sc);
}

void
vmt_tclo_reboot(struct vmt_softc *sc)
{
	vmt_do_reboot(sc);
}

void
vmt_tclo_poweron(struct vmt_softc *sc)
{
	vmt_tclo_state_change_success(sc, 1, VM_STATE_CHANGE_POWERON);

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK) != 0) {
		DPRINTF("%s: error sending poweron response\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_suspend(struct vmt_softc *sc)
{
	log(LOG_KERN | LOG_NOTICE,
	    "VMware guest entering suspended state\n");

	suspend_randomness();

	vmt_tclo_state_change_success(sc, 1, VM_STATE_CHANGE_SUSPEND);
	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK) != 0) {
		DPRINTF("%s: error sending suspend response\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_resume(struct vmt_softc *sc)
{
	log(LOG_KERN | LOG_NOTICE,
	    "VMware guest resuming from suspended state\n");

	/* force guest info update */
	vmt_clear_guest_info(sc);
	vmt_update_guest_info(sc);
	vmt_resume();

	vmt_tclo_state_change_success(sc, 1, VM_STATE_CHANGE_RESUME);
	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK) != 0) {
		DPRINTF("%s: error sending resume response\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_capreg(struct vmt_softc *sc)
{
	/* don't know if this is important at all */
	if (vm_rpc_send_rpci_tx(sc,
	    "vmx.capability.unified_loop toolbox") != 0) {
		DPRINTF("%s: unable to set unified loop\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}

	if (vm_rpci_response_successful(sc) == 0) {
		DPRINTF("%s: host rejected unified loop setting\n",
		    DEVNAME(sc));
	}

	/* the trailing space is apparently important here */
	if (vm_rpc_send_rpci_tx(sc,
	    "tools.capability.statechange ") != 0) {
		DPRINTF("%s: unable to send statechange capability\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}

	if (vm_rpci_response_successful(sc) == 0) {
		DPRINTF("%s: host rejected statechange capability\n",
		    DEVNAME(sc));
	}

	if (vm_rpc_send_rpci_tx(sc, "tools.set.version %u",
	    VM_VERSION_UNMANAGED) != 0) {
		DPRINTF("%s: unable to set tools version\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}

	vmt_clear_guest_info(sc);
	vmt_update_guest_uptime(sc);

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, VM_RPC_REPLY_OK) != 0) {
		DPRINTF("%s: error sending capabilities_register"
		    " response\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_broadcastip(struct vmt_softc *sc)
{
	struct ifnet *iface;
	struct sockaddr_in *guest_ip;
	char ip[INET_ADDRSTRLEN];

	/* find first available ipv4 address */
	guest_ip = NULL;

	NET_LOCK_SHARED();
	TAILQ_FOREACH(iface, &ifnetlist, if_list) {
		struct ifaddr *iface_addr;

		/* skip loopback */
		if (strncmp(iface->if_xname, "lo", 2) == 0 &&
		    iface->if_xname[2] >= '0' &&
		    iface->if_xname[2] <= '9') {
			continue;
		}

		TAILQ_FOREACH(iface_addr, &iface->if_addrlist,
		    ifa_list) {
			if (iface_addr->ifa_addr->sa_family != AF_INET)
				continue;

			guest_ip = satosin(iface_addr->ifa_addr);
			inet_ntop(AF_INET, &guest_ip->sin_addr, ip,
			    sizeof(ip));
			break;
		}
	}
	NET_UNLOCK_SHARED();

	if (guest_ip != NULL) {
		if (vm_rpc_send_rpci_tx(sc, "info-set guestinfo.ip %s",
		    ip) != 0) {
			DPRINTF("%s: unable to send guest IP address\n",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}

		if (vm_rpc_send_str(&sc->sc_tclo_rpc,
		    VM_RPC_REPLY_OK) != 0) {
			DPRINTF("%s: error sending broadcastIP"
			    " response\n", DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}
	} else {
		if (vm_rpc_send_str(&sc->sc_tclo_rpc,
		    VM_RPC_REPLY_ERROR_IP_ADDR) != 0) {
			DPRINTF("%s: error sending broadcastIP"
			    " error response\n", DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}
	}
}

void
vmt_set_backup_status(struct vmt_softc *sc, const char *state, int code,
    const char *desc)
{
	if (vm_rpc_send_rpci_tx(sc, "vmbackup.eventSet %s %d %s",
	    state, code, desc) != 0) {
		DPRINTF("%s: setting backup status failed\n", DEVNAME(sc));
	}
}

void
vmt_quiesce_task(void *data)
{
	struct vmt_softc *sc = data;
	int err;

	DPRINTF("%s: quiescing filesystems for backup\n", DEVNAME(sc));
	err = vfs_stall(curproc, 1);
	if (err != 0) {
		printf("%s: unable to quiesce filesystems\n", DEVNAME(sc));
		vfs_stall(curproc, 0);

		vmt_set_backup_status(sc, "req.aborted", VM_BACKUP_SYNC_ERROR,
		    "vfs_stall failed");
		vmt_set_backup_status(sc, "req.done", VM_BACKUP_SUCCESS, "");
		sc->sc_quiesce = 0;
		return;
	}

	DPRINTF("%s: filesystems quiesced\n", DEVNAME(sc));
	vmt_set_backup_status(sc, "prov.snapshotCommit", VM_BACKUP_SUCCESS, "");
}

void
vmt_quiesce_done_task(void *data)
{
	struct vmt_softc *sc = data;

	vfs_stall(curproc, 0);

	if (sc->sc_quiesce == -1)
		vmt_set_backup_status(sc, "req.aborted", VM_BACKUP_REMOTE_ABORT,
		    "");

	vmt_set_backup_status(sc, "req.done", VM_BACKUP_SUCCESS, "");
	sc->sc_quiesce = 0;
}

void
vmt_tclo_abortbackup(struct vmt_softc *sc)
{
	const char *reply = VM_RPC_REPLY_OK;

	if (sc->sc_quiesce > 0) {
		DPRINTF("%s: aborting backup\n", DEVNAME(sc));
		sc->sc_quiesce = -1;
		task_set(&sc->sc_quiesce_task, vmt_quiesce_done_task, sc);
		task_add(systq, &sc->sc_quiesce_task);
	} else {
		DPRINTF("%s: can't abort, no backup in progress\n",
		    DEVNAME(sc));
		reply = VM_RPC_REPLY_ERROR;
	}

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, reply) != 0) {
		DPRINTF("%s: error sending vmbackup.abort reply\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_startbackup(struct vmt_softc *sc)
{
	const char *reply = VM_RPC_REPLY_OK;

	if (sc->sc_quiesce == 0) {
		DPRINTF("%s: starting quiesce\n", DEVNAME(sc));
		vmt_set_backup_status(sc, "reset", VM_BACKUP_SUCCESS, "");

		task_set(&sc->sc_quiesce_task, vmt_quiesce_task, sc);
		task_add(systq, &sc->sc_quiesce_task);
		sc->sc_quiesce = 1;
	} else {
		DPRINTF("%s: can't start backup, already in progress\n",
		    DEVNAME(sc));
		reply = VM_RPC_REPLY_ERROR;
	}

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, reply) != 0) {
		DPRINTF("%s: error sending vmbackup.start reply\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

void
vmt_tclo_backupdone(struct vmt_softc *sc)
{
	const char *reply = VM_RPC_REPLY_OK;
	if (sc->sc_quiesce > 0) {
		DPRINTF("%s: backup complete\n", DEVNAME(sc));
		task_set(&sc->sc_quiesce_task, vmt_quiesce_done_task, sc);
		task_add(systq, &sc->sc_quiesce_task);
	} else {
		DPRINTF("%s: got backup complete, but not doing a backup\n",
		    DEVNAME(sc));
		reply = VM_RPC_REPLY_ERROR;
	}

	if (vm_rpc_send_str(&sc->sc_tclo_rpc, reply) != 0) {
		DPRINTF("%s: error sending vmbackup.snapshotDone reply\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
	}
}

int
vmt_tclo_process(struct vmt_softc *sc, const char *name)
{
	int i;

	/* Search for rpc command and call handler */
	for (i = 0; vmt_tclo_rpc[i].name != NULL; i++) {
		if (strcmp(vmt_tclo_rpc[i].name, sc->sc_rpc_buf) == 0) {
			vmt_tclo_rpc[i].cb(sc);
			return (0);
		}
	}

	DPRINTF("%s: unknown command: \"%s\"\n", DEVNAME(sc), name);

	return (-1);
}

void
vmt_tclo_tick(void *xarg)
{
	struct vmt_softc *sc = xarg;
	u_int32_t rlen;
	u_int16_t ack;
	int delay;

	/* By default, poll every second for new messages */
	delay = 1;

	if (sc->sc_quiesce > 0) {
		/* abort quiesce if it's taking too long */
		if (sc->sc_quiesce++ == VM_BACKUP_TIMEOUT) {
			printf("%s: aborting quiesce\n", DEVNAME(sc));
			sc->sc_quiesce = -1;
			task_set(&sc->sc_quiesce_task, vmt_quiesce_done_task,
			    sc);
			task_add(systq, &sc->sc_quiesce_task);
		} else
			vmt_set_backup_status(sc, "req.keepAlive",
			    VM_BACKUP_SUCCESS, "");
	}

	/* reopen tclo channel if it's currently closed */
	if (sc->sc_tclo_rpc.channel == 0 &&
	    sc->sc_tclo_rpc.cookie1 == 0 &&
	    sc->sc_tclo_rpc.cookie2 == 0) {
		if (vm_rpc_open(&sc->sc_tclo_rpc, VM_RPC_OPEN_TCLO) != 0) {
			DPRINTF("%s: unable to reopen TCLO channel\n",
			    DEVNAME(sc));
			delay = 15;
			goto out;
		}

		if (vm_rpc_send_str(&sc->sc_tclo_rpc,
		    VM_RPC_RESET_REPLY) != 0) {
			DPRINTF("%s: failed to send reset reply\n",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
			goto out;
		} else {
			sc->sc_rpc_error = 0;
		}
	}

	if (sc->sc_tclo_ping) {
		if (vm_rpc_send(&sc->sc_tclo_rpc, NULL, 0) != 0) {
			DPRINTF("%s: failed to send TCLO outgoing ping\n",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
			goto out;
		}
	}

	if (vm_rpc_get_length(&sc->sc_tclo_rpc, &rlen, &ack) != 0) {
		DPRINTF("%s: failed to get length of incoming TCLO data\n",
		    DEVNAME(sc));
		sc->sc_rpc_error = 1;
		goto out;
	}

	if (rlen == 0) {
		sc->sc_tclo_ping = 1;
		goto out;
	}

	if (rlen >= VMT_RPC_BUFLEN) {
		rlen = VMT_RPC_BUFLEN - 1;
	}
	if (vm_rpc_get_data(&sc->sc_tclo_rpc, sc->sc_rpc_buf, rlen, ack) != 0) {
		DPRINTF("%s: failed to get incoming TCLO data\n", DEVNAME(sc));
		sc->sc_rpc_error = 1;
		goto out;
	}
	sc->sc_tclo_ping = 0;

	/* The VM host can queue multiple messages; continue without delay */
	delay = 0;

	if (vmt_tclo_process(sc, sc->sc_rpc_buf) != 0) {
		if (vm_rpc_send_str(&sc->sc_tclo_rpc,
		    VM_RPC_REPLY_ERROR) != 0) {
			DPRINTF("%s: error sending unknown command reply\n",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}
	}

	if (sc->sc_rpc_error == 1) {
		/* On error, give time to recover and wait a second */
		delay = 1;
	}

out:
	timeout_add_sec(&sc->sc_tclo_tick, delay);
}

size_t
vmt_xdr_ifaddr(struct ifaddr *ifa, char *data)
{
	struct sockaddr_in *sin;
	struct vm_nicinfo_addr_v4 v4;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct vm_nicinfo_addr_v6 v6;
#endif

	/* skip loopback addresses and anything that isn't ipv4/v6 */
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		sin = satosin(ifa->ifa_addr);
		if ((ntohl(sin->sin_addr.s_addr) >>
		    IN_CLASSA_NSHIFT) != IN_LOOPBACKNET) {
			if (data != NULL) {
				memset(&v4, 0, sizeof(v4));
				htobem32(&v4.v4_addr_type,
				    VM_NICINFO_ADDR_IPV4);
				htobem32(&v4.v4_addr_len,
				    sizeof(struct in_addr));
				memcpy(&v4.v4_addr, &sin->sin_addr.s_addr,
				    sizeof(struct in_addr));
				htobem32(&v4.v4_prefix_len,
				    rtable_satoplen(AF_INET, ifa->ifa_netmask));
				memcpy(data, &v4, sizeof(v4));
			}
			return (sizeof (v4));
		}
		break;

#ifdef INET6
	case AF_INET6:
		sin6 = satosin6(ifa->ifa_addr);
		if (!IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr) &&
		    !IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
			if (data != NULL) {
				memset(&v6, 0, sizeof(v6));
				htobem32(&v6.v6_addr_type,
				    VM_NICINFO_ADDR_IPV6);
				htobem32(&v6.v6_addr_len,
				    sizeof(sin6->sin6_addr));
				memcpy(&v6.v6_addr, &sin6->sin6_addr,
				    sizeof(sin6->sin6_addr));
				htobem32(&v6.v6_prefix_len,
				    rtable_satoplen(AF_INET6,
				        ifa->ifa_netmask));
				memcpy(data, &v6, sizeof(v6));
			}
			return (sizeof (v6));
		}
		break;
#endif

	default:
		break;
	}

	return (0);
}

size_t
vmt_xdr_nic_entry(struct ifnet *iface, char *data)
{
	struct ifaddr *iface_addr;
	struct sockaddr_dl *sdl;
	struct vm_nicinfo_nic nic;
	struct vm_nicinfo_nic_nomac nnic;
	char *nicdata;
	const char *mac;
	size_t addrsize, total;
	int addrs;

	total = 0;
	addrs = 0;

	/* work out if we have a mac address */
	sdl = iface->if_sadl;
	if (sdl != NULL && sdl->sdl_alen &&
	    (sdl->sdl_type == IFT_ETHER || sdl->sdl_type == IFT_CARP))
		mac = ether_sprintf(sdl->sdl_data + sdl->sdl_nlen);
	else
		mac = NULL;

	if (data != NULL) {
		nicdata = data;
		if (mac != NULL)
			data += sizeof(nic);
		else
			data += sizeof(nnic);
	}

	TAILQ_FOREACH(iface_addr, &iface->if_addrlist, ifa_list) {
		addrsize = vmt_xdr_ifaddr(iface_addr, data);
		if (addrsize == 0)
			continue;

		if (data != NULL)
			data += addrsize;
		total += addrsize;
		addrs++;
		if (addrs == VM_NICINFO_MAX_ADDRS)
			break;
	}

	if (addrs == 0)
		return (0);

	if (data != NULL) {
		/* fill in mac address, if any */
		if (mac != NULL) {
			memset(&nic, 0, sizeof(nic));
			htobem32(&nic.ni_mac_len, strlen(mac));
			strncpy(nic.ni_mac, mac, VM_NICINFO_MAC_LEN);
			htobem32(&nic.ni_num_addrs, addrs);
			memcpy(nicdata, &nic, sizeof(nic));
		} else {
			nnic.nn_mac_len = 0;
			htobem32(&nnic.nn_num_addrs, addrs);
			memcpy(nicdata, &nnic, sizeof(nnic));
		}

		/* we don't actually set anything in vm_nicinfo_nic_post */
	}

	if (mac != NULL)
		total += sizeof(nic);
	else
		total += sizeof(nnic);
	total += sizeof(struct vm_nicinfo_nic_post);
	return (total);
}

size_t
vmt_xdr_nic_info(char *data)
{
	struct ifnet *iface;
	struct vm_nicinfo_nic_list nl;
	size_t total, nictotal;
	char *listdata = NULL;
	int nics;

	NET_ASSERT_LOCKED();

	total = sizeof(nl);
	if (data != NULL) {
		listdata = data;
		data += sizeof(nl);
	}

	nics = 0;
	TAILQ_FOREACH(iface, &ifnetlist, if_list) {
		nictotal = vmt_xdr_nic_entry(iface, data);
		if (nictotal == 0)
			continue;
		
		if (data != NULL)
			data += nictotal;

		total += nictotal;
		nics++;
		if (nics == VM_NICINFO_MAX_NICS)
			break;
	}

	if (listdata != NULL) {
		memset(&nl, 0, sizeof(nl));
		htobem32(&nl.nl_version, VM_NICINFO_VERSION);
		htobem32(&nl.nl_nic_list, 1);
		htobem32(&nl.nl_num_nics, nics);
		memcpy(listdata, &nl, sizeof(nl));
	}

	/* we don't actually set anything in vm_nicinfo_nic_list_post */
	total += sizeof(struct vm_nicinfo_nic_list_post);

	return (total);
}

void
vmt_nicinfo_task(void *data)
{
	struct vmt_softc *sc = data;
	size_t nic_info_size;
	char *nic_info;

	NET_LOCK();

	nic_info_size = vmt_xdr_nic_info(NULL) + sizeof(VM_NICINFO_CMD) - 1;
	nic_info = malloc(nic_info_size, M_DEVBUF, M_WAITOK | M_ZERO);

	strncpy(nic_info, VM_NICINFO_CMD, nic_info_size);
	vmt_xdr_nic_info(nic_info + sizeof(VM_NICINFO_CMD) - 1);

	NET_UNLOCK();

	if (nic_info_size != sc->sc_nic_info_size ||
	    (memcmp(nic_info, sc->sc_nic_info, nic_info_size) != 0)) {
		if (vm_rpc_send_rpci_tx_buf(sc, nic_info,
		    nic_info_size) != 0) {
			DPRINTF("%s: unable to send nic info",
			    DEVNAME(sc));
			sc->sc_rpc_error = 1;
		}

		free(sc->sc_nic_info, M_DEVBUF, sc->sc_nic_info_size);
		sc->sc_nic_info = nic_info;
		sc->sc_nic_info_size = nic_info_size;
	} else {
		free(nic_info, M_DEVBUF, nic_info_size);
	}
}

#define BACKDOOR_OP_I386(op, frame)		\
	__asm__ volatile (			\
		"pushal;"			\
		"pushl %%eax;"			\
		"movl 0x18(%%eax), %%ebp;"	\
		"movl 0x14(%%eax), %%edi;"	\
		"movl 0x10(%%eax), %%esi;"	\
		"movl 0x0c(%%eax), %%edx;"	\
		"movl 0x08(%%eax), %%ecx;"	\
		"movl 0x04(%%eax), %%ebx;"	\
		"movl 0x00(%%eax), %%eax;"	\
		op				\
		"xchgl %%eax, 0x00(%%esp);"	\
		"movl %%ebp, 0x18(%%eax);"	\
		"movl %%edi, 0x14(%%eax);"	\
		"movl %%esi, 0x10(%%eax);"	\
		"movl %%edx, 0x0c(%%eax);"	\
		"movl %%ecx, 0x08(%%eax);"	\
		"movl %%ebx, 0x04(%%eax);"	\
		"popl 0x00(%%eax);"		\
		"popal;"			\
		::"a"(frame)			\
	)

#define BACKDOOR_OP_AMD64(op, frame)		\
	__asm__ volatile (			\
		"pushq %%rbp;			\n\t" \
		"pushq %%rax;			\n\t" \
		"movq 0x30(%%rax), %%rbp;	\n\t" \
		"movq 0x28(%%rax), %%rdi;	\n\t" \
		"movq 0x20(%%rax), %%rsi;	\n\t" \
		"movq 0x18(%%rax), %%rdx;	\n\t" \
		"movq 0x10(%%rax), %%rcx;	\n\t" \
		"movq 0x08(%%rax), %%rbx;	\n\t" \
		"movq 0x00(%%rax), %%rax;	\n\t" \
		op				"\n\t" \
		"xchgq %%rax, 0x00(%%rsp);	\n\t" \
		"movq %%rbp, 0x30(%%rax);	\n\t" \
		"movq %%rdi, 0x28(%%rax);	\n\t" \
		"movq %%rsi, 0x20(%%rax);	\n\t" \
		"movq %%rdx, 0x18(%%rax);	\n\t" \
		"movq %%rcx, 0x10(%%rax);	\n\t" \
		"movq %%rbx, 0x08(%%rax);	\n\t" \
		"popq 0x00(%%rax);		\n\t" \
		"popq %%rbp;			\n\t" \
		: /* No outputs. */ : "a" (frame) \
		  /* No pushal on amd64 so warn gcc about the clobbered registers. */ \
		: "rbx", "rcx", "rdx", "rdi", "rsi", "cc", "memory" \
	)


#ifdef __i386__
#define BACKDOOR_OP(op, frame) BACKDOOR_OP_I386(op, frame)
#else
#define BACKDOOR_OP(op, frame) BACKDOOR_OP_AMD64(op, frame)
#endif

void
vm_cmd(struct vm_backdoor *frame)
{
	BACKDOOR_OP("inl %%dx, %%eax;", frame);
}

void
vm_ins(struct vm_backdoor *frame)
{
	BACKDOOR_OP("cld;\n\trep insb;", frame);
}

void
vm_outs(struct vm_backdoor *frame)
{
	BACKDOOR_OP("cld;\n\trep outsb;", frame);
}

int
vm_rpc_open(struct vm_rpc *rpc, uint32_t proto)
{
	struct vm_backdoor frame;

	bzero(&frame, sizeof(frame));
	frame.eax.word      = VM_MAGIC;
	frame.ebx.word      = proto | VM_RPC_FLAG_COOKIE;
	frame.ecx.part.low  = VM_CMD_RPC;
	frame.ecx.part.high = VM_RPC_OPEN;
	frame.edx.part.low  = VM_PORT_CMD;
	frame.edx.part.high = 0;

	vm_cmd(&frame);

	if (frame.ecx.part.high != 1 || frame.edx.part.low != 0) {
		/* open-vm-tools retries without VM_RPC_FLAG_COOKIE here.. */
		DPRINTF("vmware: open failed, eax=%08x, ecx=%08x, edx=%08x\n",
		    frame.eax.word, frame.ecx.word, frame.edx.word);
		return EIO;
	}

	rpc->channel = frame.edx.part.high;
	rpc->cookie1 = frame.esi.word;
	rpc->cookie2 = frame.edi.word;

	return 0;
}

int
vm_rpc_close(struct vm_rpc *rpc)
{
	struct vm_backdoor frame;

	bzero(&frame, sizeof(frame));
	frame.eax.word      = VM_MAGIC;
	frame.ebx.word      = 0;
	frame.ecx.part.low  = VM_CMD_RPC;
	frame.ecx.part.high = VM_RPC_CLOSE;
	frame.edx.part.low  = VM_PORT_CMD;
	frame.edx.part.high = rpc->channel;
	frame.edi.word      = rpc->cookie2;
	frame.esi.word      = rpc->cookie1;

	vm_cmd(&frame);

	if (frame.ecx.part.high == 0 || frame.ecx.part.low != 0) {
		DPRINTF("vmware: close failed, eax=%08x, ecx=%08x\n",
		    frame.eax.word, frame.ecx.word);
		return EIO;
	}

	rpc->channel = 0;
	rpc->cookie1 = 0;
	rpc->cookie2 = 0;

	return 0;
}

int
vm_rpc_send(const struct vm_rpc *rpc, const uint8_t *buf, uint32_t length)
{
	struct vm_backdoor frame;

	/* Send the length of the command. */
	bzero(&frame, sizeof(frame));
	frame.eax.word = VM_MAGIC;
	frame.ebx.word = length;
	frame.ecx.part.low  = VM_CMD_RPC;
	frame.ecx.part.high = VM_RPC_SET_LENGTH;
	frame.edx.part.low  = VM_PORT_CMD;
	frame.edx.part.high = rpc->channel;
	frame.esi.word = rpc->cookie1;
	frame.edi.word = rpc->cookie2;

	vm_cmd(&frame);

	if ((frame.ecx.part.high & VM_RPC_REPLY_SUCCESS) == 0) {
		DPRINTF("vmware: sending length failed, eax=%08x, ecx=%08x\n",
		    frame.eax.word, frame.ecx.word);
		return EIO;
	}

	if (length == 0)
		return 0; /* Only need to poke once if command is null. */

	/* Send the command using enhanced RPC. */
	bzero(&frame, sizeof(frame));
	frame.eax.word = VM_MAGIC;
	frame.ebx.word = VM_RPC_ENH_DATA;
	frame.ecx.word = length;
	frame.edx.part.low  = VM_PORT_RPC;
	frame.edx.part.high = rpc->channel;
	frame.ebp.word = rpc->cookie1;
	frame.edi.word = rpc->cookie2;
#ifdef __amd64__
	frame.esi.quad = (uint64_t)buf;
#else
	frame.esi.word = (uint32_t)buf;
#endif

	vm_outs(&frame);

	if (frame.ebx.word != VM_RPC_ENH_DATA) {
		/* open-vm-tools retries on VM_RPC_REPLY_CHECKPOINT */
		DPRINTF("vmware: send failed, ebx=%08x\n", frame.ebx.word);
		return EIO;
	}

	return 0;
}

int
vm_rpc_send_str(const struct vm_rpc *rpc, const uint8_t *str)
{
	return vm_rpc_send(rpc, str, strlen(str));
}

int
vm_rpc_get_data(const struct vm_rpc *rpc, char *data, uint32_t length,
    uint16_t dataid)
{
	struct vm_backdoor frame;

	/* Get data using enhanced RPC. */
	bzero(&frame, sizeof(frame));
	frame.eax.word      = VM_MAGIC;
	frame.ebx.word      = VM_RPC_ENH_DATA;
	frame.ecx.word      = length;
	frame.edx.part.low  = VM_PORT_RPC;
	frame.edx.part.high = rpc->channel;
	frame.esi.word      = rpc->cookie1;
#ifdef __amd64__
	frame.edi.quad      = (uint64_t)data;
#else
	frame.edi.word      = (uint32_t)data;
#endif
	frame.ebp.word      = rpc->cookie2;

	vm_ins(&frame);

	/* NUL-terminate the data */
	data[length] = '\0';

	if (frame.ebx.word != VM_RPC_ENH_DATA) {
		DPRINTF("vmware: get data failed, ebx=%08x\n",
		    frame.ebx.word);
		return EIO;
	}

	/* Acknowledge data received. */
	bzero(&frame, sizeof(frame));
	frame.eax.word      = VM_MAGIC;
	frame.ebx.word      = dataid;
	frame.ecx.part.low  = VM_CMD_RPC;
	frame.ecx.part.high = VM_RPC_GET_END;
	frame.edx.part.low  = VM_PORT_CMD;
	frame.edx.part.high = rpc->channel;
	frame.esi.word      = rpc->cookie1;
	frame.edi.word      = rpc->cookie2;

	vm_cmd(&frame);

	if (frame.ecx.part.high == 0) {
		DPRINTF("vmware: ack data failed, eax=%08x, ecx=%08x\n",
		    frame.eax.word, frame.ecx.word);
		return EIO;
	}

	return 0;
}

int
vm_rpc_get_length(const struct vm_rpc *rpc, uint32_t *length, uint16_t *dataid)
{
	struct vm_backdoor frame;

	bzero(&frame, sizeof(frame));
	frame.eax.word      = VM_MAGIC;
	frame.ebx.word      = 0;
	frame.ecx.part.low  = VM_CMD_RPC;
	frame.ecx.part.high = VM_RPC_GET_LENGTH;
	frame.edx.part.low  = VM_PORT_CMD;
	frame.edx.part.high = rpc->channel;
	frame.esi.word      = rpc->cookie1;
	frame.edi.word      = rpc->cookie2;

	vm_cmd(&frame);

	if ((frame.ecx.part.high & VM_RPC_REPLY_SUCCESS) == 0) {
		DPRINTF("vmware: get length failed, eax=%08x, ecx=%08x\n",
		    frame.eax.word, frame.ecx.word);
		return EIO;
	}
	if ((frame.ecx.part.high & VM_RPC_REPLY_DORECV) == 0) {
		*length = 0;
		*dataid = 0;
	} else {
		*length = frame.ebx.word;
		*dataid = frame.edx.part.high;
	}

	return 0;
}

int
vm_rpci_response_successful(struct vmt_softc *sc)
{
	return (sc->sc_rpc_buf[0] == '1' && sc->sc_rpc_buf[1] == ' ');
}

int
vm_rpc_send_rpci_tx_buf(struct vmt_softc *sc, const uint8_t *buf,
    uint32_t length)
{
	struct vm_rpc rpci;
	u_int32_t rlen;
	u_int16_t ack;
	int result = 0;

	if (vm_rpc_open(&rpci, VM_RPC_OPEN_RPCI) != 0) {
		DPRINTF("%s: rpci channel open failed\n", DEVNAME(sc));
		return EIO;
	}

	if (vm_rpc_send(&rpci, buf, length) != 0) {
		DPRINTF("%s: unable to send rpci command\n", DEVNAME(sc));
		result = EIO;
		goto out;
	}

	if (vm_rpc_get_length(&rpci, &rlen, &ack) != 0) {
		DPRINTF("%s: failed to get length of rpci response data\n",
		    DEVNAME(sc));
		result = EIO;
		goto out;
	}

	if (rlen > 0) {
		if (rlen >= VMT_RPC_BUFLEN) {
			rlen = VMT_RPC_BUFLEN - 1;
		}

		if (vm_rpc_get_data(&rpci, sc->sc_rpc_buf, rlen, ack) != 0) {
			DPRINTF("%s: failed to get rpci response data\n",
			    DEVNAME(sc));
			result = EIO;
			goto out;
		}
	}

out:
	if (vm_rpc_close(&rpci) != 0) {
		DPRINTF("%s: unable to close rpci channel\n", DEVNAME(sc));
	}

	return result;
}

int
vm_rpc_send_rpci_tx(struct vmt_softc *sc, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(sc->sc_rpc_buf, VMT_RPC_BUFLEN, fmt, args);
	va_end(args);

	if (len >= VMT_RPC_BUFLEN) {
		DPRINTF("%s: rpci command didn't fit in buffer\n", DEVNAME(sc));
		return EIO;
	}

	return vm_rpc_send_rpci_tx_buf(sc, sc->sc_rpc_buf, len);
}

#if 0
	struct vm_backdoor frame;

	bzero(&frame, sizeof(frame));

	frame.eax.word = VM_MAGIC;
	frame.ecx.part.low = VM_CMD_GET_VERSION;
	frame.edx.part.low  = VM_PORT_CMD;

	printf("\n");
	printf("eax 0x%08x\n", frame.eax.word);
	printf("ebx 0x%08x\n", frame.ebx.word);
	printf("ecx 0x%08x\n", frame.ecx.word);
	printf("edx 0x%08x\n", frame.edx.word);
	printf("ebp 0x%08x\n", frame.ebp.word);
	printf("edi 0x%08x\n", frame.edi.word);
	printf("esi 0x%08x\n", frame.esi.word);

	vm_cmd(&frame);

	printf("-\n");
	printf("eax 0x%08x\n", frame.eax.word);
	printf("ebx 0x%08x\n", frame.ebx.word);
	printf("ecx 0x%08x\n", frame.ecx.word);
	printf("edx 0x%08x\n", frame.edx.word);
	printf("ebp 0x%08x\n", frame.ebp.word);
	printf("edi 0x%08x\n", frame.edi.word);
	printf("esi 0x%08x\n", frame.esi.word);
#endif

/*
 * Notes on tracing backdoor activity in vmware-guestd:
 *
 * - Find the addresses of the inl / rep insb / rep outsb
 *   instructions used to perform backdoor operations.
 *   One way to do this is to disassemble vmware-guestd:
 *
 *   $ objdump -S /emul/freebsd/sbin/vmware-guestd > vmware-guestd.S
 *
 *   and search for '<tab>in ' in the resulting file.  The rep insb and
 *   rep outsb code is directly below that.
 *
 * - Run vmware-guestd under gdb, setting up breakpoints as follows:
 *   (the addresses shown here are the ones from VMware-server-1.0.10-203137,
 *   the last version that actually works in FreeBSD emulation on OpenBSD)
 *
 * break *0x805497b   (address of 'in' instruction)
 * commands 1
 * silent
 * echo INOUT\n
 * print/x $ecx
 * print/x $ebx
 * print/x $edx
 * continue
 * end
 * break *0x805497c   (address of instruction after 'in')
 * commands 2
 * silent
 * echo ===\n
 * print/x $ecx
 * print/x $ebx
 * print/x $edx
 * echo \n
 * continue
 * end
 * break *0x80549b7   (address of instruction before 'rep insb')
 * commands 3
 * silent
 * set variable $inaddr = $edi
 * set variable $incount = $ecx
 * continue
 * end
 * break *0x80549ba   (address of instruction after 'rep insb')
 * commands 4
 * silent
 * echo IN\n
 * print $incount
 * x/s $inaddr
 * echo \n
 * continue
 * end
 * break *0x80549fb    (address of instruction before 'rep outsb')
 * commands 5
 * silent
 * echo OUT\n
 * print $ecx
 * x/s $esi
 * echo \n
 * continue
 * end
 *
 * This will produce a log of the backdoor operations, including the
 * data sent and received and the relevant register values.  You can then
 * match the register values to the various constants in this file.
 */
