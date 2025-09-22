/*	$OpenBSD: vmm.c,v 1.133 2025/08/13 10:26:31 dv Exp $	*/

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
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include <dev/vmm/vmm.h>

#include <net/if.h>

#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <imsg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vmd.h"
#include "atomicio.h"
#include "proc.h"

void	vmm_sighdlr(int, short, void *);
int	vmm_start_vm(struct imsg *, uint32_t *, pid_t *);
int	vmm_dispatch_parent(int, struct privsep_proc *, struct imsg *);
void	vmm_run(struct privsep *, struct privsep_proc *, void *);
void	vmm_dispatch_vm(int, short, void *);
int	terminate_vm(struct vm_terminate_params *);
int	get_info_vm(struct privsep *, struct imsg *, int);
int	opentap(char *);

extern struct vmd *env;

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	vmm_dispatch_parent  },
};

void
vmm(struct privsep *ps, struct privsep_proc *p)
{
	proc_run(ps, p, procs, nitems(procs), vmm_run, NULL);
}

void
vmm_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	if (config_init(ps->ps_env) == -1)
		fatal("failed to initialize configuration");

	/*
	 * We aren't root, so we can't chroot(2). Use unveil(2) instead.
	 */
	if (unveil(env->argv0, "x") == -1)
		fatal("unveil %s", env->argv0);
	if (unveil(NULL, NULL) == -1)
		fatal("unveil lock");

	/*
	 * pledge in the vmm process:
	 * stdio - for malloc and basic I/O including events.
	 * vmm - for the vmm ioctls and operations.
	 * proc, exec - for forking and execing new vm's.
	 * sendfd - for sending send/recv fds to vm proc.
	 * recvfd - for disks, interfaces and other fds.
	 */
	if (pledge("stdio vmm sendfd recvfd proc exec", NULL) == -1)
		fatal("pledge");

	signal_del(&ps->ps_evsigchld);
	signal_set(&ps->ps_evsigchld, SIGCHLD, vmm_sighdlr, ps);
	signal_add(&ps->ps_evsigchld, NULL);
}

