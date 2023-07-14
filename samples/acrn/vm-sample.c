// SPDX-License-Identifier: GPL-2.0
/*
 * A sample program to run a User VM on the ACRN hypervisor
 *
 * This sample runs in a Service VM, which is a privileged VM of ACRN.
 * CONFIG_ACRN_HSM need to be enabled in the Service VM.
 *
 * Guest VM code in guest16.s will be executed after the VM launched.
 *
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/acrn.h>

#define GUEST_MEMORY_SIZE	(1024*1024)
void *guest_memory;

extern const unsigned char guest16[], guest16_end[];
static char io_request_page[4096] __attribute__((aligned(4096)));
static struct acrn_io_request *io_req_buf = (struct acrn_io_request *)io_request_page;

__u16 vcpu_num;
__u16 vmid;

int hsm_fd;
int is_running = 1;

void vm_exit(int sig)
{
	sig = sig;

	is_running = 0;
	ioctl(hsm_fd, ACRN_IOCTL_PAUSE_VM, vmid);
	ioctl(hsm_fd, ACRN_IOCTL_DESTROY_IOREQ_CLIENT, 0);
}

int main(int argc, char **argv)
{
	int vcpu_id, ret;
	struct acrn_vm_creation create_vm = {0};
	struct acrn_vm_memmap ram_map = {0};
	struct acrn_vcpu_regs regs;
	struct acrn_io_request *io_req;
	struct acrn_ioreq_notify __attribute__((aligned(8))) notify;

	argc = argc;
	argv = argv;

	ret = posix_memalign(&guest_memory, 4096, GUEST_MEMORY_SIZE);
	if (ret < 0) {
		printf("No enough memory!\n");
		return -1;
	}
	hsm_fd = open("/dev/acrn_hsm", O_RDWR|O_CLOEXEC);

	create_vm.ioreq_buf = (__u64)io_req_buf;
	ret = ioctl(hsm_fd, ACRN_IOCTL_CREATE_VM, &create_vm);
	printf("Created VM! [%d]\n", ret);
	vcpu_num = create_vm.vcpu_num;
	vmid = create_vm.vmid;

	/* setup guest memory */
	ram_map.type = ACRN_MEMMAP_RAM;
	ram_map.vma_base = (__u64)guest_memory;
	ram_map.len = GUEST_MEMORY_SIZE;
	ram_map.user_vm_pa = 0;
	ram_map.attr = ACRN_MEM_ACCESS_RWX;
	ret = ioctl(hsm_fd, ACRN_IOCTL_SET_MEMSEG, &ram_map);
	printf("Set up VM memory! [%d]\n", ret);

	memcpy(guest_memory, guest16, guest16_end-guest16);

	/* setup vcpu registers */
	memset(&regs, 0, sizeof(regs));
	regs.vcpu_id = 0;
	regs.vcpu_regs.rip = 0;

	/* CR0_ET | CR0_NE */
	regs.vcpu_regs.cr0 = 0x30U;
	regs.vcpu_regs.cs_ar = 0x009FU;
	regs.vcpu_regs.cs_sel = 0xF000U;
	regs.vcpu_regs.cs_limit = 0xFFFFU;
	regs.vcpu_regs.cs_base = 0 & 0xFFFF0000UL;
	regs.vcpu_regs.rip = 0 & 0xFFFFUL;

	ret = ioctl(hsm_fd, ACRN_IOCTL_SET_VCPU_REGS, &regs);
	printf("Set up VM BSP registers! [%d]\n", ret);

	/* create an ioreq client for this VM */
	ret = ioctl(hsm_fd, ACRN_IOCTL_CREATE_IOREQ_CLIENT, 0);
	printf("Created IO request client! [%d]\n", ret);

	/* run vm */
	ret = ioctl(hsm_fd, ACRN_IOCTL_START_VM, vmid);
	printf("Start VM! [%d]\n", ret);

	signal(SIGINT, vm_exit);
	while (is_running) {
		ret = ioctl(hsm_fd, ACRN_IOCTL_ATTACH_IOREQ_CLIENT, 0);

		for (vcpu_id = 0; vcpu_id < vcpu_num; vcpu_id++) {
			io_req = &io_req_buf[vcpu_id];
			if ((__sync_add_and_fetch(&io_req->processed, 0) == ACRN_IOREQ_STATE_PROCESSING)
					&& (!io_req->kernel_handled))
				if (io_req->type == ACRN_IOREQ_TYPE_PORTIO) {
					int bytes, port, in;

					port = io_req->reqs.pio_request.address;
					bytes = io_req->reqs.pio_request.size;
					in = (io_req->reqs.pio_request.direction == ACRN_IOREQ_DIR_READ);
					printf("Guest VM %s PIO[%x] with size[%x]\n", in ? "read" : "write", port, bytes);

					notify.vmid = vmid;
					notify.vcpu = vcpu_id;
					ioctl(hsm_fd, ACRN_IOCTL_NOTIFY_REQUEST_FINISH, &notify);
				}
		}
	}

	ret = ioctl(hsm_fd, ACRN_IOCTL_DESTROY_VM, NULL);
	printf("Destroy VM! [%d]\n", ret);
	close(hsm_fd);
	free(guest_memory);
	return 0;
}
