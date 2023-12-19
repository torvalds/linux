// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2021 Amazon.com, Inc. or its affiliates.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#include <stdint.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>

#include <sys/eventfd.h>

#define SHINFO_REGION_GVA	0xc0000000ULL
#define SHINFO_REGION_GPA	0xc0000000ULL
#define SHINFO_REGION_SLOT	10

#define DUMMY_REGION_GPA	(SHINFO_REGION_GPA + (3 * PAGE_SIZE))
#define DUMMY_REGION_SLOT	11

#define DUMMY_REGION_GPA_2	(SHINFO_REGION_GPA + (4 * PAGE_SIZE))
#define DUMMY_REGION_SLOT_2	12

#define SHINFO_ADDR	(SHINFO_REGION_GPA)
#define VCPU_INFO_ADDR	(SHINFO_REGION_GPA + 0x40)
#define PVTIME_ADDR	(SHINFO_REGION_GPA + PAGE_SIZE)
#define RUNSTATE_ADDR	(SHINFO_REGION_GPA + PAGE_SIZE + PAGE_SIZE - 15)

#define SHINFO_VADDR	(SHINFO_REGION_GVA)
#define VCPU_INFO_VADDR	(SHINFO_REGION_GVA + 0x40)
#define RUNSTATE_VADDR	(SHINFO_REGION_GVA + PAGE_SIZE + PAGE_SIZE - 15)

#define EVTCHN_VECTOR	0x10

#define EVTCHN_TEST1 15
#define EVTCHN_TEST2 66
#define EVTCHN_TIMER 13

enum {
	TEST_INJECT_VECTOR = 0,
	TEST_RUNSTATE_runnable,
	TEST_RUNSTATE_blocked,
	TEST_RUNSTATE_offline,
	TEST_RUNSTATE_ADJUST,
	TEST_RUNSTATE_DATA,
	TEST_STEAL_TIME,
	TEST_EVTCHN_MASKED,
	TEST_EVTCHN_UNMASKED,
	TEST_EVTCHN_SLOWPATH,
	TEST_EVTCHN_SEND_IOCTL,
	TEST_EVTCHN_HCALL,
	TEST_EVTCHN_HCALL_SLOWPATH,
	TEST_EVTCHN_HCALL_EVENTFD,
	TEST_TIMER_SETUP,
	TEST_TIMER_WAIT,
	TEST_TIMER_RESTORE,
	TEST_POLL_READY,
	TEST_POLL_TIMEOUT,
	TEST_POLL_MASKED,
	TEST_POLL_WAKE,
	TEST_TIMER_PAST,
	TEST_LOCKING_SEND_RACE,
	TEST_LOCKING_POLL_RACE,
	TEST_LOCKING_POLL_TIMEOUT,
	TEST_DONE,

	TEST_GUEST_SAW_IRQ,
};

#define XEN_HYPERCALL_MSR	0x40000000

#define MIN_STEAL_TIME		50000

#define SHINFO_RACE_TIMEOUT	2	/* seconds */

#define __HYPERVISOR_set_timer_op	15
#define __HYPERVISOR_sched_op		29
#define __HYPERVISOR_event_channel_op	32

#define SCHEDOP_poll			3

#define EVTCHNOP_send			4

#define EVTCHNSTAT_interdomain		2

struct evtchn_send {
	u32 port;
};

struct sched_poll {
	u32 *ports;
	unsigned int nr_ports;
	u64 timeout;
};

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
	uint64_t time[5]; /* Extra field for overrun check */
};

struct compat_vcpu_runstate_info {
	uint32_t state;
	uint64_t state_entry_time;
	uint64_t time[5];
} __attribute__((__packed__));;

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

static volatile bool guest_saw_irq;

static void evtchn_handler(struct ex_regs *regs)
{
	struct vcpu_info *vi = (void *)VCPU_INFO_VADDR;
	vi->evtchn_upcall_pending = 0;
	vi->evtchn_pending_sel = 0;
	guest_saw_irq = true;

	GUEST_SYNC(TEST_GUEST_SAW_IRQ);
}

