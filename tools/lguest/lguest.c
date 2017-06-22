/*P:100
 * This is the Launcher code, a simple program which lays out the "physical"
 * memory for the new Guest by mapping the kernel image and the virtual
 * devices, then opens /dev/lguest to tell the kernel about the Guest and
 * control it.
:*/
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/if_tun.h>
#include <sys/uio.h>
#include <termios.h>
#include <getopt.h>
#include <assert.h>
#include <sched.h>
#include <limits.h>
#include <stddef.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <sys/user.h>
#include <linux/pci_regs.h>

#ifndef VIRTIO_F_ANY_LAYOUT
#define VIRTIO_F_ANY_LAYOUT		27
#endif

/*L:110
 * We can ignore the 43 include files we need for this program, but I do want
 * to draw attention to the use of kernel-style types.
 *
 * As Linus said, "C is a Spartan language, and so should your naming be."  I
 * like these abbreviations, so we define them here.  Note that u64 is always
 * unsigned long long, which works on all Linux systems: this means that we can
 * use %llu in printf for any u64.
 */
typedef unsigned long long u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
/*:*/

#define VIRTIO_CONFIG_NO_LEGACY
#define VIRTIO_PCI_NO_LEGACY
#define VIRTIO_BLK_NO_LEGACY
#define VIRTIO_NET_NO_LEGACY

/* Use in-kernel ones, which defines VIRTIO_F_VERSION_1 */
#include "../../include/uapi/linux/virtio_config.h"
#include "../../include/uapi/linux/virtio_net.h"
#include "../../include/uapi/linux/virtio_blk.h"
#include "../../include/uapi/linux/virtio_console.h"
#include "../../include/uapi/linux/virtio_rng.h"
#include <linux/virtio_ring.h>
#include "../../include/uapi/linux/virtio_pci.h"
#include <asm/bootparam.h>
#include "../../include/linux/lguest_launcher.h"

#define BRIDGE_PFX "bridge:"
#ifndef SIOCBRADDIF
#define SIOCBRADDIF	0x89a2		/* add interface to bridge      */
#endif
/* We can have up to 256 pages for devices. */
#define DEVICE_PAGES 256
/* This will occupy 3 pages: it must be a power of 2. */
#define VIRTQUEUE_NUM 256

/*L:120
 * verbose is both a global flag and a macro.  The C preprocessor allows
 * this, and although I wouldn't recommend it, it works quite nicely here.
 */
static bool verbose;
#define verbose(args...) \
	do { if (verbose) printf(args); } while(0)
/*:*/

/* The pointer to the start of guest memory. */
static void *guest_base;
/* The maximum guest physical address allowed, and maximum possible. */
static unsigned long guest_limit, guest_max, guest_mmio;
/* The /dev/lguest file descriptor. */
static int lguest_fd;

/* a per-cpu variable indicating whose vcpu is currently running */
static unsigned int __thread cpu_id;

/* 5 bit device number in the PCI_CONFIG_ADDR => 32 only */
#define MAX_PCI_DEVICES 32

/* This is our list of devices. */
struct device_list {
	/* Counter to assign interrupt numbers. */
	unsigned int next_irq;

	/* Counter to print out convenient device numbers. */
	unsigned int device_num;

	/* PCI devices. */
	struct device *pci[MAX_PCI_DEVICES];
};

/* The list of Guest devices, based on command line arguments. */
static struct device_list devices;

/*
 * Just like struct virtio_pci_cfg_cap in uapi/linux/virtio_pci.h,
 * but uses a u32 explicitly for the data.
 */
struct virtio_pci_cfg_cap_u32 {
	struct virtio_pci_cap cap;
	u32 pci_cfg_data; /* Data for BAR access. */
};

struct virtio_pci_mmio {
	struct virtio_pci_common_cfg cfg;
	u16 notify;
	u8 isr;
	u8 padding;
	/* Device-specific configuration follows this. */
};

/* This is the layout (little-endian) of the PCI config space. */
struct pci_config {
	u16 vendor_id, device_id;
	u16 command, status;
	u8 revid, prog_if, subclass, class;
	u8 cacheline_size, lat_timer, header_type, bist;
	u32 bar[6];
	u32 cardbus_cis_ptr;
	u16 subsystem_vendor_id, subsystem_device_id;
	u32 expansion_rom_addr;
	u8 capabilities, reserved1[3];
	u32 reserved2;
	u8 irq_line, irq_pin, min_grant, max_latency;

	/* Now, this is the linked capability list. */
	struct virtio_pci_cap common;
	struct virtio_pci_notify_cap notify;
	struct virtio_pci_cap isr;
	struct virtio_pci_cap device;
	struct virtio_pci_cfg_cap_u32 cfg_access;
};

/* The device structure describes a single device. */
struct device {
	/* The name of this device, for --verbose. */
	const char *name;

	/* Any queues attached to this device */
	struct virtqueue *vq;

	/* Is it operational */
	bool running;

	/* Has it written FEATURES_OK but not re-checked it? */
	bool wrote_features_ok;

	/* PCI configuration */
	union {
		struct pci_config config;
		u32 config_words[sizeof(struct pci_config) / sizeof(u32)];
	};

	/* Features we offer, and those accepted. */
	u64 features, features_accepted;

	/* Device-specific config hangs off the end of this. */
	struct virtio_pci_mmio *mmio;

	/* PCI MMIO resources (all in BAR0) */
	size_t mmio_size;
	u32 mmio_addr;

	/* Device-specific data. */
	void *priv;
};

/* The virtqueue structure describes a queue attached to a device. */
struct virtqueue {
	struct virtqueue *next;

	/* Which device owns me. */
	struct device *dev;

	/* Name for printing errors. */
	const char *name;

	/* The actual ring of buffers. */
	struct vring vring;

	/* The information about this virtqueue (we only use queue_size on) */
	struct virtio_pci_common_cfg pci_config;

	/* Last available index we saw. */
	u16 last_avail_idx;

	/* How many are used since we sent last irq? */
	unsigned int pending_used;

	/* Eventfd where Guest notifications arrive. */
	int eventfd;

	/* Function for the thread which is servicing this virtqueue. */
	void (*service)(struct virtqueue *vq);
	pid_t thread;
};

/* Remember the arguments to the program so we can "reboot" */
static char **main_args;

/* The original tty settings to restore on exit. */
static struct termios orig_term;

/*
 * We have to be careful with barriers: our devices are all run in separate
 * threads and so we need to make sure that changes visible to the Guest happen
 * in precise order.
 */
#define wmb() __asm__ __volatile__("" : : : "memory")
#define rmb() __asm__ __volatile__("lock; addl $0,0(%%esp)" : : : "memory")
#define mb() __asm__ __volatile__("lock; addl $0,0(%%esp)" : : : "memory")

/* Wrapper for the last available index.  Makes it easier to change. */
#define lg_last_avail(vq)	((vq)->last_avail_idx)

/*
 * The virtio configuration space is defined to be little-endian.  x86 is
 * little-endian too, but it's nice to be explicit so we have these helpers.
 */
#define cpu_to_le16(v16) (v16)
#define cpu_to_le32(v32) (v32)
#define cpu_to_le64(v64) (v64)
#define le16_to_cpu(v16) (v16)
#define le32_to_cpu(v32) (v32)
#define le64_to_cpu(v64) (v64)

/*
 * A real device would ignore weird/non-compliant driver behaviour.  We
 * stop and flag it, to help debugging Linux problems.
 */
