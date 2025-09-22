/*	$OpenBSD: arm64_vm.c,v 1.7 2025/06/12 21:04:37 dv Exp $	*/
/*
 * Copyright (c) 2024 Dave Voutila <dv@openbsd.org>
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

#include "vmd.h"
#include "vmm.h"

void
create_memory_map(struct vm_create_params *vcp)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
}

int
load_firmware(struct vmd_vm *vm, struct vcpu_reg_state *vrs)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

void
init_emulated_hw(struct vmop_create_params *vcp, int child_cdrom,
    int child_disks[][VM_MAX_BASE_PER_DISK], int *child_taps)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
}

void
pause_vm_md(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
}

void
unpause_vm_md(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
}

void *
hvaddr_mem(paddr_t gpa, size_t len)
{	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (NULL);
}

int
write_mem(paddr_t dst, const void *buf, size_t len)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
read_mem(paddr_t src, void *buf, size_t len)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
intr_pending(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

void
intr_toggle_el(struct vmd_vm *vm, int irq, int val)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
}

int
intr_ack(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

void
vcpu_assert_irq(uint32_t vm_id, uint32_t vcpu_id, int irq)
{
	fatalx("%s: unimplemented", __func__);
}

void
vcpu_deassert_irq(uint32_t vm_id, uint32_t vcpu_id, int irq)
{
	fatalx("%s: unimplemented", __func__);
}

int
vcpu_exit(struct vm_run_params *vrp)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

uint8_t
vcpu_exit_pci(struct vm_run_params *vrp)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (0xff);
}

void
set_return_data(struct vm_exit *vei, uint32_t data)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return;
}

void
get_input_data(struct vm_exit *vei, uint32_t *data)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return;
}

int
sev_init(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
sev_shutdown(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
sev_activate(struct vmd_vm *vm, int vcpu_id)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
sev_encrypt_memory(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
sev_encrypt_state(struct vmd_vm *vm, int vcpu_id)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

int
sev_launch_finalize(struct vmd_vm *vm)
{
	fatalx("%s: unimplemented", __func__);
	/* NOTREACHED */
	return (-1);
}

void
psp_setup(void)
{
}
