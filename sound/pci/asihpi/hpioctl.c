/*******************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Common Linux HPI ioctl and module probe/remove functions
*******************************************************************************/
#define SOURCEFILE_NAME "hpioctl.c"

#include "hpi_internal.h"
#include "hpi_version.h"
#include "hpimsginit.h"
#include "hpidebug.h"
#include "hpimsgx.h"
#include "hpioctl.h"
#include "hpicmn.h"

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>
#include <linux/pci.h>
#include <linux/stringify.h>
#include <linux/module.h>

#ifdef MODULE_FIRMWARE
MODULE_FIRMWARE("asihpi/dsp5000.bin");
MODULE_FIRMWARE("asihpi/dsp6200.bin");
MODULE_FIRMWARE("asihpi/dsp6205.bin");
MODULE_FIRMWARE("asihpi/dsp6400.bin");
MODULE_FIRMWARE("asihpi/dsp6600.bin");
MODULE_FIRMWARE("asihpi/dsp8700.bin");
MODULE_FIRMWARE("asihpi/dsp8900.bin");
#endif

static int prealloc_stream_buf;
module_param(prealloc_stream_buf, int, S_IRUGO);
MODULE_PARM_DESC(prealloc_stream_buf,
	"Preallocate size for per-adapter stream buffer");

/* Allow the debug level to be changed after module load.
 E.g.   echo 2 > /sys/module/asihpi/parameters/hpiDebugLevel
*/
module_param(hpi_debug_level, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(hpi_debug_level, "debug verbosity 0..5");

/* List of adapters found */
static struct hpi_adapter adapters[HPI_MAX_ADAPTERS];

/* Wrapper function to HPI_Message to enable dumping of the
   message and response types.
*/
static void hpi_send_recv_f(struct hpi_message *phm, struct hpi_response *phr,
	struct file *file)
{
	if ((phm->adapter_index >= HPI_MAX_ADAPTERS)
		&& (phm->object != HPI_OBJ_SUBSYSTEM))
		phr->error = HPI_ERROR_INVALID_OBJ_INDEX;
	else
		hpi_send_recv_ex(phm, phr, file);
}

/* This is called from hpifunc.c functions, called by ALSA
 * (or other kernel process) In this case there is no file descriptor
 * available for the message cache code
 */
void hpi_send_recv(struct hpi_message *phm, struct hpi_response *phr)
{
	hpi_send_recv_f(phm, phr, HOWNER_KERNEL);
}

EXPORT_SYMBOL(hpi_send_recv);
/* for radio-asihpi */

int asihpi_hpi_release(struct file *file)
{
	struct hpi_message hm;
	struct hpi_response hr;

/* HPI_DEBUG_LOG(INFO,"hpi_release file %p, pid %d\n", file, current->pid); */
	/* close the subsystem just in case the application forgot to. */
	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_CLOSE);
	hpi_send_recv_ex(&hm, &hr, file);
	return 0;
}