#define bad_driver(d, fmt, ...) \
	errx(1, "%s: bad driver: " fmt, (d)->name, ## __VA_ARGS__)
#define bad_driver_vq(vq, fmt, ...)			       \
	errx(1, "%s vq %s: bad driver: " fmt, (vq)->dev->name, \
	     vq->name, ## __VA_ARGS__)

/* Is this iovec empty? */
static bool iov_empty(const struct iovec iov[], unsigned int num_iov)
{
	unsigned int i;

	for (i = 0; i < num_iov; i++)
		if (iov[i].iov_len)
			return false;
	return true;
}

/* Take len bytes from the front of this iovec. */
static void iov_consume(struct device *d,
			struct iovec iov[], unsigned num_iov,
			void *dest, unsigned len)
{
	unsigned int i;

	for (i = 0; i < num_iov; i++) {
		unsigned int used;

		used = iov[i].iov_len < len ? iov[i].iov_len : len;
		if (dest) {
			memcpy(dest, iov[i].iov_base, used);
			dest += used;
		}
		iov[i].iov_base += used;
		iov[i].iov_len -= used;
		len -= used;
	}
	if (len != 0)
		bad_driver(d, "iovec too short!");
}

/*L:100
 * The Launcher code itself takes us out into userspace, that scary place where
 * pointers run wild and free!  Unfortunately, like most userspace programs,
 * it's quite boring (which is why everyone likes to hack on the kernel!).
 * Perhaps if you make up an Lguest Drinking Game at this point, it will get
 * you through this section.  Or, maybe not.
 *
 * The Launcher sets up a big chunk of memory to be the Guest's "physical"
 * memory and stores it in "guest_base".  In other words, Guest physical ==
 * Launcher virtual with an offset.
 *
 * This can be tough to get your head around, but usually it just means that we
 * use these trivial conversion functions when the Guest gives us its
 * "physical" addresses:
 */
static void *from_guest_phys(unsigned long addr)
{
	return guest_base + addr;
}

static unsigned long to_guest_phys(const void *addr)
{
	return (addr - guest_base);
}

/*L:130
 * Loading the Kernel.
 *
 * We start with couple of simple helper routines.  open_or_die() avoids
 * error-checking code cluttering the callers:
 */
static int open_or_die(const char *name, int flags)
{
	int fd = open(name, flags);
	if (fd < 0)
		err(1, "Failed to open %s", name);
	return fd;
}

/* map_zeroed_pages() takes a number of pages. */
static void *map_zeroed_pages(unsigned int num)
{
	int fd = open_or_die("/dev/zero", O_RDONLY);
	void *addr;

	/*
	 * We use a private mapping (ie. if we write to the page, it will be
	 * copied). We allocate an extra two pages PROT_NONE to act as guard
	 * pages against read/write attempts that exceed allocated space.
	 */
	addr = mmap(NULL, getpagesize() * (num+2),
		    PROT_NONE, MAP_PRIVATE, fd, 0);

	if (addr == MAP_FAILED)
		err(1, "Mmapping %u pages of /dev/zero", num);

	if (mprotect(addr + getpagesize(), getpagesize() * num,
		     PROT_READ|PROT_WRITE) == -1)
		err(1, "mprotect rw %u pages failed", num);

	/*
	 * One neat mmap feature is that you can close the fd, and it
	 * stays mapped.
	 */
	close(fd);

	/* Return address after PROT_NONE page */
	return addr + getpagesize();
}

/* Get some bytes which won't be mapped into the guest. */
static unsigned long get_mmio_region(size_t size)
{
	unsigned long addr = guest_mmio;
	size_t i;

	if (!size)
		return addr;

	/* Size has to be a power of 2 (and multiple of 16) */
	for (i = 1; i < size; i <<= 1);

	guest_mmio += i;

	return addr;
}

/*
 * This routine is used to load the kernel or initrd.  It tries mmap, but if
 * that fails (Plan 9's kernel file isn't nicely aligned on page boundaries),
 * it falls back to reading the memory in.
 */
static void map_at(int fd, void *addr, unsigned long offset, unsigned long len)
{
	ssize_t r;

	/*
	 * We map writable even though for some segments are marked read-only.
	 * The kernel really wants to be writable: it patches its own
	 * instructions.
	 *
	 * MAP_PRIVATE means that the page won't be copied until a write is
	 * done to it.  This allows us to share untouched memory between
	 * Guests.
	 */
	if (mmap(addr, len, PROT_READ|PROT_WRITE,
		 MAP_FIXED|MAP_PRIVATE, fd, offset) != MAP_FAILED)
		return;

	/* pread does a seek and a read in one shot: saves a few lines. */
	r = pread(fd, addr, len, offset);
	if (r != len)
		err(1, "Reading offset %lu len %lu gave %zi", offset, len, r);
}

/*
 * This routine takes an open vmlinux image, which is in ELF, and maps it into
 * the Guest memory.  ELF = Embedded Linking Format, which is the format used
 * by all modern binaries on Linux including the kernel.
 *
 * The ELF headers give *two* addresses: a physical address, and a virtual
 * address.  We use the physical address; the Guest will map itself to the
 * virtual address.
 *
 * We return the starting address.
 */
static unsigned long map_elf(int elf_fd, const Elf32_Ehdr *ehdr)
{
	Elf32_Phdr phdr[ehdr->e_phnum];
	unsigned int i;

	/*
	 * Sanity checks on the main ELF header: an x86 executable with a
	 * reasonable number of correctly-sized program headers.
	 */
	if (ehdr->e_type != ET_EXEC
	    || ehdr->e_machine != EM_386
	    || ehdr->e_phentsize != sizeof(Elf32_Phdr)
	    || ehdr->e_phnum < 1 || ehdr->e_phnum > 65536U/sizeof(Elf32_Phdr))
		errx(1, "Malformed elf header");

	/*
	 * An ELF executable contains an ELF header and a number of "program"
	 * headers which indicate which parts ("segments") of the program to
	 * load where.
	 */

	/* We read in all the program headers at once: */
	if (lseek(elf_fd, ehdr->e_phoff, SEEK_SET) < 0)
		err(1, "Seeking to program headers");
	if (read(elf_fd, phdr, sizeof(phdr)) != sizeof(phdr))
		err(1, "Reading program headers");

	/*
	 * Try all the headers: there are usually only three.  A read-only one,
	 * a read-write one, and a "note" section which we don't load.
	 */
	for (i = 0; i < ehdr->e_phnum; i++) {
		/* If this isn't a loadable segment, we ignore it */
		if (phdr[i].p_type != PT_LOAD)
			continue;

		verbose("Section %i: size %i addr %p\n",
			i, phdr[i].p_memsz, (void *)phdr[i].p_paddr);

		/* We map this section of the file at its physical address. */
		map_at(elf_fd, from_guest_phys(phdr[i].p_paddr),
		       phdr[i].p_offset, phdr[i].p_filesz);
	}

	/* The entry point is given in the ELF header. */
	return ehdr->e_entry;
}

/*L:150
 * A bzImage, unlike an ELF file, is not meant to be loaded.  You're supposed
 * to jump into it and it will unpack itself.  We used to have to perform some
 * hairy magic because the unpacking code scared me.
 *
 * Fortunately, Jeremy Fitzhardinge convinced me it wasn't that hard and wrote
 * a small patch to jump over the tricky bits in the Guest, so now we just read
 * the funky header so we know where in the file to load, and away we go!
 */
static unsigned long load_bzimage(int fd)
{
	struct boot_params boot;
	int r;
	/* Modern bzImages get loaded at 1M. */
	void *p = from_guest_phys(0x100000);

	/*
	 * Go back to the start of the file and read the header.  It should be
	 * a Linux boot header (see Documentation/x86/boot.txt)
	 */
	lseek(fd, 0, SEEK_SET);
	read(fd, &boot, sizeof(boot));

	/* Inside the setup_hdr, we expect the magic "HdrS" */
	if (memcmp(&boot.hdr.header, "HdrS", 4) != 0)
		errx(1, "This doesn't look like a bzImage to me");

	/* Skip over the extra sectors of the header. */
	lseek(fd, (boot.hdr.setup_sects+1) * 512, SEEK_SET);

	/* Now read everything into memory. in nice big chunks. */
	while ((r = read(fd, p, 65536)) > 0)
		p += r;

	/* Finally, code32_start tells us where to enter the kernel. */
	return boot.hdr.code32_start;
}

/*L:140
 * Loading the kernel is easy when it's a "vmlinux", but most kernels
 * come wrapped up in the self-decompressing "bzImage" format.  With a little
 * work, we can load those, too.
 */
static unsigned long load_kernel(int fd)
{
	Elf32_Ehdr hdr;

	/* Read in the first few bytes. */
	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		err(1, "Reading kernel");

	/* If it's an ELF file, it starts with "\177ELF" */
	if (memcmp(hdr.e_ident, ELFMAG, SELFMAG) == 0)
		return map_elf(fd, &hdr);

	/* Otherwise we assume it's a bzImage, and try to load it. */
	return load_bzimage(fd);
}

/*
 * This is a trivial little helper to align pages.  Andi Kleen hated it because
 * it calls getpagesize() twice: "it's dumb code."
 *
 * Kernel guys get really het up about optimization, even when it's not
 * necessary.  I leave this code as a reaction against that.
 */
static inline unsigned long page_align(unsigned long addr)
{
	/* Add upwards and truncate downwards. */
	return ((addr + getpagesize()-1) & ~(getpagesize()-1));
}

/*L:180
 * An "initial ram disk" is a disk image loaded into memory along with the
 * kernel which the kernel can use to boot from without needing any drivers.
 * Most distributions now use this as standard: the initrd contains the code to
 * load the appropriate driver modules for the current machine.
 *
 * Importantly, James Morris works for RedHat, and Fedora uses initrds for its
 * kernels.  He sent me this (and tells me when I break it).
 */
static unsigned long load_initrd(const char *name, unsigned long mem)
{
	int ifd;
	struct stat st;
	unsigned long len;

	ifd = open_or_die(name, O_RDONLY);
	/* fstat() is needed to get the file size. */
	if (fstat(ifd, &st) < 0)
		err(1, "fstat() on initrd '%s'", name);

	/*
	 * We map the initrd at the top of memory, but mmap wants it to be
	 * page-aligned, so we round the size up for that.
	 */
	len = page_align(st.st_size);
	map_at(ifd, from_guest_phys(mem - len), 0, st.st_size);
	/*
	 * Once a file is mapped, you can close the file descriptor.  It's a
	 * little odd, but quite useful.
	 */
	close(ifd);
	verbose("mapped initrd %s size=%lu @ %p\n", name, len, (void*)mem-len);

	/* We return the initrd size. */
	return len;
}
/*:*/

/*
 * Simple routine to roll all the commandline arguments together with spaces
 * between them.
 */
static void concat(char *dst, char *args[])
{
	unsigned int i, len = 0;

	for (i = 0; args[i]; i++) {
		if (i) {
			strcat(dst+len, " ");
			len++;
		}
		strcpy(dst+len, args[i]);
		len += strlen(args[i]);
	}
	/* In case it's empty. */
	dst[len] = '\0';
}

/*L:185
 * This is where we actually tell the kernel to initialize the Guest.  We
 * saw the arguments it expects when we looked at initialize() in lguest_user.c:
 * the base of Guest "physical" memory, the top physical page to allow and the
 * entry point for the Guest.
 */
static void tell_kernel(unsigned long start)
{
	unsigned long args[] = { LHREQ_INITIALIZE,
				 (unsigned long)guest_base,
				 guest_limit / getpagesize(), start,
				 (guest_mmio+getpagesize()-1) / getpagesize() };
	verbose("Guest: %p - %p (%#lx, MMIO %#lx)\n",
		guest_base, guest_base + guest_limit,
		guest_limit, guest_mmio);
	lguest_fd = open_or_die("/dev/lguest", O_RDWR);
	if (write(lguest_fd, args, sizeof(args)) < 0)
		err(1, "Writing to /dev/lguest");
}
/*:*/

/*L:200
 * Device Handling.
 *
 * When the Guest gives us a buffer, it sends an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which checks it and exits with an error message
 * if something funny is going on:
 */
static void *_check_pointer(struct device *d,
			    unsigned long addr, unsigned int size,
			    unsigned int line)
{
	/*
	 * Check if the requested address and size exceeds the allocated memory,
	 * or addr + size wraps around.
	 */
	if ((addr + size) > guest_limit || (addr + size) < addr)
		bad_driver(d, "%s:%i: Invalid address %#lx",
			   __FILE__, line, addr);
	/*
	 * We return a pointer for the caller's convenience, now we know it's
	 * safe to use.
	 */
	return from_guest_phys(addr);
}
/* A macro which transparently hands the line number to the real function. */
#define check_pointer(d,addr,size) _check_pointer(d, addr, size, __LINE__)

/*
 * Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or vq->vring.num if we're
 * at the end.
 */
static unsigned next_desc(struct device *d, struct vring_desc *desc,
			  unsigned int i, unsigned int max)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc[i].flags & VRING_DESC_F_NEXT))
		return max;

	/* Check they're not leading us off end of descriptors. */
	next = desc[i].next;
	/* Make sure compiler knows to grab that: we don't want it changing! */
	wmb();

	if (next >= max)
		bad_driver(d, "Desc next is %u", next);

	return next;
}

/*
 * This actually sends the interrupt for this virtqueue, if we've used a
 * buffer.
 */
static void trigger_irq(struct virtqueue *vq)
{
	unsigned long buf[] = { LHREQ_IRQ, vq->dev->config.irq_line };

	/* Don't inform them if nothing used. */
	if (!vq->pending_used)
		return;
	vq->pending_used = 0;

	/*
	 * 2.4.7.1:
	 *
	 *  If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
	 *    The driver MUST set flags to 0 or 1. 
	 */
	if (vq->vring.avail->flags > 1)
		bad_driver_vq(vq, "avail->flags = %u\n", vq->vring.avail->flags);

	/*
	 * 2.4.7.2:
	 *
	 *  If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
	 *
	 *     - The device MUST ignore the used_event value.
	 *     - After the device writes a descriptor index into the used ring:
	 *         - If flags is 1, the device SHOULD NOT send an interrupt.
	 *         - If flags is 0, the device MUST send an interrupt.
	 */
	if (vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT) {
		return;
	}

	/*
	 * 4.1.4.5.1:
	 *
	 *  If MSI-X capability is disabled, the device MUST set the Queue
	 *  Interrupt bit in ISR status before sending a virtqueue notification
	 *  to the driver.
	 */
	vq->dev->mmio->isr = 0x1;

	/* Send the Guest an interrupt tell them we used something up. */
	if (write(lguest_fd, buf, sizeof(buf)) != 0)
		err(1, "Triggering irq %i", vq->dev->config.irq_line);
}

/*
 * This looks in the virtqueue for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function waits if necessary, and returns the descriptor number found.
 */
static unsigned wait_for_vq_desc(struct virtqueue *vq,
				 struct iovec iov[],
				 unsigned int *out_num, unsigned int *in_num)
{
	unsigned int i, head, max;
	struct vring_desc *desc;
	u16 last_avail = lg_last_avail(vq);

	/*
	 * 2.4.7.1:
	 *
	 *   The driver MUST handle spurious interrupts from the device.
	 *
	 * That's why this is a while loop.
	 */

	/* There's nothing available? */
	while (last_avail == vq->vring.avail->idx) {
		u64 event;

		/*
		 * Since we're about to sleep, now is a good time to tell the
		 * Guest about what we've used up to now.
		 */
		trigger_irq(vq);

		/* OK, now we need to know about added descriptors. */
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;

		/*
		 * They could have slipped one in as we were doing that: make
		 * sure it's written, then check again.
		 */
		mb();
		if (last_avail != vq->vring.avail->idx) {
			vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
			break;
		}

		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (read(vq->eventfd, &event, sizeof(event)) != sizeof(event))
			errx(1, "Event read failed?");

		/* We don't need to be notified again. */
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
	}

	/* Check it isn't doing very strange things with descriptor numbers. */
	if ((u16)(vq->vring.avail->idx - last_avail) > vq->vring.num)
		bad_driver_vq(vq, "Guest moved used index from %u to %u",
			      last_avail, vq->vring.avail->idx);

	/* 
	 * Make sure we read the descriptor number *after* we read the ring
	 * update; don't let the cpu or compiler change the order.
	 */
	rmb();

	/*
	 * Grab the next descriptor number they're advertising, and increment
	 * the index we've seen.
	 */
	head = vq->vring.avail->ring[last_avail % vq->vring.num];
	lg_last_avail(vq)++;

	/* If their number is silly, that's a fatal mistake. */
	if (head >= vq->vring.num)
		bad_driver_vq(vq, "Guest says index %u is available", head);

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;

	max = vq->vring.num;
	desc = vq->vring.desc;
	i = head;

	/*
	 * We have to read the descriptor after we read the descriptor number,
	 * but there's a data dependency there so the CPU shouldn't reorder
	 * that: no rmb() required.
	 */

	do {
		/*
		 * If this is an indirect entry, then this buffer contains a
		 * descriptor table which we handle as if it's any normal
		 * descriptor chain.
		 */
		if (desc[i].flags & VRING_DESC_F_INDIRECT) {
			/* 2.4.5.3.1:
			 *
			 *  The driver MUST NOT set the VIRTQ_DESC_F_INDIRECT
			 *  flag unless the VIRTIO_F_INDIRECT_DESC feature was
			 *  negotiated.
			 */
			if (!(vq->dev->features_accepted &
			      (1<<VIRTIO_RING_F_INDIRECT_DESC)))
				bad_driver_vq(vq, "vq indirect not negotiated");

			/*
			 * 2.4.5.3.1:
			 *
			 *   The driver MUST NOT set the VIRTQ_DESC_F_INDIRECT
			 *   flag within an indirect descriptor (ie. only one
			 *   table per descriptor).
			 */
			if (desc != vq->vring.desc)
				bad_driver_vq(vq, "Indirect within indirect");

			/*
			 * Proposed update VIRTIO-134 spells this out:
			 *
			 *   A driver MUST NOT set both VIRTQ_DESC_F_INDIRECT
			 *   and VIRTQ_DESC_F_NEXT in flags.
			 */
			if (desc[i].flags & VRING_DESC_F_NEXT)
				bad_driver_vq(vq, "indirect and next together");

			if (desc[i].len % sizeof(struct vring_desc))
				bad_driver_vq(vq,
					      "Invalid size for indirect table");
			/*
			 * 2.4.5.3.2:
			 *
			 *  The device MUST ignore the write-only flag
			 *  (flags&VIRTQ_DESC_F_WRITE) in the descriptor that
			 *  refers to an indirect table.
			 *
			 * We ignore it here: :)
			 */

			max = desc[i].len / sizeof(struct vring_desc);
			desc = check_pointer(vq->dev, desc[i].addr, desc[i].len);
			i = 0;

			/* 2.4.5.3.1:
			 *
			 *  A driver MUST NOT create a descriptor chain longer
			 *  than the Queue Size of the device.
			 */
			if (max > vq->pci_config.queue_size)
				bad_driver_vq(vq,
					      "indirect has too many entries");
		}

		/* Grab the first descriptor, and check it's OK. */
		iov[*out_num + *in_num].iov_len = desc[i].len;
		iov[*out_num + *in_num].iov_base
			= check_pointer(vq->dev, desc[i].addr, desc[i].len);
		/* If this is an input descriptor, increment that count. */
		if (desc[i].flags & VRING_DESC_F_WRITE)
			(*in_num)++;
		else {
			/*
			 * If it's an output descriptor, they're all supposed
			 * to come before any input descriptors.
			 */
			if (*in_num)
				bad_driver_vq(vq,
					      "Descriptor has out after in");
			(*out_num)++;
		}

		/* If we've got too many, that implies a descriptor loop. */
		if (*out_num + *in_num > max)
			bad_driver_vq(vq, "Looped descriptor");
	} while ((i = next_desc(vq->dev, desc, i, max)) != max);

	return head;
}

/*
 * After we've used one of their buffers, we tell the Guest about it.  Sometime
 * later we'll want to send them an interrupt using trigger_irq(); note that
 * wait_for_vq_desc() does that for us if it has to wait.
 */
