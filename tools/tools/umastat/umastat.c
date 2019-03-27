/*-
 * Copyright (c) 2005 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>

#include <vm/uma.h>
#include <vm/uma_int.h>

#include <err.h>
#include <kvm.h>
#include <limits.h>
#include <memstat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static struct nlist namelist[] = {
#define X_UMA_KEGS	0
	{ .n_name = "_uma_kegs" },
#define X_MP_MAXCPUS	1
	{ .n_name = "_mp_maxcpus" },
#define X_MP_MAXID	2 
	{ .n_name = "_mp_maxid" },
#define	X_ALLCPU	3
	{ .n_name = "_all_cpus" },
	{ .n_name = "" },
};

static void
usage(void)
{

	fprintf(stderr, "umastat [-M core [-N system]]\n");
	exit(-1);
}

static int
kread(kvm_t *kvm, void *kvm_pointer, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, (unsigned long)kvm_pointer + offset, address,
	    size);
	if (ret < 0)
		return (MEMSTAT_ERROR_KVM);
	if ((size_t)ret != size)
		return (MEMSTAT_ERROR_KVM_SHORTREAD);
	return (0);
}

static int
kread_string(kvm_t *kvm, const void *kvm_pointer, char *buffer, int buflen)
{
	ssize_t ret;
	int i;

	for (i = 0; i < buflen; i++) {
		ret = kvm_read(kvm, (unsigned long)kvm_pointer + i,
		    &(buffer[i]), sizeof(char));
		if (ret < 0)
			return (MEMSTAT_ERROR_KVM);
		if ((size_t)ret != sizeof(char))
			return (MEMSTAT_ERROR_KVM_SHORTREAD);
		if (buffer[i] == '\0')
			return (0);
	}
	/* Truncate. */
	buffer[i-1] = '\0';
	return (0);
}

static int
kread_symbol(kvm_t *kvm, int index, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, namelist[index].n_value + offset, address, size);
	if (ret < 0)
		return (MEMSTAT_ERROR_KVM);
	if ((size_t)ret != size)
		return (MEMSTAT_ERROR_KVM_SHORTREAD);
	return (0);
}

static const struct flaginfo {
	u_int32_t	 fi_flag;
	const char	*fi_name;
} flaginfo[] = {
	{ UMA_ZFLAG_MULTI, "multi" },
	{ UMA_ZFLAG_DRAINING, "draining" },
	{ UMA_ZFLAG_BUCKET, "bucket" },
	{ UMA_ZFLAG_INTERNAL, "internal" },
	{ UMA_ZFLAG_FULL, "full" },
	{ UMA_ZFLAG_CACHEONLY, "cacheonly" },
	{ UMA_ZONE_PAGEABLE, "pageable" },
	{ UMA_ZONE_ZINIT, "zinit" },
	{ UMA_ZONE_STATIC, "static" },
	{ UMA_ZONE_OFFPAGE, "offpage" },
	{ UMA_ZONE_MALLOC, "malloc" },
	{ UMA_ZONE_NOFREE, "nofree" },
	{ UMA_ZONE_MTXCLASS, "mtxclass" },
	{ UMA_ZONE_VM, "vm" },
	{ UMA_ZONE_HASH, "hash" },
	{ UMA_ZONE_SECONDARY, "secondary" },
	{ UMA_ZONE_MAXBUCKET, "maxbucket" },
	{ UMA_ZONE_CACHESPREAD, "cachespread" },
	{ UMA_ZONE_VTOSLAB, "vtoslab" },
	{ UMA_ZONE_NODUMP, "nodump" },
	{ UMA_ZONE_PCPU, "pcpu" },
};
static const int flaginfo_count = sizeof(flaginfo) / sizeof(struct flaginfo);

static void
uma_print_keg_flags(struct uma_keg *ukp, const char *spaces)
{
	int count, i;

	if (!ukp->uk_flags) {
		printf("%suk_flags = 0;\n", spaces);
		return;
	}

	printf("%suk_flags = ", spaces);
	for (i = 0, count = 0; i < flaginfo_count; i++) {
		if (ukp->uk_flags & flaginfo[i].fi_flag) {
			if (count++ > 0)
				printf(" | ");
			printf("%s", flaginfo[i].fi_name);
		}

	}
	printf(";\n");
}

static void
uma_print_keg_align(struct uma_keg *ukp, const char *spaces)
{

	switch(ukp->uk_align) {
	case UMA_ALIGN_PTR:
		printf("%suk_align = UMA_ALIGN_PTR;\n", spaces);
		break;

#if 0
	case UMA_ALIGN_LONG:
		printf("%suk_align = UMA_ALIGN_LONG;\n", spaces);
		break;

	case UMA_ALIGN_INT:
		printf("%suk_align = UMA_ALIGN_INT;\n", spaces);
		break;
#endif

	case UMA_ALIGN_SHORT:
		printf("%suk_align = UMA_ALIGN_SHORT;\n", spaces);
		break;

	case UMA_ALIGN_CHAR:
		printf("%suk_align = UMA_ALIGN_CHAR;\n", spaces);
		break;

	case UMA_ALIGN_CACHE:
		printf("%suk_align = UMA_ALIGN_CACHE;\n", spaces);
		break;

	default:
		printf("%suk_align = %d\n", spaces, ukp->uk_align);
	}
}