long asihpi_hpi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct hpi_ioctl_linux __user *phpi_ioctl_data;
	void __user *puhm;
	void __user *puhr;
	union hpi_message_buffer_v1 *hm;
	union hpi_response_buffer_v1 *hr;
	u16 res_max_size;
	u32 uncopied_bytes;
	int err = 0;

	if (cmd != HPI_IOCTL_LINUX)
		return -EINVAL;

	hm = kmalloc(sizeof(*hm), GFP_KERNEL);
	hr = kmalloc(sizeof(*hr), GFP_KERNEL);
	if (!hm || !hr) {
		err = -ENOMEM;
		goto out;
	}

	phpi_ioctl_data = (struct hpi_ioctl_linux __user *)arg;

	/* Read the message and response pointers from user space.  */
	if (get_user(puhm, &phpi_ioctl_data->phm)
		|| get_user(puhr, &phpi_ioctl_data->phr)) {
		err = -EFAULT;
		goto out;
	}

	/* Now read the message size and data from user space.  */
	if (get_user(hm->h.size, (u16 __user *)puhm)) {
		err = -EFAULT;
		goto out;
	}
	if (hm->h.size > sizeof(*hm))
		hm->h.size = sizeof(*hm);

	/* printk(KERN_INFO "message size %d\n", hm->h.wSize); */

	uncopied_bytes = copy_from_user(hm, puhm, hm->h.size);
	if (uncopied_bytes) {
		HPI_DEBUG_LOG(ERROR, "uncopied bytes %d\n", uncopied_bytes);
		err = -EFAULT;
		goto out;
	}

	if (get_user(res_max_size, (u16 __user *)puhr)) {
		err = -EFAULT;
		goto out;
	}
	/* printk(KERN_INFO "user response size %d\n", res_max_size); */
	if (res_max_size < sizeof(struct hpi_response_header)) {
		HPI_DEBUG_LOG(WARNING, "small res size %d\n", res_max_size);
		err = -EFAULT;
		goto out;
	}

	switch (hm->h.function) {
	case HPI_SUBSYS_CREATE_ADAPTER:
	case HPI_ADAPTER_DELETE:
		/* Application must not use these functions! */
		hr->h.size = sizeof(hr->h);
		hr->h.error = HPI_ERROR_INVALID_OPERATION;
		hr->h.function = hm->h.function;
		uncopied_bytes = copy_to_user(puhr, hr, hr->h.size);
		if (uncopied_bytes)
			err = -EFAULT;
		else
			err = 0;
		goto out;
	}

	hr->h.size = res_max_size;
	if (hm->h.object == HPI_OBJ_SUBSYSTEM) {
		hpi_send_recv_f(&hm->m0, &hr->r0, file);
	} else {
		u16 __user *ptr = NULL;
		u32 size = 0;
		/* -1=no data 0=read from user mem, 1=write to user mem */
		int wrflag = -1;
		struct hpi_adapter *pa = NULL;

		if (hm->h.adapter_index < ARRAY_SIZE(adapters))
			pa = &adapters[hm->h.adapter_index];

		if (!pa || !pa->adapter || !pa->adapter->type) {
			hpi_init_response(&hr->r0, hm->h.object,
				hm->h.function, HPI_ERROR_BAD_ADAPTER_NUMBER);

			uncopied_bytes =
				copy_to_user(puhr, hr, sizeof(hr->h));
			if (uncopied_bytes)
				err = -EFAULT;
			else
				err = 0;
			goto out;
		}

		if (mutex_lock_interruptible(&pa->mutex)) {
			err = -EINTR;
			goto out;
		}

		/* Dig out any pointers embedded in the message.  */
		switch (hm->h.function) {
		case HPI_OSTREAM_WRITE:
		case HPI_ISTREAM_READ:{
				/* Yes, sparse, this is correct. */
				ptr = (u16 __user *)hm->m0.u.d.u.data.pb_data;
				size = hm->m0.u.d.u.data.data_size;

				/* Allocate buffer according to application request.
				   ?Is it better to alloc/free for the duration
				   of the transaction?
				 */
				if (pa->buffer_size < size) {
					HPI_DEBUG_LOG(DEBUG,
						"Realloc adapter %d stream "
						"buffer from %zd to %d\n",
						hm->h.adapter_index,
						pa->buffer_size, size);
					if (pa->p_buffer) {
						pa->buffer_size = 0;
						vfree(pa->p_buffer);
					}
					pa->p_buffer = vmalloc(size);
					if (pa->p_buffer)
						pa->buffer_size = size;
					else {
						HPI_DEBUG_LOG(ERROR,
							"HPI could not allocate "
							"stream buffer size %d\n",
							size);

						mutex_unlock(&pa->mutex);
						err = -EINVAL;
						goto out;
					}
				}

				hm->m0.u.d.u.data.pb_data = pa->p_buffer;
				if (hm->h.function == HPI_ISTREAM_READ)
					/* from card, WRITE to user mem */
					wrflag = 1;
				else
					wrflag = 0;
				break;
			}

		default:
			size = 0;
			break;
		}

		if (size && (wrflag == 0)) {
			uncopied_bytes =
				copy_from_user(pa->p_buffer, ptr, size);
			if (uncopied_bytes)
				HPI_DEBUG_LOG(WARNING,
					"Missed %d of %d "
					"bytes from user\n", uncopied_bytes,
					size);
		}

		hpi_send_recv_f(&hm->m0, &hr->r0, file);

		if (size && (wrflag == 1)) {
			uncopied_bytes =
				copy_to_user(ptr, pa->p_buffer, size);
			if (uncopied_bytes)
				HPI_DEBUG_LOG(WARNING,
					"Missed %d of %d " "bytes to user\n",
					uncopied_bytes, size);
		}

		mutex_unlock(&pa->mutex);
	}

	/* on return response size must be set */
	/*printk(KERN_INFO "response size %d\n", hr->h.wSize); */

	if (!hr->h.size) {
		HPI_DEBUG_LOG(ERROR, "response zero size\n");
		err = -EFAULT;
		goto out;
	}

	if (hr->h.size > res_max_size) {
		HPI_DEBUG_LOG(ERROR, "response too big %d %d\n", hr->h.size,
			res_max_size);
		hr->h.error = HPI_ERROR_RESPONSE_BUFFER_TOO_SMALL;
		hr->h.specific_error = hr->h.size;
		hr->h.size = sizeof(hr->h);
	}

	uncopied_bytes = copy_to_user(puhr, hr, hr->h.size);
	if (uncopied_bytes) {
		HPI_DEBUG_LOG(ERROR, "uncopied bytes %d\n", uncopied_bytes);
		err = -EFAULT;
		goto out;
	}

