/*	$OpenBSD: vmd.c,v 1.169 2025/08/13 10:26:31 dv Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/ttycom.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>
#include <ctype.h>
#include <grp.h>

#include <dev/vmm/vmm.h>

#include "proc.h"
#include "atomicio.h"
#include "vmd.h"

__dead void usage(void);

int	 main(int, char **);
int	 vmd_configure(void);
void	 vmd_sighdlr(int sig, short event, void *arg);
void	 vmd_shutdown(void);
int	 vmd_dispatch_control(int, struct privsep_proc *, struct imsg *);
int	 vmd_dispatch_vmm(int, struct privsep_proc *, struct imsg *);
int	 vmd_dispatch_agentx(int, struct privsep_proc *, struct imsg *);
int	 vmd_dispatch_priv(int, struct privsep_proc *, struct imsg *);

int	 vm_instance(struct privsep *, struct vmd_vm **,
	    struct vmop_create_params *, uid_t);
int	 vm_checkinsflag(struct vmop_create_params *, unsigned int, uid_t);
int	 vm_claimid(const char *, int, uint32_t *);
void	 start_vm_batch(int, short, void*);

static inline void vm_terminate(struct vmd_vm *, const char *);

struct vmd	*env;

static struct privsep_proc procs[] = {
	/* Keep "priv" on top as procs[0] */
	{ "priv",	PROC_PRIV,	vmd_dispatch_priv, priv },
	{ "control",	PROC_CONTROL,	vmd_dispatch_control, control },
	{ "vmm",	PROC_VMM,	vmd_dispatch_vmm, vmm,
	  vmm_shutdown, "/" },
	{ "agentx", 	PROC_AGENTX,	vmd_dispatch_agentx, vm_agentx,
	  vm_agentx_shutdown, "/" }
};

enum privsep_procid privsep_process;

struct event staggered_start_timer;

/* For the privileged process */
static struct privsep_proc *proc_priv = &procs[0];
static struct passwd proc_privpw;
static const uint8_t zero_mac[ETHER_ADDR_LEN];

const char		 default_conffile[] = VMD_CONF;
const char		*conffile = default_conffile;

int
vmd_dispatch_control(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct privsep			*ps = p->p_ps;
	int				 res = 0, cmd = 0, verbose;
	unsigned int			 v = 0, flags;
	struct vmop_create_params	 vmc;
	struct vmop_id			 vid;
	struct vmop_result		 vmr;
	struct vmd_vm			*vm = NULL;
	char				*str = NULL;
	uint32_t			 peer_id, type, vm_id = 0;
	struct control_sock		*rcs;

	peer_id = imsg_get_id(imsg);
	type = imsg_get_type(imsg);

	switch (type) {
	case IMSG_VMDOP_START_VM_REQUEST:
		vmop_create_params_read(imsg, &vmc);
		vmc.vmc_kernel = imsg_get_fd(imsg);

		/* Try registering our VM in our list of known VMs. */
		if (vm_register(ps, &vmc, &vm, 0, vmc.vmc_owner.uid)) {
			res = errno;

			/* Did we have a failure during lookup of a parent? */
			if (vm == NULL) {
				cmd = IMSG_VMDOP_START_VM_RESPONSE;
				break;
			}

			/* Does the VM already exist? */
			if (res == EALREADY) {
				/* Is it already running? */
				if (vm->vm_state & VM_STATE_RUNNING) {
					cmd = IMSG_VMDOP_START_VM_RESPONSE;
					break;
				}

				/* If not running, are our flags ok? */
				if (vmc.vmc_flags &&
				    vmc.vmc_flags != VMOP_CREATE_KERNEL) {
					cmd = IMSG_VMDOP_START_VM_RESPONSE;
					break;
				}
			}
			res = 0;
		}

		/* Try to start the launch of the VM. */
		res = config_setvm(ps, vm, peer_id,
		    vm->vm_params.vmc_owner.uid);
		if (res)
			cmd = IMSG_VMDOP_START_VM_RESPONSE;
		break;
	case IMSG_VMDOP_WAIT_VM_REQUEST:
	case IMSG_VMDOP_TERMINATE_VM_REQUEST:
		vmop_id_read(imsg, &vid);
		flags = vid.vid_flags;
		cmd = IMSG_VMDOP_TERMINATE_VM_RESPONSE;

		if ((vm_id = vid.vid_id) == 0) {
			/* Lookup vm (id) by name */
			if ((vm = vm_getbyname(vid.vid_name)) == NULL) {
				res = ENOENT;
				break;
			}
			vm_id = vm->vm_vmid;
		} else if ((vm = vm_getbyvmid(vm_id)) == NULL) {
			res = ENOENT;
			break;
		}

		/* Validate curent state of vm */
		if ((vm->vm_state & VM_STATE_SHUTDOWN) &&
		    (flags & VMOP_FORCE) == 0) {
				res = EALREADY;
				break;
		} else if (!(vm->vm_state & VM_STATE_RUNNING)) {
			res = EINVAL;
			break;
		} else if (vm_checkperm(vm, &vm->vm_params.vmc_owner,
		    vid.vid_uid)) {
			res = EPERM;
			break;
		}

		/* Only relay TERMINATION requests, not WAIT requests */
		if (type == IMSG_VMDOP_TERMINATE_VM_REQUEST) {
			memset(&vid, 0, sizeof(vid));
			vid.vid_id = vm_id;
			vid.vid_flags = flags;

			if (proc_compose_imsg(ps, PROC_VMM, type, peer_id,
			    -1, &vid, sizeof(vid)) == -1)
				return (-1);
		}
		break;
	case IMSG_VMDOP_GET_INFO_VM_REQUEST:
		proc_forward_imsg(ps, imsg, PROC_VMM, -1);
		break;
	case IMSG_VMDOP_LOAD:
		str = imsg_string_read(imsg, PATH_MAX);
		/* fallthrough */
	case IMSG_VMDOP_RELOAD:
		if (vmd_reload(0, str) == -1)
			cmd = IMSG_CTL_FAIL;
		else
			cmd = IMSG_CTL_OK;
		free(str);
		break;
	case IMSG_CTL_RESET:
		v = imsg_uint_read(imsg);
		if (vmd_reload(v, NULL) == -1)
			cmd = IMSG_CTL_FAIL;
		else
			cmd = IMSG_CTL_OK;
		break;
	case IMSG_CTL_VERBOSE:
		verbose = imsg_int_read(imsg);
		log_setverbose(verbose);

		proc_compose_imsg(ps, PROC_VMM, type, -1, -1, &verbose,
		    sizeof(verbose));
		proc_compose_imsg(ps, PROC_PRIV, type, -1, -1, &verbose,
		    sizeof(verbose));
		cmd = IMSG_CTL_OK;
		break;
	case IMSG_VMDOP_PAUSE_VM:
	case IMSG_VMDOP_UNPAUSE_VM:
		vmop_id_read(imsg, &vid);
		if (vid.vid_id == 0) {
			if ((vm = vm_getbyname(vid.vid_name)) == NULL) {
				res = ENOENT;
				cmd = type == IMSG_VMDOP_PAUSE_VM
				    ? IMSG_VMDOP_PAUSE_VM_RESPONSE
				    : IMSG_VMDOP_UNPAUSE_VM_RESPONSE;
				break;
			} else {
				vid.vid_id = vm->vm_vmid;
			}
		} else if ((vm = vm_getbyid(vid.vid_id)) == NULL) {
			res = ENOENT;
			cmd = type == IMSG_VMDOP_PAUSE_VM
			    ? IMSG_VMDOP_PAUSE_VM_RESPONSE
			    : IMSG_VMDOP_UNPAUSE_VM_RESPONSE;
			break;
		}
		if (vm_checkperm(vm, &vm->vm_params.vmc_owner,
		    vid.vid_uid) != 0) {
			res = EPERM;
			cmd = type == IMSG_VMDOP_PAUSE_VM
			    ? IMSG_VMDOP_PAUSE_VM_RESPONSE
			    : IMSG_VMDOP_UNPAUSE_VM_RESPONSE;
			break;
		}
		proc_compose_imsg(ps, PROC_VMM, type, vm->vm_peerid, -1,
		    &vid, sizeof(vid));
		break;
	case IMSG_VMDOP_DONE:
		control_reset(&ps->ps_csock);
		TAILQ_FOREACH(rcs, &ps->ps_rcsocks, cs_entry)
			control_reset(rcs);
		cmd = 0;
		break;
	default:
		return (-1);
	}

	switch (cmd) {
	case 0:
		break;
	case IMSG_VMDOP_START_VM_RESPONSE:
	case IMSG_VMDOP_TERMINATE_VM_RESPONSE:
		memset(&vmr, 0, sizeof(vmr));
		vmr.vmr_result = res;
		vmr.vmr_id = vm_id;
		if (proc_compose_imsg(ps, PROC_CONTROL, cmd, peer_id, -1,
		    &vmr, sizeof(vmr)) == -1)
			return (-1);
		break;
	default:
		if (proc_compose_imsg(ps, PROC_CONTROL, cmd, peer_id, -1,
		    &res, sizeof(res)) == -1)
			return (-1);
		break;
	}

	return (0);
}