LIST_HEAD(bucketlist, uma_bucket);

static void
uma_print_bucket(struct uma_bucket *ubp, const char *spaces __unused)
{

	printf("{ ub_cnt = %d, ub_entries = %d }", ubp->ub_cnt,
	    ubp->ub_entries);
}

static void
uma_print_bucketlist(kvm_t *kvm, struct bucketlist *bucketlist,
    const char *name, const char *spaces)
{
	struct uma_bucket *ubp, ub;
	uint64_t total_entries, total_cnt;
	int count, ret;

	printf("%s%s {", spaces, name);

	total_entries = total_cnt = 0;
	count = 0;
	for (ubp = LIST_FIRST(bucketlist); ubp != NULL; ubp =
	    LIST_NEXT(&ub, ub_link)) {
		ret = kread(kvm, ubp, &ub, sizeof(ub), 0);
		if (ret != 0)
			errx(-1, "uma_print_bucketlist: %s", kvm_geterr(kvm));
		if (count % 2 == 0)
			printf("\n%s  ", spaces);
		uma_print_bucket(&ub, "");
		printf(" ");
		total_entries += ub.ub_entries;
		total_cnt += ub.ub_cnt;
		count++;
	}

	printf("\n");
	printf("%s};  // total cnt %ju, total entries %ju\n", spaces,
	    total_cnt, total_entries);
}

static void
uma_print_cache(kvm_t *kvm, struct uma_cache *cache, const char *name,
    int cpu, const char *spaces, int *ub_cnt_add, int *ub_entries_add)
{
	struct uma_bucket ub;
	int ret;

	printf("%s%s[%d] = {\n", spaces, name, cpu);
	printf("%s  uc_frees = %ju;\n", spaces, cache->uc_frees);
	printf("%s  uc_allocs = %ju;\n", spaces, cache->uc_allocs);

	if (cache->uc_freebucket != NULL) {
		ret = kread(kvm, cache->uc_freebucket, &ub, sizeof(ub), 0);
		if (ret != 0)
			errx(-1, "uma_print_cache: %s", kvm_geterr(kvm));
		printf("%s  uc_freebucket ", spaces);
		uma_print_bucket(&ub, spaces);
		printf(";\n");
		if (ub_cnt_add != NULL)
			*ub_cnt_add += ub.ub_cnt;
		if (ub_entries_add != NULL)
			*ub_entries_add += ub.ub_entries;
	} else
		printf("%s  uc_freebucket = NULL;\n", spaces);
	if (cache->uc_allocbucket != NULL) {
		ret = kread(kvm, cache->uc_allocbucket, &ub, sizeof(ub), 0);
		if (ret != 0)
			errx(-1, "uma_print_cache: %s", kvm_geterr(kvm));
		printf("%s  uc_allocbucket ", spaces);
		uma_print_bucket(&ub, spaces);
		printf(";\n");
		if (ub_cnt_add != NULL)
			*ub_cnt_add += ub.ub_cnt;
		if (ub_entries_add != NULL)
			*ub_entries_add += ub.ub_entries;
	} else
		printf("%s  uc_allocbucket = NULL;\n", spaces);
	printf("%s};\n", spaces);
}