out:
	kfree(hm);
	kfree(hr);
	return err;
}

int asihpi_adapter_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *pci_id)
{
	int idx, nm;
	int adapter_index;
	unsigned int memlen;
	struct hpi_message hm;
	struct hpi_response hr;
	struct hpi_adapter adapter;
	struct hpi_pci pci;

	memset(&adapter, 0, sizeof(adapter));

	dev_printk(KERN_DEBUG, &pci_dev->dev,
		"probe %04x:%04x,%04x:%04x,%04x\n", pci_dev->vendor,
		pci_dev->device, pci_dev->subsystem_vendor,
		pci_dev->subsystem_device, pci_dev->devfn);

	if (pci_enable_device(pci_dev) < 0) {
		dev_err(&pci_dev->dev,
			"pci_enable_device failed, disabling device\n");
		return -EIO;
	}

	pci_set_master(pci_dev);	/* also sets latency timer if < 16 */

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_CREATE_ADAPTER);
	hpi_init_response(&hr, HPI_OBJ_SUBSYSTEM, HPI_SUBSYS_CREATE_ADAPTER,
		HPI_ERROR_PROCESSING_MESSAGE);

	hm.adapter_index = HPI_ADAPTER_INDEX_INVALID;

	nm = HPI_MAX_ADAPTER_MEM_SPACES;

	for (idx = 0; idx < nm; idx++) {
		HPI_DEBUG_LOG(INFO, "resource %d %pR\n", idx,
			&pci_dev->resource[idx]);

		if (pci_resource_flags(pci_dev, idx) & IORESOURCE_MEM) {
			memlen = pci_resource_len(pci_dev, idx);
			pci.ap_mem_base[idx] =
				ioremap(pci_resource_start(pci_dev, idx),
				memlen);
			if (!pci.ap_mem_base[idx]) {
				HPI_DEBUG_LOG(ERROR,
					"ioremap failed, aborting\n");
				/* unmap previously mapped pci mem space */
				goto err;
			}
		}
	}

	pci.pci_dev = pci_dev;
	hm.u.s.resource.bus_type = HPI_BUS_PCI;
	hm.u.s.resource.r.pci = &pci;

	/* call CreateAdapterObject on the relevant hpi module */
	hpi_send_recv_ex(&hm, &hr, HOWNER_KERNEL);
	if (hr.error)
		goto err;

	adapter_index = hr.u.s.adapter_index;
	adapter.adapter = hpi_find_adapter(adapter_index);

	if (prealloc_stream_buf) {
		adapter.p_buffer = vmalloc(prealloc_stream_buf);
		if (!adapter.p_buffer) {
			HPI_DEBUG_LOG(ERROR,
				"HPI could not allocate "
				"kernel buffer size %d\n",
				prealloc_stream_buf);
			goto err;
		}
	}

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_OPEN);
	hm.adapter_index = adapter.adapter->index;
	hpi_send_recv_ex(&hm, &hr, HOWNER_KERNEL);

	if (hr.error)
		goto err;

	/* WARNING can't init mutex in 'adapter'
	 * and then copy it to adapters[] ?!?!
	 */
	adapters[adapter_index] = adapter;
	mutex_init(&adapters[adapter_index].mutex);
	pci_set_drvdata(pci_dev, &adapters[adapter_index]);

	dev_info(&pci_dev->dev, "probe succeeded for ASI%04X HPI index %d\n",
		 adapter.adapter->type, adapter_index);

	return 0;

