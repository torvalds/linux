// SPDX-License-Identifier: GPL-2.0
#include "kvm_util.h"
#include "test_util.h"
#include "proc_util.h"

static FILE *open_proc_interrupts(void)
{
	FILE *fp;

	fp = fopen("/proc/interrupts", "r");
	TEST_ASSERT(fp, "fopen(/proc/interrupts) failed");

	return fp;
}

unsigned int vfio_msix_to_host_irq(const char *device_bdf, int msix)
{
	char search_string[64];
	char line[4096];
	int irq = -1;
	FILE *fp;

	fp = open_proc_interrupts();

	snprintf(search_string, sizeof(search_string), "vfio-msix[%d]", msix);

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, device_bdf) && strstr(line, search_string)) {
			TEST_ASSERT_EQ(1, sscanf(line, "%d:", &irq));
			break;
		}
	}

	fclose(fp);

	TEST_ASSERT(irq != -1, "Failed to locate IRQ for %s %s", device_bdf,
		    search_string);
	return (unsigned int)irq;
}

void proc_irq_set_smp_affinity(unsigned int irq, int cpu)
{
	char path[PATH_MAX];
	int r, fd;

	snprintf(path, sizeof(path), "/proc/irq/%u/smp_affinity_list", irq);
	fd = open(path, O_RDWR);
	TEST_ASSERT(fd >= 0, "Failed to open %s", path);

	r = dprintf(fd, "%d\n", cpu);
	TEST_ASSERT(r > 0, "Failed to affinitize IRQ-%u to CPU %d", irq, cpu);

	kvm_close(fd);
}
