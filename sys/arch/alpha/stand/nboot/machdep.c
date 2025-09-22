/*	$OpenBSD: machdep.c,v 1.1 2023/03/11 20:56:01 miod Exp $	*/
/*	$NetBSD: boot.c,v 1.10 1997/01/18 01:58:33 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)boot.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <lib/libkern/libkern.h>
#include <lib/libsa/stand.h>
#include <lib/libsa/loadfile.h>
#include <stand/boot/cmd.h>

#include <sys/exec.h>
#include <sys/stat.h>
#include <sys/reboot.h>

#include <machine/rpb.h>
#include <machine/prom.h>
#include <machine/autoconf.h>

void init_prom_calls(void);
void OSFpal(void);
void halt(void);
long alpha_rpcc(void);

int
main()
{
#ifdef DEBUG
	struct rpb *r;
	struct mddt *mddtp;
	struct mddt_cluster *memc;
	int i;
#endif
	static char boot_file[128];
	extern char *kernelfile;
	char *s;
	int uppercase = 1;

	/* Init prom callback vector. */
	init_prom_calls();

	cninit();

	/* switch to OSF pal code. */
	OSFpal();

#ifdef DEBUG
	r = (struct rpb *)HWRPB_ADDR;
	mddtp = (struct mddt *)(HWRPB_ADDR + r->rpb_memdat_off);
	printf("%d memory clusters\n", mddtp->mddt_cluster_cnt);
	for (i = 0; i < mddtp->mddt_cluster_cnt; i++) {
		memc = &mddtp->mddt_clusters[i];
		printf("%d: (%d) %lx-%lx\n", i, memc->mddt_usage,
		    memc->mddt_pfn << PAGE_SHIFT,
		    (memc->mddt_pfn + memc->mddt_pg_cnt) << PAGE_SHIFT);
	}
#endif

	prom_getenv(PROM_E_BOOTED_FILE, boot_file, sizeof(boot_file));

	if (boot_file[0] != '\0') {
		/*
		 * If boot_file is all caps, then we are likely on an old
		 * (DEC 3000) prom which upper cases everything.
		 */
		for (s = boot_file; *s != '\0'; s++) {
			if (islower(*s)) {
				uppercase = 0;
				break;
			}
		}
		if (uppercase) {
			for (s = boot_file; *s != '\0'; s++)
				*s = tolower(*s);
		}
		kernelfile = boot_file;
		/*
		 * Unfortunately there doesn't seem to be a way to tell apart
		 * a kernel file provided via "boot -fi" (in which case we
		 * ought to boot immediately by setting bootprompt to zero)
		 * from a kernel file provided via the SRM environment (in
		 * which case we want to default to interactive operation).
		 */
	}

	boot(0);
}

void
devboot(dev_t dev /* ignored */, char *path)
{
	/*
	 * Although there is unfortunately no way to use SRM device names
	 * here, we need to provide a dummy device name since the boot loader
	 * code relies upon "device:path" syntax.
	 */
	snprintf(path, BOOTDEVLEN, "disk");
}

void
machdep()
{
	/* Nothing to do - everything done in main() already */
}

void
run_loadfile(uint64_t *marks, int howto)
{
	extern paddr_t ptbr_save;
	static struct bootinfo_v1 bootinfo_v1;
	u_int64_t entry;
	char boot_flags[128];

	/*
	 * Fill in the bootinfo for the kernel.
	 */
	bzero(&bootinfo_v1, sizeof(bootinfo_v1));
	bootinfo_v1.ssym = marks[MARK_SYM];
	bootinfo_v1.esym = marks[MARK_END];
//	bcopy(name, bootinfo_v1.booted_kernel,
//	    sizeof(bootinfo_v1.booted_kernel));
	prom_getenv(PROM_E_BOOTED_OSFLAGS, boot_flags, sizeof(boot_flags));
	bcopy(boot_flags, bootinfo_v1.boot_flags,
	    sizeof(bootinfo_v1.boot_flags));
	bootinfo_v1.hwrpb = (void *)HWRPB_ADDR;
	bootinfo_v1.hwrpbsize = ((struct rpb *)HWRPB_ADDR)->rpb_size;
	bootinfo_v1.cngetc = NULL;
	bootinfo_v1.cnputc = NULL;
	bootinfo_v1.cnpollc = NULL;
	bootinfo_v1.howto = howto;

	entry = marks[MARK_START];
	(*(void (*)(u_int64_t, u_int64_t, u_int64_t, void *, u_int64_t,
	    u_int64_t))entry)(0, ptbr_save, BOOTINFO_MAGIC, &bootinfo_v1, 1, 0);
}

/*
 * "machine tty" command to select a different console is not supported,
 */

int
cnspeed(dev_t dev, int sp)
{
	return 9600;
}

char *
ttyname(int fd)
{
	return "console";
}

dev_t
ttydev(char *name)
{
	return NODEV;
}

time_t
getsecs()
{
	static long tnsec;
	static long lastpcc, wrapsecs;
	long curpcc;

	if (tnsec == 0) {
		tnsec = 1;
		lastpcc = alpha_rpcc() & 0xffffffff;
		wrapsecs = (0xffffffff /
		    ((struct rpb *)HWRPB_ADDR)->rpb_cc_freq) + 1;

#if 0
		printf("getsecs: cc freq = %d, time to wrap = %d\n",
		    ((struct rpb *)HWRPB_ADDR)->rpb_cc_freq, wrapsecs);
#endif
	}

	curpcc = alpha_rpcc() & 0xffffffff;
	if (curpcc < lastpcc)
		curpcc += 0x100000000;

	tnsec += ((curpcc - lastpcc) * 1000000000) / ((struct rpb *)HWRPB_ADDR)->rpb_cc_freq;
	lastpcc = curpcc;

	return (tnsec / 1000000000);
}