static void add_used(struct virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem *used;

	/*
	 * The virtqueue contains a ring of used buffers.  Get a pointer to the
	 * next entry in that used ring.
	 */
	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = head;
	used->len = len;
	/* Make sure buffer is written before we update index. */
	wmb();
	vq->vring.used->idx++;
	vq->pending_used++;
}

/* And here's the combo meal deal.  Supersize me! */
static void add_used_and_trigger(struct virtqueue *vq, unsigned head, int len)
{
	add_used(vq, head, len);
	trigger_irq(vq);
}

/*
 * The Console
 *
 * We associate some data with the console for our exit hack.
 */
struct console_abort {
	/* How many times have they hit ^C? */
	int count;
	/* When did they start? */
	struct timeval start;
};

/* This is the routine which handles console input (ie. stdin). */
static void console_input(struct virtqueue *vq)
{
	int len;
	unsigned int head, in_num, out_num;
	struct console_abort *abort = vq->dev->priv;
	struct iovec iov[vq->vring.num];

	/* Make sure there's a descriptor available. */
	head = wait_for_vq_desc(vq, iov, &out_num, &in_num);
	if (out_num)
		bad_driver_vq(vq, "Output buffers in console in queue?");

	/* Read into it.  This is where we usually wait. */
	len = readv(STDIN_FILENO, iov, in_num);
	if (len <= 0) {
		/* Ran out of input? */
		warnx("Failed to get console input, ignoring console.");
		/*
		 * For simplicity, dying threads kill the whole Launcher.  So
		 * just nap here.
		 */
		for (;;)
			pause();
	}

	/* Tell the Guest we used a buffer. */
	add_used_and_trigger(vq, head, len);

	/*
	 * Three ^C within one second?  Exit.
	 *
	 * This is such a hack, but works surprisingly well.  Each ^C has to
	 * be in a buffer by itself, so they can't be too fast.  But we check
	 * that we get three within about a second, so they can't be too
	 * slow.
	 */
	if (len != 1 || ((char *)iov[0].iov_base)[0] != 3) {
		abort->count = 0;
		return;
	}

	abort->count++;
	if (abort->count == 1)
		gettimeofday(&abort->start, NULL);
	else if (abort->count == 3) {
		struct timeval now;
		gettimeofday(&now, NULL);
		/* Kill all Launcher processes with SIGINT, like normal ^C */
		if (now.tv_sec <= abort->start.tv_sec+1)
			kill(0, SIGINT);
		abort->count = 0;
	}
}

/* This is the routine which handles console output (ie. stdout). */
static void console_output(struct virtqueue *vq)
{
	unsigned int head, out, in;
	struct iovec iov[vq->vring.num];

	/* We usually wait in here, for the Guest to give us something. */
	head = wait_for_vq_desc(vq, iov, &out, &in);
	if (in)
		bad_driver_vq(vq, "Input buffers in console output queue?");

	/* writev can return a partial write, so we loop here. */
	while (!iov_empty(iov, out)) {
		int len = writev(STDOUT_FILENO, iov, out);
		if (len <= 0) {
			warn("Write to stdout gave %i (%d)", len, errno);
			break;
		}
		iov_consume(vq->dev, iov, out, NULL, len);
	}

	/*
	 * We're finished with that buffer: if we're going to sleep,
	 * wait_for_vq_desc() will prod the Guest with an interrupt.
	 */
	add_used(vq, head, 0);
}

/*
 * The Network
 *
 * Handling output for network is also simple: we get all the output buffers
 * and write them to /dev/net/tun.
 */
struct net_info {
	int tunfd;
};

static void net_output(struct virtqueue *vq)
{
	struct net_info *net_info = vq->dev->priv;
	unsigned int head, out, in;
	struct iovec iov[vq->vring.num];

	/* We usually wait in here for the Guest to give us a packet. */
	head = wait_for_vq_desc(vq, iov, &out, &in);
	if (in)
		bad_driver_vq(vq, "Input buffers in net output queue?");
	/*
	 * Send the whole thing through to /dev/net/tun.  It expects the exact
	 * same format: what a coincidence!
	 */
	if (writev(net_info->tunfd, iov, out) < 0)
		warnx("Write to tun failed (%d)?", errno);

	/*
	 * Done with that one; wait_for_vq_desc() will send the interrupt if
	 * all packets are processed.
	 */
	add_used(vq, head, 0);
}

/*
 * Handling network input is a bit trickier, because I've tried to optimize it.
 *
 * First we have a helper routine which tells is if from this file descriptor
 * (ie. the /dev/net/tun device) will block:
 */
static bool will_block(int fd)
{
	fd_set fdset;
	struct timeval zero = { 0, 0 };
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	return select(fd+1, &fdset, NULL, NULL, &zero) != 1;
}

/*
 * This handles packets coming in from the tun device to our Guest.  Like all
 * service routines, it gets called again as soon as it returns, so you don't
 * see a while(1) loop here.
 */
static void net_input(struct virtqueue *vq)
{
	int len;
	unsigned int head, out, in;
	struct iovec iov[vq->vring.num];
	struct net_info *net_info = vq->dev->priv;

	/*
	 * Get a descriptor to write an incoming packet into.  This will also
	 * send an interrupt if they're out of descriptors.
	 */
	head = wait_for_vq_desc(vq, iov, &out, &in);
	if (out)
		bad_driver_vq(vq, "Output buffers in net input queue?");

	/*
	 * If it looks like we'll block reading from the tun device, send them
	 * an interrupt.
	 */
	if (vq->pending_used && will_block(net_info->tunfd))
		trigger_irq(vq);

	/*
	 * Read in the packet.  This is where we normally wait (when there's no
	 * incoming network traffic).
	 */
	len = readv(net_info->tunfd, iov, in);
	if (len <= 0)
		warn("Failed to read from tun (%d).", errno);

	/*
	 * Mark that packet buffer as used, but don't interrupt here.  We want
	 * to wait until we've done as much work as we can.
	 */
	add_used(vq, head, len);
}
/*:*/

/* This is the helper to create threads: run the service routine in a loop. */
static int do_thread(void *_vq)
{
	struct virtqueue *vq = _vq;

	for (;;)
		vq->service(vq);
	return 0;
}

/*
 * When a child dies, we kill our entire process group with SIGTERM.  This
 * also has the side effect that the shell restores the console for us!
 */
static void kill_launcher(int signal)
{
	kill(0, SIGTERM);
}

static void reset_vq_pci_config(struct virtqueue *vq)
{
	vq->pci_config.queue_size = VIRTQUEUE_NUM;
	vq->pci_config.queue_enable = 0;
}

static void reset_device(struct device *dev)
{
	struct virtqueue *vq;

	verbose("Resetting device %s\n", dev->name);

	/* Clear any features they've acked. */
	dev->features_accepted = 0;

	/* We're going to be explicitly killing threads, so ignore them. */
	signal(SIGCHLD, SIG_IGN);

	/*
	 * 4.1.4.3.1:
	 *
	 *   The device MUST present a 0 in queue_enable on reset. 
	 *
	 * This means we set it here, and reset the saved ones in every vq.
	 */
	dev->mmio->cfg.queue_enable = 0;

	/* Get rid of the virtqueue threads */
	for (vq = dev->vq; vq; vq = vq->next) {
		vq->last_avail_idx = 0;
		reset_vq_pci_config(vq);
		if (vq->thread != (pid_t)-1) {
			kill(vq->thread, SIGTERM);
			waitpid(vq->thread, NULL, 0);
			vq->thread = (pid_t)-1;
		}
	}
	dev->running = false;
	dev->wrote_features_ok = false;

	/* Now we care if threads die. */
	signal(SIGCHLD, (void *)kill_launcher);
}

static void cleanup_devices(void)
{
	unsigned int i;

	for (i = 1; i < MAX_PCI_DEVICES; i++) {
		struct device *d = devices.pci[i];
		if (!d)
			continue;
		reset_device(d);
	}

	/* If we saved off the original terminal settings, restore them now. */
	if (orig_term.c_lflag & (ISIG|ICANON|ECHO))
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
}

/*L:217
 * We do PCI.  This is mainly done to let us test the kernel virtio PCI
 * code.
 */

/* Linux expects a PCI host bridge: ours is a dummy, and first on the bus. */
static struct device pci_host_bridge;

static void init_pci_host_bridge(void)
{
	pci_host_bridge.name = "PCI Host Bridge";
	pci_host_bridge.config.class = 0x06; /* bridge */
	pci_host_bridge.config.subclass = 0; /* host bridge */
	devices.pci[0] = &pci_host_bridge;
}

/* The IO ports used to read the PCI config space. */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/*
 * Not really portable, but does help readability: this is what the Guest
 * writes to the PCI_CONFIG_ADDR IO port.
 */
union pci_config_addr {
	struct {
		unsigned mbz: 2;
		unsigned offset: 6;
		unsigned funcnum: 3;
		unsigned devnum: 5;
		unsigned busnum: 8;
		unsigned reserved: 7;
		unsigned enabled : 1;
	} bits;
	u32 val;
};

/*
 * We cache what they wrote to the address port, so we know what they're
 * talking about when they access the data port.
 */
static union pci_config_addr pci_config_addr;

static struct device *find_pci_device(unsigned int index)
{
	return devices.pci[index];
}

/* PCI can do 1, 2 and 4 byte reads; we handle that here. */
static void ioread(u16 off, u32 v, u32 mask, u32 *val)
{
	assert(off < 4);
	assert(mask == 0xFF || mask == 0xFFFF || mask == 0xFFFFFFFF);
	*val = (v >> (off * 8)) & mask;
}

/* PCI can do 1, 2 and 4 byte writes; we handle that here. */
static void iowrite(u16 off, u32 v, u32 mask, u32 *dst)
{
	assert(off < 4);
	assert(mask == 0xFF || mask == 0xFFFF || mask == 0xFFFFFFFF);
	*dst &= ~(mask << (off * 8));
	*dst |= (v & mask) << (off * 8);
}

/*
 * Where PCI_CONFIG_DATA accesses depends on the previous write to
 * PCI_CONFIG_ADDR.
 */
static struct device *dev_and_reg(u32 *reg)
{
	if (!pci_config_addr.bits.enabled)
		return NULL;

	if (pci_config_addr.bits.funcnum != 0)
		return NULL;

	if (pci_config_addr.bits.busnum != 0)
		return NULL;

	if (pci_config_addr.bits.offset * 4 >= sizeof(struct pci_config))
		return NULL;

	*reg = pci_config_addr.bits.offset;
	return find_pci_device(pci_config_addr.bits.devnum);
}

/*
 * We can get invalid combinations of values while they're writing, so we
 * only fault if they try to write with some invalid bar/offset/length.
 */
static bool valid_bar_access(struct device *d,
			     struct virtio_pci_cfg_cap_u32 *cfg_access)
{
	/* We only have 1 bar (BAR0) */
	if (cfg_access->cap.bar != 0)
		return false;

	/* Check it's within BAR0. */
	if (cfg_access->cap.offset >= d->mmio_size
	    || cfg_access->cap.offset + cfg_access->cap.length > d->mmio_size)
		return false;

	/* Check length is 1, 2 or 4. */
	if (cfg_access->cap.length != 1
	    && cfg_access->cap.length != 2
	    && cfg_access->cap.length != 4)
		return false;

	/*
	 * 4.1.4.7.2:
	 *
	 *  The driver MUST NOT write a cap.offset which is not a multiple of
	 *  cap.length (ie. all accesses MUST be aligned).
	 */
	if (cfg_access->cap.offset % cfg_access->cap.length != 0)
		return false;

	/* Return pointer into word in BAR0. */
	return true;
}

/* Is this accessing the PCI config address port?. */
static bool is_pci_addr_port(u16 port)
{
	return port >= PCI_CONFIG_ADDR && port < PCI_CONFIG_ADDR + 4;
}

static bool pci_addr_iowrite(u16 port, u32 mask, u32 val)
{
	iowrite(port - PCI_CONFIG_ADDR, val, mask,
		&pci_config_addr.val);
	verbose("PCI%s: %#x/%x: bus %u dev %u func %u reg %u\n",
		pci_config_addr.bits.enabled ? "" : " DISABLED",
		val, mask,
		pci_config_addr.bits.busnum,
		pci_config_addr.bits.devnum,
		pci_config_addr.bits.funcnum,
		pci_config_addr.bits.offset);
	return true;
}

static void pci_addr_ioread(u16 port, u32 mask, u32 *val)
{
	ioread(port - PCI_CONFIG_ADDR, pci_config_addr.val, mask, val);
}

/* Is this accessing the PCI config data port?. */
static bool is_pci_data_port(u16 port)
{
	return port >= PCI_CONFIG_DATA && port < PCI_CONFIG_DATA + 4;
}

static void emulate_mmio_write(struct device *d, u32 off, u32 val, u32 mask);