err:
	for (idx = 0; idx < HPI_MAX_ADAPTER_MEM_SPACES; idx++) {
		if (pci.ap_mem_base[idx]) {
			iounmap(pci.ap_mem_base[idx]);
			pci.ap_mem_base[idx] = NULL;
		}
	}

	if (adapter.p_buffer) {
		adapter.buffer_size = 0;
		vfree(adapter.p_buffer);
	}

	HPI_DEBUG_LOG(ERROR, "adapter_probe failed\n");
	return -ENODEV;
}

void asihpi_adapter_remove(struct pci_dev *pci_dev)
{
	int idx;
	struct hpi_message hm;
	struct hpi_response hr;
	struct hpi_adapter *pa;
	struct hpi_pci pci;

	pa = pci_get_drvdata(pci_dev);
	pci = pa->adapter->pci;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_ADAPTER,
		HPI_ADAPTER_DELETE);
	hm.adapter_index = pa->adapter->index;
	hpi_send_recv_ex(&hm, &hr, HOWNER_KERNEL);

	/* unmap PCI memory space, mapped during device init. */
	for (idx = 0; idx < HPI_MAX_ADAPTER_MEM_SPACES; idx++) {
		if (pci.ap_mem_base[idx])
			iounmap(pci.ap_mem_base[idx]);
	}

	if (pa->p_buffer)
		vfree(pa->p_buffer);

	if (1)
		dev_info(&pci_dev->dev,
			 "remove %04x:%04x,%04x:%04x,%04x, HPI index %d\n",
			 pci_dev->vendor, pci_dev->device,
			 pci_dev->subsystem_vendor, pci_dev->subsystem_device,
			 pci_dev->devfn, pa->adapter->index);

	memset(pa, 0, sizeof(*pa));
}

void __init asihpi_init(void)
{
	struct hpi_message hm;
	struct hpi_response hr;

	memset(adapters, 0, sizeof(adapters));

	printk(KERN_INFO "ASIHPI driver " HPI_VER_STRING "\n");

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_DRIVER_LOAD);
	hpi_send_recv_ex(&hm, &hr, HOWNER_KERNEL);
}

void asihpi_exit(void)
{
	struct hpi_message hm;
	struct hpi_response hr;

	hpi_init_message_response(&hm, &hr, HPI_OBJ_SUBSYSTEM,
		HPI_SUBSYS_DRIVER_UNLOAD);
	hpi_send_recv_ex(&hm, &hr, HOWNER_KERNEL);
}
