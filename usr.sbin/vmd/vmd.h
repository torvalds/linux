/*	$OpenBSD: vmd.h,v 1.140 2025/08/08 13:40:12 dv Exp $	*/

/*
 * Copyright (c) 2015 Mike Larkin <mlarkin@openbsd.org>
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <dev/vmm/vmm.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet6/in6_var.h>

#include <limits.h>
#include <stdio.h>
#include <pthread.h>

#include "proc.h"

#ifndef VMD_H
#define VMD_H

#define SET(_v, _m)		((_v) |= (_m))
#define CLR(_v, _m)		((_v) &= ~(_m))
#define ISSET(_v, _m)		((_v) & (_m))

#define nitems(_a)      (sizeof((_a)) / sizeof((_a)[0]))

#define CTASSERT(x)	extern char  _ctassert[(x) ? 1 : -1 ] \
			    __attribute__((__unused__))

#define KB(x)	(x * 1024UL)
#define MB(x)	(x * 1024UL * 1024UL)
#define GB(x)	(x * 1024UL * 1024UL * 1024UL)

#define VMD_USER		"_vmd"
#define VMD_CONF		"/etc/vm.conf"
#define SOCKET_NAME		"/var/run/vmd.sock"
#define VMM_NODE		"/dev/vmm"
#define PSP_NODE		"/dev/psp"
#define VM_DEFAULT_BIOS		"/etc/firmware/vmm-bios"
#define VM_DEFAULT_KERNEL	"/bsd"
#define VM_DEFAULT_DEVICE	"hd0a"
#define VM_BOOT_CONF		"/etc/boot.conf"
#define VM_NAME_MAX		64
#define VM_MAX_BASE_PER_DISK	4
#define VM_TTYNAME_MAX		16
#define VM_MAX_DISKS_PER_VM	4
#define VM_MAX_NICS_PER_VM	4

#define VM_PCI_MMIO_BAR_SIZE	0x00010000
#define VM_PCI_IO_BAR_BASE	0x1000
#define VM_PCI_IO_BAR_END	0xFFFF
#define VM_PCI_IO_BAR_SIZE	0x1000

#define MAX_TAP			256
#define NR_BACKLOG		5
#define VMD_SWITCH_TYPE		"bridge"
#define VM_DEFAULT_MEMORY	512 * 1024 * 1024	/* 512 MiB */

#define VMD_DEFAULT_STAGGERED_START_DELAY 30

/* Launch mode identifiers for when a vm fork+exec's. */
#define VMD_LAUNCH_VM		1
#define VMD_LAUNCH_DEV		2

#define VMD_DEVTYPE_NET		'n'
#define VMD_DEVTYPE_DISK	'd'

/* Rate-limit fast reboots */
#define VM_START_RATE_SEC	6	/* min. seconds since last reboot */
#define VM_START_RATE_LIMIT	3	/* max. number of fast reboots */

/* vmd -> vmctl error codes */
#define VMD_BIOS_MISSING	1001
#define VMD_DISK_MISSING	1002
					/* 1003 is obsolete VMD_DISK_INVALID */
#define VMD_VM_STOP_INVALID	1004
#define VMD_CDROM_MISSING	1005
#define VMD_CDROM_INVALID	1006
#define VMD_PARENT_INVALID	1007

#define IMSG_AGENTX_PEERID	(uint32_t)-2

/* Image file signatures */
#define VM_MAGIC_QCOW		"QFI\xfb"

/* 100.64.0.0/10 from rfc6598 (IPv4 Prefix for Shared Address Space) */
#define VMD_DHCP_PREFIX		"100.64.0.0/10"

/* Unique local address for IPv6 */
#define VMD_ULA_PREFIX		"fd00::/8"

