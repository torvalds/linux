// SPDX-License-Identifier: GPL-2.0-only
/*
 * svm_vmcall_test
 *
 * Copyright © 2021 Amazon.com, Inc. or its affiliates.
 *
 * Xen shared_info / pvclock testing
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#include <stdint.h>
#include <time.h>
#include <sched.h>
#include <signal.h>

#include <sys/eventfd.h>

#define VCPU_ID		5

#define SHINFO_REGION_GVA	0xc0000000ULL
#define SHINFO_REGION_GPA	0xc0000000ULL
#define SHINFO_REGION_SLOT	10

#define DUMMY_REGION_GPA	(SHINFO_REGION_GPA + (2 * PAGE_SIZE))
#define DUMMY_REGION_SLOT	11

#define SHINFO_ADDR	(SHINFO_REGION_GPA)
#define PVTIME_ADDR	(SHINFO_REGION_GPA + PAGE_SIZE)
#define RUNSTATE_ADDR	(SHINFO_REGION_GPA + PAGE_SIZE + 0x20)
#define VCPU_INFO_ADDR	(SHINFO_REGION_GPA + 0x40)

#define SHINFO_VADDR	(SHINFO_REGION_GVA)
#define RUNSTATE_VADDR	(SHINFO_REGION_GVA + PAGE_SIZE + 0x20)
#define VCPU_INFO_VADDR	(SHINFO_REGION_GVA + 0x40)

#define EVTCHN_VECTOR	0x10

static struct kvm_vm *vm;

#define XEN_HYPERCALL_MSR	0x40000000

#define MIN_STEAL_TIME		50000

struct pvclock_vcpu_time_info {
	u32   version;
	u32   pad0;
	u64   tsc_timestamp;
	u64   system_time;
	u32   tsc_to_system_mul;
	s8    tsc_shift;
	u8    flags;
	u8    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

struct pvclock_wall_clock {
	u32   version;
	u32   sec;
	u32   nsec;
} __attribute__((__packed__));

struct vcpu_runstate_info {
    uint32_t state;
    uint64_t state_entry_time;
    uint64_t time[4];
};

struct arch_vcpu_info {
    unsigned long cr2;
    unsigned long pad; /* sizeof(vcpu_info_t) == 64 */
};

struct vcpu_info {
	uint8_t evtchn_upcall_pending;
	uint8_t evtchn_upcall_mask;
	unsigned long evtchn_pending_sel;
	struct arch_vcpu_info arch;
	struct pvclock_vcpu_time_info time;
}; /* 64 bytes (x86) */

struct shared_info {
	struct vcpu_info vcpu_info[32];
	unsigned long evtchn_pending[64];
	unsigned long evtchn_mask[64];
	struct pvclock_wall_clock wc;
	uint32_t wc_sec_hi;
	/* arch_shared_info here */
};

#define RUNSTATE_running  0
#define RUNSTATE_runnable 1
#define RUNSTATE_blocked  2
#define RUNSTATE_offline  3

static const char *runstate_names[] = {
	"running",
	"runnable",
	"blocked",
	"offline"
};

struct {
	struct kvm_irq_routing info;
	struct kvm_irq_routing_entry entries[2];
} irq_routes;

static void evtchn_handler(struct ex_regs *regs)
{
	struct vcpu_info *vi = (void *)VCPU_INFO_VADDR;
	vi->evtchn_upcall_pending = 0;
	vi->evtchn_pending_sel = 0;

	GUEST_SYNC(0x20);
}

static void guest_code(void)
{
	struct vcpu_runstate_info *rs = (void *)RUNSTATE_VADDR;

	__asm__ __volatile__(
		"sti\n"
		"nop\n"
	);

	/* Trigger an interrupt injection */
	GUEST_SYNC(0);

	/* Test having the host set runstates manually */
	GUEST_SYNC(RUNSTATE_runnable);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] != 0);
	GUEST_ASSERT(rs->state == 0);

	GUEST_SYNC(RUNSTATE_blocked);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] != 0);
	GUEST_ASSERT(rs->state == 0);

	GUEST_SYNC(RUNSTATE_offline);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] != 0);
	GUEST_ASSERT(rs->state == 0);

	/* Test runstate time adjust */
	GUEST_SYNC(4);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] == 0x5a);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] == 0x6b6b);

	/* Test runstate time set */
	GUEST_SYNC(5);
	GUEST_ASSERT(rs->state_entry_time >= 0x8000);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] == 0);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] == 0x6b6b);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] == 0x5a);

	/* sched_yield() should result in some 'runnable' time */
	GUEST_SYNC(6);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] >= MIN_STEAL_TIME);

	/* Attempt to deliver a *masked* interrupt */
	GUEST_SYNC(7);

	/* Wait until we see the bit set */
	struct shared_info *si = (void *)SHINFO_VADDR;
	while (!si->evtchn_pending[0])
		__asm__ __volatile__ ("rep nop" : : : "memory");

	/* Now deliver an *unmasked* interrupt */
	GUEST_SYNC(8);

	while (!si->evtchn_pending[1])
		__asm__ __volatile__ ("rep nop" : : : "memory");

	/* Change memslots and deliver an interrupt */
	GUEST_SYNC(9);

	for (;;)
		__asm__ __volatile__ ("rep nop" : : : "memory");
}