static void guest_wait_for_irq(void)
{
	while (!guest_saw_irq)
		__asm__ __volatile__ ("rep nop" : : : "memory");
	guest_saw_irq = false;
}

static void guest_code(void)
{
	struct vcpu_runstate_info *rs = (void *)RUNSTATE_VADDR;
	int i;

	__asm__ __volatile__(
		"sti\n"
		"nop\n"
	);

	/* Trigger an interrupt injection */
	GUEST_SYNC(TEST_INJECT_VECTOR);

	guest_wait_for_irq();

	/* Test having the host set runstates manually */
	GUEST_SYNC(TEST_RUNSTATE_runnable);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] != 0);
	GUEST_ASSERT(rs->state == 0);

	GUEST_SYNC(TEST_RUNSTATE_blocked);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] != 0);
	GUEST_ASSERT(rs->state == 0);

	GUEST_SYNC(TEST_RUNSTATE_offline);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] != 0);
	GUEST_ASSERT(rs->state == 0);

	/* Test runstate time adjust */
	GUEST_SYNC(TEST_RUNSTATE_ADJUST);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] == 0x5a);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] == 0x6b6b);

	/* Test runstate time set */
	GUEST_SYNC(TEST_RUNSTATE_DATA);
	GUEST_ASSERT(rs->state_entry_time >= 0x8000);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] == 0);
	GUEST_ASSERT(rs->time[RUNSTATE_blocked] == 0x6b6b);
	GUEST_ASSERT(rs->time[RUNSTATE_offline] == 0x5a);

	/* sched_yield() should result in some 'runnable' time */
	GUEST_SYNC(TEST_STEAL_TIME);
	GUEST_ASSERT(rs->time[RUNSTATE_runnable] >= MIN_STEAL_TIME);

	/* Attempt to deliver a *masked* interrupt */
	GUEST_SYNC(TEST_EVTCHN_MASKED);

	/* Wait until we see the bit set */
	struct shared_info *si = (void *)SHINFO_VADDR;
	while (!si->evtchn_pending[0])
		__asm__ __volatile__ ("rep nop" : : : "memory");

	/* Now deliver an *unmasked* interrupt */
	GUEST_SYNC(TEST_EVTCHN_UNMASKED);

	guest_wait_for_irq();

	/* Change memslots and deliver an interrupt */
	GUEST_SYNC(TEST_EVTCHN_SLOWPATH);

	guest_wait_for_irq();

	/* Deliver event channel with KVM_XEN_HVM_EVTCHN_SEND */
	GUEST_SYNC(TEST_EVTCHN_SEND_IOCTL);

	guest_wait_for_irq();

	GUEST_SYNC(TEST_EVTCHN_HCALL);

	/* Our turn. Deliver event channel (to ourselves) with
	 * EVTCHNOP_send hypercall. */
	struct evtchn_send s = { .port = 127 };
	xen_hypercall(__HYPERVISOR_event_channel_op, EVTCHNOP_send, &s);

	guest_wait_for_irq();

	GUEST_SYNC(TEST_EVTCHN_HCALL_SLOWPATH);

	/*
	 * Same again, but this time the host has messed with memslots so it
	 * should take the slow path in kvm_xen_set_evtchn().
	 */
	xen_hypercall(__HYPERVISOR_event_channel_op, EVTCHNOP_send, &s);

	guest_wait_for_irq();

	GUEST_SYNC(TEST_EVTCHN_HCALL_EVENTFD);

	/* Deliver "outbound" event channel to an eventfd which
	 * happens to be one of our own irqfds. */
	s.port = 197;
	xen_hypercall(__HYPERVISOR_event_channel_op, EVTCHNOP_send, &s);

	guest_wait_for_irq();

	GUEST_SYNC(TEST_TIMER_SETUP);

	/* Set a timer 100ms in the future. */
	xen_hypercall(__HYPERVISOR_set_timer_op,
		      rs->state_entry_time + 100000000, NULL);

	GUEST_SYNC(TEST_TIMER_WAIT);

	/* Now wait for the timer */
	guest_wait_for_irq();

	GUEST_SYNC(TEST_TIMER_RESTORE);

	/* The host has 'restored' the timer. Just wait for it. */
	guest_wait_for_irq();

	GUEST_SYNC(TEST_POLL_READY);

	/* Poll for an event channel port which is already set */
	u32 ports[1] = { EVTCHN_TIMER };
	struct sched_poll p = {
		.ports = ports,
		.nr_ports = 1,
		.timeout = 0,
	};

	xen_hypercall(__HYPERVISOR_sched_op, SCHEDOP_poll, &p);

	GUEST_SYNC(TEST_POLL_TIMEOUT);

	/* Poll for an unset port and wait for the timeout. */
	p.timeout = 100000000;
	xen_hypercall(__HYPERVISOR_sched_op, SCHEDOP_poll, &p);

	GUEST_SYNC(TEST_POLL_MASKED);

	/* A timer will wake the masked port we're waiting on, while we poll */
	p.timeout = 0;
	xen_hypercall(__HYPERVISOR_sched_op, SCHEDOP_poll, &p);

	GUEST_SYNC(TEST_POLL_WAKE);

	/* A timer wake an *unmasked* port which should wake us with an
	 * actual interrupt, while we're polling on a different port. */
	ports[0]++;
	p.timeout = 0;
	xen_hypercall(__HYPERVISOR_sched_op, SCHEDOP_poll, &p);

	guest_wait_for_irq();

	GUEST_SYNC(TEST_TIMER_PAST);

	/* Timer should have fired already */
	guest_wait_for_irq();

	GUEST_SYNC(TEST_LOCKING_SEND_RACE);
	/* Racing host ioctls */

	guest_wait_for_irq();

	GUEST_SYNC(TEST_LOCKING_POLL_RACE);
	/* Racing vmcall against host ioctl */

	ports[0] = 0;

	p = (struct sched_poll) {
		.ports = ports,
		.nr_ports = 1,
		.timeout = 0
	};