int
vmm_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	struct privsep		*ps = p->p_ps;
	int			 res = 0, cmd = 0, verbose;
	struct vmd_vm		*vm = NULL;
	struct vm_terminate_params vtp;
	struct vmop_id		 vid;
	struct vmop_result	 vmr;
	struct vmop_addr_result  var;
	uint32_t		 id = 0, vm_id, type;
	pid_t			 pid, vm_pid = 0;
	unsigned int		 mode, flags;

	pid = imsg_get_pid(imsg);
	type = imsg_get_type(imsg);

	/* Parent uses peer id to communicate vm id. */
	vm_id = imsg_get_id(imsg);

	switch (type) {
	case IMSG_VMDOP_START_VM_REQUEST:
		res = config_getvm(ps, imsg);
		if (res == -1) {
			res = errno;
			cmd = IMSG_VMDOP_START_VM_RESPONSE;
		}
		break;
	case IMSG_VMDOP_START_VM_CDROM:
		res = config_getcdrom(ps, imsg);
		if (res == -1) {
			res = errno;
			cmd = IMSG_VMDOP_START_VM_RESPONSE;
		}
		break;
	case IMSG_VMDOP_START_VM_DISK:
		res = config_getdisk(ps, imsg);
		if (res == -1) {
			res = errno;
			cmd = IMSG_VMDOP_START_VM_RESPONSE;
		}
		break;
	case IMSG_VMDOP_START_VM_IF:
		res = config_getif(ps, imsg);
		if (res == -1) {
			res = errno;
			cmd = IMSG_VMDOP_START_VM_RESPONSE;
		}
		break;
	case IMSG_VMDOP_START_VM_END:
		res = vmm_start_vm(imsg, &id, &vm_pid);
		/* Check if the ID can be mapped correctly */
		if (res == 0 && (id = vm_id2vmid(id, NULL)) == 0)
			res = ENOENT;
		cmd = IMSG_VMDOP_START_VM_RESPONSE;
		break;
	case IMSG_VMDOP_TERMINATE_VM_REQUEST:
		vmop_id_read(imsg, &vid);
		id = vid.vid_id;
		flags = vid.vid_flags;

		DPRINTF("%s: recv'ed TERMINATE_VM for %d", __func__, id);

		cmd = IMSG_VMDOP_TERMINATE_VM_RESPONSE;

		if (id == 0) {
			res = ENOENT;
		} else if ((vm = vm_getbyvmid(id)) != NULL) {
			if (flags & VMOP_FORCE) {
				vtp.vtp_vm_id = vm_vmid2id(vm->vm_vmid, vm);
				vm->vm_state |= VM_STATE_SHUTDOWN;
				(void)terminate_vm(&vtp);
				res = 0;
			} else if (!(vm->vm_state & VM_STATE_SHUTDOWN)) {
				log_debug("%s: sending shutdown request"
				    " to vm %d", __func__, id);

				/*
				 * Request reboot but mark the VM as shutting
				 * down. This way we can terminate the VM after
				 * the triple fault instead of reboot and
				 * avoid being stuck in the ACPI-less powerdown
				 * ("press any key to reboot") of the VM.
				 */
				vm->vm_state |= VM_STATE_SHUTDOWN;
				if (imsg_compose_event(&vm->vm_iev,
				    IMSG_VMDOP_VM_REBOOT,
				    0, 0, -1, NULL, 0) == -1)
					res = errno;
				else
					res = 0;
			} else {
				/*
				 * VM is currently being shutdown.
				 * Check to see if the VM process is still
				 * active.  If not, return VMD_VM_STOP_INVALID.
				 */
				if (vm_vmid2id(vm->vm_vmid, vm) == 0) {
					log_debug("%s: no vm running anymore",
					    __func__);
					res = VMD_VM_STOP_INVALID;
				}
			}
		} else {
			/* VM doesn't exist, cannot stop vm */
			log_debug("%s: cannot stop vm that is not running",
			    __func__);
			res = VMD_VM_STOP_INVALID;
		}
		break;
	case IMSG_VMDOP_GET_INFO_VM_REQUEST:
		res = get_info_vm(ps, imsg, 0);
		cmd = IMSG_VMDOP_GET_INFO_VM_END_DATA;
		break;
	case IMSG_VMDOP_CONFIG:
		config_getconfig(env, imsg);
		break;
	case IMSG_CTL_RESET:
		mode = imsg_uint_read(imsg);
		if (mode & CONFIG_VMS) {
			/* Terminate and remove all VMs */
			vmm_shutdown();
			mode &= ~CONFIG_VMS;
		}
		config_purge(env, mode);
		break;
	case IMSG_CTL_VERBOSE:
		verbose = imsg_int_read(imsg);
		log_setverbose(verbose);
		env->vmd_verbose = verbose;
		/* Forward message to each VM process */
		TAILQ_FOREACH(vm, env->vmd_vms, vm_entry) {
			imsg_compose_event(&vm->vm_iev, type, -1, pid, -1,
			    &verbose, sizeof(verbose));
		}
		break;
	case IMSG_VMDOP_PAUSE_VM:
		vmop_id_read(imsg, &vid);
		id = vid.vid_id;
		if ((vm = vm_getbyvmid(id)) == NULL) {
			res = ENOENT;
			cmd = IMSG_VMDOP_PAUSE_VM_RESPONSE;
			break;
		}
		imsg_compose_event(&vm->vm_iev, type, -1, pid,
		    imsg_get_fd(imsg), &vid, sizeof(vid));
		break;
	case IMSG_VMDOP_UNPAUSE_VM:
		vmop_id_read(imsg, &vid);
		id = vid.vid_id;
		if ((vm = vm_getbyvmid(id)) == NULL) {
			res = ENOENT;
			cmd = IMSG_VMDOP_UNPAUSE_VM_RESPONSE;
			break;
		}
		imsg_compose_event(&vm->vm_iev, type, -1, pid,
		    imsg_get_fd(imsg), &vid, sizeof(vid));
		break;
	case IMSG_VMDOP_PRIV_GET_ADDR_RESPONSE:
		vmop_addr_result_read(imsg, &var);
		if ((vm = vm_getbyvmid(var.var_vmid)) == NULL) {
			res = ENOENT;
			break;
		}
		/* Forward hardware address details to the guest vm */
		imsg_compose_event(&vm->vm_iev, type, -1, pid,
		    imsg_get_fd(imsg), &var, sizeof(var));
		break;
	case IMSG_VMDOP_RECEIVE_VMM_FD:
		if (env->vmd_fd > -1)
			fatalx("already received vmm fd");
		env->vmd_fd = imsg_get_fd(imsg);

		/* Get and terminate all running VMs */
		get_info_vm(ps, NULL, 1);
		break;
	case IMSG_VMDOP_RECEIVE_PSP_FD:
		if (env->vmd_psp_fd > -1)
			fatalx("already received psp fd");
		env->vmd_psp_fd = imsg_get_fd(imsg);
		break;
	default:
		return (-1);
	}

	switch (cmd) {
	case 0:
		break;
	case IMSG_VMDOP_START_VM_RESPONSE:
		if (res != 0) {
			/* Remove local reference if it exists */
			if ((vm = vm_getbyvmid(vm_id)) != NULL) {
				log_debug("%s: removing vm, START_VM_RESPONSE",
				    __func__);
				vm_remove(vm, __func__);
			}
		}
		if (id == 0)
			id = vm_id;
		/* FALLTHROUGH */
	case IMSG_VMDOP_PAUSE_VM_RESPONSE:
	case IMSG_VMDOP_UNPAUSE_VM_RESPONSE:
	case IMSG_VMDOP_TERMINATE_VM_RESPONSE:
		memset(&vmr, 0, sizeof(vmr));
		vmr.vmr_result = res;
		vmr.vmr_id = id;
		vmr.vmr_pid = vm_pid;
		if (proc_compose_imsg(ps, PROC_PARENT, cmd, vm_id, -1, &vmr,
		    sizeof(vmr)) == -1)
			return (-1);
		break;
	default:
		if (proc_compose_imsg(ps, PROC_PARENT, cmd, vm_id, -1, &res,
		    sizeof(res)) == -1)
			return (-1);
		break;
	}

	return (0);
}