int
vmd_dispatch_vmm(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct vmop_result	 vmr;
	struct privsep		*ps = p->p_ps;
	struct vmd_vm		*vm;
	struct vm_create_params	*vcp;
	struct vmop_info_result	 vir;
	uint32_t		 peer_id, type;

	peer_id = imsg_get_id(imsg);
	type = imsg_get_type(imsg);

	switch (type) {
	case IMSG_VMDOP_PAUSE_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);
		if ((vm = vm_getbyvmid(vmr.vmr_id)) == NULL)
			break;
		proc_compose_imsg(ps, PROC_CONTROL, type, vm->vm_peerid, -1,
		    &vmr, sizeof(vmr));
		log_info("%s: paused vm %d successfully",
		    vm->vm_params.vmc_params.vcp_name, vm->vm_vmid);
		vm->vm_state |= VM_STATE_PAUSED;
		break;
	case IMSG_VMDOP_UNPAUSE_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);
		if ((vm = vm_getbyvmid(vmr.vmr_id)) == NULL)
			break;
		proc_compose_imsg(ps, PROC_CONTROL, type, vm->vm_peerid, -1,
		    &vmr, sizeof(vmr));
		log_info("%s: unpaused vm %d successfully.",
		    vm->vm_params.vmc_params.vcp_name, vm->vm_vmid);
		vm->vm_state &= ~VM_STATE_PAUSED;
		break;
	case IMSG_VMDOP_START_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);
		if ((vm = vm_getbyvmid(vmr.vmr_id)) == NULL)
			break;
		vm->vm_pid = vmr.vmr_pid;
		vcp = &vm->vm_params.vmc_params;
		vcp->vcp_id = vmr.vmr_id;

		/*
		 * If the peerid is not -1, forward the response back to the
		 * the control socket.  If it is -1, the request originated
		 * from the parent, not the control socket.
		 */
		if (vm->vm_peerid != (uint32_t)-1) {
			(void)strlcpy(vmr.vmr_ttyname, vm->vm_ttyname,
			    sizeof(vmr.vmr_ttyname));
			if (proc_compose_imsg(ps, PROC_CONTROL, type,
			    vm->vm_peerid, -1, &vmr, sizeof(vmr)) == -1) {
				errno = vmr.vmr_result;
				log_warn("%s: failed to forward vm result",
				    vcp->vcp_name);
				vm_terminate(vm, __func__);
				return (-1);
			}
		}

		if (vmr.vmr_result) {
			log_warnx("%s: failed to start vm", vcp->vcp_name);
			vm_terminate(vm, __func__);
			errno = vmr.vmr_result;
			break;
		}

		/* Now configure all the interfaces */
		if (vm_priv_ifconfig(ps, vm) == -1) {
			log_warn("%s: failed to configure vm", vcp->vcp_name);
			vm_terminate(vm, __func__);
			break;
		}

		log_info("started %s (vm %d) successfully, tty %s",
		    vcp->vcp_name, vm->vm_vmid, vm->vm_ttyname);
		break;
	case IMSG_VMDOP_TERMINATE_VM_RESPONSE:
		vmop_result_read(imsg, &vmr);

		if (vmr.vmr_result) {
			DPRINTF("%s: forwarding TERMINATE VM for vm id %d",
			    __func__, vmr.vmr_id);
			proc_forward_imsg(ps, imsg, PROC_CONTROL, -1);
		} else {
			if ((vm = vm_getbyvmid(vmr.vmr_id)) == NULL)
				break;
			/* Mark VM as shutting down */
			vm->vm_state |= VM_STATE_SHUTDOWN;
		}
		break;
	case IMSG_VMDOP_TERMINATE_VM_EVENT:
		vmop_result_read(imsg, &vmr);
		DPRINTF("%s: handling TERMINATE_EVENT for vm id %d ret %d",
		    __func__, vmr.vmr_id, vmr.vmr_result);
		if ((vm = vm_getbyvmid(vmr.vmr_id)) == NULL) {
			log_debug("%s: vm %d is no longer available",
			    __func__, vmr.vmr_id);
			break;
		}
		if (vmr.vmr_result != EAGAIN ||
		    vm->vm_params.vmc_bootdevice) {
			vm_terminate(vm, __func__);
		} else {
			/* Stop VM instance but keep the tty open */
			vm_stop(vm, 1, __func__);
			config_setvm(ps, vm, (uint32_t)-1, vm->vm_uid);
		}

		/* The error is meaningless for deferred responses */
		vmr.vmr_result = 0;

		if (proc_compose_imsg(ps, PROC_CONTROL,
		    IMSG_VMDOP_TERMINATE_VM_EVENT, peer_id, -1, &vmr,
		    sizeof(vmr)) == -1)
			return (-1);
		break;
	case IMSG_VMDOP_GET_INFO_VM_DATA:
		vmop_info_result_read(imsg, &vir);
		if ((vm = vm_getbyvmid(vir.vir_info.vir_id)) != NULL) {
			memset(vir.vir_ttyname, 0, sizeof(vir.vir_ttyname));
			if (vm->vm_ttyname[0] != '\0')
				strlcpy(vir.vir_ttyname, vm->vm_ttyname,
				    sizeof(vir.vir_ttyname));
			log_debug("%s: running vm: %d, vm_state: 0x%x",
			    __func__, vm->vm_vmid, vm->vm_state);
			vir.vir_state = vm->vm_state;
			/* get the user id who started the vm */
			vir.vir_uid = vm->vm_uid;
			vir.vir_gid = vm->vm_params.vmc_owner.gid;
		}
		if (proc_compose_imsg(ps,
		    peer_id == IMSG_AGENTX_PEERID ? PROC_AGENTX : PROC_CONTROL,
		    type, peer_id, -1, &vir, sizeof(vir)) == -1) {
			if (vm)
				vm_terminate(vm, __func__);
			return (-1);
		}
		break;
	case IMSG_VMDOP_GET_INFO_VM_END_DATA:
		/*
		 * PROC_VMM has responded with the *running* VMs, now we
		 * append the others. These use the special value 0 for their
		 * kernel id to indicate that they are not running.
		 */
		TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
			if (!(vm->vm_state & VM_STATE_RUNNING)) {
				memset(&vir, 0, sizeof(vir));
				vir.vir_info.vir_id = vm->vm_vmid;
				strlcpy(vir.vir_info.vir_name,
				    vm->vm_params.vmc_params.vcp_name,
				    VMM_MAX_NAME_LEN);
				vir.vir_info.vir_memory_size =
				    vm->vm_params.vmc_params.
				    vcp_memranges[0].vmr_size;
				vir.vir_info.vir_ncpus =
				    vm->vm_params.vmc_params.vcp_ncpus;
				/* get the configured user id for this vm */
				vir.vir_uid = vm->vm_params.vmc_owner.uid;
				vir.vir_gid = vm->vm_params.vmc_owner.gid;
				log_debug("%s: vm: %d, vm_state: 0x%x",
				    __func__, vm->vm_vmid, vm->vm_state);
				vir.vir_state = vm->vm_state;
				if (proc_compose_imsg(ps,
				    peer_id == IMSG_AGENTX_PEERID ?
				    PROC_AGENTX : PROC_CONTROL,
				    IMSG_VMDOP_GET_INFO_VM_DATA, peer_id, -1,
				    &vir, sizeof(vir)) == -1) {
					log_debug("%s: GET_INFO_VM_END failed",
					    __func__);
					vm_terminate(vm, __func__);
					return (-1);
				}
			}
		}
		proc_forward_imsg(ps, imsg,
		    peer_id == IMSG_AGENTX_PEERID ? PROC_AGENTX : PROC_CONTROL,
		    -1);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