wait_for_timer:
	/*
	 * Poll for a timer wake event while the worker thread is mucking with
	 * the shared info.  KVM XEN drops timer IRQs if the shared info is
	 * invalid when the timer expires.  Arbitrarily poll 100 times before
	 * giving up and asking the VMM to re-arm the timer.  100 polls should
	 * consume enough time to beat on KVM without taking too long if the
	 * timer IRQ is dropped due to an invalid event channel.
	 */
	for (i = 0; i < 100 && !guest_saw_irq; i++)
		__xen_hypercall(__HYPERVISOR_sched_op, SCHEDOP_poll, &p);

	/*
	 * Re-send the timer IRQ if it was (likely) dropped due to the timer
	 * expiring while the event channel was invalid.
	 */
	if (!guest_saw_irq) {
		GUEST_SYNC(TEST_LOCKING_POLL_TIMEOUT);
		goto wait_for_timer;
	}
	guest_saw_irq = false;

	GUEST_SYNC(TEST_DONE);
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

static struct vcpu_info *vinfo;
static struct kvm_vcpu *vcpu;

static void handle_alrm(int sig)
{
	if (vinfo)
		printf("evtchn_upcall_pending 0x%x\n", vinfo->evtchn_upcall_pending);
	vcpu_dump(stdout, vcpu, 0);
	TEST_FAIL("IRQ delivery timed out");
}