static bool pci_data_iowrite(u16 port, u32 mask, u32 val)
{
	u32 reg, portoff;
	struct device *d = dev_and_reg(&reg);

	/* Complain if they don't belong to a device. */
	if (!d)
		return false;

	/* They can do 1 byte writes, etc. */
	portoff = port - PCI_CONFIG_DATA;

	/*
	 * PCI uses a weird way to determine the BAR size: the OS
	 * writes all 1's, and sees which ones stick.
	 */
	if (&d->config_words[reg] == &d->config.bar[0]) {
		int i;

		iowrite(portoff, val, mask, &d->config.bar[0]);
		for (i = 0; (1 << i) < d->mmio_size; i++)
			d->config.bar[0] &= ~(1 << i);
		return true;
	} else if ((&d->config_words[reg] > &d->config.bar[0]
		    && &d->config_words[reg] <= &d->config.bar[6])
		   || &d->config_words[reg] == &d->config.expansion_rom_addr) {
		/* Allow writing to any other BAR, or expansion ROM */
		iowrite(portoff, val, mask, &d->config_words[reg]);
		return true;
		/* We let them override latency timer and cacheline size */
	} else if (&d->config_words[reg] == (void *)&d->config.cacheline_size) {
		/* Only let them change the first two fields. */
		if (mask == 0xFFFFFFFF)
			mask = 0xFFFF;
		iowrite(portoff, val, mask, &d->config_words[reg]);
		return true;
	} else if (&d->config_words[reg] == (void *)&d->config.command
		   && mask == 0xFFFF) {
		/* Ignore command writes. */
		return true;
	} else if (&d->config_words[reg]
		   == (void *)&d->config.cfg_access.cap.bar
		   || &d->config_words[reg]
		   == &d->config.cfg_access.cap.length
		   || &d->config_words[reg]
		   == &d->config.cfg_access.cap.offset) {

		/*
		 * The VIRTIO_PCI_CAP_PCI_CFG capability
		 * provides a backdoor to access the MMIO
		 * regions without mapping them.  Weird, but
		 * useful.
		 */
		iowrite(portoff, val, mask, &d->config_words[reg]);
		return true;
	} else if (&d->config_words[reg] == &d->config.cfg_access.pci_cfg_data) {
		u32 write_mask;

		/*
		 * 4.1.4.7.1:
		 *
		 *  Upon detecting driver write access to pci_cfg_data, the
		 *  device MUST execute a write access at offset cap.offset at
		 *  BAR selected by cap.bar using the first cap.length bytes
		 *  from pci_cfg_data.
		 */

		/* Must be bar 0 */
		if (!valid_bar_access(d, &d->config.cfg_access))
			return false;

		iowrite(portoff, val, mask, &d->config.cfg_access.pci_cfg_data);

		/*
		 * Now emulate a write.  The mask we use is set by
		 * len, *not* this write!
		 */
		write_mask = (1ULL<<(8*d->config.cfg_access.cap.length)) - 1;
		verbose("Window writing %#x/%#x to bar %u, offset %u len %u\n",
			d->config.cfg_access.pci_cfg_data, write_mask,
			d->config.cfg_access.cap.bar,
			d->config.cfg_access.cap.offset,
			d->config.cfg_access.cap.length);

		emulate_mmio_write(d, d->config.cfg_access.cap.offset,
				   d->config.cfg_access.pci_cfg_data,
				   write_mask);
		return true;
	}

	/*
	 * 4.1.4.1:
	 *
	 *  The driver MUST NOT write into any field of the capability
	 *  structure, with the exception of those with cap_type
	 *  VIRTIO_PCI_CAP_PCI_CFG...
	 */
	return false;
}

static u32 emulate_mmio_read(struct device *d, u32 off, u32 mask);

static void pci_data_ioread(u16 port, u32 mask, u32 *val)
{
	u32 reg;
	struct device *d = dev_and_reg(&reg);

	if (!d)
		return;

	/* Read through the PCI MMIO access window is special */
	if (&d->config_words[reg] == &d->config.cfg_access.pci_cfg_data) {
		u32 read_mask;

		/*
		 * 4.1.4.7.1:
		 *
		 *  Upon detecting driver read access to pci_cfg_data, the
		 *  device MUST execute a read access of length cap.length at
		 *  offset cap.offset at BAR selected by cap.bar and store the
		 *  first cap.length bytes in pci_cfg_data.
		 */
		/* Must be bar 0 */
		if (!valid_bar_access(d, &d->config.cfg_access))
			bad_driver(d,
			     "Invalid cfg_access to bar%u, offset %u len %u",
			     d->config.cfg_access.cap.bar,
			     d->config.cfg_access.cap.offset,
			     d->config.cfg_access.cap.length);

		/*
		 * Read into the window.  The mask we use is set by
		 * len, *not* this read!
		 */
		read_mask = (1ULL<<(8*d->config.cfg_access.cap.length))-1;
		d->config.cfg_access.pci_cfg_data
			= emulate_mmio_read(d,
					    d->config.cfg_access.cap.offset,
					    read_mask);
		verbose("Window read %#x/%#x from bar %u, offset %u len %u\n",
			d->config.cfg_access.pci_cfg_data, read_mask,
			d->config.cfg_access.cap.bar,
			d->config.cfg_access.cap.offset,
			d->config.cfg_access.cap.length);
	}
	ioread(port - PCI_CONFIG_DATA, d->config_words[reg], mask, val);
}

/*L:216
 * This is where we emulate a handful of Guest instructions.  It's ugly
 * and we used to do it in the kernel but it grew over time.
 */

/*
 * We use the ptrace syscall's pt_regs struct to talk about registers
 * to lguest: these macros convert the names to the offsets.
 */
#define getreg(name) getreg_off(offsetof(struct user_regs_struct, name))
#define setreg(name, val) \
	setreg_off(offsetof(struct user_regs_struct, name), (val))

static u32 getreg_off(size_t offset)
{
	u32 r;
	unsigned long args[] = { LHREQ_GETREG, offset };

	if (pwrite(lguest_fd, args, sizeof(args), cpu_id) < 0)
		err(1, "Getting register %u", offset);
	if (pread(lguest_fd, &r, sizeof(r), cpu_id) != sizeof(r))
		err(1, "Reading register %u", offset);

	return r;
}

static void setreg_off(size_t offset, u32 val)
{
	unsigned long args[] = { LHREQ_SETREG, offset, val };

	if (pwrite(lguest_fd, args, sizeof(args), cpu_id) < 0)
		err(1, "Setting register %u", offset);
}

/* Get register by instruction encoding */
static u32 getreg_num(unsigned regnum, u32 mask)
{
	/* 8 bit ops use regnums 4-7 for high parts of word */
	if (mask == 0xFF && (regnum & 0x4))
		return getreg_num(regnum & 0x3, 0xFFFF) >> 8;

	switch (regnum) {
	case 0: return getreg(eax) & mask;
	case 1: return getreg(ecx) & mask;
	case 2: return getreg(edx) & mask;
	case 3: return getreg(ebx) & mask;
	case 4: return getreg(esp) & mask;
	case 5: return getreg(ebp) & mask;
	case 6: return getreg(esi) & mask;
	case 7: return getreg(edi) & mask;
	}
	abort();
}

/* Set register by instruction encoding */
static void setreg_num(unsigned regnum, u32 val, u32 mask)
{
	/* Don't try to set bits out of range */
	assert(~(val & ~mask));

	/* 8 bit ops use regnums 4-7 for high parts of word */
	if (mask == 0xFF && (regnum & 0x4)) {
		/* Construct the 16 bits we want. */
		val = (val << 8) | getreg_num(regnum & 0x3, 0xFF);
		setreg_num(regnum & 0x3, val, 0xFFFF);
		return;
	}

	switch (regnum) {
	case 0: setreg(eax, val | (getreg(eax) & ~mask)); return;
	case 1: setreg(ecx, val | (getreg(ecx) & ~mask)); return;
	case 2: setreg(edx, val | (getreg(edx) & ~mask)); return;
	case 3: setreg(ebx, val | (getreg(ebx) & ~mask)); return;
	case 4: setreg(esp, val | (getreg(esp) & ~mask)); return;
	case 5: setreg(ebp, val | (getreg(ebp) & ~mask)); return;
	case 6: setreg(esi, val | (getreg(esi) & ~mask)); return;
	case 7: setreg(edi, val | (getreg(edi) & ~mask)); return;
	}
	abort();
}

/* Get bytes of displacement appended to instruction, from r/m encoding */
static u32 insn_displacement_len(u8 mod_reg_rm)
{
	/* Switch on the mod bits */
	switch (mod_reg_rm >> 6) {
	case 0:
		/* If mod == 0, and r/m == 101, 16-bit displacement follows */
		if ((mod_reg_rm & 0x7) == 0x5)
			return 2;
		/* Normally, mod == 0 means no literal displacement */
		return 0;
	case 1:
		/* One byte displacement */
		return 1;
	case 2:
		/* Four byte displacement */
		return 4;
	case 3:
		/* Register mode */
		return 0;
	}
	abort();
}

static void emulate_insn(const u8 insn[])
{
	unsigned long args[] = { LHREQ_TRAP, 13 };
	unsigned int insnlen = 0, in = 0, small_operand = 0, byte_access;
	unsigned int eax, port, mask;
	/*
	 * Default is to return all-ones on IO port reads, which traditionally
	 * means "there's nothing there".
	 */
	u32 val = 0xFFFFFFFF;

	/*
	 * This must be the Guest kernel trying to do something, not userspace!
	 * The bottom two bits of the CS segment register are the privilege
	 * level.
	 */
	if ((getreg(xcs) & 3) != 0x1)
		goto no_emulate;

	/* Decoding x86 instructions is icky. */

	/*
	 * Around 2.6.33, the kernel started using an emulation for the
	 * cmpxchg8b instruction in early boot on many configurations.  This
	 * code isn't paravirtualized, and it tries to disable interrupts.
	 * Ignore it, which will Mostly Work.
	 */
	if (insn[insnlen] == 0xfa) {
		/* "cli", or Clear Interrupt Enable instruction.  Skip it. */
		insnlen = 1;
		goto skip_insn;
	}

	/*
	 * 0x66 is an "operand prefix".  It means a 16, not 32 bit in/out.
	 */
	if (insn[insnlen] == 0x66) {
		small_operand = 1;
		/* The instruction is 1 byte so far, read the next byte. */
		insnlen = 1;
	}

	/* If the lower bit isn't set, it's a single byte access */
	byte_access = !(insn[insnlen] & 1);

	/*
	 * Now we can ignore the lower bit and decode the 4 opcodes
	 * we need to emulate.
	 */
	switch (insn[insnlen] & 0xFE) {
	case 0xE4: /* in     <next byte>,%al */
		port = insn[insnlen+1];
		insnlen += 2;
		in = 1;
		break;
	case 0xEC: /* in     (%dx),%al */
		port = getreg(edx) & 0xFFFF;
		insnlen += 1;
		in = 1;
		break;
	case 0xE6: /* out    %al,<next byte> */
		port = insn[insnlen+1];
		insnlen += 2;
		break;
	case 0xEE: /* out    %al,(%dx) */
		port = getreg(edx) & 0xFFFF;
		insnlen += 1;
		break;
	default:
		/* OK, we don't know what this is, can't emulate. */
		goto no_emulate;
	}

	/* Set a mask of the 1, 2 or 4 bytes, depending on size of IO */
	if (byte_access)
		mask = 0xFF;
	else if (small_operand)
		mask = 0xFFFF;
	else
		mask = 0xFFFFFFFF;

	/*
	 * If it was an "IN" instruction, they expect the result to be read
	 * into %eax, so we change %eax.
	 */
	eax = getreg(eax);

	if (in) {
		/* This is the PS/2 keyboard status; 1 means ready for output */
		if (port == 0x64)
			val = 1;
		else if (is_pci_addr_port(port))
			pci_addr_ioread(port, mask, &val);
		else if (is_pci_data_port(port))
			pci_data_ioread(port, mask, &val);

		/* Clear the bits we're about to read */
		eax &= ~mask;
		/* Copy bits in from val. */
		eax |= val & mask;
		/* Now update the register. */
		setreg(eax, eax);
	} else {
		if (is_pci_addr_port(port)) {
			if (!pci_addr_iowrite(port, mask, eax))
				goto bad_io;
		} else if (is_pci_data_port(port)) {
			if (!pci_data_iowrite(port, mask, eax))
				goto bad_io;
		}
		/* There are many other ports, eg. CMOS clock, serial
		 * and parallel ports, so we ignore them all. */
	}

	verbose("IO %s of %x to %u: %#08x\n",
		in ? "IN" : "OUT", mask, port, eax);
skip_insn:
	/* Finally, we've "done" the instruction, so move past it. */
	setreg(eip, getreg(eip) + insnlen);
	return;

bad_io:
	warnx("Attempt to %s port %u (%#x mask)",
	      in ? "read from" : "write to", port, mask);

no_emulate:
	/* Inject trap into Guest. */
	if (write(lguest_fd, args, sizeof(args)) < 0)
		err(1, "Reinjecting trap 13 for fault at %#x", getreg(eip));
}

static struct device *find_mmio_region(unsigned long paddr, u32 *off)
{
	unsigned int i;

	for (i = 1; i < MAX_PCI_DEVICES; i++) {
		struct device *d = devices.pci[i];

		if (!d)
			continue;
		if (paddr < d->mmio_addr)
			continue;
		if (paddr >= d->mmio_addr + d->mmio_size)
			continue;
		*off = paddr - d->mmio_addr;
		return d;
	}
	return NULL;
}

/* FIXME: Use vq array. */
static struct virtqueue *vq_by_num(struct device *d, u32 num)
{
	struct virtqueue *vq = d->vq;

	while (num-- && vq)
		vq = vq->next;

	return vq;
}

static void save_vq_config(const struct virtio_pci_common_cfg *cfg,
			   struct virtqueue *vq)
{
	vq->pci_config = *cfg;
}

static void restore_vq_config(struct virtio_pci_common_cfg *cfg,
			      struct virtqueue *vq)
{
	/* Only restore the per-vq part */
	size_t off = offsetof(struct virtio_pci_common_cfg, queue_size);

	memcpy((void *)cfg + off, (void *)&vq->pci_config + off,
	       sizeof(*cfg) - off);
}

/*
 * 4.1.4.3.2:
 *
 *  The driver MUST configure the other virtqueue fields before
 *  enabling the virtqueue with queue_enable.
 *
 * When they enable the virtqueue, we check that their setup is valid.
 */