vmd_dispatch_agentx(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct privsep	*ps = p->p_ps;
	uint32_t	 type;

	type = imsg_get_type(imsg);
	switch (type) {
	case IMSG_VMDOP_GET_INFO_VM_REQUEST:
		proc_forward_imsg(ps, imsg, PROC_VMM, -1);
		return (0);
	default:
		break;
	}
	return (-1);
}

int
vmd_dispatch_priv(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	uint32_t	 type;

	type = imsg_get_type(imsg);
	switch (type) {
	case IMSG_VMDOP_PRIV_GET_ADDR_RESPONSE:
		proc_forward_imsg(p->p_ps, imsg, PROC_VMM, -1);
		break;
	default:
		return (-1);
	}

	return (0);
}


void
vmd_sighdlr(int sig, short event, void *arg)
{
	if (privsep_process != PROC_PARENT)
		return;
	log_debug("%s: handling signal", __func__);

	switch (sig) {
	case SIGHUP:
		log_info("%s: reload requested with SIGHUP", __func__);

		/*
		 * This is safe because libevent uses async signal handlers
		 * that run in the event loop and not in signal context.
		 */
		(void)vmd_reload(0, NULL);
		break;
	case SIGPIPE:
		log_info("%s: ignoring SIGPIPE", __func__);
		break;
	case SIGUSR1:
		log_info("%s: ignoring SIGUSR1", __func__);
		break;
	case SIGTERM:
	case SIGINT:
		vmd_shutdown();
		break;
	default:
		fatalx("unexpected signal");
	}
}

__dead void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-dnv] [-D macro=value] [-f file]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct privsep		*ps;
	int			 ch;
	enum privsep_procid	 proc_id = PROC_PARENT;
	int			 vm_launch = 0;
	int			 vmm_fd = -1, vm_fd = -1, psp_fd = -1;
	const char		*errp, *title = NULL;
	int			 argc0 = argc;
	char			 dev_type = '\0';

	log_init(0, LOG_DAEMON);

	if ((env = calloc(1, sizeof(*env))) == NULL)
		fatal("calloc: env");
	env->vmd_fd = -1;
	env->vmd_fd6 = -1;

	while ((ch = getopt(argc, argv, "D:P:V:X:df:i:j:nt:vp:")) != -1) {
		switch (ch) {
		case 'D':
			if (cmdline_symset(optarg) < 0)
				log_warnx("could not parse macro definition %s",
				    optarg);
			break;
		case 'd':
			env->vmd_debug = 2;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'v':
			env->vmd_verbose++;
			break;
		/* vmd fork/exec */
		case 'n':
			env->vmd_noaction = 1;
			break;
		case 'P':
			title = optarg;
			proc_id = proc_getid(procs, nitems(procs), title);
			if (proc_id == PROC_MAX)
				fatalx("invalid process name");
			break;
		/* child vm and device fork/exec */
		case 'p':
			title = optarg;
			break;
		case 'V':
			vm_launch = VMD_LAUNCH_VM;
			vm_fd = strtonum(optarg, 0, 128, &errp);
			if (errp)
				fatalx("invalid vm fd");
			break;
		case 'X':
			vm_launch = VMD_LAUNCH_DEV;
			vm_fd = strtonum(optarg, 0, 128, &errp);
			if (errp)
				fatalx("invalid device fd");
			break;
		case 't':
			dev_type = *optarg;
			switch (dev_type) {
			case VMD_DEVTYPE_NET:
			case VMD_DEVTYPE_DISK:
				break;
			default: fatalx("invalid device type");
			}
			break;
		case 'i':
			vmm_fd = strtonum(optarg, 0, 128, &errp);
			if (errp)
				fatalx("invalid vmm fd");
			break;
		case 'j':
			/* -1 means no PSP available */
			psp_fd = strtonum(optarg, -1, 128, &errp);
			if (errp)
				fatalx("invalid psp fd");
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	if (argc > 0)
		usage();

	if (env->vmd_noaction && !env->vmd_debug)
		env->vmd_debug = 1;

	log_init(env->vmd_debug, LOG_DAEMON);
	log_setverbose(env->vmd_verbose);

	/* Re-exec from the vmm child process requires an absolute path. */
	if (proc_id == PROC_PARENT && *argv[0] != '/' && !env->vmd_noaction)
		fatalx("re-exec requires execution with an absolute path");
	env->argv0 = argv[0];

	/* check for root privileges */
	if (env->vmd_noaction == 0 && !vm_launch) {
		if (geteuid())
			fatalx("need root privileges");
	}

	ps = &env->vmd_ps;
	ps->ps_env = env;
	env->vmd_psp_fd = psp_fd;

	if (config_init(env) == -1)
		fatal("failed to initialize configuration");

	if ((ps->ps_pw = getpwnam(VMD_USER)) == NULL)
		fatal("unknown user %s", VMD_USER);

	/* First proc runs as root without pledge but in default chroot */
	proc_priv->p_pw = &proc_privpw; /* initialized to all 0 */
	proc_priv->p_chroot = ps->ps_pw->pw_dir; /* from VMD_USER */

	/*
	 * If we're launching a new vm or its device, we short out here.
	 */
	if (vm_launch == VMD_LAUNCH_VM) {
		vm_main(vm_fd, vmm_fd);
		/* NOTREACHED */
	} else if (vm_launch == VMD_LAUNCH_DEV) {
		if (dev_type == VMD_DEVTYPE_NET) {
			log_procinit("vm/%s/vionet", title);
			vionet_main(vm_fd, vmm_fd);
			/* NOTREACHED */
		} else if (dev_type == VMD_DEVTYPE_DISK) {
			log_procinit("vm/%s/vioblk", title);
			vioblk_main(vm_fd, vmm_fd);
			/* NOTREACHED */
		}
		fatalx("unsupported device type '%c'", dev_type);
	}

	/* Open /dev/vmm early. */
	if (env->vmd_noaction == 0 && proc_id == PROC_PARENT) {
		env->vmd_fd = open(VMM_NODE, O_RDWR | O_CLOEXEC);
		if (env->vmd_fd == -1)
			fatal("%s", VMM_NODE);
	}

	/* Configure the control socket */
	ps->ps_csock.cs_name = SOCKET_NAME;
	TAILQ_INIT(&ps->ps_rcsocks);

	/* Configuration will be parsed after forking the children */
	env->vmd_conffile = conffile;

	if (env->vmd_noaction)
		ps->ps_noaction = 1;
	if (title != NULL)
		ps->ps_title[proc_id] = title;

	/* only the parent returns */
	proc_init(ps, procs, nitems(procs), env->vmd_debug, argc0, argv,
	    proc_id);

	if (ps->ps_noaction == 0)
		log_info("startup");

	event_init();

	signal_set(&ps->ps_evsigint, SIGINT, vmd_sighdlr, ps);
	signal_set(&ps->ps_evsigterm, SIGTERM, vmd_sighdlr, ps);
	signal_set(&ps->ps_evsighup, SIGHUP, vmd_sighdlr, ps);
	signal_set(&ps->ps_evsigpipe, SIGPIPE, vmd_sighdlr, ps);
	signal_set(&ps->ps_evsigusr1, SIGUSR1, vmd_sighdlr, ps);

	signal_add(&ps->ps_evsigint, NULL);
	signal_add(&ps->ps_evsigterm, NULL);
	signal_add(&ps->ps_evsighup, NULL);
	signal_add(&ps->ps_evsigpipe, NULL);
	signal_add(&ps->ps_evsigusr1, NULL);

	if (!env->vmd_noaction)
		proc_connect(ps);

	env->vmd_psp_fd = -1;
	if (env->vmd_noaction == 0 && proc_id == PROC_PARENT)
		psp_setup();

	if (vmd_configure() == -1)
		fatalx("configuration failed");

	event_dispatch();

	log_debug("exiting");

	return (0);
}

void
start_vm_batch(int fd, short type, void *args)
{
	int		i = 0;
	struct vmd_vm	*vm;

	log_debug("%s: starting batch of %d vms", __func__,
	    env->vmd_cfg.parallelism);
	TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
		if (!(vm->vm_state & VM_STATE_WAITING)) {
			log_debug("%s: not starting vm %s (disabled)",
			    __func__,
			    vm->vm_params.vmc_params.vcp_name);
			continue;
		}
		i++;
		if (i > env->vmd_cfg.parallelism) {
			evtimer_add(&staggered_start_timer,
			    &env->vmd_cfg.delay);
			break;
		}
		vm->vm_state &= ~VM_STATE_WAITING;
		config_setvm(&env->vmd_ps, vm, -1, vm->vm_params.vmc_owner.uid);
	}
	log_debug("%s: done starting vms", __func__);
}