enum imsg_type {
	IMSG_VMDOP_START_VM_REQUEST = IMSG_PROC_MAX,
	IMSG_VMDOP_START_VM_CDROM,
	IMSG_VMDOP_START_VM_DISK,
	IMSG_VMDOP_START_VM_IF,
	IMSG_VMDOP_START_VM_END,
	IMSG_VMDOP_START_VM_RESPONSE,
	IMSG_VMDOP_PAUSE_VM,
	IMSG_VMDOP_PAUSE_VM_RESPONSE,
	IMSG_VMDOP_UNPAUSE_VM,
	IMSG_VMDOP_UNPAUSE_VM_RESPONSE,
	IMSG_VMDOP_WAIT_VM_REQUEST,
	IMSG_VMDOP_TERMINATE_VM_REQUEST,
	IMSG_VMDOP_TERMINATE_VM_RESPONSE,
	IMSG_VMDOP_TERMINATE_VM_EVENT,
	IMSG_VMDOP_GET_INFO_VM_REQUEST,
	IMSG_VMDOP_GET_INFO_VM_DATA,
	IMSG_VMDOP_GET_INFO_VM_END_DATA,
	IMSG_VMDOP_LOAD,
	IMSG_VMDOP_RECEIVE_VMM_FD,
	IMSG_VMDOP_RECEIVE_PSP_FD,
	IMSG_VMDOP_RELOAD,
	IMSG_VMDOP_PRIV_IFDESCR,
	IMSG_VMDOP_PRIV_IFADD,
	IMSG_VMDOP_PRIV_IFEXISTS,
	IMSG_VMDOP_PRIV_IFUP,
	IMSG_VMDOP_PRIV_IFDOWN,
	IMSG_VMDOP_PRIV_IFGROUP,
	IMSG_VMDOP_PRIV_IFADDR,
	IMSG_VMDOP_PRIV_IFADDR6,
	IMSG_VMDOP_PRIV_IFRDOMAIN,
	IMSG_VMDOP_PRIV_GET_ADDR,
	IMSG_VMDOP_PRIV_GET_ADDR_RESPONSE,
	IMSG_VMDOP_VM_SHUTDOWN,
	IMSG_VMDOP_VM_REBOOT,
	IMSG_VMDOP_CONFIG,
	IMSG_VMDOP_DONE,
	/* Device Operation Messages */
	IMSG_DEVOP_HOSTMAC,
	IMSG_DEVOP_MSG,
	IMSG_DEVOP_VIONET_MSG,
};

struct vmop_result {
	int			 vmr_result;
	uint32_t		 vmr_id;
	pid_t			 vmr_pid;
	char			 vmr_ttyname[VM_TTYNAME_MAX];
};

struct vmop_info_result {
	struct vm_info_result	 vir_info;
	char			 vir_ttyname[VM_TTYNAME_MAX];
	uid_t			 vir_uid;
	int64_t			 vir_gid;
	unsigned int		 vir_state;
};

struct vmop_id {
	uint32_t		 vid_id;
	char			 vid_name[VMM_MAX_NAME_LEN];
	uid_t			 vid_uid;
	unsigned int		 vid_flags;
#define VMOP_FORCE		0x01
#define VMOP_WAIT		0x02
};

struct vmop_ifreq {
	uint32_t			 vfr_id;
	char				 vfr_name[IF_NAMESIZE];
	char				 vfr_value[VM_NAME_MAX];
	struct sockaddr_storage		 vfr_addr;
	struct sockaddr_storage		 vfr_mask;
};

struct vmop_addr_req {
	uint32_t		 var_vmid;
	unsigned int		 var_nic_idx;
};

struct vmop_addr_result {
	uint32_t		 var_vmid;
	unsigned int		 var_nic_idx;
	uint8_t			 var_addr[ETHER_ADDR_LEN];
};

struct vmop_owner {
	uid_t			 uid;
	int64_t			 gid;
};

struct vmop_create_params {
	struct vm_create_params	 vmc_params;
	unsigned int		 vmc_flags;
#define VMOP_CREATE_CPU		0x01
#define VMOP_CREATE_KERNEL	0x02
#define VMOP_CREATE_MEMORY	0x04
#define VMOP_CREATE_NETWORK	0x08
#define VMOP_CREATE_DISK	0x10
#define VMOP_CREATE_CDROM	0x20
#define VMOP_CREATE_INSTANCE	0x40