void
vmm_sighdlr(int sig, short event, void *arg)
{
	struct privsep *ps = arg;
	int status, ret = 0;
	uint32_t vmid;
	pid_t pid;
	struct vmop_result vmr;
	struct vmd_vm *vm;
	struct vm_terminate_params vtp;

	log_debug("%s: handling signal %d", __func__, sig);
	switch (sig) {
	case SIGCHLD:
		do {
			pid = waitpid(-1, &status, WNOHANG);
			if (pid <= 0)
				continue;

			if (WIFEXITED(status) || WIFSIGNALED(status)) {
				vm = vm_getbypid(pid);
				if (vm == NULL) {
					/*
					 * If the VM is gone already, it
					 * got terminated via a
					 * IMSG_VMDOP_TERMINATE_VM_REQUEST.
					 */
					continue;
				}

				if (WIFEXITED(status))
					ret = WEXITSTATUS(status);

				/* Don't reboot on pending shutdown */
				if (ret == EAGAIN &&
				    (vm->vm_state & VM_STATE_SHUTDOWN))
					ret = 0;

				vmid = vm->vm_params.vmc_params.vcp_id;
				vtp.vtp_vm_id = vmid;

				if (terminate_vm(&vtp) == 0)
					log_debug("%s: terminated vm %s"
					    " (id %d)", __func__,
					    vm->vm_params.vmc_params.vcp_name,
					    vm->vm_vmid);

				memset(&vmr, 0, sizeof(vmr));
				vmr.vmr_result = ret;
				vmr.vmr_id = vm_id2vmid(vmid, vm);
				if (proc_compose_imsg(ps, PROC_PARENT,
				    IMSG_VMDOP_TERMINATE_VM_EVENT,
				    vm->vm_peerid, -1, &vmr, sizeof(vmr)) == -1)
					log_warnx("could not signal "
					    "termination of VM %u to "
					    "parent", vm->vm_vmid);

				vm_remove(vm, __func__);
			} else
				fatalx("unexpected cause of SIGCHLD");
		} while (pid > 0 || (pid == -1 && errno == EINTR));
		break;
	default:
		fatalx("unexpected signal");
	}
}

/*
 * vmm_shutdown
 *
 * Terminate VMs on shutdown to avoid "zombie VM" processes.
 */
void
vmm_shutdown(void)
{
	struct vm_terminate_params vtp;
	struct vmd_vm *vm, *vm_next;

	TAILQ_FOREACH_SAFE(vm, env->vmd_vms, vm_entry, vm_next) {
		vtp.vtp_vm_id = vm_vmid2id(vm->vm_vmid, vm);

		/* XXX suspend or request graceful shutdown */
		(void)terminate_vm(&vtp);
		vm_remove(vm, __func__);
	}
}

/*
 * vmm_pipe
 *
 * Create a new imsg control channel between vmm parent and a VM
 * (can be called on both sides).
 */