int
vmd_configure(void)
{
	int			ncpus;
	struct vmd_switch	*vsw;
	int ncpu_mib[] = {CTL_HW, HW_NCPUONLINE};
	size_t ncpus_sz = sizeof(ncpus);

	/*
	 * pledge in the parent process:
	 * stdio - for malloc and basic I/O including events.
	 * rpath - for reload to open and read the configuration files.
	 * wpath - for opening disk images and tap devices.
	 * tty - for openpty and TIOCUCNTL.
	 * proc - run kill to terminate its children safely.
	 * sendfd - for disks, interfaces and other fds.
	 * recvfd - for send and receive.
	 * getpw - lookup user or group id by name.
	 * chown, fattr - change tty ownership
	 * flock - locking disk files
	 */
	if (pledge("stdio rpath wpath proc tty recvfd sendfd getpw"
	    " chown fattr flock", NULL) == -1)
		fatal("pledge");

	if ((env->vmd_ptmfd = getptmfd()) == -1)
		fatal("getptmfd %s", PATH_PTMDEV);

	if (parse_config(env->vmd_conffile) == -1) {
		proc_kill(&env->vmd_ps);
		exit(1);
	}

	if (env->vmd_noaction) {
		fprintf(stderr, "configuration OK\n");
		proc_kill(&env->vmd_ps);
		exit(0);
	}

	/* Send VMM device fd to vmm proc. */
	proc_compose_imsg(&env->vmd_ps, PROC_VMM,
	    IMSG_VMDOP_RECEIVE_VMM_FD, -1, env->vmd_fd, NULL, 0);

	/* Send PSP device fd to vmm proc. */
	if (env->vmd_psp_fd != -1) {
		proc_compose_imsg(&env->vmd_ps, PROC_VMM,
		    IMSG_VMDOP_RECEIVE_PSP_FD, -1, env->vmd_psp_fd, NULL, 0);
	}

	/* Send shared global configuration to all children */
	if (config_setconfig(env) == -1)
		return (-1);

	TAILQ_FOREACH(vsw, env->vmd_switches, sw_entry) {
		if (vsw->sw_running)
			continue;
		if (vm_priv_brconfig(&env->vmd_ps, vsw) == -1) {
			log_warn("%s: failed to create switch %s",
			    __func__, vsw->sw_name);
			switch_remove(vsw);
			return (-1);
		}
	}

	if (!(env->vmd_cfg.cfg_flags & VMD_CFG_STAGGERED_START)) {
		env->vmd_cfg.delay.tv_sec = VMD_DEFAULT_STAGGERED_START_DELAY;
		if (sysctl(ncpu_mib, nitems(ncpu_mib), &ncpus, &ncpus_sz, NULL, 0) == -1)
			ncpus = 1;
		env->vmd_cfg.parallelism = ncpus;
		log_debug("%s: setting staggered start configuration to "
		    "parallelism: %d and delay: %lld",
		    __func__, ncpus, (long long) env->vmd_cfg.delay.tv_sec);
	}

	log_debug("%s: starting vms in staggered fashion", __func__);
	evtimer_set(&staggered_start_timer, start_vm_batch, NULL);
	/* start first batch */
	start_vm_batch(0, 0, NULL);

	return (0);
}

int
vmd_reload(unsigned int reset, const char *filename)
{
	struct vmd_vm		*vm, *next_vm;
	struct vmd_switch	*vsw;
	int			 reload = 0;

	/* Switch back to the default config file */
	if (filename == NULL || *filename == '\0') {
		filename = env->vmd_conffile;
		reload = 1;
	}

	log_debug("%s: level %d config file %s", __func__, reset, filename);

	if (reset) {
		/* Purge the configuration */
		config_purge(env, reset);
		config_setreset(env, reset);
	} else {
		/*
		 * Load or reload the configuration.
		 *
		 * Reloading removes all non-running VMs before processing the
		 * config file, whereas loading only adds to the existing list
		 * of VMs.
		 */

		if (reload) {
			TAILQ_FOREACH_SAFE(vm, env->vmd_vms, vm_entry,
			    next_vm) {
				if (!(vm->vm_state & VM_STATE_RUNNING)) {
					DPRINTF("%s: calling vm_remove",
					    __func__);
					vm_remove(vm, __func__);
				}
			}
		}

		if (parse_config(filename) == -1) {
			log_debug("%s: failed to load config file %s",
			    __func__, filename);
			return (-1);
		}

		if (reload) {
			/* Update shared global configuration in all children */
			if (config_setconfig(env) == -1)
				return (-1);
		}

		TAILQ_FOREACH(vsw, env->vmd_switches, sw_entry) {
			if (vsw->sw_running)
				continue;
			if (vm_priv_brconfig(&env->vmd_ps, vsw) == -1) {
				log_warn("%s: failed to create switch %s",
				    __func__, vsw->sw_name);
				switch_remove(vsw);
				return (-1);
			}
		}

		log_debug("%s: starting vms in staggered fashion", __func__);
		evtimer_set(&staggered_start_timer, start_vm_batch, NULL);
		/* start first batch */
		start_vm_batch(0, 0, NULL);

		}

	return (0);
}

void
vmd_shutdown(void)
{
	struct vmd_vm *vm, *vm_next;

	log_debug("%s: performing shutdown", __func__);

	TAILQ_FOREACH_SAFE(vm, env->vmd_vms, vm_entry, vm_next) {
		vm_remove(vm, __func__);
	}

	proc_kill(&env->vmd_ps);
	free(env);

	log_warnx("terminating");
	exit(0);
}

struct vmd_vm *
vm_getbyvmid(uint32_t vmid)
{
	struct vmd_vm	*vm;

	if (vmid == 0)
		return (NULL);
	TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
		if (vm->vm_vmid == vmid)
			return (vm);
	}

	return (NULL);
}

struct vmd_vm *
vm_getbyid(uint32_t id)
{
	struct vmd_vm	*vm;

	if (id == 0)
		return (NULL);
	TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
		if (vm->vm_params.vmc_params.vcp_id == id)
			return (vm);
	}

	return (NULL);
}

uint32_t
vm_id2vmid(uint32_t id, struct vmd_vm *vm)
{
	if (vm == NULL && (vm = vm_getbyid(id)) == NULL)
		return (0);
	DPRINTF("%s: vmm id %u is vmid %u", __func__,
	    id, vm->vm_vmid);
	return (vm->vm_vmid);
}