	/* same flags; check for access to these resources */
	unsigned int		 vmc_checkaccess;

	/* userland-only part of the create params */
	unsigned int		 vmc_bootdevice;
#define VMBOOTDEV_AUTO		0
#define VMBOOTDEV_DISK		1
#define VMBOOTDEV_CDROM		2
#define VMBOOTDEV_NET		3
	unsigned int		 vmc_ifflags[VM_MAX_NICS_PER_VM];
#define VMIFF_UP		0x01
#define VMIFF_LOCKED		0x02
#define VMIFF_LOCAL		0x04
#define VMIFF_RDOMAIN		0x08
#define VMIFF_OPTMASK		(VMIFF_LOCKED|VMIFF_LOCAL|VMIFF_RDOMAIN)

	size_t			 vmc_ndisks;
	char			 vmc_disks[VM_MAX_DISKS_PER_VM][PATH_MAX];
	unsigned int		 vmc_disktypes[VM_MAX_DISKS_PER_VM];
	unsigned int		 vmc_diskbases[VM_MAX_DISKS_PER_VM];
#define VMDF_RAW		0x01
#define VMDF_QCOW2		0x02

	char			 vmc_cdrom[PATH_MAX];
	int			 vmc_kernel;

	size_t			 vmc_nnics;
	char			 vmc_ifnames[VM_MAX_NICS_PER_VM][IF_NAMESIZE];
	char			 vmc_ifswitch[VM_MAX_NICS_PER_VM][VM_NAME_MAX];
	char			 vmc_ifgroup[VM_MAX_NICS_PER_VM][IF_NAMESIZE];
	unsigned int		 vmc_ifrdomain[VM_MAX_NICS_PER_VM];
	uint8_t			 vmc_macs[VM_MAX_NICS_PER_VM][6];

	struct vmop_owner	 vmc_owner;

	/* instance template params */
	char			 vmc_instance[VMM_MAX_NAME_LEN];
	struct vmop_owner	 vmc_insowner;
	unsigned int		 vmc_insflags;
};

struct vmd_if {
	char			*vif_name;
	char			*vif_switch;
	char			*vif_group;
	int			 vif_fd;
	unsigned int		 vif_rdomain;
	unsigned int		 vif_flags;
	TAILQ_ENTRY(vmd_if)	 vif_entry;
};

struct vmd_switch {
	uint32_t		 sw_id;
	char			*sw_name;
	char			 sw_ifname[IF_NAMESIZE];
	char			*sw_group;
	unsigned int		 sw_rdomain;
	unsigned int		 sw_flags;
	int			 sw_running;
	TAILQ_ENTRY(vmd_switch)	 sw_entry;
};
TAILQ_HEAD(switchlist, vmd_switch);

struct vmd_vm {
	struct vmop_create_params vm_params;
	pid_t			 vm_pid;
	uint32_t		 vm_vmid;
	uint32_t		 vm_sev_handle;
	uint32_t		 vm_sev_asid[VMM_MAX_VCPUS_PER_VM];

#define VM_SEV_NSEGMENTS	128
	size_t			 vm_sev_nmemsegments;
	struct vm_mem_range	 vm_sev_memsegments[VM_SEV_NSEGMENTS];

	int			 vm_kernel;
	char			*vm_kernel_path; /* Used by vm.conf. */

	int			 vm_cdrom;
	int			 vm_disks[VM_MAX_DISKS_PER_VM][VM_MAX_BASE_PER_DISK];
	struct vmd_if		 vm_ifs[VM_MAX_NICS_PER_VM];
	char			 vm_ttyname[VM_TTYNAME_MAX];
	int			 vm_tty;
	uint32_t		 vm_peerid;
	/* When set, VM was defined in a config file */
	int			 vm_from_config;
	struct imsgev		 vm_iev;
	uid_t			 vm_uid;
	unsigned int		 vm_state;
/* When set, VM is running now (PROC_PARENT only) */
#define VM_STATE_RUNNING	0x01
/* When set, VM is not started by default (PROC_PARENT only) */
#define VM_STATE_DISABLED	0x02
/* When set, VM is marked to be shut down */
#define VM_STATE_SHUTDOWN	0x04
#define VM_STATE_PAUSED		0x10
#define VM_STATE_WAITING	0x20