static void check_virtqueue(struct device *d, struct virtqueue *vq)
{
	/* Because lguest is 32 bit, all the descriptor high bits must be 0 */
	if (vq->pci_config.queue_desc_hi
	    || vq->pci_config.queue_avail_hi
	    || vq->pci_config.queue_used_hi)
		bad_driver_vq(vq, "invalid 64-bit queue address");

	/*
	 * 2.4.1:
	 *
	 *  The driver MUST ensure that the physical address of the first byte
	 *  of each virtqueue part is a multiple of the specified alignment
	 *  value in the above table.
	 */
	if (vq->pci_config.queue_desc_lo % 16
	    || vq->pci_config.queue_avail_lo % 2
	    || vq->pci_config.queue_used_lo % 4)
		bad_driver_vq(vq, "invalid alignment in queue addresses");

	/* Initialize the virtqueue and check they're all in range. */
	vq->vring.num = vq->pci_config.queue_size;
	vq->vring.desc = check_pointer(vq->dev,
				       vq->pci_config.queue_desc_lo,
				       sizeof(*vq->vring.desc) * vq->vring.num);
	vq->vring.avail = check_pointer(vq->dev,
					vq->pci_config.queue_avail_lo,
					sizeof(*vq->vring.avail)
					+ (sizeof(vq->vring.avail->ring[0])
					   * vq->vring.num));
	vq->vring.used = check_pointer(vq->dev,
				       vq->pci_config.queue_used_lo,
				       sizeof(*vq->vring.used)
				       + (sizeof(vq->vring.used->ring[0])
					  * vq->vring.num));

	/*
	 * 2.4.9.1:
	 *
	 *   The driver MUST initialize flags in the used ring to 0
	 *   when allocating the used ring.
	 */
	if (vq->vring.used->flags != 0)
		bad_driver_vq(vq, "invalid initial used.flags %#x",
			      vq->vring.used->flags);
}

static void start_virtqueue(struct virtqueue *vq)
{
	/*
	 * Create stack for thread.  Since the stack grows upwards, we point
	 * the stack pointer to the end of this region.
	 */
	char *stack = malloc(32768);

	/* Create a zero-initialized eventfd. */
	vq->eventfd = eventfd(0, 0);
	if (vq->eventfd < 0)
		err(1, "Creating eventfd");

	/*
	 * CLONE_VM: because it has to access the Guest memory, and SIGCHLD so
	 * we get a signal if it dies.
	 */
	vq->thread = clone(do_thread, stack + 32768, CLONE_VM | SIGCHLD, vq);
	if (vq->thread == (pid_t)-1)
		err(1, "Creating clone");
}

static void start_virtqueues(struct device *d)
{
	struct virtqueue *vq;

	for (vq = d->vq; vq; vq = vq->next) {
		if (vq->pci_config.queue_enable)
			start_virtqueue(vq);
	}
}

static void emulate_mmio_write(struct device *d, u32 off, u32 val, u32 mask)
{
	struct virtqueue *vq;

	switch (off) {
	case offsetof(struct virtio_pci_mmio, cfg.device_feature_select):
		/*
		 * 4.1.4.3.1:
		 *
		 * The device MUST present the feature bits it is offering in
		 * device_feature, starting at bit device_feature_select ∗ 32
		 * for any device_feature_select written by the driver
		 */
		if (val == 0)
			d->mmio->cfg.device_feature = d->features;
		else if (val == 1)
			d->mmio->cfg.device_feature = (d->features >> 32);
		else
			d->mmio->cfg.device_feature = 0;
		goto feature_write_through32;
	case offsetof(struct virtio_pci_mmio, cfg.guest_feature_select):
		if (val > 1)
			bad_driver(d, "Unexpected driver select %u", val);
		goto feature_write_through32;
	case offsetof(struct virtio_pci_mmio, cfg.guest_feature):
		if (d->mmio->cfg.guest_feature_select == 0) {
			d->features_accepted &= ~((u64)0xFFFFFFFF);
			d->features_accepted |= val;
		} else {
			assert(d->mmio->cfg.guest_feature_select == 1);
			d->features_accepted &= 0xFFFFFFFF;
			d->features_accepted |= ((u64)val) << 32;
		}
		/*
		 * 2.2.1:
		 *
		 *   The driver MUST NOT accept a feature which the device did
		 *   not offer
		 */
		if (d->features_accepted & ~d->features)
			bad_driver(d, "over-accepted features %#llx of %#llx",
				   d->features_accepted, d->features);
		goto feature_write_through32;
	case offsetof(struct virtio_pci_mmio, cfg.device_status): {
		u8 prev;

		verbose("%s: device status -> %#x\n", d->name, val);
		/*
		 * 4.1.4.3.1:
		 * 
		 *  The device MUST reset when 0 is written to device_status,
		 *  and present a 0 in device_status once that is done.
		 */
		if (val == 0) {
			reset_device(d);
			goto write_through8;
		}

		/* 2.1.1: The driver MUST NOT clear a device status bit. */
		if (d->mmio->cfg.device_status & ~val)
			bad_driver(d, "unset of device status bit %#x -> %#x",
				   d->mmio->cfg.device_status, val);

		/*
		 * 2.1.2:
		 *
		 *  The device MUST NOT consume buffers or notify the driver
		 *  before DRIVER_OK.
		 */
		if (val & VIRTIO_CONFIG_S_DRIVER_OK
		    && !(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER_OK))
			start_virtqueues(d);

		/*
		 * 3.1.1:
		 *
		 *   The driver MUST follow this sequence to initialize a device:
		 *   - Reset the device.
		 *   - Set the ACKNOWLEDGE status bit: the guest OS has
                 *     notice the device.
		 *   - Set the DRIVER status bit: the guest OS knows how
                 *     to drive the device.
		 *   - Read device feature bits, and write the subset
		 *     of feature bits understood by the OS and driver
		 *     to the device. During this step the driver MAY
		 *     read (but MUST NOT write) the device-specific
		 *     configuration fields to check that it can
		 *     support the device before accepting it.
		 *   - Set the FEATURES_OK status bit.  The driver
		 *     MUST not accept new feature bits after this
		 *     step.
		 *   - Re-read device status to ensure the FEATURES_OK
		 *     bit is still set: otherwise, the device does
		 *     not support our subset of features and the
		 *     device is unusable.
		 *   - Perform device-specific setup, including
		 *     discovery of virtqueues for the device,
		 *     optional per-bus setup, reading and possibly
		 *     writing the device’s virtio configuration
		 *     space, and population of virtqueues.
		 *   - Set the DRIVER_OK status bit. At this point the
                 *     device is “live”.
		 */
		prev = 0;
		switch (val & ~d->mmio->cfg.device_status) {
		case VIRTIO_CONFIG_S_DRIVER_OK:
			prev |= VIRTIO_CONFIG_S_FEATURES_OK; /* fall thru */
		case VIRTIO_CONFIG_S_FEATURES_OK:
			prev |= VIRTIO_CONFIG_S_DRIVER; /* fall thru */
		case VIRTIO_CONFIG_S_DRIVER:
			prev |= VIRTIO_CONFIG_S_ACKNOWLEDGE; /* fall thru */
		case VIRTIO_CONFIG_S_ACKNOWLEDGE:
			break;
		default:
			bad_driver(d, "unknown device status bit %#x -> %#x",
				   d->mmio->cfg.device_status, val);
		}
		if (d->mmio->cfg.device_status != prev)
			bad_driver(d, "unexpected status transition %#x -> %#x",
				   d->mmio->cfg.device_status, val);

		/* If they just wrote FEATURES_OK, we make sure they read */
		switch (val & ~d->mmio->cfg.device_status) {
		case VIRTIO_CONFIG_S_FEATURES_OK:
			d->wrote_features_ok = true;
			break;
		case VIRTIO_CONFIG_S_DRIVER_OK:
			if (d->wrote_features_ok)
				bad_driver(d, "did not re-read FEATURES_OK");
			break;
		}
		goto write_through8;
	}
	case offsetof(struct virtio_pci_mmio, cfg.queue_select):
		vq = vq_by_num(d, val);
		/*
		 * 4.1.4.3.1:
		 *
		 *  The device MUST present a 0 in queue_size if the virtqueue
		 *  corresponding to the current queue_select is unavailable.
		 */
		if (!vq) {
			d->mmio->cfg.queue_size = 0;
			goto write_through16;
		}
		/* Save registers for old vq, if it was a valid vq */
		if (d->mmio->cfg.queue_size)
			save_vq_config(&d->mmio->cfg,
				       vq_by_num(d, d->mmio->cfg.queue_select));
		/* Restore the registers for the queue they asked for */
		restore_vq_config(&d->mmio->cfg, vq);
		goto write_through16;
	case offsetof(struct virtio_pci_mmio, cfg.queue_size):
		/*
		 * 4.1.4.3.2:
		 *
		 *  The driver MUST NOT write a value which is not a power of 2
		 *  to queue_size.
		 */
		if (val & (val-1))
			bad_driver(d, "invalid queue size %u", val);
		if (d->mmio->cfg.queue_enable)
			bad_driver(d, "changing queue size on live device");
		goto write_through16;
	case offsetof(struct virtio_pci_mmio, cfg.queue_msix_vector):
		bad_driver(d, "attempt to set MSIX vector to %u", val);
	case offsetof(struct virtio_pci_mmio, cfg.queue_enable): {
		struct virtqueue *vq = vq_by_num(d, d->mmio->cfg.queue_select);

		/*
		 * 4.1.4.3.2:
		 *
		 *  The driver MUST NOT write a 0 to queue_enable.
		 */
		if (val != 1)
			bad_driver(d, "setting queue_enable to %u", val);

		/*
		 * 3.1.1:
		 *
		 *  7. Perform device-specific setup, including discovery of
		 *     virtqueues for the device, optional per-bus setup,
		 *     reading and possibly writing the device’s virtio
		 *     configuration space, and population of virtqueues.
		 *  8. Set the DRIVER_OK status bit.
		 *
		 * All our devices require all virtqueues to be enabled, so
		 * they should have done that before setting DRIVER_OK.
		 */
		if (d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER_OK)
			bad_driver(d, "enabling vq after DRIVER_OK");

		d->mmio->cfg.queue_enable = val;
		save_vq_config(&d->mmio->cfg, vq);
		check_virtqueue(d, vq);
		goto write_through16;
	}
	case offsetof(struct virtio_pci_mmio, cfg.queue_notify_off):
		bad_driver(d, "attempt to write to queue_notify_off");
	case offsetof(struct virtio_pci_mmio, cfg.queue_desc_lo):
	case offsetof(struct virtio_pci_mmio, cfg.queue_desc_hi):
	case offsetof(struct virtio_pci_mmio, cfg.queue_avail_lo):
	case offsetof(struct virtio_pci_mmio, cfg.queue_avail_hi):
	case offsetof(struct virtio_pci_mmio, cfg.queue_used_lo):
	case offsetof(struct virtio_pci_mmio, cfg.queue_used_hi):
		/*
		 * 4.1.4.3.2:
		 *
		 *  The driver MUST configure the other virtqueue fields before
		 *  enabling the virtqueue with queue_enable.
		 */
		if (d->mmio->cfg.queue_enable)
			bad_driver(d, "changing queue on live device");

		/*
		 * 3.1.1:
		 *
		 *  The driver MUST follow this sequence to initialize a device:
		 *...
		 *  5. Set the FEATURES_OK status bit. The driver MUST not
		 *  accept new feature bits after this step.
		 */
		if (!(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_FEATURES_OK))
			bad_driver(d, "setting up vq before FEATURES_OK");

		/*
		 *  6. Re-read device status to ensure the FEATURES_OK bit is
		 *     still set...
		 */
		if (d->wrote_features_ok)
			bad_driver(d, "didn't re-read FEATURES_OK before setup");

		goto write_through32;
	case offsetof(struct virtio_pci_mmio, notify):
		vq = vq_by_num(d, val);
		if (!vq)
			bad_driver(d, "Invalid vq notification on %u", val);
		/* Notify the process handling this vq by adding 1 to eventfd */
		write(vq->eventfd, "\1\0\0\0\0\0\0\0", 8);
		goto write_through16;
	case offsetof(struct virtio_pci_mmio, isr):
		bad_driver(d, "Unexpected write to isr");
	/* Weird corner case: write to emerg_wr of console */
	case sizeof(struct virtio_pci_mmio)
		+ offsetof(struct virtio_console_config, emerg_wr):
		if (strcmp(d->name, "console") == 0) {
			char c = val;
			write(STDOUT_FILENO, &c, 1);
			goto write_through32;
		}
		/* Fall through... */
	default:
		/*
		 * 4.1.4.3.2:
		 *
		 *   The driver MUST NOT write to device_feature, num_queues,
		 *   config_generation or queue_notify_off.
		 */
		bad_driver(d, "Unexpected write to offset %u", off);
	}

feature_write_through32:
	/*
	 * 3.1.1:
	 *
	 *   The driver MUST follow this sequence to initialize a device:
	 *...
	 *   - Set the DRIVER status bit: the guest OS knows how
	 *     to drive the device.
	 *   - Read device feature bits, and write the subset
	 *     of feature bits understood by the OS and driver
	 *     to the device.
	 *...
	 *   - Set the FEATURES_OK status bit. The driver MUST not
	 *     accept new feature bits after this step.
	 */
	if (!(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER))
		bad_driver(d, "feature write before VIRTIO_CONFIG_S_DRIVER");
	if (d->mmio->cfg.device_status & VIRTIO_CONFIG_S_FEATURES_OK)
		bad_driver(d, "feature write after VIRTIO_CONFIG_S_FEATURES_OK");

	/*
	 * 4.1.3.1:
	 *
	 *  The driver MUST access each field using the “natural” access
	 *  method, i.e. 32-bit accesses for 32-bit fields, 16-bit accesses for
	 *  16-bit fields and 8-bit accesses for 8-bit fields.
	 */
write_through32:
	if (mask != 0xFFFFFFFF) {
		bad_driver(d, "non-32-bit write to offset %u (%#x)",
			   off, getreg(eip));
		return;
	}
	memcpy((char *)d->mmio + off, &val, 4);
	return;

write_through16:
	if (mask != 0xFFFF)
		bad_driver(d, "non-16-bit write to offset %u (%#x)",
			   off, getreg(eip));
	memcpy((char *)d->mmio + off, &val, 2);
	return;

write_through8:
	if (mask != 0xFF)
		bad_driver(d, "non-8-bit write to offset %u (%#x)",
			   off, getreg(eip));
	memcpy((char *)d->mmio + off, &val, 1);
	return;
}