uint32_t
vm_vmid2id(uint32_t vmid, struct vmd_vm *vm)
{
	if (vm == NULL && (vm = vm_getbyvmid(vmid)) == NULL)
		return (0);
	DPRINTF("%s: vmid %u is vmm id %u", __func__,
	    vmid, vm->vm_params.vmc_params.vcp_id);
	return (vm->vm_params.vmc_params.vcp_id);
}

struct vmd_vm *
vm_getbyname(const char *name)
{
	struct vmd_vm	*vm;

	if (name == NULL)
		return (NULL);
	TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
		if (strcmp(vm->vm_params.vmc_params.vcp_name, name) == 0)
			return (vm);
	}

	return (NULL);
}

struct vmd_vm *
vm_getbypid(pid_t pid)
{
	struct vmd_vm	*vm;

	TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
		if (vm->vm_pid == pid)
			return (vm);
	}

	return (NULL);
}

void
vm_stop(struct vmd_vm *vm, int keeptty, const char *caller)
{
	struct privsep	*ps = &env->vmd_ps;
	unsigned int	 i, j;

	if (vm == NULL)
		return;

	log_debug("%s: %s %s stopping vm %d%s",
	    __func__, ps->ps_title[privsep_process], caller,
	    vm->vm_vmid, keeptty ? ", keeping tty open" : "");

	vm->vm_state &= ~(VM_STATE_RUNNING | VM_STATE_SHUTDOWN);

	if (vm->vm_iev.ibuf.fd != -1) {
		event_del(&vm->vm_iev.ev);
		close(vm->vm_iev.ibuf.fd);
	}
	for (i = 0; i < VM_MAX_DISKS_PER_VM; i++) {
		for (j = 0; j < VM_MAX_BASE_PER_DISK; j++) {
			if (vm->vm_disks[i][j] != -1) {
				close(vm->vm_disks[i][j]);
				vm->vm_disks[i][j] = -1;
			}
		}
	}
	for (i = 0; i < VM_MAX_NICS_PER_VM; i++) {
		if (vm->vm_ifs[i].vif_fd != -1) {
			close(vm->vm_ifs[i].vif_fd);
			vm->vm_ifs[i].vif_fd = -1;
		}
		free(vm->vm_ifs[i].vif_name);
		free(vm->vm_ifs[i].vif_switch);
		free(vm->vm_ifs[i].vif_group);
		vm->vm_ifs[i].vif_name = NULL;
		vm->vm_ifs[i].vif_switch = NULL;
		vm->vm_ifs[i].vif_group = NULL;
	}
	if (vm->vm_kernel != -1) {
		close(vm->vm_kernel);
		vm->vm_kernel = -1;
	}
	if (vm->vm_cdrom != -1) {
		close(vm->vm_cdrom);
		vm->vm_cdrom = -1;
	}
	if (!keeptty) {
		vm_closetty(vm);
		vm->vm_uid = 0;
	}
}

void
vm_remove(struct vmd_vm *vm, const char *caller)
{
	struct privsep	*ps = &env->vmd_ps;

	if (vm == NULL)
		return;

	log_debug("%s: %s %s removing vm %d from running config",
	    __func__, ps->ps_title[privsep_process], caller,
	    vm->vm_vmid);

	TAILQ_REMOVE(env->vmd_vms, vm, vm_entry);

	vm_stop(vm, 0, caller);
	if (vm->vm_kernel_path != NULL && !vm->vm_from_config)
		free(vm->vm_kernel_path);
	free(vm);
}

int
vm_claimid(const char *name, int uid, uint32_t *id)
{
	struct name2id *n2i = NULL;

	TAILQ_FOREACH(n2i, env->vmd_known, entry)
		if (strcmp(n2i->name, name) == 0 && n2i->uid == uid)
			goto out;

	if (++env->vmd_nvm == 0) {
		log_warnx("too many vms");
		return (-1);
	}
	if ((n2i = calloc(1, sizeof(struct name2id))) == NULL) {
		log_warnx("could not alloc vm name");
		return (-1);
	}
	n2i->id = env->vmd_nvm;
	n2i->uid = uid;
	if (strlcpy(n2i->name, name, sizeof(n2i->name)) >= sizeof(n2i->name)) {
		log_warnx("vm name too long");
		free(n2i);
		return (-1);
	}
	TAILQ_INSERT_TAIL(env->vmd_known, n2i, entry);

out:
	*id = n2i->id;
	return (0);
}

int
vm_register(struct privsep *ps, struct vmop_create_params *vmc,
    struct vmd_vm **ret_vm, uint32_t id, uid_t uid)
{
	struct vmd_vm		*vm = NULL, *vm_parent = NULL;
	struct vm_create_params	*vcp = &vmc->vmc_params;
	struct vmop_owner	*vmo = NULL;
	uint32_t		 nid, rng;
	unsigned int		 i, j;
	struct vmd_switch	*sw;
	char			*s;
	int			 ret = 0;

	/* Check if this is an instance of another VM */
	if ((ret = vm_instance(ps, &vm_parent, vmc, uid)) != 0) {
		errno = ret; /* XXX might set invalid errno */
		return (-1);
	}

	errno = 0;
	*ret_vm = NULL;

	if ((vm = vm_getbyname(vcp->vcp_name)) != NULL ||
	    (vm = vm_getbyvmid(vcp->vcp_id)) != NULL) {
		if (vm_checkperm(vm, &vm->vm_params.vmc_owner,
		    uid) != 0) {
			errno = EPERM;
			goto fail;
		}
		vm->vm_kernel = vmc->vmc_kernel;
		*ret_vm = vm;
		errno = EALREADY;
		goto fail;
	}

	if (vm_parent != NULL)
		vmo = &vm_parent->vm_params.vmc_insowner;

	/* non-root users can only start existing VMs or instances */
	if (vm_checkperm(NULL, vmo, uid) != 0) {
		log_warnx("permission denied");
		errno = EPERM;
		goto fail;
	}
	if (vmc->vmc_flags == 0) {
		log_warnx("invalid configuration, no devices");
		errno = VMD_DISK_MISSING;
		goto fail;
	}
	if (vcp->vcp_ncpus == 0)
		vcp->vcp_ncpus = 1;
	if (vcp->vcp_memranges[0].vmr_size == 0)
		vcp->vcp_memranges[0].vmr_size = VM_DEFAULT_MEMORY;
	if (vcp->vcp_ncpus > VMM_MAX_VCPUS_PER_VM) {
		log_warnx("invalid number of CPUs");
		goto fail;
	} else if (vmc->vmc_ndisks > VM_MAX_DISKS_PER_VM) {
		log_warnx("invalid number of disks");
		goto fail;
	} else if (vmc->vmc_nnics > VM_MAX_NICS_PER_VM) {
		log_warnx("invalid number of interfaces");
		goto fail;
	} else if (vmc->vmc_kernel == -1 && vmc->vmc_ndisks == 0
	    && strlen(vmc->vmc_cdrom) == 0) {
		log_warnx("no kernel or disk/cdrom specified");
		goto fail;
	} else if (strlen(vcp->vcp_name) == 0) {
		log_warnx("invalid VM name");
		goto fail;
	} else if (*vcp->vcp_name == '-' || *vcp->vcp_name == '.' ||
	    *vcp->vcp_name == '_') {
		log_warnx("invalid VM name");
		goto fail;
	} else {
		for (s = vcp->vcp_name; *s != '\0'; ++s) {
			if (!(isalnum((unsigned char)*s) || *s == '.' || \
			    *s == '-' || *s == '_')) {
				log_warnx("invalid VM name");
				goto fail;
			}
		}
	}

	if ((vm = calloc(1, sizeof(*vm))) == NULL)
		goto fail;

	memcpy(&vm->vm_params, vmc, sizeof(vm->vm_params));
	vmc = &vm->vm_params;
	vcp = &vmc->vmc_params;
	vm->vm_pid = -1;
	vm->vm_tty = -1;
	vm->vm_kernel = -1;
	vm->vm_state &= ~VM_STATE_PAUSED;

	if (vmc->vmc_kernel > -1)
		vm->vm_kernel = vmc->vmc_kernel;