	/* For rate-limiting */
	struct timeval		 vm_start_tv;
	int			 vm_start_limit;

	TAILQ_ENTRY(vmd_vm)	 vm_entry;
};
TAILQ_HEAD(vmlist, vmd_vm);

struct name2id {
	char			name[VMM_MAX_NAME_LEN];
	int			uid;
	int32_t			id;
	TAILQ_ENTRY(name2id)	entry;
};
TAILQ_HEAD(name2idlist, name2id);

struct local_prefix {
	struct in_addr		 lp_in;
	struct in_addr		 lp_mask;
	struct in6_addr		 lp_in6;
	struct in6_addr		 lp_mask6;
};

#define SUN_PATH_LEN		(sizeof(((struct sockaddr_un *)NULL)->sun_path))
struct vmd_agentx {
	int			 ax_enabled;
	char			 ax_path[SUN_PATH_LEN];
	/*
	 * SNMP-VIEW-BASED-ACM-MIB:vacmContextName
	 * Should probably be a define in agentx.h
	 */
	char			 ax_context[32 + 1];
};

struct vmd_config {
	unsigned int		 cfg_flags;
#define VMD_CFG_INET6		0x01
#define VMD_CFG_AUTOINET6	0x02
#define VMD_CFG_STAGGERED_START	0x04

	struct timeval		 delay;
	int			 parallelism;
	struct local_prefix	 cfg_localprefix;
	struct vmd_agentx	 cfg_agentx;
};

struct vmd {
	struct privsep		 vmd_ps;
	const char		*vmd_conffile;
	char			*argv0;	/* abs. path to vmd for exec, unveil */

	/* global configuration that is sent to the children */
	struct vmd_config	 vmd_cfg;

	int			 vmd_debug;
	int			 vmd_verbose;
	int			 vmd_noaction;

	uint32_t		 vmd_nvm;
	struct vmlist		*vmd_vms;
	struct name2idlist	*vmd_known;
	uint32_t		 vmd_nswitches;
	struct switchlist	*vmd_switches;

	int			 vmd_fd;
	int			 vmd_fd6;
	int			 vmd_ptmfd;
	int			 vmd_psp_fd;
};

struct vm_dev_pipe {
	int			 read;
	int			 write;
	struct event		 read_ev;
};

enum pipe_msg_type {
	I8253_RESET_CHAN_0 = 0,
	I8253_RESET_CHAN_1 = 1,
	I8253_RESET_CHAN_2 = 2,
	NS8250_ZERO_READ,
	NS8250_RATELIMIT,
	MC146818_RESCHEDULE_PER,
	VIRTIO_NOTIFY,
	VIRTIO_RAISE_IRQ,
	VIRTIO_THREAD_START,
	VIRTIO_THREAD_PAUSE,
	VIRTIO_THREAD_STOP,
	VIRTIO_THREAD_ACK,
	VMMCI_SET_TIMEOUT_SHORT,
	VMMCI_SET_TIMEOUT_LONG,
};

static inline struct sockaddr_in *
ss2sin(struct sockaddr_storage *ss)
{
	return ((struct sockaddr_in *)ss);
}

static inline struct sockaddr_in6 *
ss2sin6(struct sockaddr_storage *ss)
{
	return ((struct sockaddr_in6 *)ss);
}

struct packet_ctx {
	uint8_t			 pc_htype;
	uint8_t			 pc_hlen;
	uint8_t			 pc_smac[ETHER_ADDR_LEN];
	uint8_t			 pc_dmac[ETHER_ADDR_LEN];

	struct sockaddr_storage	 pc_src;
	struct sockaddr_storage	 pc_dst;
};

/* packet.c */
ssize_t	 assemble_hw_header(unsigned char *, size_t, size_t,
	    struct packet_ctx *, unsigned int);
