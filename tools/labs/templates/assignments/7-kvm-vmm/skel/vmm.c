#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>

#include "vm.h"
#include "vcpu.h"

#define UNUSED_PARAMETER(P)     ((void)(P))

int main(int argc, char **argv) {
	UNUSED_PARAMETER(argc);
	UNUSED_PARAMETER(argv);
    struct vm virtual_machine;
    struct vcpu virtual_cpu;

    /* TODO: Initialize the VM. We will use 0x100000 bytes for the memory */
    /* TODO: Initialize the VCPU */
    /* TODO: Setup real mode. We will use guest_16_bits to test this.
    /* TODO: IF real mode works all right. We can try to set up long mode*/

    for (;;) {
        /* TODO: Run the VCPU with KVM_RUN */

        /* TODO: Handle VMEXITs */
        switch (vcpu->kvm_run->exit_reason) {
            case KVM_EXIT_HLT: {goto check;}
            case KVM_EXIT_MMIO: {
                /* TODO: Handle MMIO read/write. Data is available in the shared memory at 
                vcpu->kvm_run */
            }
            case KVM_EXIT_IO: {
                /* TODO: Handle IO ports write (e.g. outb). Data is available in the shared memory
                at vcpu->kvm_run. The data is at vcpu->kvm_run + vcpu->kvm_run->io.data_offset; */
            }
        }

        fprintf(stderr,	"\nGot exit_reason %d,"
                    " expected KVM_EXIT_HLT (%d)\n",
                    vcpu->kvm_run->exit_reason, KVM_EXIT_HLT);
        exit(1);
    }

    /* We verify that the guest code ran accordingly */
    check:
    if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) < 0) {
		perror("KVM_GET_REGS");
		exit(1);
	}

    /* Verify that the guest has written 42 to RAX |*/
	if (regs.rax != 42) {
		printf("Wrong result: {E,R,}AX is %lld\n", regs.rax);
		return 0;
	}

    /* Verify that the guest has written 42 at 0x400 */
	memcpy(&memval, &vm->mem[0x400], sz);
	if (memval != 42) {
		printf("Wrong result: memory at 0x400 is %lld\n",
		       (unsigned long long)memval);
		return 0;
	}

	printf("%s\n", "Finished vmm");
	return 0;
} 