	for (i = 0; i < VM_MAX_DISKS_PER_VM; i++)
		for (j = 0; j < VM_MAX_BASE_PER_DISK; j++)
			vm->vm_disks[i][j] = -1;
	for (i = 0; i < VM_MAX_NICS_PER_VM; i++)
		vm->vm_ifs[i].vif_fd = -1;
	for (i = 0; i < vmc->vmc_nnics; i++) {
		if ((sw = switch_getbyname(vmc->vmc_ifswitch[i])) != NULL) {
			/* inherit per-interface flags from the switch */
			vmc->vmc_ifflags[i] |= (sw->sw_flags & VMIFF_OPTMASK);
		}

		/*
		 * If the MAC address is zero, always randomize it in vmd(8)
		 * because we cannot rely on the guest OS to do the right
		 * thing like OpenBSD does.  Based on ether_fakeaddr()
		 * from the kernel, incremented by one to differentiate
		 * the source.
		 */
		if (memcmp(zero_mac, &vmc->vmc_macs[i], ETHER_ADDR_LEN) == 0) {
			rng = arc4random();
			vmc->vmc_macs[i][0] = 0xfe;
			vmc->vmc_macs[i][1] = 0xe1;
			vmc->vmc_macs[i][2] = 0xba + 1;
			vmc->vmc_macs[i][3] = 0xd0 | ((i + 1) & 0xf);
			vmc->vmc_macs[i][4] = rng;
			vmc->vmc_macs[i][5] = rng >> 8;
		}
	}
	vm->vm_cdrom = -1;
	vm->vm_iev.ibuf.fd = -1;

	/*
	 * Assign a new internal Id if not specified and we succeed in
	 * claiming a new Id.
	 */
	if (id != 0)
		vm->vm_vmid = id;
	else if (vm_claimid(vcp->vcp_name, uid, &nid) == -1)
		goto fail;
	else
		vm->vm_vmid = nid;

	log_debug("%s: registering vm %d", __func__, vm->vm_vmid);
	TAILQ_INSERT_TAIL(env->vmd_vms, vm, vm_entry);

	*ret_vm = vm;
	return (0);
 fail:
	if (errno == 0)
		errno = EINVAL;
	return (-1);
}

int
vm_instance(struct privsep *ps, struct vmd_vm **vm_parent,
    struct vmop_create_params *vmc, uid_t uid)
{
	char			*name;
	struct vm_create_params	*vcp = &vmc->vmc_params;
	struct vmop_create_params *vmcp;
	struct vm_create_params	*vcpp;
	unsigned int		 i, j;

	/* return without error if the parent is NULL (nothing to inherit) */
	if ((vmc->vmc_flags & VMOP_CREATE_INSTANCE) == 0 ||
	    vmc->vmc_instance[0] == '\0')
		return (0);

	if ((*vm_parent = vm_getbyname(vmc->vmc_instance)) == NULL) {
		return (VMD_PARENT_INVALID);
	}

	vmcp = &(*vm_parent)->vm_params;
	vcpp = &vmcp->vmc_params;

	/* Are we allowed to create an instance from this VM? */
	if (vm_checkperm(NULL, &vmcp->vmc_insowner, uid) != 0) {
		log_warnx("vm \"%s\" no permission to create vm instance",
		    vcpp->vcp_name);
		return (ENAMETOOLONG);
	}

	name = vcp->vcp_name;

	if (vm_getbyname(vcp->vcp_name) != NULL ||
	    vm_getbyvmid(vcp->vcp_id) != NULL) {
		return (EPROCLIM);
	}

	/* CPU */
	if (vcp->vcp_ncpus == 0)
		vcp->vcp_ncpus = vcpp->vcp_ncpus;
	if (vm_checkinsflag(vmcp, VMOP_CREATE_CPU, uid) != 0 &&
	    vcp->vcp_ncpus != vcpp->vcp_ncpus) {
		log_warnx("vm \"%s\" no permission to set cpus", name);
		return (EPERM);
	}

	/* memory */
	if (vcp->vcp_memranges[0].vmr_size == 0)
		vcp->vcp_memranges[0].vmr_size =
		    vcpp->vcp_memranges[0].vmr_size;
	if (vm_checkinsflag(vmcp, VMOP_CREATE_MEMORY, uid) != 0 &&
	    vcp->vcp_memranges[0].vmr_size !=
	    vcpp->vcp_memranges[0].vmr_size) {
		log_warnx("vm \"%s\" no permission to set memory", name);
		return (EPERM);
	}

	/* disks cannot be inherited */
	if (vm_checkinsflag(vmcp, VMOP_CREATE_DISK, uid) != 0 &&
	    vmc->vmc_ndisks) {
		log_warnx("vm \"%s\" no permission to set disks", name);
		return (EPERM);
	}
	for (i = 0; i < vmc->vmc_ndisks; i++) {
		/* Check if this disk is already used in the parent */
		for (j = 0; j < vmcp->vmc_ndisks; j++) {
			if (strcmp(vmc->vmc_disks[i],
			    vmcp->vmc_disks[j]) == 0) {
				log_warnx("vm \"%s\" disk %s cannot be reused",
				    name, vmc->vmc_disks[i]);
				return (EBUSY);
			}
		}
		vmc->vmc_checkaccess |= VMOP_CREATE_DISK;
	}

	/* interfaces */
	if (vmc->vmc_nnics > 0 &&
	    vm_checkinsflag(vmcp, VMOP_CREATE_NETWORK, uid) != 0 &&
	    vmc->vmc_nnics != vmcp->vmc_nnics) {
		log_warnx("vm \"%s\" no permission to set interfaces", name);
		return (EPERM);
	}
	for (i = 0; i < vmcp->vmc_nnics; i++) {
		/* Interface got overwritten */
		if (i < vmc->vmc_nnics)
			continue;

		/* Copy interface from parent */
		vmc->vmc_ifflags[i] = vmcp->vmc_ifflags[i];
		(void)strlcpy(vmc->vmc_ifnames[i], vmcp->vmc_ifnames[i],
		    sizeof(vmc->vmc_ifnames[i]));
		(void)strlcpy(vmc->vmc_ifswitch[i], vmcp->vmc_ifswitch[i],
		    sizeof(vmc->vmc_ifswitch[i]));
		(void)strlcpy(vmc->vmc_ifgroup[i], vmcp->vmc_ifgroup[i],
		    sizeof(vmc->vmc_ifgroup[i]));
		memcpy(vmc->vmc_macs[i], vmcp->vmc_macs[i],
		    sizeof(vmc->vmc_macs[i]));
		vmc->vmc_ifrdomain[i] = vmcp->vmc_ifrdomain[i];
		vmc->vmc_nnics++;
	}
	for (i = 0; i < vmc->vmc_nnics; i++) {
		for (j = 0; j < vmcp->vmc_nnics; j++) {
			if (memcmp(zero_mac, vmc->vmc_macs[i],
			    sizeof(vmc->vmc_macs[i])) != 0 &&
			    memcmp(vmcp->vmc_macs[i], vmc->vmc_macs[i],
			    sizeof(vmc->vmc_macs[i])) != 0) {
				log_warnx("vm \"%s\" lladdr cannot be reused",
				    name);
				return (EBUSY);
			}
			if (strlen(vmc->vmc_ifnames[i]) &&
			    strcmp(vmc->vmc_ifnames[i],
			    vmcp->vmc_ifnames[j]) == 0) {
				log_warnx("vm \"%s\" %s cannot be reused",
				    vmc->vmc_ifnames[i], name);
				return (EBUSY);
			}
		}
	}

	/* kernel */
	if (vmc->vmc_kernel > -1 || ((*vm_parent)->vm_kernel_path != NULL &&
		strnlen((*vm_parent)->vm_kernel_path, PATH_MAX) < PATH_MAX)) {
		if (vm_checkinsflag(vmcp, VMOP_CREATE_KERNEL, uid) != 0) {
			log_warnx("vm \"%s\" no permission to set boot image",
			    name);
			return (EPERM);
		}
		vmc->vmc_checkaccess |= VMOP_CREATE_KERNEL;
	}