static u32 emulate_mmio_read(struct device *d, u32 off, u32 mask)
{
	u8 isr;
	u32 val = 0;

	switch (off) {
	case offsetof(struct virtio_pci_mmio, cfg.device_feature_select):
	case offsetof(struct virtio_pci_mmio, cfg.device_feature):
	case offsetof(struct virtio_pci_mmio, cfg.guest_feature_select):
	case offsetof(struct virtio_pci_mmio, cfg.guest_feature):
		/*
		 * 3.1.1:
		 *
		 *   The driver MUST follow this sequence to initialize a device:
		 *...
		 *   - Set the DRIVER status bit: the guest OS knows how
		 *     to drive the device.
		 *   - Read device feature bits, and write the subset
		 *     of feature bits understood by the OS and driver
		 *     to the device.
		 */
		if (!(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER))
			bad_driver(d,
				   "feature read before VIRTIO_CONFIG_S_DRIVER");
		goto read_through32;
	case offsetof(struct virtio_pci_mmio, cfg.msix_config):
		bad_driver(d, "read of msix_config");
	case offsetof(struct virtio_pci_mmio, cfg.num_queues):
		goto read_through16;
	case offsetof(struct virtio_pci_mmio, cfg.device_status):
		/* As they did read, any write of FEATURES_OK is now fine. */
		d->wrote_features_ok = false;
		goto read_through8;
	case offsetof(struct virtio_pci_mmio, cfg.config_generation):
		/*
		 * 4.1.4.3.1:
		 *
		 *  The device MUST present a changed config_generation after
		 *  the driver has read a device-specific configuration value
		 *  which has changed since any part of the device-specific
		 *  configuration was last read.
		 *
		 * This is simple: none of our devices change config, so this
		 * is always 0.
		 */
		goto read_through8;
	case offsetof(struct virtio_pci_mmio, notify):
		/*
		 * 3.1.1:
		 *
		 *   The driver MUST NOT notify the device before setting
		 *   DRIVER_OK.
		 */
		if (!(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER_OK))
			bad_driver(d, "notify before VIRTIO_CONFIG_S_DRIVER_OK");
		goto read_through16;
	case offsetof(struct virtio_pci_mmio, isr):
		if (mask != 0xFF)
			bad_driver(d, "non-8-bit read from offset %u (%#x)",
				   off, getreg(eip));
		isr = d->mmio->isr;
		/*
		 * 4.1.4.5.1:
		 *
		 *  The device MUST reset ISR status to 0 on driver read. 
		 */
		d->mmio->isr = 0;
		return isr;
	case offsetof(struct virtio_pci_mmio, padding):
		bad_driver(d, "read from padding (%#x)", getreg(eip));
	default:
		/* Read from device config space, beware unaligned overflow */
		if (off > d->mmio_size - 4)
			bad_driver(d, "read past end (%#x)", getreg(eip));

		/*
		 * 3.1.1:
		 *  The driver MUST follow this sequence to initialize a device:
		 *...
		 *  3. Set the DRIVER status bit: the guest OS knows how to
		 *  drive the device.
		 *  4. Read device feature bits, and write the subset of
		 *  feature bits understood by the OS and driver to the
		 *  device. During this step the driver MAY read (but MUST NOT
		 *  write) the device-specific configuration fields to check
		 *  that it can support the device before accepting it.
		 */
		if (!(d->mmio->cfg.device_status & VIRTIO_CONFIG_S_DRIVER))
			bad_driver(d,
				   "config read before VIRTIO_CONFIG_S_DRIVER");

		if (mask == 0xFFFFFFFF)
			goto read_through32;
		else if (mask == 0xFFFF)
			goto read_through16;
		else
			goto read_through8;
	}

	/*
	 * 4.1.3.1:
	 *
	 *  The driver MUST access each field using the “natural” access
	 *  method, i.e. 32-bit accesses for 32-bit fields, 16-bit accesses for
	 *  16-bit fields and 8-bit accesses for 8-bit fields.
	 */
read_through32:
	if (mask != 0xFFFFFFFF)
		bad_driver(d, "non-32-bit read to offset %u (%#x)",
			   off, getreg(eip));
	memcpy(&val, (char *)d->mmio + off, 4);
	return val;

read_through16:
	if (mask != 0xFFFF)
		bad_driver(d, "non-16-bit read to offset %u (%#x)",
			   off, getreg(eip));
	memcpy(&val, (char *)d->mmio + off, 2);
	return val;

read_through8:
	if (mask != 0xFF)
		bad_driver(d, "non-8-bit read to offset %u (%#x)",
			   off, getreg(eip));
	memcpy(&val, (char *)d->mmio + off, 1);
	return val;
}

static void emulate_mmio(unsigned long paddr, const u8 *insn)
{
	u32 val, off, mask = 0xFFFFFFFF, insnlen = 0;
	struct device *d = find_mmio_region(paddr, &off);
	unsigned long args[] = { LHREQ_TRAP, 14 };

	if (!d) {
		warnx("MMIO touching %#08lx (not a device)", paddr);
		goto reinject;
	}

	/* Prefix makes it a 16 bit op */
	if (insn[0] == 0x66) {
		mask = 0xFFFF;
		insnlen++;
	}

	/* iowrite */
	if (insn[insnlen] == 0x89) {
		/* Next byte is r/m byte: bits 3-5 are register. */
		val = getreg_num((insn[insnlen+1] >> 3) & 0x7, mask);
		emulate_mmio_write(d, off, val, mask);
		insnlen += 2 + insn_displacement_len(insn[insnlen+1]);
	} else if (insn[insnlen] == 0x8b) { /* ioread */
		/* Next byte is r/m byte: bits 3-5 are register. */
		val = emulate_mmio_read(d, off, mask);
		setreg_num((insn[insnlen+1] >> 3) & 0x7, val, mask);
		insnlen += 2 + insn_displacement_len(insn[insnlen+1]);
	} else if (insn[0] == 0x88) { /* 8-bit iowrite */
		mask = 0xff;
		/* Next byte is r/m byte: bits 3-5 are register. */
		val = getreg_num((insn[1] >> 3) & 0x7, mask);
		emulate_mmio_write(d, off, val, mask);
		insnlen = 2 + insn_displacement_len(insn[1]);
	} else if (insn[0] == 0x8a) { /* 8-bit ioread */
		mask = 0xff;
		val = emulate_mmio_read(d, off, mask);
		setreg_num((insn[1] >> 3) & 0x7, val, mask);
		insnlen = 2 + insn_displacement_len(insn[1]);
	} else {
		warnx("Unknown MMIO instruction touching %#08lx:"
		     " %02x %02x %02x %02x at %u",
		     paddr, insn[0], insn[1], insn[2], insn[3], getreg(eip));
	reinject:
		/* Inject trap into Guest. */
		if (write(lguest_fd, args, sizeof(args)) < 0)
			err(1, "Reinjecting trap 14 for fault at %#x",
			    getreg(eip));
		return;
	}

	/* Finally, we've "done" the instruction, so move past it. */
	setreg(eip, getreg(eip) + insnlen);
}

/*L:190
 * Device Setup
 *
 * All devices need a descriptor so the Guest knows it exists, and a "struct
 * device" so the Launcher can keep track of it.  We have common helper
 * routines to allocate and manage them.
 */
static void add_pci_virtqueue(struct device *dev,
			      void (*service)(struct virtqueue *),
			      const char *name)
{
	struct virtqueue **i, *vq = malloc(sizeof(*vq));

	/* Initialize the virtqueue */
	vq->next = NULL;
	vq->last_avail_idx = 0;
	vq->dev = dev;
	vq->name = name;

	/*
	 * This is the routine the service thread will run, and its Process ID
	 * once it's running.
	 */
	vq->service = service;
	vq->thread = (pid_t)-1;

	/* Initialize the configuration. */
	reset_vq_pci_config(vq);
	vq->pci_config.queue_notify_off = 0;

	/* Add one to the number of queues */
	vq->dev->mmio->cfg.num_queues++;

	/*
	 * Add to tail of list, so dev->vq is first vq, dev->vq->next is
	 * second.
	 */
	for (i = &dev->vq; *i; i = &(*i)->next);
	*i = vq;
}

/* The Guest accesses the feature bits via the PCI common config MMIO region */
static void add_pci_feature(struct device *dev, unsigned bit)
{
	dev->features |= (1ULL << bit);
}

/* For devices with no config. */
static void no_device_config(struct device *dev)
{
	dev->mmio_addr = get_mmio_region(dev->mmio_size);

	dev->config.bar[0] = dev->mmio_addr;
	/* Bottom 4 bits must be zero */
	assert(~(dev->config.bar[0] & 0xF));
}

/* This puts the device config into BAR0 */
static void set_device_config(struct device *dev, const void *conf, size_t len)
{
	/* Set up BAR 0 */
	dev->mmio_size += len;
	dev->mmio = realloc(dev->mmio, dev->mmio_size);
	memcpy(dev->mmio + 1, conf, len);

	/*
	 * 4.1.4.6:
	 *
	 *  The device MUST present at least one VIRTIO_PCI_CAP_DEVICE_CFG
	 *  capability for any device type which has a device-specific
	 *  configuration.
	 */
	/* Hook up device cfg */
	dev->config.cfg_access.cap.cap_next
		= offsetof(struct pci_config, device);

	/*
	 * 4.1.4.6.1:
	 *
	 *  The offset for the device-specific configuration MUST be 4-byte
	 *  aligned.
	 */
	assert(dev->config.cfg_access.cap.cap_next % 4 == 0);

	/* Fix up device cfg field length. */
	dev->config.device.length = len;

	/* The rest is the same as the no-config case */
	no_device_config(dev);
}

static void init_cap(struct virtio_pci_cap *cap, size_t caplen, int type,
		     size_t bar_offset, size_t bar_bytes, u8 next)
{
	cap->cap_vndr = PCI_CAP_ID_VNDR;
	cap->cap_next = next;
	cap->cap_len = caplen;
	cap->cfg_type = type;
	cap->bar = 0;
	memset(cap->padding, 0, sizeof(cap->padding));
	cap->offset = bar_offset;
	cap->length = bar_bytes;
}

/*
 * This sets up the pci_config structure, as defined in the virtio 1.0
 * standard (and PCI standard).
 */
static void init_pci_config(struct pci_config *pci, u16 type,
			    u8 class, u8 subclass)
{
	size_t bar_offset, bar_len;

	/*
	 * 4.1.4.4.1:
	 *
	 *  The device MUST either present notify_off_multiplier as an even
	 *  power of 2, or present notify_off_multiplier as 0.
	 *
	 * 2.1.2:
	 *
	 *   The device MUST initialize device status to 0 upon reset. 
	 */
	memset(pci, 0, sizeof(*pci));

	/* 4.1.2.1: Devices MUST have the PCI Vendor ID 0x1AF4 */
	pci->vendor_id = 0x1AF4;
	/* 4.1.2.1: ... PCI Device ID calculated by adding 0x1040 ... */
	pci->device_id = 0x1040 + type;

	/*
	 * PCI have specific codes for different types of devices.
	 * Linux doesn't care, but it's a good clue for people looking
	 * at the device.
	 */
	pci->class = class;
	pci->subclass = subclass;

	/*
	 * 4.1.2.1:
	 *
	 *  Non-transitional devices SHOULD have a PCI Revision ID of 1 or
	 *  higher
	 */
	pci->revid = 1;

	/*
	 * 4.1.2.1:
	 *
	 *  Non-transitional devices SHOULD have a PCI Subsystem Device ID of
	 *  0x40 or higher.
	 */
	pci->subsystem_device_id = 0x40;

	/* We use our dummy interrupt controller, and irq_line is the irq */
	pci->irq_line = devices.next_irq++;
	pci->irq_pin = 0;

	/* Support for extended capabilities. */
	pci->status = (1 << 4);

	/* Link them in. */
	/*
	 * 4.1.4.3.1:
	 *
	 *  The device MUST present at least one common configuration
	 *  capability.
	 */
	pci->capabilities = offsetof(struct pci_config, common);

	/* 4.1.4.3.1 ... offset MUST be 4-byte aligned. */
	assert(pci->capabilities % 4 == 0);

	bar_offset = offsetof(struct virtio_pci_mmio, cfg);
	bar_len = sizeof(((struct virtio_pci_mmio *)0)->cfg);
	init_cap(&pci->common, sizeof(pci->common), VIRTIO_PCI_CAP_COMMON_CFG,
		 bar_offset, bar_len,
		 offsetof(struct pci_config, notify));

	/*
	 * 4.1.4.4.1:
	 *
	 *  The device MUST present at least one notification capability.
	 */
	bar_offset += bar_len;
	bar_len = sizeof(((struct virtio_pci_mmio *)0)->notify);

	/*
	 * 4.1.4.4.1:
	 *
	 *  The cap.offset MUST be 2-byte aligned.
	 */
	assert(pci->common.cap_next % 2 == 0);

	/* FIXME: Use a non-zero notify_off, for per-queue notification? */
	/*
	 * 4.1.4.4.1:
	 *
	 *  The value cap.length presented by the device MUST be at least 2 and
	 *  MUST be large enough to support queue notification offsets for all
	 *  supported queues in all possible configurations.
	 */
	assert(bar_len >= 2);

	init_cap(&pci->notify.cap, sizeof(pci->notify),
		 VIRTIO_PCI_CAP_NOTIFY_CFG,
		 bar_offset, bar_len,
		 offsetof(struct pci_config, isr));

	bar_offset += bar_len;
	bar_len = sizeof(((struct virtio_pci_mmio *)0)->isr);
	/*
	 * 4.1.4.5.1:
	 *
	 *  The device MUST present at least one VIRTIO_PCI_CAP_ISR_CFG
	 *  capability.
	 */
	init_cap(&pci->isr, sizeof(pci->isr),
		 VIRTIO_PCI_CAP_ISR_CFG,
		 bar_offset, bar_len,
		 offsetof(struct pci_config, cfg_access));

	/*
	 * 4.1.4.7.1:
	 *
	 * The device MUST present at least one VIRTIO_PCI_CAP_PCI_CFG
	 * capability.
	 */
	/* This doesn't have any presence in the BAR */
	init_cap(&pci->cfg_access.cap, sizeof(pci->cfg_access),
		 VIRTIO_PCI_CAP_PCI_CFG,
		 0, 0, 0);

	bar_offset += bar_len + sizeof(((struct virtio_pci_mmio *)0)->padding);
	assert(bar_offset == sizeof(struct virtio_pci_mmio));

	/*
	 * This gets sewn in and length set in set_device_config().
	 * Some devices don't have a device configuration interface, so
	 * we never expose this if we don't call set_device_config().
	 */
	init_cap(&pci->device, sizeof(pci->device), VIRTIO_PCI_CAP_DEVICE_CFG,
		 bar_offset, 0, 0);
}

