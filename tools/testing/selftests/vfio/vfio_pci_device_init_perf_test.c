// SPDX-License-Identifier: GPL-2.0-only
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/sizes.h>
#include <linux/time64.h>
#include <linux/vfio.h>

#include <libvfio.h>

#include "../kselftest_harness.h"

static char **device_bdfs;
static int nr_devices;

struct thread_args {
	struct iommu *iommu;
	int device_index;
	struct timespec start;
	struct timespec end;
	pthread_barrier_t *barrier;
};

FIXTURE(vfio_pci_device_init_perf_test) {
	pthread_t *threads;
	pthread_barrier_t barrier;
	struct thread_args *thread_args;
	struct iommu *iommu;
};

FIXTURE_VARIANT(vfio_pci_device_init_perf_test) {
	const char *iommu_mode;
};

#define FIXTURE_VARIANT_ADD_IOMMU_MODE(_iommu_mode)			\
FIXTURE_VARIANT_ADD(vfio_pci_device_init_perf_test, _iommu_mode) {	\
	.iommu_mode = #_iommu_mode,					\
}

FIXTURE_VARIANT_ADD_ALL_IOMMU_MODES();

FIXTURE_SETUP(vfio_pci_device_init_perf_test)
{
	int i;

	self->iommu = iommu_init(variant->iommu_mode);
	self->threads = calloc(nr_devices, sizeof(self->threads[0]));
	self->thread_args = calloc(nr_devices, sizeof(self->thread_args[0]));

	pthread_barrier_init(&self->barrier, NULL, nr_devices);

	for (i = 0; i < nr_devices; i++) {
		self->thread_args[i].iommu = self->iommu;
		self->thread_args[i].barrier = &self->barrier;
		self->thread_args[i].device_index = i;
	}
}

FIXTURE_TEARDOWN(vfio_pci_device_init_perf_test)
{
	iommu_cleanup(self->iommu);
	free(self->threads);
	free(self->thread_args);
}

static s64 to_ns(struct timespec ts)
{
	return (s64)ts.tv_nsec + NSEC_PER_SEC * (s64)ts.tv_sec;
}

static struct timespec to_timespec(s64 ns)
{
	struct timespec ts = {
		.tv_nsec = ns % NSEC_PER_SEC,
		.tv_sec = ns / NSEC_PER_SEC,
	};

	return ts;
}

static struct timespec timespec_sub(struct timespec a, struct timespec b)
{
	return to_timespec(to_ns(a) - to_ns(b));
}

static struct timespec timespec_min(struct timespec a, struct timespec b)
{
	return to_ns(a) < to_ns(b) ? a : b;
}

static struct timespec timespec_max(struct timespec a, struct timespec b)
{
	return to_ns(a) > to_ns(b) ? a : b;
}

static void *thread_main(void *__args)
{
	struct thread_args *args = __args;
	struct vfio_pci_device *device;

	pthread_barrier_wait(args->barrier);

	clock_gettime(CLOCK_MONOTONIC, &args->start);
	device = vfio_pci_device_init(device_bdfs[args->device_index], args->iommu);
	clock_gettime(CLOCK_MONOTONIC, &args->end);

	pthread_barrier_wait(args->barrier);

	vfio_pci_device_cleanup(device);
	return NULL;
}

TEST_F(vfio_pci_device_init_perf_test, init)
{
	struct timespec start = to_timespec(INT64_MAX), end = {};
	struct timespec min = to_timespec(INT64_MAX);
	struct timespec max = {};
	struct timespec avg = {};
	struct timespec wall_time;
	s64 thread_ns = 0;
	int i;

	for (i = 0; i < nr_devices; i++) {
		pthread_create(&self->threads[i], NULL, thread_main,
			       &self->thread_args[i]);
	}

	for (i = 0; i < nr_devices; i++) {
		struct thread_args *args = &self->thread_args[i];
		struct timespec init_time;

		pthread_join(self->threads[i], NULL);

		start = timespec_min(start, args->start);
		end = timespec_max(end, args->end);

		init_time = timespec_sub(args->end, args->start);
		min = timespec_min(min, init_time);
		max = timespec_max(max, init_time);
		thread_ns += to_ns(init_time);
	}

	avg = to_timespec(thread_ns / nr_devices);
	wall_time = timespec_sub(end, start);

	printf("Wall time: %lu.%09lus\n",
	       wall_time.tv_sec, wall_time.tv_nsec);
	printf("Min init time (per device): %lu.%09lus\n",
	       min.tv_sec, min.tv_nsec);
	printf("Max init time (per device): %lu.%09lus\n",
	       max.tv_sec, max.tv_nsec);
	printf("Avg init time (per device): %lu.%09lus\n",
	       avg.tv_sec, avg.tv_nsec);
}

int main(int argc, char *argv[])
{
	int i;

	device_bdfs = vfio_selftests_get_bdfs(&argc, argv, &nr_devices);

	printf("Testing parallel initialization of %d devices:\n", nr_devices);
	for (i = 0; i < nr_devices; i++)
		printf("    %s\n", device_bdfs[i]);

	return test_harness_run(argc, argv);
}