	/* cdrom */
	if (strlen(vmc->vmc_cdrom) > 0) {
		if (vm_checkinsflag(vmcp, VMOP_CREATE_CDROM, uid) != 0) {
			log_warnx("vm \"%s\" no permission to set cdrom", name);
			return (EPERM);
		}
		vmc->vmc_checkaccess |= VMOP_CREATE_CDROM;
	} else if (strlcpy(vmc->vmc_cdrom, vmcp->vmc_cdrom,
	    sizeof(vmc->vmc_cdrom)) >= sizeof(vmc->vmc_cdrom)) {
		log_warnx("vm \"%s\" cdrom name too long", name);
		return (EINVAL);
	}

	/* user */
	if (vmc->vmc_owner.uid == 0)
		vmc->vmc_owner.uid = vmcp->vmc_owner.uid;
	else if (vmc->vmc_owner.uid != uid &&
	    vmc->vmc_owner.uid != vmcp->vmc_owner.uid) {
		log_warnx("vm \"%s\" user mismatch", name);
		return (EPERM);
	}

	/* group */
	if (vmc->vmc_owner.gid == 0)
		vmc->vmc_owner.gid = vmcp->vmc_owner.gid;
	else if (vmc->vmc_owner.gid != vmcp->vmc_owner.gid) {
		log_warnx("vm \"%s\" group mismatch", name);
		return (EPERM);
	}

	/* child instances */
	if (vmc->vmc_insflags) {
		log_warnx("vm \"%s\" cannot change instance permissions", name);
		return (EPERM);
	}
	if (vmcp->vmc_insflags & VMOP_CREATE_INSTANCE) {
		vmc->vmc_insowner.gid = vmcp->vmc_insowner.gid;
		vmc->vmc_insowner.uid = vmcp->vmc_insowner.gid;
		vmc->vmc_insflags = vmcp->vmc_insflags;
	} else {
		vmc->vmc_insowner.gid = 0;
		vmc->vmc_insowner.uid = 0;
		vmc->vmc_insflags = 0;
	}

	/* finished, remove instance flags */
	vmc->vmc_flags &= ~VMOP_CREATE_INSTANCE;

	return (0);
}

/*
 * vm_checkperm
 *
 * Checks if the user represented by the 'uid' parameter is allowed to
 * manipulate the VM described by the 'vm' parameter (or connect to said VM's
 * console.)
 *
 * Parameters:
 *  vm: the VM whose permission is to be checked
 *  vmo: the required uid/gid to be checked
 *  uid: the user ID of the user making the request
 *
 * Return values:
 *   0: the permission should be granted
 *  -1: the permission check failed (also returned if vm == null)
 */
int
vm_checkperm(struct vmd_vm *vm, struct vmop_owner *vmo, uid_t uid)
{
	struct group	*gr;
	struct passwd	*pw;
	char		**grmem;

	/* root has no restrictions */
	if (uid == 0)
		return (0);

	if (vmo == NULL)
		return (-1);

	/* check user */
	if (vm == NULL) {
		if  (vmo->uid == uid)
			return (0);
	} else {
		/*
		 * check user of running vm (the owner of a running vm can
		 * be different to (or more specific than) the configured owner.
		 */
		if (((vm->vm_state & VM_STATE_RUNNING) && vm->vm_uid == uid) ||
		    (!(vm->vm_state & VM_STATE_RUNNING) && vmo->uid == uid))
			return (0);
	}

	/* check groups */
	if (vmo->gid != -1) {
		if ((pw = getpwuid(uid)) == NULL)
			return (-1);
		if (pw->pw_gid == vmo->gid)
			return (0);
		if ((gr = getgrgid(vmo->gid)) != NULL) {
			for (grmem = gr->gr_mem; *grmem; grmem++)
				if (strcmp(*grmem, pw->pw_name) == 0)
					return (0);
		}
	}

	return (-1);
}

/*
 * vm_checkinsflag
 *
 * Checks whether the non-root user is allowed to set an instance option.
 *
 * Parameters:
 *  vmc: the VM create parameters
 *  flag: the flag to be checked
 *  uid: the user ID of the user making the request
 *
 * Return values:
 *   0: the permission should be granted
 *  -1: the permission check failed (also returned if vm == null)
 */
int
vm_checkinsflag(struct vmop_create_params *vmc, unsigned int flag, uid_t uid)
{
	/* root has no restrictions */
	if (uid == 0)
		return (0);

	if ((vmc->vmc_insflags & flag) == 0)
		return (-1);

	return (0);
}

/*
 * vm_checkaccess
 *
 * Checks if the user represented by the 'uid' parameter is allowed to
 * access the file described by the 'path' parameter.
 *
 * Parameters:
 *  fd: the file descriptor of the opened file
 *  uflag: check if the userid has access to the file
 *  uid: the user ID of the user making the request
 *  amode: the access flags of R_OK and W_OK
 *
 * Return values:
 *   0: the permission should be granted
 *  -1: the permission check failed
 */
int
vm_checkaccess(int fd, unsigned int uflag, uid_t uid, int amode)
{
	struct group	*gr;
	struct passwd	*pw;
	char		**grmem;
	struct stat	 st;
	mode_t		 mode;

	if (fd == -1)
		return (-1);

	/*
	 * File has to be accessible and a regular file
	 */
	if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode))
		return (-1);

	/* root has no restrictions */
	if (uid == 0 || uflag == 0)
		return (0);

	/* check other */
	mode = amode & W_OK ? S_IWOTH : 0;
	mode |= amode & R_OK ? S_IROTH : 0;
	if ((st.st_mode & mode) == mode)
		return (0);

	/* check user */
	mode = amode & W_OK ? S_IWUSR : 0;
	mode |= amode & R_OK ? S_IRUSR : 0;
	if (uid == st.st_uid && (st.st_mode & mode) == mode)
		return (0);

	/* check groups */
	mode = amode & W_OK ? S_IWGRP : 0;
	mode |= amode & R_OK ? S_IRGRP : 0;
	if ((st.st_mode & mode) != mode)
		return (-1);
	if ((pw = getpwuid(uid)) == NULL)
		return (-1);
	if (pw->pw_gid == st.st_gid)
		return (0);
	if ((gr = getgrgid(st.st_gid)) != NULL) {
		for (grmem = gr->gr_mem; *grmem; grmem++)
			if (strcmp(*grmem, pw->pw_name) == 0)
				return (0);
	}

	return (-1);
}