ssize_t	 assemble_udp_ip_header(unsigned char *, size_t, size_t,
	    struct packet_ctx *pc, unsigned char *, size_t);
ssize_t	 decode_hw_header(unsigned char *, size_t, size_t, struct packet_ctx *,
	    unsigned int);
ssize_t	 decode_udp_ip_header(unsigned char *, size_t, size_t,
	    struct packet_ctx *);

/* vmd.c */
int	 vmd_reload(unsigned int, const char *);
struct vmd_vm *vm_getbyid(uint32_t);
struct vmd_vm *vm_getbyvmid(uint32_t);
uint32_t vm_id2vmid(uint32_t, struct vmd_vm *);
uint32_t vm_vmid2id(uint32_t, struct vmd_vm *);
struct vmd_vm *vm_getbyname(const char *);
struct vmd_vm *vm_getbypid(pid_t);
void	 vm_stop(struct vmd_vm *, int, const char *);
void	 vm_remove(struct vmd_vm *, const char *);
int	 vm_register(struct privsep *, struct vmop_create_params *,
	    struct vmd_vm **, uint32_t, uid_t);
int	 vm_checkperm(struct vmd_vm *, struct vmop_owner *, uid_t);
int	 vm_checkaccess(int, unsigned int, uid_t, int);
int	 vm_opentty(struct vmd_vm *);
void	 vm_closetty(struct vmd_vm *);
void	 switch_remove(struct vmd_switch *);
struct vmd_switch *switch_getbyname(const char *);
uint32_t prefixlen2mask(uint8_t);
void	 prefixlen2mask6(u_int8_t, struct in6_addr *);
void	 getmonotime(struct timeval *);
int	 close_fd(int);

void	 vmop_result_read(struct imsg *, struct vmop_result *);
void	 vmop_info_result_read(struct imsg *, struct vmop_info_result *);
void	 vmop_id_read(struct imsg *, struct vmop_id *);
void	 vmop_ifreq_read(struct imsg *, struct vmop_ifreq *);
void	 vmop_addr_req_read(struct imsg *, struct vmop_addr_req *);
void	 vmop_addr_result_read(struct imsg *, struct vmop_addr_result *);
void	 vmop_owner_read(struct imsg *, struct vmop_owner *);
void	 vmop_create_params_read(struct imsg *, struct vmop_create_params *);
void	 vmop_config_read(struct imsg *, struct vmd_config *);

/* priv.c */
void	 priv(struct privsep *, struct privsep_proc *);
int	 priv_getiftype(char *, char *, unsigned int *);
int	 priv_findname(const char *, const char **);
int	 priv_validgroup(const char *);
int	 vm_priv_ifconfig(struct privsep *, struct vmd_vm *);
int	 vm_priv_brconfig(struct privsep *, struct vmd_switch *);
uint32_t vm_priv_addr(struct local_prefix *, uint32_t, int, int);
int	 vm_priv_addr6(struct local_prefix *, uint32_t, int, int,
    	    struct in6_addr *);

/* vmm.c */
void	 vmm(struct privsep *, struct privsep_proc *);
void	 vmm_shutdown(void);
int	 opentap(char *);
int	 fd_hasdata(int);
int	 vmm_pipe(struct vmd_vm *, int, void (*)(int, short, void *));

/* {mach}_vm.c (md interface) */
void	 create_memory_map(struct vm_create_params *);
int	 load_firmware(struct vmd_vm *, struct vcpu_reg_state *);
void	 init_emulated_hw(struct vmop_create_params *, int,
    int[][VM_MAX_BASE_PER_DISK], int *);
int	 vcpu_reset(uint32_t, uint32_t, struct vcpu_reg_state *);
void	 pause_vm_md(struct vmd_vm *);
void	 unpause_vm_md(struct vmd_vm *);
void	*hvaddr_mem(paddr_t, size_t);
struct vm_mem_range *
	 find_gpa_range(struct vm_create_params *, paddr_t, size_t);