static int cmp_timespec(struct timespec *a, struct timespec *b)
{
	if (a->tv_sec > b->tv_sec)
		return 1;
	else if (a->tv_sec < b->tv_sec)
		return -1;
	else if (a->tv_nsec > b->tv_nsec)
		return 1;
	else if (a->tv_nsec < b->tv_nsec)
		return -1;
	else
		return 0;
}

static void handle_alrm(int sig)
{
	TEST_FAIL("IRQ delivery timed out");
}

int main(int argc, char *argv[])
{
	struct timespec min_ts, max_ts, vm_ts;
	bool verbose;

	verbose = argc > 1 && (!strncmp(argv[1], "-v", 3) ||
			       !strncmp(argv[1], "--verbose", 10));

	int xen_caps = kvm_check_cap(KVM_CAP_XEN_HVM);
	if (!(xen_caps & KVM_XEN_HVM_CONFIG_SHARED_INFO) ) {
		print_skip("KVM_XEN_HVM_CONFIG_SHARED_INFO not available");
		exit(KSFT_SKIP);
	}

	bool do_runstate_tests = !!(xen_caps & KVM_XEN_HVM_CONFIG_RUNSTATE);
	bool do_eventfd_tests = !!(xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_2LEVEL);

	clock_gettime(CLOCK_REALTIME, &min_ts);

	vm = vm_create_default(VCPU_ID, 0, (void *) guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/* Map a region for the shared_info page */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    SHINFO_REGION_GPA, SHINFO_REGION_SLOT, 2, 0);
	virt_map(vm, SHINFO_REGION_GVA, SHINFO_REGION_GPA, 2);

	struct shared_info *shinfo = addr_gpa2hva(vm, SHINFO_VADDR);

	int zero_fd = open("/dev/zero", O_RDONLY);
	TEST_ASSERT(zero_fd != -1, "Failed to open /dev/zero");

	struct kvm_xen_hvm_config hvmc = {
		.flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL,
		.msr = XEN_HYPERCALL_MSR,
	};
	vm_ioctl(vm, KVM_XEN_HVM_CONFIG, &hvmc);

	struct kvm_xen_hvm_attr lm = {
		.type = KVM_XEN_ATTR_TYPE_LONG_MODE,
		.u.long_mode = 1,
	};
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &lm);

	struct kvm_xen_hvm_attr ha = {
		.type = KVM_XEN_ATTR_TYPE_SHARED_INFO,
		.u.shared_info.gfn = SHINFO_REGION_GPA / PAGE_SIZE,
	};
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &ha);

	/*
	 * Test what happens when the HVA of the shinfo page is remapped after
	 * the kernel has a reference to it. But make sure we copy the clock
	 * info over since that's only set at setup time, and we test it later.
	 */
	struct pvclock_wall_clock wc_copy = shinfo->wc;
	void *m = mmap(shinfo, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE, zero_fd, 0);
	TEST_ASSERT(m == shinfo, "Failed to map /dev/zero over shared info");
	shinfo->wc = wc_copy;

	struct kvm_xen_vcpu_attr vi = {
		.type = KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO,
		.u.gpa = VCPU_INFO_ADDR,
	};
	vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &vi);

	struct kvm_xen_vcpu_attr pvclock = {
		.type = KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
		.u.gpa = PVTIME_ADDR,
	};
	vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &pvclock);

	struct kvm_xen_hvm_attr vec = {
		.type = KVM_XEN_ATTR_TYPE_UPCALL_VECTOR,
		.u.vector = EVTCHN_VECTOR,
	};
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &vec);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);
	vm_install_exception_handler(vm, EVTCHN_VECTOR, evtchn_handler);

	if (do_runstate_tests) {
		struct kvm_xen_vcpu_attr st = {
			.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
			.u.gpa = RUNSTATE_ADDR,
		};
		vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &st);
	}

	int irq_fd[2] = { -1, -1 };

	if (do_eventfd_tests) {
		irq_fd[0] = eventfd(0, 0);
		irq_fd[1] = eventfd(0, 0);

		/* Unexpected, but not a KVM failure */
		if (irq_fd[0] == -1 || irq_fd[1] == -1)
			do_eventfd_tests = false;
	}

	if (do_eventfd_tests) {
		irq_routes.info.nr = 2;

		irq_routes.entries[0].gsi = 32;
		irq_routes.entries[0].type = KVM_IRQ_ROUTING_XEN_EVTCHN;
		irq_routes.entries[0].u.xen_evtchn.port = 15;
		irq_routes.entries[0].u.xen_evtchn.vcpu = VCPU_ID;
		irq_routes.entries[0].u.xen_evtchn.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

		irq_routes.entries[1].gsi = 33;
		irq_routes.entries[1].type = KVM_IRQ_ROUTING_XEN_EVTCHN;
		irq_routes.entries[1].u.xen_evtchn.port = 66;
		irq_routes.entries[1].u.xen_evtchn.vcpu = VCPU_ID;
		irq_routes.entries[1].u.xen_evtchn.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

		vm_ioctl(vm, KVM_SET_GSI_ROUTING, &irq_routes);

		struct kvm_irqfd ifd = { };

		ifd.fd = irq_fd[0];
		ifd.gsi = 32;
		vm_ioctl(vm, KVM_IRQFD, &ifd);

		ifd.fd = irq_fd[1];
		ifd.gsi = 33;
		vm_ioctl(vm, KVM_IRQFD, &ifd);

		struct sigaction sa = { };
		sa.sa_handler = handle_alrm;
		sigaction(SIGALRM, &sa, NULL);
	}

	struct vcpu_info *vinfo = addr_gpa2hva(vm, VCPU_INFO_VADDR);
	vinfo->evtchn_upcall_pending = 0;

	struct vcpu_runstate_info *rs = addr_gpa2hva(vm, RUNSTATE_ADDR);
	rs->state = 0x5a;

	bool evtchn_irq_expected = false;

	for (;;) {
		volatile struct kvm_run *run = vcpu_state(vm, VCPU_ID);
		struct ucall uc;

		vcpu_run(vm, VCPU_ID);

		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_FAIL("%s", (const char *)uc.args[0]);
			/* NOT REACHED */
		case UCALL_SYNC: {
			struct kvm_xen_vcpu_attr rst;
			long rundelay;

			if (do_runstate_tests)
				TEST_ASSERT(rs->state_entry_time == rs->time[0] +
					    rs->time[1] + rs->time[2] + rs->time[3],
					    "runstate times don't add up");

			switch (uc.args[1]) {
			case 0:
				if (verbose)
					printf("Delivering evtchn upcall\n");
				evtchn_irq_expected = true;
				vinfo->evtchn_upcall_pending = 1;
				break;

			case RUNSTATE_runnable...RUNSTATE_offline:
				TEST_ASSERT(!evtchn_irq_expected, "Event channel IRQ not seen");
				if (!do_runstate_tests)
					goto done;
				if (verbose)
					printf("Testing runstate %s\n", runstate_names[uc.args[1]]);
				rst.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_CURRENT;
				rst.u.runstate.state = uc.args[1];
				vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case 4:
				if (verbose)
					printf("Testing RUNSTATE_ADJUST\n");
				rst.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADJUST;
				memset(&rst.u, 0, sizeof(rst.u));
				rst.u.runstate.state = (uint64_t)-1;
				rst.u.runstate.time_blocked =
					0x5a - rs->time[RUNSTATE_blocked];
				rst.u.runstate.time_offline =
					0x6b6b - rs->time[RUNSTATE_offline];
				rst.u.runstate.time_runnable = -rst.u.runstate.time_blocked -
					rst.u.runstate.time_offline;
				vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case 5:
				if (verbose)
					printf("Testing RUNSTATE_DATA\n");
				rst.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_DATA;
				memset(&rst.u, 0, sizeof(rst.u));
				rst.u.runstate.state = RUNSTATE_running;
				rst.u.runstate.state_entry_time = 0x6b6b + 0x5a;
				rst.u.runstate.time_blocked = 0x6b6b;
				rst.u.runstate.time_offline = 0x5a;
				vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case 6:
				if (verbose)
					printf("Testing steal time\n");
				/* Yield until scheduler delay exceeds target */
				rundelay = get_run_delay() + MIN_STEAL_TIME;
				do {
					sched_yield();
				} while (get_run_delay() < rundelay);
				break;

			case 7:
				if (!do_eventfd_tests)
					goto done;
				if (verbose)
					printf("Testing masked event channel\n");
				shinfo->evtchn_mask[0] = 0x8000;
				eventfd_write(irq_fd[0], 1UL);
				alarm(1);
				break;

			case 8:
				if (verbose)
					printf("Testing unmasked event channel\n");
				/* Unmask that, but deliver the other one */
				shinfo->evtchn_pending[0] = 0;
				shinfo->evtchn_mask[0] = 0;
				eventfd_write(irq_fd[1], 1UL);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case 9:
				if (verbose)
					printf("Testing event channel after memslot change\n");
				vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
							    DUMMY_REGION_GPA, DUMMY_REGION_SLOT, 1, 0);
				eventfd_write(irq_fd[0], 1UL);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case 0x20:
				TEST_ASSERT(evtchn_irq_expected, "Unexpected event channel IRQ");
				evtchn_irq_expected = false;
				if (shinfo->evtchn_pending[1] &&
				    shinfo->evtchn_pending[0])
					goto done;
				break;
			}
			break;
		}
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
		}
	}

 done:
	clock_gettime(CLOCK_REALTIME, &max_ts);

	/*
	 * Just a *really* basic check that things are being put in the
	 * right place. The actual calculations are much the same for
	 * Xen as they are for the KVM variants, so no need to check.
	 */
	struct pvclock_wall_clock *wc;
	struct pvclock_vcpu_time_info *ti, *ti2;

	wc = addr_gpa2hva(vm, SHINFO_REGION_GPA + 0xc00);
	ti = addr_gpa2hva(vm, SHINFO_REGION_GPA + 0x40 + 0x20);
	ti2 = addr_gpa2hva(vm, PVTIME_ADDR);

	if (verbose) {
		printf("Wall clock (v %d) %d.%09d\n", wc->version, wc->sec, wc->nsec);
		printf("Time info 1: v %u tsc %" PRIu64 " time %" PRIu64 " mul %u shift %u flags %x\n",
		       ti->version, ti->tsc_timestamp, ti->system_time, ti->tsc_to_system_mul,
		       ti->tsc_shift, ti->flags);
		printf("Time info 2: v %u tsc %" PRIu64 " time %" PRIu64 " mul %u shift %u flags %x\n",
		       ti2->version, ti2->tsc_timestamp, ti2->system_time, ti2->tsc_to_system_mul,
		       ti2->tsc_shift, ti2->flags);
	}

	vm_ts.tv_sec = wc->sec;
	vm_ts.tv_nsec = wc->nsec;
	TEST_ASSERT(wc->version && !(wc->version & 1),
		    "Bad wallclock version %x", wc->version);
	TEST_ASSERT(cmp_timespec(&min_ts, &vm_ts) <= 0, "VM time too old");
	TEST_ASSERT(cmp_timespec(&max_ts, &vm_ts) >= 0, "VM time too new");

	TEST_ASSERT(ti->version && !(ti->version & 1),
		    "Bad time_info version %x", ti->version);
	TEST_ASSERT(ti2->version && !(ti2->version & 1),
		    "Bad time_info version %x", ti->version);

	if (do_runstate_tests) {
		/*
		 * Fetch runstate and check sanity. Strictly speaking in the
		 * general case we might not expect the numbers to be identical
		 * but in this case we know we aren't running the vCPU any more.
		 */
		struct kvm_xen_vcpu_attr rst = {
			.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_DATA,
		};
		vcpu_ioctl(vm, VCPU_ID, KVM_XEN_VCPU_GET_ATTR, &rst);

		if (verbose) {
			printf("Runstate: %s(%d), entry %" PRIu64 " ns\n",
			       rs->state <= RUNSTATE_offline ? runstate_names[rs->state] : "unknown",
			       rs->state, rs->state_entry_time);
			for (int i = RUNSTATE_running; i <= RUNSTATE_offline; i++) {
				printf("State %s: %" PRIu64 " ns\n",
				       runstate_names[i], rs->time[i]);
			}
		}
		TEST_ASSERT(rs->state == rst.u.runstate.state, "Runstate mismatch");
		TEST_ASSERT(rs->state_entry_time == rst.u.runstate.state_entry_time,
			    "State entry time mismatch");
		TEST_ASSERT(rs->time[RUNSTATE_running] == rst.u.runstate.time_running,
			    "Running time mismatch");
		TEST_ASSERT(rs->time[RUNSTATE_runnable] == rst.u.runstate.time_runnable,
			    "Runnable time mismatch");
		TEST_ASSERT(rs->time[RUNSTATE_blocked] == rst.u.runstate.time_blocked,
			    "Blocked time mismatch");
		TEST_ASSERT(rs->time[RUNSTATE_offline] == rst.u.runstate.time_offline,
			    "Offline time mismatch");

		TEST_ASSERT(rs->state_entry_time == rs->time[0] +
			    rs->time[1] + rs->time[2] + rs->time[3],
			    "runstate times don't add up");
	}
	kvm_vm_free(vm);
	return 0;
}