int
vm_opentty(struct vmd_vm *vm)
{
	struct stat		 st;
	struct group		*gr;
	uid_t			 uid;
	gid_t			 gid;
	mode_t			 mode;
	int			 on = 1, tty_slave;

	/*
	 * Open tty with pre-opened PTM fd
	 */
	if (fdopenpty(env->vmd_ptmfd, &vm->vm_tty, &tty_slave, vm->vm_ttyname,
	    NULL, NULL) == -1) {
		log_warn("fdopenpty");
		return (-1);
	}
	close(tty_slave);

	/*
	 * We use user ioctl(2) mode to pass break commands.
	 */
	if (ioctl(vm->vm_tty, TIOCUCNTL, &on) == -1) {
		log_warn("could not enable user ioctl mode on %s",
		    vm->vm_ttyname);
		goto fail;
	}

	uid = vm->vm_uid;
	gid = vm->vm_params.vmc_owner.gid;

	if (vm->vm_params.vmc_owner.gid != -1) {
		mode = 0660;
	} else if ((gr = getgrnam("tty")) != NULL) {
		gid = gr->gr_gid;
		mode = 0620;
	} else {
		mode = 0600;
		gid = 0;
	}

	log_debug("%s: vm %s tty %s uid %d gid %d mode %o",
	    __func__, vm->vm_params.vmc_params.vcp_name,
	    vm->vm_ttyname, uid, gid, mode);

	/*
	 * Change ownership and mode of the tty as required.
	 * Loosely based on the implementation of sshpty.c
	 */
	if (fstat(vm->vm_tty, &st) == -1) {
		log_warn("fstat failed for %s", vm->vm_ttyname);
		goto fail;
	}

	if (st.st_uid != uid || st.st_gid != gid) {
		if (chown(vm->vm_ttyname, uid, gid) == -1) {
			log_warn("chown %s %d %d failed, uid %d",
			    vm->vm_ttyname, uid, gid, getuid());

			/* Ignore failure on read-only filesystems */
			if (!((errno == EROFS) &&
			    (st.st_uid == uid || st.st_uid == 0)))
				goto fail;
		}
	}

	if ((st.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != mode) {
		if (chmod(vm->vm_ttyname, mode) == -1) {
			log_warn("chmod %s %o failed, uid %d",
			    vm->vm_ttyname, mode, getuid());

			/* Ignore failure on read-only filesystems */
			if (!((errno == EROFS) &&
			    (st.st_uid == uid || st.st_uid == 0)))
				goto fail;
		}
	}

	return (0);
 fail:
	vm_closetty(vm);
	return (-1);
}

void
vm_closetty(struct vmd_vm *vm)
{
	if (vm->vm_tty != -1) {
		/* Release and close the tty */
		if (fchown(vm->vm_tty, 0, 0) == -1)
			log_warn("chown %s 0 0 failed", vm->vm_ttyname);
		if (fchmod(vm->vm_tty, 0666) == -1)
			log_warn("chmod %s 0666 failed", vm->vm_ttyname);
		close(vm->vm_tty);
		vm->vm_tty = -1;
	}
	memset(&vm->vm_ttyname, 0, sizeof(vm->vm_ttyname));
}

void
switch_remove(struct vmd_switch *vsw)
{
	if (vsw == NULL)
		return;

	TAILQ_REMOVE(env->vmd_switches, vsw, sw_entry);

	free(vsw->sw_group);
	free(vsw->sw_name);
	free(vsw);
}

struct vmd_switch *
switch_getbyname(const char *name)
{
	struct vmd_switch	*vsw;

	if (name == NULL)
		return (NULL);
	TAILQ_FOREACH(vsw, env->vmd_switches, sw_entry) {
		if (strcmp(vsw->sw_name, name) == 0)
			return (vsw);
	}

	return (NULL);
}

uint32_t
prefixlen2mask(uint8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	if (prefixlen > 32)
		prefixlen = 32;

	return (htonl(0xffffffff << (32 - prefixlen)));
}

void
prefixlen2mask6(uint8_t prefixlen, struct in6_addr *mask)
{
	struct in6_addr	 s6;
	int		 i;

	if (prefixlen > 128)
		prefixlen = 128;

	memset(&s6, 0, sizeof(s6));
	for (i = 0; i < prefixlen / 8; i++)
		s6.s6_addr[i] = 0xff;
	i = prefixlen % 8;
	if (i)
		s6.s6_addr[prefixlen / 8] = 0xff00 >> i;

	memcpy(mask, &s6, sizeof(s6));
}

void
getmonotime(struct timeval *tv)
{
	struct timespec	 ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		fatal("clock_gettime");

	TIMESPEC_TO_TIMEVAL(tv, &ts);
}

static inline void
vm_terminate(struct vmd_vm *vm, const char *caller)
{
	if (vm->vm_from_config)
		vm_stop(vm, 0, caller);
	else {
		/* vm_remove calls vm_stop */
		vm_remove(vm, caller);
	}
}

/*
 * Utility function for closing vm file descriptors. Assumes an fd of -1 was
 * already closed or never opened.
 *
 * Returns 0 on success, otherwise -1 on failure.
 */
int
close_fd(int fd)
{
	int	ret;

	if (fd == -1)
		return (0);

#ifdef POSIX_CLOSE_RESTART
	do { ret = close(fd); } while (ret == -1 && errno == EINTR);
#else
	ret = close(fd);
#endif /* POSIX_CLOSE_RESTART */

	if (ret == -1 && errno == EIO)
		log_warn("%s(%d)", __func__, fd);

	return (ret);
}


void
vmop_result_read(struct imsg *imsg, struct vmop_result *vmr)
{
	if (imsg_get_data(imsg, vmr, sizeof(*vmr)))
		fatal("%s", __func__);

	vmr->vmr_ttyname[sizeof(vmr->vmr_ttyname) - 1] = '\0';
}

void
vmop_info_result_read(struct imsg *imsg, struct vmop_info_result *vir)
{
	struct vm_info_result *r;

	if (imsg_get_data(imsg, vir, sizeof(*vir)))
		fatal("%s", __func__);

	r = &vir->vir_info;
	r->vir_name[sizeof(r->vir_name) - 1] = '\0';

	vir->vir_ttyname[sizeof(vir->vir_ttyname) - 1] = '\0';
}

void
vmop_id_read(struct imsg *imsg, struct vmop_id *vid)
{
	if (imsg_get_data(imsg, vid, sizeof(*vid)))
		fatal("%s", __func__);

	vid->vid_name[sizeof(vid->vid_name) - 1] = '\0';
}

void
vmop_ifreq_read(struct imsg *imsg, struct vmop_ifreq *vfr)
{
	if (imsg_get_data(imsg, vfr, sizeof(*vfr)))
		fatal("%s", __func__);

	vfr->vfr_name[sizeof(vfr->vfr_name) - 1] = '\0';
	vfr->vfr_value[sizeof(vfr->vfr_value) - 1] = '\0';
}

void
vmop_addr_req_read(struct imsg *imsg, struct vmop_addr_req *var)
{
	if (imsg_get_data(imsg, var, sizeof(*var)))
		fatal("%s", __func__);
}

void
vmop_addr_result_read(struct imsg *imsg, struct vmop_addr_result *var)
{
	if (imsg_get_data(imsg, var, sizeof(*var)))
		fatal("%s", __func__);
}

void
vmop_owner_read(struct imsg *imsg, struct vmop_owner *vo)
{
	if (imsg_get_data(imsg, vo, sizeof(*vo)))
		fatal("%s", __func__);
}

void
vmop_create_params_read(struct imsg *imsg, struct vmop_create_params *vmc)
{
	struct vm_create_params *vcp;
	size_t i, n;

	if (imsg_get_data(imsg, vmc, sizeof(*vmc)))
		fatal("%s", __func__);

	vcp = &vmc->vmc_params;
	vcp->vcp_name[sizeof(vcp->vcp_name) - 1] = '\0';

	n = sizeof(vmc->vmc_disks) / sizeof(vmc->vmc_disks[0]);
	for (i = 0; i < n; i++)
		vmc->vmc_disks[i][sizeof(vmc->vmc_disks[i]) - 1] = '\0';

	n = sizeof(vmc->vmc_ifnames) / sizeof(vmc->vmc_ifnames[0]);
	for (i = 0; i < n; i++)
		vmc->vmc_ifnames[i][sizeof(vmc->vmc_ifnames[i]) - 1] = '\0';

	n = sizeof(vmc->vmc_ifswitch) / sizeof(vmc->vmc_ifswitch[0]);
	for (i = 0; i < n; i++)
		vmc->vmc_ifswitch[i][sizeof(vmc->vmc_ifswitch[i]) - 1] = '\0';

	n = sizeof(vmc->vmc_ifgroup) / sizeof(vmc->vmc_ifgroup[0]);
	for (i = 0; i < n; i++)
		vmc->vmc_ifgroup[i][sizeof(vmc->vmc_ifgroup[i]) - 1] = '\0';

	vmc->vmc_instance[sizeof(vmc->vmc_instance) - 1] = '\0';
}

void
vmop_config_read(struct imsg *imsg, struct vmd_config *cfg)
{
	struct vmd_agentx *ax;

	if (imsg_get_data(imsg, cfg, sizeof(*cfg)))
		fatal("%s", __func__);

	ax = &cfg->cfg_agentx;
	ax->ax_path[sizeof(ax->ax_path) - 1] = '\0';
	ax->ax_context[sizeof(ax->ax_context) - 1] = '\0';
}