int
vmm_pipe(struct vmd_vm *vm, int fd, void (*cb)(int, short, void *))
{
	struct imsgev	*iev = &vm->vm_iev;

	/*
	 * Set to close-on-exec as vmm_pipe is used after fork+exec to
	 * establish async ipc between vm and vmd's vmm process. This
	 * prevents future vm processes or virtio subprocesses from
	 * inheriting this control channel.
	 */
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
		log_warn("failed to set close-on-exec for vmm ipc channel");
		return (-1);
	}

	if (imsgbuf_init(&iev->ibuf, fd) == -1) {
		log_warn("failed to init imsgbuf");
		return (-1);
	}
	imsgbuf_allow_fdpass(&iev->ibuf);
	iev->handler = cb;
	iev->data = vm;
	imsg_event_add(iev);

	return (0);
}

/*
 * vmm_dispatch_vm
 *
 * imsg callback for messages that are received from a VM child process.
 */
void
vmm_dispatch_vm(int fd, short event, void *arg)
{
	struct vmd_vm		*vm = arg;
	struct imsgev		*iev = &vm->vm_iev;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	ssize_t			 n;
	unsigned int		 i;
	uint32_t		 type;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("%s: imsgbuf_read", __func__);
		if (n == 0) {
			/* This pipe is dead, so remove the event handler */
			event_del(&iev->ev);
			return;
		}
	}

	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE) {
				/* This pipe is dead, remove the handler */
				event_del(&iev->ev);
				return;
			}
			fatal("%s: imsgbuf_write fd %d", __func__, ibuf->fd);
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get", __func__);
		if (n == 0)
			break;

		type = imsg_get_type(&imsg);
		switch (type) {
		case IMSG_VMDOP_VM_SHUTDOWN:
			vm->vm_state |= VM_STATE_SHUTDOWN;
			break;
		case IMSG_VMDOP_VM_REBOOT:
			vm->vm_state &= ~VM_STATE_SHUTDOWN;
			break;
		case IMSG_VMDOP_PAUSE_VM_RESPONSE:
		case IMSG_VMDOP_UNPAUSE_VM_RESPONSE:
			for (i = 0; i < nitems(procs); i++) {
				if (procs[i].p_id == PROC_PARENT) {
					proc_forward_imsg(procs[i].p_ps,
					    &imsg, PROC_PARENT, -1);
					break;
				}
			}
			break;

		default:
			fatalx("%s: got invalid imsg %d from %s", __func__,
			    type, vm->vm_params.vmc_params.vcp_name);
		}
		imsg_free(&imsg);
	}
	imsg_event_add(iev);
}

/*
 * terminate_vm
 *
 * Requests vmm(4) to terminate the VM whose ID is provided in the
 * supplied vm_terminate_params structure (vtp->vtp_vm_id)
 *
 * Parameters
 *  vtp: vm_terminate_params struct containing the ID of the VM to terminate
 *
 * Return values:
 *  0: success
 *  !0: ioctl to vmm(4) failed (eg, ENOENT if the supplied VM is not valid)
 */
int
terminate_vm(struct vm_terminate_params *vtp)
{
	if (ioctl(env->vmd_fd, VMM_IOC_TERM, vtp) == -1)
		return (errno);

	return (0);
}

/*
 * opentap
 *
 * Opens the next available tap device, up to MAX_TAP.
 *
 * Parameters
 *  ifname: a buffer of at least IF_NAMESIZE bytes.
 *
 * Returns a file descriptor to the tap node opened or -1 if no tap devices were
 * available, setting errno to the open(2) error.
 */
int
opentap(char *ifname)
{
	int err = 0, i, fd;
	char path[PATH_MAX];

	for (i = 0; i < MAX_TAP; i++) {
		snprintf(path, PATH_MAX, "/dev/tap%d", i);

		errno = 0;
		fd = open(path, O_RDWR | O_NONBLOCK);
		if (fd != -1)
			break;
		err = errno;
		if (err == EBUSY) {
			/* Busy...try next tap. */
			continue;
		} else if (err == ENOENT) {
			/* Ran out of /dev/tap* special files. */
			break;
		} else {
			log_warn("%s: unexpected error", __func__);
			break;
		}
	}

	/* Record the last opened tap device. */
	snprintf(ifname, IF_NAMESIZE, "tap%d", i);

	if (err)
		errno = err;
	return (fd);
}

/*
 * vmm_start_vm
 *
 * Prepares and fork+execs a new VM process.
 *
 * Parameters:
 *  imsg: The VM data structure that is including the VM create parameters.
 *  id: Returns the VM id as reported by the kernel and obtained from the VM.
 *  pid: Returns the VM pid to the parent.
 *
 * Return values:
 *  0: success
 *  !0: failure - typically an errno indicating the source of the failure
 */