int
main(int argc, char *argv[])
{
	LIST_HEAD(, uma_keg) uma_kegs;
	char name[MEMTYPE_MAXNAME];
	struct uma_keg *kzp, kz;
	struct uma_zone *uzp, *uzp_userspace;
	kvm_t *kvm;
	int all_cpus, cpu, mp_maxcpus, mp_maxid, ret, ub_cnt, ub_entries;
	size_t uzp_userspace_len;
	char *memf, *nlistf;
	int ch;
	char errbuf[_POSIX2_LINE_MAX];

	memf = nlistf = NULL;
	while ((ch = getopt(argc, argv, "M:N:")) != -1) {
		switch (ch) {
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();
	if (nlistf != NULL && memf == NULL)
		usage();

	kvm = kvm_openfiles(nlistf, memf, NULL, 0, errbuf);
	if (kvm == NULL)
		errx(-1, "kvm_openfiles: %s", errbuf);

	if (kvm_nlist(kvm, namelist) != 0)
		err(-1, "kvm_nlist");

	if (namelist[X_UMA_KEGS].n_type == 0 ||
	    namelist[X_UMA_KEGS].n_value == 0)
		errx(-1, "kvm_nlist return");

	ret = kread_symbol(kvm, X_MP_MAXCPUS, &mp_maxcpus, sizeof(mp_maxcpus),
	    0);
	if (ret != 0)
		errx(-1, "kread_symbol: %s", kvm_geterr(kvm));

	printf("mp_maxcpus = %d\n", mp_maxcpus);

	ret = kread_symbol(kvm, X_MP_MAXID, &mp_maxid, sizeof(mp_maxid), 0);
	if (ret != 0)
		errx(-1, "kread_symbol: %s", kvm_geterr(kvm));

	printf("mp_maxid = %d\n", mp_maxid);

	ret = kread_symbol(kvm, X_ALLCPU, &all_cpus, sizeof(all_cpus), 0);
	if (ret != 0)
		errx(-1, "kread_symbol: %s", kvm_geterr(kvm));

	printf("all_cpus = %x\n", all_cpus);

	ret = kread_symbol(kvm, X_UMA_KEGS, &uma_kegs, sizeof(uma_kegs), 0);
	if (ret != 0)
		errx(-1, "kread_symbol: %s", kvm_geterr(kvm));

	/*
	 * uma_zone_t ends in an array of mp_maxid cache entries.  However,
	 * it is statically declared as an array of size 1, so we need to
	 * provide additional space.
	 */
	uzp_userspace_len = sizeof(struct uma_zone) + mp_maxid *
	    sizeof(struct uma_cache);
	uzp_userspace = malloc(uzp_userspace_len);
	if (uzp_userspace == NULL)
		err(-1, "malloc");

	for (kzp = LIST_FIRST(&uma_kegs); kzp != NULL; kzp =
	    LIST_NEXT(&kz, uk_link)) {
		ret = kread(kvm, kzp, &kz, sizeof(kz), 0);
		if (ret != 0) {
			free(uzp_userspace);
			errx(-1, "kread: %s", kvm_geterr(kvm));
		}
		printf("Keg {\n");

		uma_print_keg_align(&kz, "  ");
		printf("  uk_pages = %d\n", kz.uk_pages);
		printf("  uk_free = %d\n", kz.uk_free);
		printf("  uk_reserve = %d\n", kz.uk_reserve);
		printf("  uk_size = %d\n", kz.uk_size);
		printf("  uk_rsize = %d\n", kz.uk_rsize);
		printf("  uk_maxpages = %d\n", kz.uk_maxpages);

		printf("  uk_pgoff = %d\n", kz.uk_pgoff);
		printf("  uk_ppera = %d\n", kz.uk_ppera);
		printf("  uk_ipers = %d\n", kz.uk_ipers);
		uma_print_keg_flags(&kz, "  ");

		if (LIST_FIRST(&kz.uk_zones) == NULL) {
			printf("; No zones.\n");
			printf("};\n");
			continue;
		}
		for (uzp = LIST_FIRST(&kz.uk_zones); uzp != NULL; uzp =
		    LIST_NEXT(uzp_userspace, uz_link)) {
			/*
			 * We actually copy in twice: once with the base
			 * structure, so that we can then decide if we also
			 * need to copy in the caches.  This prevents us
			 * from reading past the end of the base UMA zones,
			 * which is unlikely to cause problems but could.
			 */
			ret = kread(kvm, uzp, uzp_userspace,
			    sizeof(struct uma_zone), 0);
			if (ret != 0) {
				free(uzp_userspace);
				errx(-1, "kread: %s", kvm_geterr(kvm));
			}
			if (!(kz.uk_flags & UMA_ZFLAG_INTERNAL)) {
				ret = kread(kvm, uzp, uzp_userspace,
				    uzp_userspace_len, 0);
				if (ret != 0) {
					free(uzp_userspace);
					errx(-1, "kread: %s",
					    kvm_geterr(kvm));
				}
			}
			ret = kread_string(kvm, uzp_userspace->uz_name, name,
			    MEMTYPE_MAXNAME);
			if (ret != 0) {
				free(uzp_userspace);
				errx(-1, "kread_string: %s", kvm_geterr(kvm));
			}
			printf("  Zone {\n");
			printf("    uz_name = \"%s\";\n", name);
			printf("    uz_allocs = %lu;\n",
			    uzp_userspace->uz_allocs);
			printf("    uz_frees = %lu;\n",
			    uzp_userspace->uz_frees);
			printf("    uz_fails = %lu;\n",
			    uzp_userspace->uz_fails);
			printf("    uz_sleeps = %ju;\n",
			    uzp_userspace->uz_sleeps);
			printf("    uz_count = %u;\n",
			    uzp_userspace->uz_count);
			uma_print_bucketlist(kvm, (void *)
			    &uzp_userspace->uz_buckets, "uz_buckets",
			    "    ");

			if (!(kz.uk_flags & UMA_ZFLAG_INTERNAL)) {
				ub_cnt = ub_entries = 0;
				for (cpu = 0; cpu <= mp_maxid; cpu++) {
					/* if (CPU_ABSENT(cpu)) */
					if ((all_cpus & (1 << cpu)) == 0)
						continue;
					uma_print_cache(kvm,
					    &uzp_userspace->uz_cpu[cpu],
					    "uc_cache", cpu, "    ", &ub_cnt,
					    &ub_entries);
				}
				printf("    // %d cache total cnt, %d total "
				    "entries\n", ub_cnt, ub_entries);
			}

			printf("  };\n");
		}
		printf("};\n");
	}

	free(uzp_userspace);
	return (0);
}
