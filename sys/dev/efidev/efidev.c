/*-
 * Copyright (c) 2016 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <machine/efi.h>
#include <sys/efiio.h>

static d_ioctl_t efidev_ioctl;

static struct cdevsw efi_cdevsw = {
	.d_name = "efi",
	.d_version = D_VERSION,
	.d_ioctl = efidev_ioctl,
};
	
static int
efidev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t addr,
    int flags __unused, struct thread *td __unused)
{
	int error;

	switch (cmd) {
	case EFIIOC_GET_TABLE:
	{
		struct efi_get_table_ioc *egtioc =
		    (struct efi_get_table_ioc *)addr;

		error = efi_get_table(&egtioc->uuid, &egtioc->ptr);
		break;
	}
	case EFIIOC_GET_TIME:
	{
		struct efi_tm *tm = (struct efi_tm *)addr;

		error = efi_get_time(tm);
		break;
	}
	case EFIIOC_SET_TIME:
	{
		struct efi_tm *tm = (struct efi_tm *)addr;

		error = efi_set_time(tm);
		break;
	}
	case EFIIOC_VAR_GET:
	{
		struct efi_var_ioc *ev = (struct efi_var_ioc *)addr;
		void *data;
		efi_char *name;

		data = malloc(ev->datasize, M_TEMP, M_WAITOK);
		name = malloc(ev->namesize, M_TEMP, M_WAITOK);
		error = copyin(ev->name, name, ev->namesize);
		if (error)
			goto vg_out;
		if (name[ev->namesize / sizeof(efi_char) - 1] != 0) {
			error = EINVAL;
			goto vg_out;
		}

		error = efi_var_get(name, &ev->vendor, &ev->attrib,
		    &ev->datasize, data);

		if (error == 0) {
			error = copyout(data, ev->data, ev->datasize);
		} else if (error == EOVERFLOW) {
			/*
			 * Pass back the size we really need, but
			 * convert the error to 0 so the copyout
			 * happens. datasize was updated in the
			 * efi_var_get call.
			 */
			ev->data = NULL;
			error = 0;
		}
vg_out:
		free(data, M_TEMP);
		free(name, M_TEMP);
		break;
	}
	case EFIIOC_VAR_NEXT:
	{
		struct efi_var_ioc *ev = (struct efi_var_ioc *)addr;
		efi_char *name;

		name = malloc(ev->namesize, M_TEMP, M_WAITOK);
		error = copyin(ev->name, name, ev->namesize);
		if (error)
			goto vn_out;
		/* Note: namesize is the buffer size, not the string lenght */

		error = efi_var_nextname(&ev->namesize, name, &ev->vendor);
		if (error == 0) {
			error = copyout(name, ev->name, ev->namesize);
		} else if (error == EOVERFLOW) {
			ev->name = NULL;
			error = 0;
		}
	vn_out:
		free(name, M_TEMP);
		break;
	}
	case EFIIOC_VAR_SET:
	{
		struct efi_var_ioc *ev = (struct efi_var_ioc *)addr;
		void *data = NULL;
		efi_char *name;

		/* datasize == 0 -> delete (more or less) */
		if (ev->datasize > 0)
			data = malloc(ev->datasize, M_TEMP, M_WAITOK);
		name = malloc(ev->namesize, M_TEMP, M_WAITOK);
		if (ev->datasize) {
			error = copyin(ev->data, data, ev->datasize);
			if (error)
				goto vs_out;
		}
		error = copyin(ev->name, name, ev->namesize);
		if (error)
			goto vs_out;
		if (name[ev->namesize / sizeof(efi_char) - 1] != 0) {
			error = EINVAL;
			goto vs_out;
		}

		error = efi_var_set(name, &ev->vendor, ev->attrib, ev->datasize,
		    data);
vs_out:
		free(data, M_TEMP);
		free(name, M_TEMP);
		break;
	}
	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static struct cdev *efidev;

static int
efidev_modevents(module_t m, int event, void *arg __unused)
{
	struct make_dev_args mda;
	int error;

	switch (event) {
	case MOD_LOAD:
		/*
		 * If we have no efi environment, then don't create the device.
		 */
		if (efi_rt_ok() != 0)
			return (0);
		make_dev_args_init(&mda);
		mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
		mda.mda_devsw = &efi_cdevsw;
		mda.mda_uid = UID_ROOT;
		mda.mda_gid = GID_WHEEL;
		mda.mda_mode = 0700;
		error = make_dev_s(&mda, &efidev, "efi");
		return (error);

	case MOD_UNLOAD:
		if (efidev != NULL)
			destroy_dev(efidev);
		efidev = NULL;
		return (0);

	case MOD_SHUTDOWN:
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t efidev_moddata = {
	.name = "efidev",
	.evhand = efidev_modevents,
	.priv = NULL,
};

DECLARE_MODULE(efidev, efidev_moddata, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(efidev, 1);
MODULE_DEPEND(efidev, efirt, 1, 1, 1);