static void *juggle_shinfo_state(void *arg)
{
	struct kvm_vm *vm = (struct kvm_vm *)arg;

	struct kvm_xen_hvm_attr cache_activate = {
		.type = KVM_XEN_ATTR_TYPE_SHARED_INFO,
		.u.shared_info.gfn = SHINFO_REGION_GPA / PAGE_SIZE
	};

	struct kvm_xen_hvm_attr cache_deactivate = {
		.type = KVM_XEN_ATTR_TYPE_SHARED_INFO,
		.u.shared_info.gfn = KVM_XEN_INVALID_GFN
	};

	for (;;) {
		__vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &cache_activate);
		__vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &cache_deactivate);
		pthread_testcancel();
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	struct timespec min_ts, max_ts, vm_ts;
	struct kvm_xen_hvm_attr evt_reset;
	struct kvm_vm *vm;
	pthread_t thread;
	bool verbose;
	int ret;

	verbose = argc > 1 && (!strncmp(argv[1], "-v", 3) ||
			       !strncmp(argv[1], "--verbose", 10));

	int xen_caps = kvm_check_cap(KVM_CAP_XEN_HVM);
	TEST_REQUIRE(xen_caps & KVM_XEN_HVM_CONFIG_SHARED_INFO);

	bool do_runstate_tests = !!(xen_caps & KVM_XEN_HVM_CONFIG_RUNSTATE);
	bool do_runstate_flag = !!(xen_caps & KVM_XEN_HVM_CONFIG_RUNSTATE_UPDATE_FLAG);
	bool do_eventfd_tests = !!(xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_2LEVEL);
	bool do_evtchn_tests = do_eventfd_tests && !!(xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_SEND);

	clock_gettime(CLOCK_REALTIME, &min_ts);

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	/* Map a region for the shared_info page */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    SHINFO_REGION_GPA, SHINFO_REGION_SLOT, 3, 0);
	virt_map(vm, SHINFO_REGION_GVA, SHINFO_REGION_GPA, 3);

	struct shared_info *shinfo = addr_gpa2hva(vm, SHINFO_VADDR);

	int zero_fd = open("/dev/zero", O_RDONLY);
	TEST_ASSERT(zero_fd != -1, "Failed to open /dev/zero");

	struct kvm_xen_hvm_config hvmc = {
		.flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL,
		.msr = XEN_HYPERCALL_MSR,
	};

	/* Let the kernel know that we *will* use it for sending all
	 * event channels, which lets it intercept SCHEDOP_poll */
	if (do_evtchn_tests)
		hvmc.flags |= KVM_XEN_HVM_CONFIG_EVTCHN_SEND;

	vm_ioctl(vm, KVM_XEN_HVM_CONFIG, &hvmc);

	struct kvm_xen_hvm_attr lm = {
		.type = KVM_XEN_ATTR_TYPE_LONG_MODE,
		.u.long_mode = 1,
	};
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &lm);

	if (do_runstate_flag) {
		struct kvm_xen_hvm_attr ruf = {
			.type = KVM_XEN_ATTR_TYPE_RUNSTATE_UPDATE_FLAG,
			.u.runstate_update_flag = 1,
		};
		vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &ruf);

		ruf.u.runstate_update_flag = 0;
		vm_ioctl(vm, KVM_XEN_HVM_GET_ATTR, &ruf);
		TEST_ASSERT(ruf.u.runstate_update_flag == 1,
			    "Failed to read back RUNSTATE_UPDATE_FLAG attr");
	}

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
	vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &vi);

	struct kvm_xen_vcpu_attr pvclock = {
		.type = KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
		.u.gpa = PVTIME_ADDR,
	};
	vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &pvclock);

	struct kvm_xen_hvm_attr vec = {
		.type = KVM_XEN_ATTR_TYPE_UPCALL_VECTOR,
		.u.vector = EVTCHN_VECTOR,
	};
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &vec);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vm, EVTCHN_VECTOR, evtchn_handler);

	if (do_runstate_tests) {
		struct kvm_xen_vcpu_attr st = {
			.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
			.u.gpa = RUNSTATE_ADDR,
		};
		vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &st);
	}

	int irq_fd[2] = { -1, -1 };

	if (do_eventfd_tests) {
		irq_fd[0] = eventfd(0, 0);
		irq_fd[1] = eventfd(0, 0);

		/* Unexpected, but not a KVM failure */
		if (irq_fd[0] == -1 || irq_fd[1] == -1)
			do_evtchn_tests = do_eventfd_tests = false;
	}

	if (do_eventfd_tests) {
		irq_routes.info.nr = 2;

		irq_routes.entries[0].gsi = 32;
		irq_routes.entries[0].type = KVM_IRQ_ROUTING_XEN_EVTCHN;
		irq_routes.entries[0].u.xen_evtchn.port = EVTCHN_TEST1;
		irq_routes.entries[0].u.xen_evtchn.vcpu = vcpu->id;
		irq_routes.entries[0].u.xen_evtchn.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

		irq_routes.entries[1].gsi = 33;
		irq_routes.entries[1].type = KVM_IRQ_ROUTING_XEN_EVTCHN;
		irq_routes.entries[1].u.xen_evtchn.port = EVTCHN_TEST2;
		irq_routes.entries[1].u.xen_evtchn.vcpu = vcpu->id;
		irq_routes.entries[1].u.xen_evtchn.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

		vm_ioctl(vm, KVM_SET_GSI_ROUTING, &irq_routes.info);

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

	struct kvm_xen_vcpu_attr tmr = {
		.type = KVM_XEN_VCPU_ATTR_TYPE_TIMER,
		.u.timer.port = EVTCHN_TIMER,
		.u.timer.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL,
		.u.timer.expires_ns = 0
	};

	if (do_evtchn_tests) {
		struct kvm_xen_hvm_attr inj = {
			.type = KVM_XEN_ATTR_TYPE_EVTCHN,
			.u.evtchn.send_port = 127,
			.u.evtchn.type = EVTCHNSTAT_interdomain,
			.u.evtchn.flags = 0,
			.u.evtchn.deliver.port.port = EVTCHN_TEST1,
			.u.evtchn.deliver.port.vcpu = vcpu->id + 1,
			.u.evtchn.deliver.port.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL,
		};
		vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &inj);

		/* Test migration to a different vCPU */
		inj.u.evtchn.flags = KVM_XEN_EVTCHN_UPDATE;
		inj.u.evtchn.deliver.port.vcpu = vcpu->id;
		vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &inj);

		inj.u.evtchn.send_port = 197;
		inj.u.evtchn.deliver.eventfd.port = 0;
		inj.u.evtchn.deliver.eventfd.fd = irq_fd[1];
		inj.u.evtchn.flags = 0;
		vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &inj);

		vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
	}
	vinfo = addr_gpa2hva(vm, VCPU_INFO_VADDR);
	vinfo->evtchn_upcall_pending = 0;

	struct vcpu_runstate_info *rs = addr_gpa2hva(vm, RUNSTATE_ADDR);
	rs->state = 0x5a;

	bool evtchn_irq_expected = false;

	for (;;) {
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC: {
			struct kvm_xen_vcpu_attr rst;
			long rundelay;

			if (do_runstate_tests)
				TEST_ASSERT(rs->state_entry_time == rs->time[0] +
					    rs->time[1] + rs->time[2] + rs->time[3],
					    "runstate times don't add up");

			switch (uc.args[1]) {
			case TEST_INJECT_VECTOR:
				if (verbose)
					printf("Delivering evtchn upcall\n");
				evtchn_irq_expected = true;
				vinfo->evtchn_upcall_pending = 1;
				break;

			case TEST_RUNSTATE_runnable...TEST_RUNSTATE_offline:
				TEST_ASSERT(!evtchn_irq_expected, "Event channel IRQ not seen");
				if (!do_runstate_tests)
					goto done;
				if (verbose)
					printf("Testing runstate %s\n", runstate_names[uc.args[1]]);
				rst.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_CURRENT;
				rst.u.runstate.state = uc.args[1] + RUNSTATE_runnable -
					TEST_RUNSTATE_runnable;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case TEST_RUNSTATE_ADJUST:
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
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case TEST_RUNSTATE_DATA:
				if (verbose)
					printf("Testing RUNSTATE_DATA\n");
				rst.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_DATA;
				memset(&rst.u, 0, sizeof(rst.u));
				rst.u.runstate.state = RUNSTATE_running;
				rst.u.runstate.state_entry_time = 0x6b6b + 0x5a;
				rst.u.runstate.time_blocked = 0x6b6b;
				rst.u.runstate.time_offline = 0x5a;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &rst);
				break;

			case TEST_STEAL_TIME:
				if (verbose)
					printf("Testing steal time\n");
				/* Yield until scheduler delay exceeds target */
				rundelay = get_run_delay() + MIN_STEAL_TIME;
				do {
					sched_yield();
				} while (get_run_delay() < rundelay);
				break;

			case TEST_EVTCHN_MASKED:
				if (!do_eventfd_tests)
					goto done;
				if (verbose)
					printf("Testing masked event channel\n");
				shinfo->evtchn_mask[0] = 1UL << EVTCHN_TEST1;
				eventfd_write(irq_fd[0], 1UL);
				alarm(1);
				break;

			case TEST_EVTCHN_UNMASKED:
				if (verbose)
					printf("Testing unmasked event channel\n");
				/* Unmask that, but deliver the other one */
				shinfo->evtchn_pending[0] = 0;
				shinfo->evtchn_mask[0] = 0;
				eventfd_write(irq_fd[1], 1UL);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_EVTCHN_SLOWPATH:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[1] = 0;
				if (verbose)
					printf("Testing event channel after memslot change\n");
				vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
							    DUMMY_REGION_GPA, DUMMY_REGION_SLOT, 1, 0);
				eventfd_write(irq_fd[0], 1UL);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_EVTCHN_SEND_IOCTL:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				if (!do_evtchn_tests)
					goto done;

				shinfo->evtchn_pending[0] = 0;
				if (verbose)
					printf("Testing injection with KVM_XEN_HVM_EVTCHN_SEND\n");

				struct kvm_irq_routing_xen_evtchn e;
				e.port = EVTCHN_TEST2;
				e.vcpu = vcpu->id;
				e.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL;

				vm_ioctl(vm, KVM_XEN_HVM_EVTCHN_SEND, &e);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_EVTCHN_HCALL:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[1] = 0;

				if (verbose)
					printf("Testing guest EVTCHNOP_send direct to evtchn\n");
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_EVTCHN_HCALL_SLOWPATH:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[0] = 0;

				if (verbose)
					printf("Testing guest EVTCHNOP_send direct to evtchn after memslot change\n");
				vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
							    DUMMY_REGION_GPA_2, DUMMY_REGION_SLOT_2, 1, 0);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_EVTCHN_HCALL_EVENTFD:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[0] = 0;

				if (verbose)
					printf("Testing guest EVTCHNOP_send to eventfd\n");
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_TIMER_SETUP:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[1] = 0;

				if (verbose)
					printf("Testing guest oneshot timer\n");
				break;

			case TEST_TIMER_WAIT:
				memset(&tmr, 0, sizeof(tmr));
				tmr.type = KVM_XEN_VCPU_ATTR_TYPE_TIMER;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_GET_ATTR, &tmr);
				TEST_ASSERT(tmr.u.timer.port == EVTCHN_TIMER,
					    "Timer port not returned");
				TEST_ASSERT(tmr.u.timer.priority == KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL,
					    "Timer priority not returned");
				TEST_ASSERT(tmr.u.timer.expires_ns > rs->state_entry_time,
					    "Timer expiry not returned");
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_TIMER_RESTORE:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				shinfo->evtchn_pending[0] = 0;

				if (verbose)
					printf("Testing restored oneshot timer\n");

				tmr.u.timer.expires_ns = rs->state_entry_time + 100000000;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
				evtchn_irq_expected = true;
				alarm(1);
				break;

			case TEST_POLL_READY:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");

				if (verbose)
					printf("Testing SCHEDOP_poll with already pending event\n");
				shinfo->evtchn_pending[0] = shinfo->evtchn_mask[0] = 1UL << EVTCHN_TIMER;
				alarm(1);
				break;

			case TEST_POLL_TIMEOUT:
				if (verbose)
					printf("Testing SCHEDOP_poll timeout\n");
				shinfo->evtchn_pending[0] = 0;
				alarm(1);
				break;

			case TEST_POLL_MASKED:
				if (verbose)
					printf("Testing SCHEDOP_poll wake on masked event\n");

				tmr.u.timer.expires_ns = rs->state_entry_time + 100000000;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
				alarm(1);
				break;

			case TEST_POLL_WAKE:
				shinfo->evtchn_pending[0] = shinfo->evtchn_mask[0] = 0;
				if (verbose)
					printf("Testing SCHEDOP_poll wake on unmasked event\n");

				evtchn_irq_expected = true;
				tmr.u.timer.expires_ns = rs->state_entry_time + 100000000;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);

				/* Read it back and check the pending time is reported correctly */
				tmr.u.timer.expires_ns = 0;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_GET_ATTR, &tmr);
				TEST_ASSERT(tmr.u.timer.expires_ns == rs->state_entry_time + 100000000,
					    "Timer not reported pending");
				alarm(1);
				break;

			case TEST_TIMER_PAST:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				/* Read timer and check it is no longer pending */
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_GET_ATTR, &tmr);
				TEST_ASSERT(!tmr.u.timer.expires_ns, "Timer still reported pending");

				shinfo->evtchn_pending[0] = 0;
				if (verbose)
					printf("Testing timer in the past\n");

				evtchn_irq_expected = true;
				tmr.u.timer.expires_ns = rs->state_entry_time - 100000000ULL;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
				alarm(1);
				break;

			case TEST_LOCKING_SEND_RACE:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");
				alarm(0);

				if (verbose)
					printf("Testing shinfo lock corruption (KVM_XEN_HVM_EVTCHN_SEND)\n");

				ret = pthread_create(&thread, NULL, &juggle_shinfo_state, (void *)vm);
				TEST_ASSERT(ret == 0, "pthread_create() failed: %s", strerror(ret));

				struct kvm_irq_routing_xen_evtchn uxe = {
					.port = 1,
					.vcpu = vcpu->id,
					.priority = KVM_IRQ_ROUTING_XEN_EVTCHN_PRIO_2LEVEL
				};

				evtchn_irq_expected = true;
				for (time_t t = time(NULL) + SHINFO_RACE_TIMEOUT; time(NULL) < t;)
					__vm_ioctl(vm, KVM_XEN_HVM_EVTCHN_SEND, &uxe);
				break;

			case TEST_LOCKING_POLL_RACE:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");

				if (verbose)
					printf("Testing shinfo lock corruption (SCHEDOP_poll)\n");

				shinfo->evtchn_pending[0] = 1;

				evtchn_irq_expected = true;
				tmr.u.timer.expires_ns = rs->state_entry_time +
							 SHINFO_RACE_TIMEOUT * 1000000000ULL;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
				break;

			case TEST_LOCKING_POLL_TIMEOUT:
				/*
				 * Optional and possibly repeated sync point.
				 * Injecting the timer IRQ may fail if the
				 * shinfo is invalid when the timer expires.
				 * If the timer has expired but the IRQ hasn't
				 * been delivered, rearm the timer and retry.
				 */
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_GET_ATTR, &tmr);

				/* Resume the guest if the timer is still pending. */
				if (tmr.u.timer.expires_ns)
					break;

				/* All done if the IRQ was delivered. */
				if (!evtchn_irq_expected)
					break;

				tmr.u.timer.expires_ns = rs->state_entry_time +
							 SHINFO_RACE_TIMEOUT * 1000000000ULL;
				vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &tmr);
				break;
			case TEST_DONE:
				TEST_ASSERT(!evtchn_irq_expected,
					    "Expected event channel IRQ but it didn't happen");

				ret = pthread_cancel(thread);
				TEST_ASSERT(ret == 0, "pthread_cancel() failed: %s", strerror(ret));

				ret = pthread_join(thread, 0);
				TEST_ASSERT(ret == 0, "pthread_join() failed: %s", strerror(ret));
				goto done;

			case TEST_GUEST_SAW_IRQ:
				TEST_ASSERT(evtchn_irq_expected, "Unexpected event channel IRQ");
				evtchn_irq_expected = false;
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
	evt_reset.type = KVM_XEN_ATTR_TYPE_EVTCHN;
	evt_reset.u.evtchn.flags = KVM_XEN_EVTCHN_RESET;
	vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &evt_reset);

	alarm(0);
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
		vcpu_ioctl(vcpu, KVM_XEN_VCPU_GET_ATTR, &rst);

		if (verbose) {
			printf("Runstate: %s(%d), entry %" PRIu64 " ns\n",
			       rs->state <= RUNSTATE_offline ? runstate_names[rs->state] : "unknown",
			       rs->state, rs->state_entry_time);
			for (int i = RUNSTATE_running; i <= RUNSTATE_offline; i++) {
				printf("State %s: %" PRIu64 " ns\n",
				       runstate_names[i], rs->time[i]);
			}
		}

		/*
		 * Exercise runstate info at all points across the page boundary, in
		 * 32-bit and 64-bit mode. In particular, test the case where it is
		 * configured in 32-bit mode and then switched to 64-bit mode while
		 * active, which takes it onto the second page.
		 */
		unsigned long runstate_addr;
		struct compat_vcpu_runstate_info *crs;
		for (runstate_addr = SHINFO_REGION_GPA + PAGE_SIZE + PAGE_SIZE - sizeof(*rs) - 4;
		     runstate_addr < SHINFO_REGION_GPA + PAGE_SIZE + PAGE_SIZE + 4; runstate_addr++) {

			rs = addr_gpa2hva(vm, runstate_addr);
			crs = (void *)rs;

			memset(rs, 0xa5, sizeof(*rs));

			/* Set to compatibility mode */
			lm.u.long_mode = 0;
			vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &lm);

			/* Set runstate to new address (kernel will write it) */
			struct kvm_xen_vcpu_attr st = {
				.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
				.u.gpa = runstate_addr,
			};
			vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &st);

			if (verbose)
				printf("Compatibility runstate at %08lx\n", runstate_addr);

			TEST_ASSERT(crs->state == rst.u.runstate.state, "Runstate mismatch");
			TEST_ASSERT(crs->state_entry_time == rst.u.runstate.state_entry_time,
				    "State entry time mismatch");
			TEST_ASSERT(crs->time[RUNSTATE_running] == rst.u.runstate.time_running,
				    "Running time mismatch");
			TEST_ASSERT(crs->time[RUNSTATE_runnable] == rst.u.runstate.time_runnable,
				    "Runnable time mismatch");
			TEST_ASSERT(crs->time[RUNSTATE_blocked] == rst.u.runstate.time_blocked,
				    "Blocked time mismatch");
			TEST_ASSERT(crs->time[RUNSTATE_offline] == rst.u.runstate.time_offline,
				    "Offline time mismatch");
			TEST_ASSERT(crs->time[RUNSTATE_offline + 1] == 0xa5a5a5a5a5a5a5a5ULL,
				    "Structure overrun");
			TEST_ASSERT(crs->state_entry_time == crs->time[0] +
				    crs->time[1] + crs->time[2] + crs->time[3],
				    "runstate times don't add up");


			/* Now switch to 64-bit mode */
			lm.u.long_mode = 1;
			vm_ioctl(vm, KVM_XEN_HVM_SET_ATTR, &lm);

			memset(rs, 0xa5, sizeof(*rs));

			/* Don't change the address, just trigger a write */
			struct kvm_xen_vcpu_attr adj = {
				.type = KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADJUST,
				.u.runstate.state = (uint64_t)-1
			};
			vcpu_ioctl(vcpu, KVM_XEN_VCPU_SET_ATTR, &adj);

			if (verbose)
				printf("64-bit runstate at %08lx\n", runstate_addr);

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
			TEST_ASSERT(rs->time[RUNSTATE_offline + 1] == 0xa5a5a5a5a5a5a5a5ULL,
				    "Structure overrun");

			TEST_ASSERT(rs->state_entry_time == rs->time[0] +
				    rs->time[1] + rs->time[2] + rs->time[3],
				    "runstate times don't add up");
		}
	}

	kvm_vm_free(vm);
	return 0;
}
