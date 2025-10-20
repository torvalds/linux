// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/sysinfo.h>

#include "kvm_util.h"

static struct kvm_vm *vm1;
static struct kvm_vm *vm2;
static int __eventfd;
static bool done;

/*
 * KVM de-assigns based on eventfd *and* GSI, but requires unique eventfds when
 * assigning (the API isn't symmetrical).  Abuse the oddity and use a per-task
 * GSI base to avoid false failures due to cross-task de-assign, i.e. so that
 * the secondary doesn't de-assign the primary's eventfd and cause assign to
 * unexpectedly succeed on the primary.
 */
#define GSI_BASE_PRIMARY	0x20
#define GSI_BASE_SECONDARY	0x30

static void juggle_eventfd_secondary(struct kvm_vm *vm, int eventfd)
{
	int r, i;

	/*
	 * The secondary task can encounter EBADF since the primary can close
	 * the eventfd at any time.  And because the primary can recreate the
	 * eventfd, at the safe fd in the file table, the secondary can also
	 * encounter "unexpected" success, e.g. if the close+recreate happens
	 * between the first and second assignments.  The secondary's role is
	 * mostly to antagonize KVM, not to detect bugs.
	 */
	for (i = 0; i < 2; i++) {
		r = __kvm_irqfd(vm, GSI_BASE_SECONDARY, eventfd, 0);
		TEST_ASSERT(!r || errno == EBUSY || errno == EBADF,
			    "Wanted success, EBUSY, or EBADF, r = %d, errno = %d",
			    r, errno);

		/* De-assign should succeed unless the eventfd was closed. */
		r = __kvm_irqfd(vm, GSI_BASE_SECONDARY + i, eventfd, KVM_IRQFD_FLAG_DEASSIGN);
		TEST_ASSERT(!r || errno == EBADF,
			    "De-assign should succeed unless the fd was closed");
	}
}

static void *secondary_irqfd_juggler(void *ign)
{
	while (!READ_ONCE(done)) {
		juggle_eventfd_secondary(vm1, READ_ONCE(__eventfd));
		juggle_eventfd_secondary(vm2, READ_ONCE(__eventfd));
	}

	return NULL;
}

static void juggle_eventfd_primary(struct kvm_vm *vm, int eventfd)
{
	int r1, r2;

	/*
	 * At least one of the assigns should fail.  KVM disallows assigning a
	 * single eventfd to multiple GSIs (or VMs), so it's possible that both
	 * assignments can fail, too.
	 */
	r1 = __kvm_irqfd(vm, GSI_BASE_PRIMARY, eventfd, 0);
	TEST_ASSERT(!r1 || errno == EBUSY,
		    "Wanted success or EBUSY, r = %d, errno = %d", r1, errno);

	r2 = __kvm_irqfd(vm, GSI_BASE_PRIMARY + 1, eventfd, 0);
	TEST_ASSERT(r1 || (r2 && errno == EBUSY),
		    "Wanted failure (EBUSY), r1 = %d, r2 = %d, errno = %d",
		    r1, r2, errno);

	/*
	 * De-assign should always succeed, even if the corresponding assign
	 * failed.
	 */
	kvm_irqfd(vm, GSI_BASE_PRIMARY, eventfd, KVM_IRQFD_FLAG_DEASSIGN);
	kvm_irqfd(vm, GSI_BASE_PRIMARY + 1, eventfd, KVM_IRQFD_FLAG_DEASSIGN);
}

int main(int argc, char *argv[])
{
	pthread_t racing_thread;
	struct kvm_vcpu *unused;
	int r, i;

	TEST_REQUIRE(kvm_arch_has_default_irqchip());

	/*
	 * Create "full" VMs, as KVM_IRQFD requires an in-kernel IRQ chip. Also
	 * create an unused vCPU as certain architectures (like arm64) need to
	 * complete IRQ chip initialization after all possible vCPUs for a VM
	 * have been created.
	 */
	vm1 = vm_create_with_one_vcpu(&unused, NULL);
	vm2 = vm_create_with_one_vcpu(&unused, NULL);

	WRITE_ONCE(__eventfd, kvm_new_eventfd());

	kvm_irqfd(vm1, 10, __eventfd, 0);

	r = __kvm_irqfd(vm1, 11, __eventfd, 0);
	TEST_ASSERT(r && errno == EBUSY,
		    "Wanted EBUSY, r = %d, errno = %d", r, errno);

	r = __kvm_irqfd(vm2, 12, __eventfd, 0);
	TEST_ASSERT(r && errno == EBUSY,
		    "Wanted EBUSY, r = %d, errno = %d", r, errno);

	/*
	 * De-assign all eventfds, along with multiple eventfds that were never
	 * assigned.  KVM's ABI is that de-assign is allowed so long as the
	 * eventfd itself is valid.
	 */
	kvm_irqfd(vm1, 11, READ_ONCE(__eventfd), KVM_IRQFD_FLAG_DEASSIGN);
	kvm_irqfd(vm1, 12, READ_ONCE(__eventfd), KVM_IRQFD_FLAG_DEASSIGN);
	kvm_irqfd(vm1, 13, READ_ONCE(__eventfd), KVM_IRQFD_FLAG_DEASSIGN);
	kvm_irqfd(vm1, 14, READ_ONCE(__eventfd), KVM_IRQFD_FLAG_DEASSIGN);
	kvm_irqfd(vm1, 10, READ_ONCE(__eventfd), KVM_IRQFD_FLAG_DEASSIGN);

	close(__eventfd);

	pthread_create(&racing_thread, NULL, secondary_irqfd_juggler, vm2);

	for (i = 0; i < 10000; i++) {
		WRITE_ONCE(__eventfd, kvm_new_eventfd());

		juggle_eventfd_primary(vm1, __eventfd);
		juggle_eventfd_primary(vm2, __eventfd);
		close(__eventfd);
	}

	WRITE_ONCE(done, true);
	pthread_join(racing_thread, NULL);
}