/*
 * This routine does all the creation and setup of a new device, but we don't
 * actually place the MMIO region until we know the size (if any) of the
 * device-specific config.  And we don't actually start the service threads
 * until later.
 *
 * See what I mean about userspace being boring?
 */
static struct device *new_pci_device(const char *name, u16 type,
				     u8 class, u8 subclass)
{
	struct device *dev = malloc(sizeof(*dev));

	/* Now we populate the fields one at a time. */
	dev->name = name;
	dev->vq = NULL;
	dev->running = false;
	dev->wrote_features_ok = false;
	dev->mmio_size = sizeof(struct virtio_pci_mmio);
	dev->mmio = calloc(1, dev->mmio_size);
	dev->features = (u64)1 << VIRTIO_F_VERSION_1;
	dev->features_accepted = 0;

	if (devices.device_num + 1 >= MAX_PCI_DEVICES)
		errx(1, "Can only handle 31 PCI devices");

	init_pci_config(&dev->config, type, class, subclass);
	assert(!devices.pci[devices.device_num+1]);
	devices.pci[++devices.device_num] = dev;

	return dev;
}

/*
 * Our first setup routine is the console.  It's a fairly simple device, but
 * UNIX tty handling makes it uglier than it could be.
 */
static void setup_console(void)
{
	struct device *dev;
	struct virtio_console_config conf;

	/* If we can save the initial standard input settings... */
	if (tcgetattr(STDIN_FILENO, &orig_term) == 0) {
		struct termios term = orig_term;
		/*
		 * Then we turn off echo, line buffering and ^C etc: We want a
		 * raw input stream to the Guest.
		 */
		term.c_lflag &= ~(ISIG|ICANON|ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &term);
	}

	dev = new_pci_device("console", VIRTIO_ID_CONSOLE, 0x07, 0x00);

	/* We store the console state in dev->priv, and initialize it. */
	dev->priv = malloc(sizeof(struct console_abort));
	((struct console_abort *)dev->priv)->count = 0;

	/*
	 * The console needs two virtqueues: the input then the output.  When
	 * they put something the input queue, we make sure we're listening to
	 * stdin.  When they put something in the output queue, we write it to
	 * stdout.
	 */
	add_pci_virtqueue(dev, console_input, "input");
	add_pci_virtqueue(dev, console_output, "output");

	/* We need a configuration area for the emerg_wr early writes. */
	add_pci_feature(dev, VIRTIO_CONSOLE_F_EMERG_WRITE);
	set_device_config(dev, &conf, sizeof(conf));

	verbose("device %u: console\n", devices.device_num);
}
/*:*/

/*M:010
 * Inter-guest networking is an interesting area.  Simplest is to have a
 * --sharenet=<name> option which opens or creates a named pipe.  This can be
 * used to send packets to another guest in a 1:1 manner.
 *
 * More sophisticated is to use one of the tools developed for project like UML
 * to do networking.
 *
 * Faster is to do virtio bonding in kernel.  Doing this 1:1 would be
 * completely generic ("here's my vring, attach to your vring") and would work
 * for any traffic.  Of course, namespace and permissions issues need to be
 * dealt with.  A more sophisticated "multi-channel" virtio_net.c could hide
 * multiple inter-guest channels behind one interface, although it would
 * require some manner of hotplugging new virtio channels.
 *
 * Finally, we could use a virtio network switch in the kernel, ie. vhost.
:*/