int
vmm_start_vm(struct imsg *imsg, uint32_t *id, pid_t *pid)
{
	struct vm_create_params	*vcp;
	struct vmd_vm		*vm;
	char			*nargv[10], num[32], vmm_fd[32], psp_fd[32];
	int			 fd, ret = EINVAL;
	int			 fds[2];
	pid_t			 vm_pid;
	size_t			 i, j, sz;
	uint32_t		 peer_id;

	peer_id = imsg_get_id(imsg);
	if ((vm = vm_getbyvmid(peer_id)) == NULL) {
		log_warnx("%s: can't find vm", __func__);
		return (ENOENT);
	}
	vcp = &vm->vm_params.vmc_params;

	if ((vm->vm_tty = imsg_get_fd(imsg)) == -1) {
		log_warnx("%s: can't get tty", __func__);
		goto err;
	}


	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC, fds)
	    == -1)
		fatal("socketpair");

	/* Start child vmd for this VM (fork, chroot, drop privs) */
	vm_pid = fork();
	if (vm_pid == -1) {
		log_warn("%s: start child failed", __func__);
		ret = EIO;
		goto err;
	}

	if (vm_pid > 0) {
		/* Parent */
		vm->vm_pid = vm_pid;
		close_fd(fds[1]);

		/* Send the details over the pipe to the child. */
		sz = atomicio(vwrite, fds[0], vm, sizeof(*vm));
		if (sz != sizeof(*vm)) {
			log_warnx("%s: failed to send config for vm '%s'",
			    __func__, vcp->vcp_name);
			ret = EIO;
			/* Defer error handling until after fd closing. */
		}

		/* As the parent/vmm process, we no longer need these fds. */
		for (i = 0 ; i < vm->vm_params.vmc_ndisks; i++) {
			for (j = 0; j < VM_MAX_BASE_PER_DISK; j++) {
				if (close_fd(vm->vm_disks[i][j]) == 0)
				    vm->vm_disks[i][j] = -1;
			}
		}
		for (i = 0 ; i < vm->vm_params.vmc_nnics; i++) {
			if (close_fd(vm->vm_ifs[i].vif_fd) == 0)
			    vm->vm_ifs[i].vif_fd = -1;
		}
		if (close_fd(vm->vm_kernel) == 0)
			vm->vm_kernel = -1;
		if (close_fd(vm->vm_cdrom) == 0)
			vm->vm_cdrom = -1;
		if (close_fd(vm->vm_tty) == 0)
			vm->vm_tty = -1;

		/* Deferred error handling from sending the vm struct. */
		if (ret == EIO)
			goto err;

		/* Send the current local prefix configuration. */
		sz = atomicio(vwrite, fds[0], &env->vmd_cfg.cfg_localprefix,
		    sizeof(env->vmd_cfg.cfg_localprefix));
		if (sz != sizeof(env->vmd_cfg.cfg_localprefix)) {
			log_warnx("%s: failed to send local prefix for vm '%s'",
			    __func__, vcp->vcp_name);
			ret = EIO;
			goto err;
		}

		/* Read back the kernel-generated vm id from the child */
		sz = atomicio(read, fds[0], &vcp->vcp_id, sizeof(vcp->vcp_id));
		if (sz != sizeof(vcp->vcp_id)) {
			log_debug("%s: failed to receive vm id from vm %s",
			    __func__, vcp->vcp_name);
			/* vmd could not allocate memory for the vm. */
			ret = ENOMEM;
			goto err;
		}

		/* Check for an invalid id. This indicates child failure. */
		if (vcp->vcp_id == 0)
			goto err;

		*id = vcp->vcp_id;
		*pid = vm->vm_pid;

		/* Wire up our pipe into the event handling. */
		if (vmm_pipe(vm, fds[0], vmm_dispatch_vm) == -1)
			fatal("setup vm pipe");
	} else {
		/* Child. Create a new session. */
		if (setsid() == -1)
			fatal("setsid");

		close_fd(fds[0]);
		close_fd(PROC_PARENT_SOCK_FILENO);

		/* Detach from terminal. */
		if (!env->vmd_debug && (fd =
			open("/dev/null", O_RDWR, 0)) != -1) {
			dup2(fd, STDIN_FILENO);
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			if (fd > 2)
				close(fd);
		}

		if (env->vmd_psp_fd > 0)
			fcntl(env->vmd_psp_fd, F_SETFD, 0); /* psp device fd */

		/*
		 * Prepare our new argv for execvp(2) with the fd of our open
		 * pipe to the parent/vmm process as an argument.
		 */
		memset(num, 0, sizeof(num));
		snprintf(num, sizeof(num), "%d", fds[1]);
		memset(vmm_fd, 0, sizeof(vmm_fd));
		snprintf(vmm_fd, sizeof(vmm_fd), "%d", env->vmd_fd);
		memset(psp_fd, 0, sizeof(psp_fd));
		snprintf(psp_fd, sizeof(psp_fd), "%d", env->vmd_psp_fd);

		i = 0;
		nargv[i++] = env->argv0;
		nargv[i++] = "-V";
		nargv[i++] = num;
		nargv[i++] = "-i";
		nargv[i++] = vmm_fd;
		nargv[i++] = "-j";
		nargv[i++] = psp_fd;
		if (env->vmd_debug)
			nargv[i++] = "-d";
		if (env->vmd_verbose == 1)
			nargv[i++] = "-v";
		else if (env->vmd_verbose > 1)
			nargv[i++] = "-vv";
		nargv[i++] = NULL;
		if (i > sizeof(nargv) / sizeof(nargv[0]))
			fatalx("%s: nargv overflow", __func__);

		/* Control resumes in vmd main(). */
		execvp(nargv[0], nargv);

		ret = errno;
		log_warn("execvp %s", nargv[0]);
		_exit(ret);
		/* NOTREACHED */
	}

	return (0);

 err:
	if (!vm->vm_from_config)
		vm_remove(vm, __func__);

	return (ret);
}