int	 write_mem(paddr_t, const void *, size_t);
int	 read_mem(paddr_t, void *, size_t);
int	 intr_ack(struct vmd_vm *);
int	 intr_pending(struct vmd_vm *);
void	 intr_toggle_el(struct vmd_vm *, int, int);
void	 vcpu_assert_irq(uint32_t, uint32_t, int);
void	 vcpu_deassert_irq(uint32_t, uint32_t, int);
int	 vcpu_exit(struct vm_run_params *);
uint8_t	 vcpu_exit_pci(struct vm_run_params *);

#ifdef __amd64__
/* x86 io functions in x86_vm.c */
void	 set_return_data(struct vm_exit *, uint32_t);
void	 get_input_data(struct vm_exit *, uint32_t *);
#endif /* __amd64 __ */

/* vm.c (mi functions) */
void	 vcpu_halt(uint32_t);
void	 vcpu_unhalt(uint32_t);
void	 vcpu_signal_run(uint32_t);
int 	 vcpu_intr(uint32_t, uint32_t, uint8_t);
void	 vm_main(int, int);
void	 mutex_lock(pthread_mutex_t *);
void	 mutex_unlock(pthread_mutex_t *);
void	 vm_pipe_init(struct vm_dev_pipe *, void (*)(int, short, void *));
void	 vm_pipe_init2(struct vm_dev_pipe *, void (*)(int, short, void *),
	    void *);
void	 vm_pipe_send(struct vm_dev_pipe *, enum pipe_msg_type);
enum pipe_msg_type vm_pipe_recv(struct vm_dev_pipe *);
int	 write_mem(paddr_t, const void *buf, size_t);
int	 remap_guest_mem(struct vmd_vm *, int);
__dead void vm_shutdown(unsigned int);

/* config.c */
int	 config_init(struct vmd *);
void	 config_purge(struct vmd *, unsigned int);
int	 config_setconfig(struct vmd *);
int	 config_getconfig(struct vmd *, struct imsg *);
int	 config_setreset(struct vmd *, unsigned int);
int	 config_setvm(struct privsep *, struct vmd_vm *, uint32_t, uid_t);
int	 config_getvm(struct privsep *, struct imsg *);
int	 config_getdisk(struct privsep *, struct imsg *);
int	 config_getif(struct privsep *, struct imsg *);
int	 config_getcdrom(struct privsep *, struct imsg *);

/* vm_agentx.c */
void vm_agentx(struct privsep *, struct privsep_proc *);
void vm_agentx_shutdown(void);

/* parse.y */
int	 parse_config(const char *);
int	 cmdline_symset(char *);
int	 parse_prefix4(const char *, struct local_prefix *, const char **);
int	 parse_prefix6(const char *, struct local_prefix *, const char **);

/* virtio.c */
int	 virtio_get_base(int, char *, size_t, int, const char *);

/* vionet.c */
__dead void vionet_main(int, int);

/* vioblk.c */
__dead void vioblk_main(int, int);

/* psp.c */
int	 psp_get_pstate(uint16_t *, uint8_t *, uint8_t *, uint8_t *, uint8_t *);
int	 psp_df_flush(void);
int	 psp_get_gstate(uint32_t, uint32_t *, uint32_t *, uint8_t *);
int	 psp_launch_start(uint32_t *, int);
int	 psp_launch_update(uint32_t, vaddr_t, size_t);
int	 psp_encrypt_state(uint32_t, uint32_t, uint32_t, uint32_t);
int	 psp_launch_measure(uint32_t);
int	 psp_launch_finish(uint32_t);
int	 psp_activate(uint32_t, uint32_t);
int	 psp_guest_shutdown(uint32_t);
void	 psp_setup(void);

/* sev.c */
int	sev_init(struct vmd_vm *);
int	sev_register_encryption(vaddr_t, size_t);
int	sev_encrypt_memory(struct vmd_vm *);
int	sev_activate(struct vmd_vm *, int);
int	sev_encrypt_state(struct vmd_vm *, int);
int	sev_launch_finalize(struct vmd_vm *);
int	sev_shutdown(struct vmd_vm *);

#endif /* VMD_H */