static u32 str2ip(const char *ipaddr)
{
	unsigned int b[4];

	if (sscanf(ipaddr, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) != 4)
		errx(1, "Failed to parse IP address '%s'", ipaddr);
	return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static void str2mac(const char *macaddr, unsigned char mac[6])
{
	unsigned int m[6];
	if (sscanf(macaddr, "%02x:%02x:%02x:%02x:%02x:%02x",
		   &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
		errx(1, "Failed to parse mac address '%s'", macaddr);
	mac[0] = m[0];
	mac[1] = m[1];
	mac[2] = m[2];
	mac[3] = m[3];
	mac[4] = m[4];
	mac[5] = m[5];
}

/*
 * This code is "adapted" from libbridge: it attaches the Host end of the
 * network device to the bridge device specified by the command line.
 *
 * This is yet another James Morris contribution (I'm an IP-level guy, so I
 * dislike bridging), and I just try not to break it.
 */
static void add_to_bridge(int fd, const char *if_name, const char *br_name)
{
	int ifidx;
	struct ifreq ifr;

	if (!*br_name)
		errx(1, "must specify bridge name");

	ifidx = if_nametoindex(if_name);
	if (!ifidx)
		errx(1, "interface %s does not exist!", if_name);

	strncpy(ifr.ifr_name, br_name, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ-1] = '\0';
	ifr.ifr_ifindex = ifidx;
	if (ioctl(fd, SIOCBRADDIF, &ifr) < 0)
		err(1, "can't add %s to bridge %s", if_name, br_name);
}

/*
 * This sets up the Host end of the network device with an IP address, brings
 * it up so packets will flow, the copies the MAC address into the hwaddr
 * pointer.
 */
static void configure_device(int fd, const char *tapif, u32 ipaddr)
{
	struct ifreq ifr;
	struct sockaddr_in sin;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, tapif);

	/* Don't read these incantations.  Just cut & paste them like I did! */
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(ipaddr);
	memcpy(&ifr.ifr_addr, &sin, sizeof(sin));
	if (ioctl(fd, SIOCSIFADDR, &ifr) != 0)
		err(1, "Setting %s interface address", tapif);
	ifr.ifr_flags = IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		err(1, "Bringing interface %s up", tapif);
}

static int get_tun_device(char tapif[IFNAMSIZ])
{
	struct ifreq ifr;
	int vnet_hdr_sz;
	int netfd;

	/* Start with this zeroed.  Messy but sure. */
	memset(&ifr, 0, sizeof(ifr));

	/*
	 * We open the /dev/net/tun device and tell it we want a tap device.  A
	 * tap device is like a tun device, only somehow different.  To tell
	 * the truth, I completely blundered my way through this code, but it
	 * works now!
	 */
	netfd = open_or_die("/dev/net/tun", O_RDWR);
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
	strcpy(ifr.ifr_name, "tap%d");
	if (ioctl(netfd, TUNSETIFF, &ifr) != 0)
		err(1, "configuring /dev/net/tun");

	if (ioctl(netfd, TUNSETOFFLOAD,
		  TUN_F_CSUM|TUN_F_TSO4|TUN_F_TSO6|TUN_F_TSO_ECN) != 0)
		err(1, "Could not set features for tun device");

	/*
	 * We don't need checksums calculated for packets coming in this
	 * device: trust us!
	 */
	ioctl(netfd, TUNSETNOCSUM, 1);

	/*
	 * In virtio before 1.0 (aka legacy virtio), we added a 16-bit
	 * field at the end of the network header iff
	 * VIRTIO_NET_F_MRG_RXBUF was negotiated.  For virtio 1.0,
	 * that became the norm, but we need to tell the tun device
	 * about our expanded header (which is called
	 * virtio_net_hdr_mrg_rxbuf in the legacy system).
	 */
	vnet_hdr_sz = sizeof(struct virtio_net_hdr_v1);
	if (ioctl(netfd, TUNSETVNETHDRSZ, &vnet_hdr_sz) != 0)
		err(1, "Setting tun header size to %u", vnet_hdr_sz);

	memcpy(tapif, ifr.ifr_name, IFNAMSIZ);
	return netfd;
}

/*L:195
 * Our network is a Host<->Guest network.  This can either use bridging or
 * routing, but the principle is the same: it uses the "tun" device to inject
 * packets into the Host as if they came in from a normal network card.  We
 * just shunt packets between the Guest and the tun device.
 */
static void setup_tun_net(char *arg)
{
	struct device *dev;
	struct net_info *net_info = malloc(sizeof(*net_info));
	int ipfd;
	u32 ip = INADDR_ANY;
	bool bridging = false;
	char tapif[IFNAMSIZ], *p;
	struct virtio_net_config conf;

	net_info->tunfd = get_tun_device(tapif);

	/* First we create a new network device. */
	dev = new_pci_device("net", VIRTIO_ID_NET, 0x02, 0x00);
	dev->priv = net_info;

	/* Network devices need a recv and a send queue, just like console. */
	add_pci_virtqueue(dev, net_input, "rx");
	add_pci_virtqueue(dev, net_output, "tx");

	/*
	 * We need a socket to perform the magic network ioctls to bring up the
	 * tap interface, connect to the bridge etc.  Any socket will do!
	 */
	ipfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (ipfd < 0)
		err(1, "opening IP socket");

	/* If the command line was --tunnet=bridge:<name> do bridging. */
	if (!strncmp(BRIDGE_PFX, arg, strlen(BRIDGE_PFX))) {
		arg += strlen(BRIDGE_PFX);
		bridging = true;
	}

	/* A mac address may follow the bridge name or IP address */
	p = strchr(arg, ':');
	if (p) {
		str2mac(p+1, conf.mac);
		add_pci_feature(dev, VIRTIO_NET_F_MAC);
		*p = '\0';
	}

	/* arg is now either an IP address or a bridge name */
	if (bridging)
		add_to_bridge(ipfd, tapif, arg);
	else
		ip = str2ip(arg);

	/* Set up the tun device. */
	configure_device(ipfd, tapif, ip);

	/* Expect Guest to handle everything except UFO */
	add_pci_feature(dev, VIRTIO_NET_F_CSUM);
	add_pci_feature(dev, VIRTIO_NET_F_GUEST_CSUM);
	add_pci_feature(dev, VIRTIO_NET_F_GUEST_TSO4);
	add_pci_feature(dev, VIRTIO_NET_F_GUEST_TSO6);
	add_pci_feature(dev, VIRTIO_NET_F_GUEST_ECN);
	add_pci_feature(dev, VIRTIO_NET_F_HOST_TSO4);
	add_pci_feature(dev, VIRTIO_NET_F_HOST_TSO6);
	add_pci_feature(dev, VIRTIO_NET_F_HOST_ECN);
	/* We handle indirect ring entries */
	add_pci_feature(dev, VIRTIO_RING_F_INDIRECT_DESC);
	set_device_config(dev, &conf, sizeof(conf));

	/* We don't need the socket any more; setup is done. */
	close(ipfd);

	if (bridging)
		verbose("device %u: tun %s attached to bridge: %s\n",
			devices.device_num, tapif, arg);
	else
		verbose("device %u: tun %s: %s\n",
			devices.device_num, tapif, arg);
}
/*:*/

/* This hangs off device->priv. */
struct vblk_info {
	/* The size of the file. */
	off64_t len;

	/* The file descriptor for the file. */
	int fd;

};

/*L:210
 * The Disk
 *
 * The disk only has one virtqueue, so it only has one thread.  It is really
 * simple: the Guest asks for a block number and we read or write that position
 * in the file.
 *
 * Before we serviced each virtqueue in a separate thread, that was unacceptably
 * slow: the Guest waits until the read is finished before running anything
 * else, even if it could have been doing useful work.
 *
 * We could have used async I/O, except it's reputed to suck so hard that
 * characters actually go missing from your code when you try to use it.
 */
static void blk_request(struct virtqueue *vq)
{
	struct vblk_info *vblk = vq->dev->priv;
	unsigned int head, out_num, in_num, wlen;
	int ret, i;
	u8 *in;
	struct virtio_blk_outhdr out;
	struct iovec iov[vq->vring.num];
	off64_t off;

	/*
	 * Get the next request, where we normally wait.  It triggers the
	 * interrupt to acknowledge previously serviced requests (if any).
	 */
	head = wait_for_vq_desc(vq, iov, &out_num, &in_num);

	/* Copy the output header from the front of the iov (adjusts iov) */
	iov_consume(vq->dev, iov, out_num, &out, sizeof(out));

	/* Find and trim end of iov input array, for our status byte. */
	in = NULL;
	for (i = out_num + in_num - 1; i >= out_num; i--) {
		if (iov[i].iov_len > 0) {
			in = iov[i].iov_base + iov[i].iov_len - 1;
			iov[i].iov_len--;
			break;
		}
	}
	if (!in)
		bad_driver_vq(vq, "Bad virtblk cmd with no room for status");

	/*
	 * For historical reasons, block operations are expressed in 512 byte
	 * "sectors".
	 */
	off = out.sector * 512;

	if (out.type & VIRTIO_BLK_T_OUT) {
		/*
		 * Write
		 *
		 * Move to the right location in the block file.  This can fail
		 * if they try to write past end.
		 */
		if (lseek64(vblk->fd, off, SEEK_SET) != off)
			err(1, "Bad seek to sector %llu", out.sector);

		ret = writev(vblk->fd, iov, out_num);
		verbose("WRITE to sector %llu: %i\n", out.sector, ret);

		/*
		 * Grr... Now we know how long the descriptor they sent was, we
		 * make sure they didn't try to write over the end of the block
		 * file (possibly extending it).
		 */
		if (ret > 0 && off + ret > vblk->len) {
			/* Trim it back to the correct length */
			ftruncate64(vblk->fd, vblk->len);
			/* Die, bad Guest, die. */
			bad_driver_vq(vq, "Write past end %llu+%u", off, ret);
		}

		wlen = sizeof(*in);
		*in = (ret >= 0 ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR);
	} else if (out.type & VIRTIO_BLK_T_FLUSH) {
		/* Flush */
		ret = fdatasync(vblk->fd);
		verbose("FLUSH fdatasync: %i\n", ret);
		wlen = sizeof(*in);
		*in = (ret >= 0 ? VIRTIO_BLK_S_OK : VIRTIO_BLK_S_IOERR);
	} else {
		/*
		 * Read
		 *
		 * Move to the right location in the block file.  This can fail
		 * if they try to read past end.
		 */
		if (lseek64(vblk->fd, off, SEEK_SET) != off)
			err(1, "Bad seek to sector %llu", out.sector);

		ret = readv(vblk->fd, iov + out_num, in_num);
		if (ret >= 0) {
			wlen = sizeof(*in) + ret;
			*in = VIRTIO_BLK_S_OK;
		} else {
			wlen = sizeof(*in);
			*in = VIRTIO_BLK_S_IOERR;
		}
	}

	/* Finished that request. */
	add_used(vq, head, wlen);
}

/*L:198 This actually sets up a virtual block device. */
static void setup_block_file(const char *filename)
{
	struct device *dev;
	struct vblk_info *vblk;
	struct virtio_blk_config conf;

	/* Create the device. */
	dev = new_pci_device("block", VIRTIO_ID_BLOCK, 0x01, 0x80);

	/* The device has one virtqueue, where the Guest places requests. */
	add_pci_virtqueue(dev, blk_request, "request");

	/* Allocate the room for our own bookkeeping */
	vblk = dev->priv = malloc(sizeof(*vblk));

	/* First we open the file and store the length. */
	vblk->fd = open_or_die(filename, O_RDWR|O_LARGEFILE);
	vblk->len = lseek64(vblk->fd, 0, SEEK_END);

	/* Tell Guest how many sectors this device has. */
	conf.capacity = cpu_to_le64(vblk->len / 512);

	/*
	 * Tell Guest not to put in too many descriptors at once: two are used
	 * for the in and out elements.
	 */
	add_pci_feature(dev, VIRTIO_BLK_F_SEG_MAX);
	conf.seg_max = cpu_to_le32(VIRTQUEUE_NUM - 2);

	set_device_config(dev, &conf, sizeof(struct virtio_blk_config));

	verbose("device %u: virtblock %llu sectors\n",
		devices.device_num, le64_to_cpu(conf.capacity));
}

/*L:211
 * Our random number generator device reads from /dev/urandom into the Guest's
 * input buffers.  The usual case is that the Guest doesn't want random numbers
 * and so has no buffers although /dev/urandom is still readable, whereas
 * console is the reverse.
 *
 * The same logic applies, however.
 */
struct rng_info {
	int rfd;
};

static void rng_input(struct virtqueue *vq)
{
	int len;
	unsigned int head, in_num, out_num, totlen = 0;
	struct rng_info *rng_info = vq->dev->priv;
	struct iovec iov[vq->vring.num];

	/* First we need a buffer from the Guests's virtqueue. */
	head = wait_for_vq_desc(vq, iov, &out_num, &in_num);
	if (out_num)
		bad_driver_vq(vq, "Output buffers in rng?");

	/*
	 * Just like the console write, we loop to cover the whole iovec.
	 * In this case, short reads actually happen quite a bit.
	 */
	while (!iov_empty(iov, in_num)) {
		len = readv(rng_info->rfd, iov, in_num);
		if (len <= 0)
			err(1, "Read from /dev/urandom gave %i", len);
		iov_consume(vq->dev, iov, in_num, NULL, len);
		totlen += len;
	}

	/* Tell the Guest about the new input. */
	add_used(vq, head, totlen);
}

/*L:199
 * This creates a "hardware" random number device for the Guest.
 */
static void setup_rng(void)
{
	struct device *dev;
	struct rng_info *rng_info = malloc(sizeof(*rng_info));

	/* Our device's private info simply contains the /dev/urandom fd. */
	rng_info->rfd = open_or_die("/dev/urandom", O_RDONLY);

	/* Create the new device. */
	dev = new_pci_device("rng", VIRTIO_ID_RNG, 0xff, 0);
	dev->priv = rng_info;

	/* The device has one virtqueue, where the Guest places inbufs. */
	add_pci_virtqueue(dev, rng_input, "input");

	/* We don't have any configuration space */
	no_device_config(dev);

	verbose("device %u: rng\n", devices.device_num);
}
/* That's the end of device setup. */

/*L:230 Reboot is pretty easy: clean up and exec() the Launcher afresh. */
static void __attribute__((noreturn)) restart_guest(void)
{
	unsigned int i;

	/*
	 * Since we don't track all open fds, we simply close everything beyond
	 * stderr.
	 */
	for (i = 3; i < FD_SETSIZE; i++)
		close(i);

	/* Reset all the devices (kills all threads). */
	cleanup_devices();

	execv(main_args[0], main_args);
	err(1, "Could not exec %s", main_args[0]);
}

/*L:220
 * Finally we reach the core of the Launcher which runs the Guest, serves
 * its input and output, and finally, lays it to rest.
 */
static void __attribute__((noreturn)) run_guest(void)
{
	for (;;) {
		struct lguest_pending notify;
		int readval;

		/* We read from the /dev/lguest device to run the Guest. */
		readval = pread(lguest_fd, &notify, sizeof(notify), cpu_id);
		if (readval == sizeof(notify)) {
			if (notify.trap == 13) {
				verbose("Emulating instruction at %#x\n",
					getreg(eip));
				emulate_insn(notify.insn);
			} else if (notify.trap == 14) {
				verbose("Emulating MMIO at %#x\n",
					getreg(eip));
				emulate_mmio(notify.addr, notify.insn);
			} else
				errx(1, "Unknown trap %i addr %#08x\n",
				     notify.trap, notify.addr);
		/* ENOENT means the Guest died.  Reading tells us why. */
		} else if (errno == ENOENT) {
			char reason[1024] = { 0 };
			pread(lguest_fd, reason, sizeof(reason)-1, cpu_id);
			errx(1, "%s", reason);
		/* ERESTART means that we need to reboot the guest */
		} else if (errno == ERESTART) {
			restart_guest();
		/* Anything else means a bug or incompatible change. */
		} else
			err(1, "Running guest failed");
	}
}
/*L:240
 * This is the end of the Launcher.  The good news: we are over halfway
 * through!  The bad news: the most fiendish part of the code still lies ahead
 * of us.
 *
 * Are you ready?  Take a deep breath and join me in the core of the Host, in
 * "make Host".
:*/

static struct option opts[] = {
	{ "verbose", 0, NULL, 'v' },
	{ "tunnet", 1, NULL, 't' },
	{ "block", 1, NULL, 'b' },
	{ "rng", 0, NULL, 'r' },
	{ "initrd", 1, NULL, 'i' },
	{ "username", 1, NULL, 'u' },
	{ "chroot", 1, NULL, 'c' },
	{ NULL },
};
static void usage(void)
{
	errx(1, "Usage: lguest [--verbose] "
	     "[--tunnet=(<ipaddr>:<macaddr>|bridge:<bridgename>:<macaddr>)\n"
	     "|--block=<filename>|--initrd=<filename>]...\n"
	     "<mem-in-mb> vmlinux [args...]");
}

/*L:105 The main routine is where the real work begins: */
int main(int argc, char *argv[])
{
	/* Memory, code startpoint and size of the (optional) initrd. */
	unsigned long mem = 0, start, initrd_size = 0;
	/* Two temporaries. */
	int i, c;
	/* The boot information for the Guest. */
	struct boot_params *boot;
	/* If they specify an initrd file to load. */
	const char *initrd_name = NULL;

	/* Password structure for initgroups/setres[gu]id */
	struct passwd *user_details = NULL;

	/* Directory to chroot to */
	char *chroot_path = NULL;

	/* Save the args: we "reboot" by execing ourselves again. */
	main_args = argv;

	/*
	 * First we initialize the device list.  We remember next interrupt
	 * number to use for devices (1: remember that 0 is used by the timer).
	 */
	devices.next_irq = 1;

	/* We're CPU 0.  In fact, that's the only CPU possible right now. */
	cpu_id = 0;

	/*
	 * We need to know how much memory so we can set up the device
	 * descriptor and memory pages for the devices as we parse the command
	 * line.  So we quickly look through the arguments to find the amount
	 * of memory now.
	 */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			mem = atoi(argv[i]) * 1024 * 1024;
			/*
			 * We start by mapping anonymous pages over all of
			 * guest-physical memory range.  This fills it with 0,
			 * and ensures that the Guest won't be killed when it
			 * tries to access it.
			 */
			guest_base = map_zeroed_pages(mem / getpagesize()
						      + DEVICE_PAGES);
			guest_limit = mem;
			guest_max = guest_mmio = mem + DEVICE_PAGES*getpagesize();
			break;
		}
	}

	/* If we exit via err(), this kills all the threads, restores tty. */
	atexit(cleanup_devices);

	/* We always have a console device, and it's always device 1. */
	setup_console();

	/* The options are fairly straight-forward */
	while ((c = getopt_long(argc, argv, "v", opts, NULL)) != EOF) {
		switch (c) {
		case 'v':
			verbose = true;
			break;
		case 't':
			setup_tun_net(optarg);
			break;
		case 'b':
			setup_block_file(optarg);
			break;
		case 'r':
			setup_rng();
			break;
		case 'i':
			initrd_name = optarg;
			break;
		case 'u':
			user_details = getpwnam(optarg);
			if (!user_details)
				err(1, "getpwnam failed, incorrect username?");
			break;
		case 'c':
			chroot_path = optarg;
			break;
		default:
			warnx("Unknown argument %s", argv[optind]);
			usage();
		}
	}
	/*
	 * After the other arguments we expect memory and kernel image name,
	 * followed by command line arguments for the kernel.
	 */
	if (optind + 2 > argc)
		usage();

	verbose("Guest base is at %p\n", guest_base);

	/* Initialize the (fake) PCI host bridge device. */
	init_pci_host_bridge();

	/* Now we load the kernel */
	start = load_kernel(open_or_die(argv[optind+1], O_RDONLY));

	/* Boot information is stashed at physical address 0 */
	boot = from_guest_phys(0);

	/* Map the initrd image if requested (at top of physical memory) */
	if (initrd_name) {
		initrd_size = load_initrd(initrd_name, mem);
		/*
		 * These are the location in the Linux boot header where the
		 * start and size of the initrd are expected to be found.
		 */
		boot->hdr.ramdisk_image = mem - initrd_size;
		boot->hdr.ramdisk_size = initrd_size;
		/* The bootloader type 0xFF means "unknown"; that's OK. */
		boot->hdr.type_of_loader = 0xFF;
	}

	/*
	 * The Linux boot header contains an "E820" memory map: ours is a
	 * simple, single region.
	 */
	boot->e820_entries = 1;
	boot->e820_map[0] = ((struct e820entry) { 0, mem, E820_RAM });
	/*
	 * The boot header contains a command line pointer: we put the command
	 * line after the boot header.
	 */
	boot->hdr.cmd_line_ptr = to_guest_phys(boot + 1);
	/* We use a simple helper to copy the arguments separated by spaces. */
	concat((char *)(boot + 1), argv+optind+2);

	/* Set kernel alignment to 16M (CONFIG_PHYSICAL_ALIGN) */
	boot->hdr.kernel_alignment = 0x1000000;

	/* Boot protocol version: 2.07 supports the fields for lguest. */
	boot->hdr.version = 0x207;

	/* X86_SUBARCH_LGUEST tells the Guest it's an lguest. */
	boot->hdr.hardware_subarch = X86_SUBARCH_LGUEST;

	/* Tell the entry path not to try to reload segment registers. */
	boot->hdr.loadflags |= KEEP_SEGMENTS;

	/* We don't support tboot: */
	boot->tboot_addr = 0;

	/* Ensure this is 0 to prevent APM from loading: */
	boot->apm_bios_info.version = 0;

	/* We tell the kernel to initialize the Guest. */
	tell_kernel(start);

	/* Ensure that we terminate if a device-servicing child dies. */
	signal(SIGCHLD, kill_launcher);

	/* If requested, chroot to a directory */
	if (chroot_path) {
		if (chroot(chroot_path) != 0)
			err(1, "chroot(\"%s\") failed", chroot_path);

		if (chdir("/") != 0)
			err(1, "chdir(\"/\") failed");

		verbose("chroot done\n");
	}

	/* If requested, drop privileges */
	if (user_details) {
		uid_t u;
		gid_t g;

		u = user_details->pw_uid;
		g = user_details->pw_gid;

		if (initgroups(user_details->pw_name, g) != 0)
			err(1, "initgroups failed");

		if (setresgid(g, g, g) != 0)
			err(1, "setresgid failed");

		if (setresuid(u, u, u) != 0)
			err(1, "setresuid failed");

		verbose("Dropping privileges completed\n");
	}

	/* Finally, run the Guest.  This doesn't return. */
	run_guest();
}
/*:*/

/*M:999
 * Mastery is done: you now know everything I do.
 *
 * But surely you have seen code, features and bugs in your wanderings which
 * you now yearn to attack?  That is the real game, and I look forward to you
 * patching and forking lguest into the Your-Name-Here-visor.
 *
 * Farewell, and good coding!
 * Rusty Russell.
 */