/*
 * get_info_vm
 *
 * Returns a list of VMs known to vmm(4).
 *
 * Parameters:
 *  ps: the privsep context.
 *  imsg: the received imsg including the peer id.
 *  terminate: terminate the listed vm.
 *
 * Return values:
 *  0: success
 *  !0: failure (eg, ENOMEM, EIO or another error code from vmm(4) ioctl)
 */
int
get_info_vm(struct privsep *ps, struct imsg *imsg, int terminate)
{
	int ret;
	size_t ct, i;
	struct vm_info_params vip;
	struct vm_info_result *info;
	struct vm_terminate_params vtp;
	struct vmop_info_result vir;
	uint32_t peer_id;

	/*
	 * We issue the VMM_IOC_INFO ioctl twice, once with an input
	 * buffer size of 0, which results in vmm(4) returning the
	 * number of bytes required back to us in vip.vip_size,
	 * and then we call it again after malloc'ing the required
	 * number of bytes.
	 *
	 * It is possible that we could fail a second time (e.g. if
	 * another VM was created in the instant between the two
	 * ioctls, but in that case the caller can just try again
	 * as vmm(4) will return a zero-sized list in that case.
	 */
	vip.vip_size = 0;
	info = NULL;
	ret = 0;
	memset(&vir, 0, sizeof(vir));

	/* First ioctl to see how many bytes needed (vip.vip_size) */
	if (ioctl(env->vmd_fd, VMM_IOC_INFO, &vip) == -1)
		return (errno);

	if (vip.vip_info_ct != 0)
		return (EIO);

	info = malloc(vip.vip_size);
	if (info == NULL)
		return (ENOMEM);

	/* Second ioctl to get the actual list */
	vip.vip_info = info;
	if (ioctl(env->vmd_fd, VMM_IOC_INFO, &vip) == -1) {
		ret = errno;
		free(info);
		return (ret);
	}

	/* Return info */
	ct = vip.vip_size / sizeof(struct vm_info_result);
	for (i = 0; i < ct; i++) {
		if (terminate) {
			vtp.vtp_vm_id = info[i].vir_id;
			if ((ret = terminate_vm(&vtp)) != 0)
				break;
			log_debug("%s: terminated vm %s (id %d)", __func__,
			    info[i].vir_name, info[i].vir_id);
			continue;
		}
		memcpy(&vir.vir_info, &info[i], sizeof(vir.vir_info));
		vir.vir_info.vir_id = vm_id2vmid(info[i].vir_id, NULL);
		peer_id = imsg_get_id(imsg);

		if (proc_compose_imsg(ps, PROC_PARENT,
		    IMSG_VMDOP_GET_INFO_VM_DATA, peer_id, -1,
		    &vir, sizeof(vir)) == -1) {
			ret = EIO;
			break;
		}
	}
	free(info);

	return (ret);
}
