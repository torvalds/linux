/*	$OpenBSD: gpt.c,v 1.108 2025/07/31 13:37:06 krw Exp $	*/
/*
 * Copyright (c) 2015 Markus Muller <mmu@grummel.net>
 * Copyright (c) 2015 Kenneth R Westerback <krw@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uuid.h>

#include "part.h"
#include "disk.h"
#include "mbr.h"
#include "misc.h"
#include "gpt.h"

#ifdef DEBUG
#define DPRINTF(x...)	printf(x)
#else
#define DPRINTF(x...)
#endif	/* DEBUG */

struct mbr		gmbr;
struct gpt_header	gh;
struct gpt_partition	gp[NGPTPARTITIONS];

const struct gpt_partition * const *sort_gpt(void);
int			  lba_free(uint64_t *, uint64_t *);
int			  add_partition(const uint8_t *, const char *, uint64_t);
int			  find_partition(const uint8_t *);
int			  get_header(const uint64_t);
int			  get_partition_table(void);
int			  init_gh(void);
int			  init_gp(const int);
uint32_t		  crc32(const u_char *, const uint32_t);
int			  protective_mbr(const struct mbr *);
int			  gpt_chk_mbr(struct dos_partition *, uint64_t);
void			  string_to_name(const unsigned int, const char *);
const char		 *name_to_string(const unsigned int);
void			  print_free(const uint64_t, const uint64_t,
    const char *);

void
string_to_name(const unsigned int pn, const char *ch)
{
	unsigned int			i;

	memset(gp[pn].gp_name, 0, sizeof(gp[pn].gp_name));

	for (i = 0; i < nitems(gp[pn].gp_name) && ch[i] != '\0'; i++)
		gp[pn].gp_name[i] = htole16((unsigned int)ch[i]);
}

const char *
name_to_string(const unsigned int pn)
{
	static char		name[GPTPARTNAMESIZE + 1];
	unsigned int		i;

	for (i = 0; i < GPTPARTNAMESIZE && gp[pn].gp_name[i] != 0; i++)
		name[i] = letoh16(gp[pn].gp_name[i]) & 0x7F;
	name[i] = '\0';

	return name;
}

/*
 * Return the index into dp[] of the EFI GPT (0xEE) partition, or -1 if no such
 * partition exists.
 *
 * Taken from kern/subr_disk.c.
 *
 */
int
gpt_chk_mbr(struct dos_partition *dp, u_int64_t dsize)
{
	struct dos_partition	*dp2;
	int			 efi, eficnt, found, i;
	uint32_t		 psize;

	found = efi = eficnt = 0;
	for (dp2 = dp, i = 0; i < NDOSPART; i++, dp2++) {
		if (dp2->dp_typ == DOSPTYP_UNUSED)
			continue;
		found++;
		if (dp2->dp_typ != DOSPTYP_EFI)
			continue;
		if (letoh32(dp2->dp_start) != GPTSECTOR)
			continue;
		psize = letoh32(dp2->dp_size);
		if (psize <= (dsize - GPTSECTOR) || psize == UINT32_MAX) {
			efi = i;
			eficnt++;
		}
	}
	if (found == 1 && eficnt == 1)
		return efi;

	return -1;
}

int
protective_mbr(const struct mbr *mbr)
{
	struct dos_partition	dp[NDOSPART], dos_partition;
	unsigned int		i;

	if (mbr->mbr_lba_self != 0)
		return -1;

	for (i = 0; i < nitems(dp); i++) {
		memset(&dos_partition, 0, sizeof(dos_partition));
		if (i < nitems(mbr->mbr_prt))
			PRT_prt_to_dp(&mbr->mbr_prt[i], mbr->mbr_lba_self,
			    mbr->mbr_lba_firstembr, &dos_partition);
		memcpy(&dp[i], &dos_partition, sizeof(dp[i]));
	}

	return gpt_chk_mbr(dp, DL_GETDSIZE(&dl));
}

int
get_header(const uint64_t sector)
{
	struct gpt_header	 legh;
	uint64_t		 gpbytes, gpsectors, lba_end;

	if (DISK_readbytes(&legh, sector, sizeof(legh)))
		return -1;

	gh.gh_sig = letoh64(legh.gh_sig);
	if (gh.gh_sig != GPTSIGNATURE) {
		DPRINTF("gpt signature: expected 0x%llx, got 0x%llx\n",
		    GPTSIGNATURE, gh.gh_sig);
		return -1;
	}

	gh.gh_rev = letoh32(legh.gh_rev);
	if (gh.gh_rev != GPTREVISION) {
		DPRINTF("gpt revision: expected 0x%x, got 0x%x\n",
		    GPTREVISION, gh.gh_rev);
		return -1;
	}

	gh.gh_lba_self = letoh64(legh.gh_lba_self);
	if (gh.gh_lba_self != sector) {
		DPRINTF("gpt self lba: expected %llu, got %llu\n",
		    sector, gh.gh_lba_self);
		return -1;
	}

	gh.gh_size = letoh32(legh.gh_size);
	if (gh.gh_size != GPTMINHDRSIZE) {
		DPRINTF("gpt header size: expected %u, got %u\n",
		    GPTMINHDRSIZE, gh.gh_size);
		return -1;
	}

	gh.gh_part_size = letoh32(legh.gh_part_size);
	if (gh.gh_part_size != GPTMINPARTSIZE) {
		DPRINTF("gpt partition size: expected %u, got %u\n",
		    GPTMINPARTSIZE, gh.gh_part_size);
		return -1;
	}

	if ((dl.d_secsize % gh.gh_part_size) != 0) {
		DPRINTF("gpt sector size %% partition size (%u %% %u) != 0\n",
		    dl.d_secsize, gh.gh_part_size);
		return -1;
	}

	gh.gh_part_num = letoh32(legh.gh_part_num);
	if (gh.gh_part_num > NGPTPARTITIONS) {
		DPRINTF("gpt partition count: expected <= %u, got %u\n",
		    NGPTPARTITIONS, gh.gh_part_num);
		return -1;
	}

	gh.gh_csum = letoh32(legh.gh_csum);
	legh.gh_csum = 0;
	legh.gh_csum = crc32((unsigned char *)&legh, gh.gh_size);
	if (legh.gh_csum != gh.gh_csum) {
		DPRINTF("gpt header checksum: expected 0x%x, got 0x%x\n",
		    legh.gh_csum, gh.gh_csum);
		/* Accept wrong-endian checksum. */
		if (swap32(legh.gh_csum) != gh.gh_csum)
			return -1;
	}

	gpbytes = gh.gh_part_num * gh.gh_part_size;
	gpsectors = (gpbytes + dl.d_secsize - 1) / dl.d_secsize;
	lba_end = DL_GETDSIZE(&dl) - gpsectors - 2;

	gh.gh_lba_end = letoh64(legh.gh_lba_end);
	if (gh.gh_lba_end > lba_end) {
		DPRINTF("gpt last usable LBA: reduced from %llu to %llu\n",
		    gh.gh_lba_end, lba_end);
		gh.gh_lba_end = lba_end;
	}

	gh.gh_lba_start = letoh64(legh.gh_lba_start);
	if (gh.gh_lba_start >= gh.gh_lba_end) {
		DPRINTF("gpt first usable LBA: expected < %llu, got %llu\n",
		    gh.gh_lba_end, gh.gh_lba_start);
		return -1;
	}

	gh.gh_part_lba = letoh64(legh.gh_part_lba);
	if (gh.gh_lba_self == GPTSECTOR) {
		if (gh.gh_part_lba <= GPTSECTOR) {
			DPRINTF("gpt partition entries start: expected > %u, "
			    "got %llu\n", GPTSECTOR, gh.gh_part_lba);
			return -1;
		}
		if (gh.gh_part_lba + gpsectors > gh.gh_lba_start) {
			DPRINTF("gpt partition entries end: expected < %llu, "
			    "got %llu\n", gh.gh_lba_start,
			    gh.gh_part_lba + gpsectors);
			return -1;
		}
	} else {
		if (gh.gh_part_lba <= gh.gh_lba_end) {
			DPRINTF("gpt partition entries start: expected > %llu, "
			    "got %llu\n", gh.gh_lba_end, gh.gh_part_lba);
			return -1;
		}
		if (gh.gh_part_lba + gpsectors > gh.gh_lba_self) {
			DPRINTF("gpt partition entries end: expected < %llu, "
			    "got %llu\n", gh.gh_lba_self,
			    gh.gh_part_lba + gpsectors);
			return -1;
		}
	}

	gh.gh_lba_alt = letoh32(legh.gh_lba_alt);
	gh.gh_part_csum = letoh32(legh.gh_part_csum);
	gh.gh_rsvd = letoh32(legh.gh_rsvd);	/* Should always be 0. */
	uuid_dec_le(&legh.gh_guid, &gh.gh_guid);

	return 0;
}

int
get_partition_table(void)
{
	struct gpt_partition	*legp;
	uint64_t		 gpbytes;
	unsigned int		 pn;
	int			 rslt = -1;
	uint32_t		 gh_part_csum;

	DPRINTF("gpt partition table being read from LBA %llu\n",
	    gh.gh_part_lba);

	gpbytes = gh.gh_part_num * gh.gh_part_size;

	legp = calloc(1, gpbytes);
	if (legp == NULL)
		err(1, "legp");

	if (DISK_readbytes(legp, gh.gh_part_lba, gpbytes))
		goto done;
	gh_part_csum = crc32((unsigned char *)legp, gpbytes);

	if (gh_part_csum != gh.gh_part_csum) {
		DPRINTF("gpt partition table checksum: expected 0x%x, "
		    "got 0x%x\n", gh.gh_part_csum, gh_part_csum);
		/* Accept wrong-endian checksum. */
		if (swap32(gh_part_csum) != gh.gh_part_csum)
			goto done;
	}

	memset(&gp, 0, sizeof(gp));
	for (pn = 0; pn < gh.gh_part_num; pn++) {
		uuid_dec_le(&legp[pn].gp_type, &gp[pn].gp_type);
		uuid_dec_le(&legp[pn].gp_guid, &gp[pn].gp_guid);
		gp[pn].gp_lba_start = letoh64(legp[pn].gp_lba_start);
		gp[pn].gp_lba_end = letoh64(legp[pn].gp_lba_end);
		gp[pn].gp_attrs = letoh64(legp[pn].gp_attrs);
		memcpy(gp[pn].gp_name, legp[pn].gp_name,
		    sizeof(gp[pn].gp_name));
	}
	rslt = 0;

 done:
	free(legp);
	return rslt;
}

void
print_free(const uint64_t start, const uint64_t end, const char *units)
{
	float			 size;
	const struct unit_type	*ut;

	size = units_size(units, end - start + 1, &ut);
	printf("%4s: Free%-32s [%12llu: %12.0f%s]\n", "", "", start, size,
	    ut->ut_abbr);
}

int
GPT_recover_partition(const char *line1, const char *line2, const char *line3)
{
	char			 type[37], guid[37], name[37], name2[37];
	struct uuid		 type_uuid, guid_uuid;
	const char		*p;
	uint64_t		 start, size, end, attrs, attrs2;
	unsigned int		 pn;
	int			 error, fields;
	unsigned int		 i;

	if (line1 == NULL) {
		/* Try to recover from disk contents. */
		error = get_header(GPTSECTOR);
		if (error != 0 || get_partition_table() != 0)
			error = get_header(DL_GETDSIZE(&dl) - 1);
		if (error == 0)
			error = get_partition_table();
		return error;
	}

	fields = sscanf(line1, "%u: %36[^[] [%llu:%llu] 0x%llx %36[^\n]", &pn,
	    type, &start, &size, &attrs, name);
	switch (fields) {
	case 4:
		attrs = 0;
		memset(name, 0, sizeof(name));
		break;
	case 5:
		memset(name, 0, sizeof(name));
		break;
	case 6:
		break;
	default:
		return -1;
	}

	fields = sscanf(line2, "%36s %36[^\n]", guid, name2);
	switch (fields) {
	case 0:
	case EOF:
		memset(guid, 0, sizeof(guid));
		break;
	case 1:
		break;
	case 2:
		memcpy(name, name2, sizeof(name));
		break;
	}

	fields = sscanf(line3, "Attributes: (0x%llx)", &attrs2);
	switch (fields) {
	case 0:
	case EOF:
		break;
	case 1:
		attrs = attrs2;
		break;
	}

	if (string_to_uuid(type, &type_uuid) != uuid_s_ok) {
		for (i = strlen(type); i > 0; i--) {
			if (!isspace((unsigned char)type[i - 1]))
				break;
			type[i - 1] = '\0';
		}
		if ((p = PRT_desc_to_guid(type)) == NULL)
			return -1;
		if (string_to_uuid(p, &type_uuid) != uuid_s_ok)
			return -1;
	}

	if (string_to_uuid(guid, &guid_uuid) != uuid_s_ok)
		return -1;
	if (uuid_is_nil(&guid_uuid, NULL))
		uuid_create(&guid_uuid, NULL);

	if (start == 0) {
		if (lba_free(&start, NULL) == -1)
			return -1;
		if (start % BLOCKALIGNMENT)
			start += (BLOCKALIGNMENT - start % BLOCKALIGNMENT);
	}

	end = start + size - 1;
	if (pn >= nitems(gp) || start < gh.gh_lba_start ||
	    end > gh.gh_lba_end || size == 0)
		return -1;

	/* Don't overwrite already recovered protected partitions! */
	if (PRT_protected_uuid(&gp[pn].gp_type) ||
	    (gp[pn].gp_attrs & GPTPARTATTR_REQUIRED))
		return -1;

	/* Don't overlap already recovered partitions. */
	for (i = 0; i < gh.gh_part_num; i++) {
		if (i == pn)
			continue;
		if (start >= gp[i].gp_lba_start && start <= gp[i].gp_lba_end)
			return -1;
		if (end >= gp[i].gp_lba_start && end <= gp[i].gp_lba_end)
			return -1;
	}

	gp[pn].gp_type = type_uuid;
	gp[pn].gp_guid = guid_uuid;
	gp[pn].gp_lba_start = start;
	gp[pn].gp_lba_end = end;
	gp[pn].gp_attrs = attrs;

	string_to_name(pn, name);

	return 0;
}

int
GPT_read(const int which)
{
	int			error;

	error = MBR_read(0, 0, &gmbr);
	if (error)
		goto done;
	error = protective_mbr(&gmbr);
	if (error == -1)
		goto done;

	switch (which) {
	case PRIMARYGPT:
		error = get_header(GPTSECTOR);
		break;
	case SECONDARYGPT:
		error = get_header(DL_GETDSIZE(&dl) - 1);
		break;
	case ANYGPT:
		error = get_header(GPTSECTOR);
		if (error != 0 || get_partition_table() != 0)
			error = get_header(DL_GETDSIZE(&dl) - 1);
		break;
	default:
		return -1;
	}

	if (error == 0)
		error = get_partition_table();

 done:
	if (error != 0) {
		/* No valid GPT found. Zap any artifacts. */
		memset(&gmbr, 0, sizeof(gmbr));
		memset(&gh, 0, sizeof(gh));
		memset(&gp, 0, sizeof(gp));
	}

	return error;
}

void
GPT_print(const char *units)
{
	const struct gpt_partition * const *sgp;
	const struct unit_type	*ut;
	const int		 secsize = dl.d_secsize;
	char			*guidstr = NULL;
	double			 size;
	uint64_t		 bs, nextbs;
	unsigned int		 i, pn;
	uint32_t		 status;

#ifdef	DEBUG
	char			*p;
	uint64_t		 sig;

	sig = htole64(gh.gh_sig);
	p = (char *)&sig;

	printf("gh_sig         : ");
	for (i = 0; i < sizeof(sig); i++)
		printf("%c", isprint((unsigned char)p[i]) ? p[i] : '?');
	printf(" (");
	for (i = 0; i < sizeof(sig); i++) {
		printf("%02x", p[i]);
		if ((i + 1) < sizeof(sig))
			printf(":");
	}
	printf(")\n");
	printf("gh_rev         : %u\n", gh.gh_rev);
	printf("gh_size        : %u (%zd)\n", gh.gh_size, sizeof(gh));
	printf("gh_csum        : 0x%x\n", gh.gh_csum);
	printf("gh_rsvd        : %u\n", gh.gh_rsvd);
	printf("gh_lba_self    : %llu\n", gh.gh_lba_self);
	printf("gh_lba_alt     : %llu\n", gh.gh_lba_alt);
	printf("gh_lba_start   : %llu\n", gh.gh_lba_start);
	printf("gh_lba_end     : %llu\n", gh.gh_lba_end);
	p = NULL;
	uuid_to_string(&gh.gh_guid, &p, &status);
	printf("gh_gh_guid     : %s\n", (status == uuid_s_ok) ? p : "<invalid>");
	free(p);
	printf("gh_gh_part_lba : %llu\n", gh.gh_part_lba);
	printf("gh_gh_part_num : %u (%zu)\n", gh.gh_part_num, nitems(gp));
	printf("gh_gh_part_size: %u (%zu)\n", gh.gh_part_size, sizeof(gp[0]));
	printf("gh_gh_part_csum: 0x%x\n", gh.gh_part_csum);
	printf("\n");
#endif	/* DEBUG */

	size = units_size(units, DL_GETDSIZE(&dl), &ut);
	printf("Disk: %s\tUsable LBA: %llu to %llu [%.0f ",
	    disk.dk_name, gh.gh_lba_start, gh.gh_lba_end, size);
	if (ut->ut_conversion == 0 && secsize != DEV_BSIZE)
		printf("%d-byte ", secsize);
	printf("%s]\n", ut->ut_lname);

	if (verbosity == VERBOSE) {
		printf("GUID: ");
		uuid_to_string(&gh.gh_guid, &guidstr, &status);
		if (status == uuid_s_ok)
			printf("%s\n", guidstr);
		else
			printf("<invalid header GUID>\n");
		free(guidstr);
	}

	GPT_print_parthdr();

	sgp = sort_gpt();
	bs = gh.gh_lba_start;
	for (i = 0; sgp && sgp[i] != NULL; i++) {
		for (pn = 0; pn < nitems(gp); pn++) {
			if (&gp[pn] == sgp[i]) {
				nextbs = gp[pn].gp_lba_start;
				if (nextbs > bs)
					print_free(bs, nextbs - 1, units);
				GPT_print_part(pn, units);
				bs = gp[pn].gp_lba_end + 1;
			}
		}
	}
	if (bs < gh.gh_lba_end)
		print_free(bs, gh.gh_lba_end, units);
}

void
GPT_print_parthdr(void)
{
	printf("   #: type                                "
	    " [       start:         size ]\n");
	if (verbosity == VERBOSE)
		printf("      guid                                 name\n");
	printf("--------------------------------------------------------"
	    "----------------\n");
}

void
GPT_print_part(const unsigned int pn, const char *units)
{
	const struct unit_type	*ut;
	char			*guidstr = NULL;
	double			 size;
	uint64_t		 attrs, end, start;
	uint32_t		 status;

	start = gp[pn].gp_lba_start;
	end = gp[pn].gp_lba_end;
	size = units_size(units, (start > end) ? 0 : end - start + 1, &ut);

	printf(" %3u: %-36s [%12lld: %12.0f%s]\n", pn,
	    PRT_uuid_to_desc(&gp[pn].gp_type, 0), start, size, ut->ut_abbr);

	if (verbosity == VERBOSE) {
		uuid_to_string(&gp[pn].gp_guid, &guidstr, &status);
		if (status != uuid_s_ok)
			printf("      <invalid partition guid>             ");
		else
			printf("      %-36s ", guidstr);
		printf("%s\n", name_to_string(pn));
		free(guidstr);
		attrs = gp[pn].gp_attrs;
		if (attrs) {
			printf("      Attributes: (0x%016llx) ", attrs);
			if (attrs & GPTPARTATTR_REQUIRED)
				printf("Required " );
			if (attrs & GPTPARTATTR_IGNORE)
				printf("Ignore ");
			if (attrs & GPTPARTATTR_BOOTABLE)
				printf("Bootable ");
			if (attrs & GPTPARTATTR_MS_READONLY)
				printf("MSReadOnly " );
			if (attrs & GPTPARTATTR_MS_SHADOW)
				printf("MSShadow ");
			if (attrs & GPTPARTATTR_MS_HIDDEN)
				printf("MSHidden ");
			if (attrs & GPTPARTATTR_MS_NOAUTOMOUNT)
				printf("MSNoAutoMount ");
			printf("\n");
		}
	}

	if (uuid_is_nil(&gp[pn].gp_type, NULL) == 0) {
		if (start > end)
			printf("partition %u first LBA is > last LBA\n", pn);
		if (start < gh.gh_lba_start || end > gh.gh_lba_end)
			printf("partition %u extends beyond usable LBA range "
			    "of %s\n", pn, disk.dk_name);
	}
}

int
find_partition(const uint8_t *beuuid)
{
	struct uuid		uuid;
	unsigned int		pn;

	uuid_dec_be(beuuid, &uuid);

	for (pn = 0; pn < gh.gh_part_num; pn++) {
		if (uuid_compare(&gp[pn].gp_type, &uuid, NULL) == 0)
			return pn;
	}
	return -1;
}

int
add_partition(const uint8_t *beuuid, const char *name, uint64_t sectors)
{
	struct uuid		uuid;
	int			rslt;
	uint64_t		end, freesectors, start;
	uint32_t		status, pn;

	uuid_dec_be(beuuid, &uuid);

	for (pn = 0; pn < gh.gh_part_num; pn++) {
		if (uuid_is_nil(&gp[pn].gp_type, NULL))
			break;
	}
	if (pn == gh.gh_part_num)
		goto done;

	rslt = lba_free(&start, &end);
	if (rslt == -1)
		goto done;

	if (start % BLOCKALIGNMENT)
		start += (BLOCKALIGNMENT - start % BLOCKALIGNMENT);
	if (start >= end)
		goto done;

	freesectors = end - start + 1;

	if (sectors == 0)
		sectors = freesectors;

	if (freesectors < sectors)
		goto done;
	else if (freesectors > sectors)
		end = start + sectors - 1;

	gp[pn].gp_type = uuid;
	gp[pn].gp_lba_start = start;
	gp[pn].gp_lba_end = end;
	string_to_name(pn, name);

	uuid_create(&gp[pn].gp_guid, &status);
	if (status == uuid_s_ok)
		return 0;

 done:
	if (pn != gh.gh_part_num)
		memset(&gp[pn], 0, sizeof(gp[pn]));
	printf("unable to add %s\n", name);
	return -1;
}

int
init_gh(void)
{
	struct gpt_header	oldgh;
	const int		secsize = dl.d_secsize;
	int			needed;
	uint32_t		status;

	memcpy(&oldgh, &gh, sizeof(oldgh));
	memset(&gh, 0, sizeof(gh));
	memset(&gmbr, 0, sizeof(gmbr));

	/* XXX Do we need the boot code? UEFI spec & Apple says no. */
	memcpy(gmbr.mbr_code, default_dmbr.dmbr_boot, sizeof(gmbr.mbr_code));
	gmbr.mbr_prt[0].prt_id = DOSPTYP_EFI;
	gmbr.mbr_prt[0].prt_bs = 1;
	gmbr.mbr_prt[0].prt_ns = UINT32_MAX;
	gmbr.mbr_signature = DOSMBR_SIGNATURE;

	needed = sizeof(gp) / secsize + 2;

	if (needed % BLOCKALIGNMENT)
		needed += (needed - (needed % BLOCKALIGNMENT));

	gh.gh_sig = GPTSIGNATURE;
	gh.gh_rev = GPTREVISION;
	gh.gh_size = GPTMINHDRSIZE;
	gh.gh_csum = 0;
	gh.gh_rsvd = 0;
	gh.gh_lba_self = 1;
	gh.gh_lba_alt = DL_GETDSIZE(&dl) - 1;
	gh.gh_lba_start = needed;
	gh.gh_lba_end = DL_GETDSIZE(&dl) - needed;
	uuid_create(&gh.gh_guid, &status);
	if (status != uuid_s_ok) {
		memcpy(&gh, &oldgh, sizeof(gh));
		return -1;
	}
	gh.gh_part_lba = 2;
	gh.gh_part_num = NGPTPARTITIONS;
	gh.gh_part_size = GPTMINPARTSIZE;
	gh.gh_part_csum = 0;

	return 0;
}

int
init_gp(const int how)
{
	struct gpt_partition	oldgp[NGPTPARTITIONS];
	const uint8_t		gpt_uuid_efi_system[] = GPT_UUID_EFI_SYSTEM;
	const uint8_t		gpt_uuid_openbsd[] = GPT_UUID_OPENBSD;
	uint64_t		prt_ns;
	int			pn, rslt;

	memcpy(&oldgp, &gp, sizeof(oldgp));
	if (how == GHANDGP)
		memset(&gp, 0, sizeof(gp));
	else {
		for (pn = 0; pn < gh.gh_part_num; pn++) {
			if (PRT_protected_uuid(&gp[pn].gp_type) ||
			    (gp[pn].gp_attrs & GPTPARTATTR_REQUIRED))
				continue;
			memset(&gp[pn], 0, sizeof(gp[pn]));
		}
	}

	rslt = 0;
	if (disk.dk_bootprt.prt_ns > 0) {
		pn = find_partition(gpt_uuid_efi_system);
		if (pn == -1) {
			rslt = add_partition(gpt_uuid_efi_system,
			    "EFI System Area", disk.dk_bootprt.prt_ns);
		} else {
			prt_ns = gp[pn].gp_lba_end - gp[pn].gp_lba_start + 1;
			if (prt_ns < disk.dk_bootprt.prt_ns) {
				printf("EFI System Area < %llu sectors\n",
				    disk.dk_bootprt.prt_ns);
				rslt = -1;
			}
		}
	}
	if (rslt == 0)
		rslt = add_partition(gpt_uuid_openbsd, "OpenBSD Area", 0);

	if (rslt != 0)
		memcpy(&gp, &oldgp, sizeof(gp));

	return rslt;
}

int
GPT_init(const int how)
{
	int			rslt = 0;

	if (how == GHANDGP)
		rslt = init_gh();
	if (rslt == 0)
		rslt = init_gp(how);

	return rslt;
}

void
GPT_zap_headers(void)
{
	struct gpt_header	legh;

	if (DISK_readbytes(&legh, GPTSECTOR, sizeof(legh)))
		return;

	if (letoh64(legh.gh_sig) == GPTSIGNATURE) {
		memset(&legh, 0, sizeof(legh));
		if (DISK_writebytes(&legh, GPTSECTOR, sizeof(legh)))
			DPRINTF("Unable to zap GPT header @ sector %d",
			    GPTSECTOR);
	}

	if (DISK_readbytes(&legh, DL_GETDSIZE(&dl) - 1, sizeof(legh)))
		return;

	if (letoh64(legh.gh_sig) == GPTSIGNATURE) {
		memset(&legh, 0, GPTMINHDRSIZE);
		if (DISK_writebytes(&legh, DL_GETDSIZE(&dl) - 1, sizeof(legh)))
			DPRINTF("Unable to zap GPT header @ sector %llu",
			    DL_GETDSIZE(&dl) - 1);
	}
}

int
GPT_write(void)
{
	struct gpt_header	 legh;
	struct gpt_partition	*legp;
	uint64_t		 altgh, altgp;
	uint64_t		 gpbytes, gpsectors;
	unsigned int		 pn;
	int			 rslt = -1;

	if (MBR_write(&gmbr))
		return -1;

	gpbytes = gh.gh_part_num * gh.gh_part_size;
	gpsectors = (gpbytes + dl.d_secsize - 1) / dl.d_secsize;

	altgh = DL_GETDSIZE(&dl) - 1;
	altgp = altgh - gpsectors;

	legh.gh_sig = htole64(GPTSIGNATURE);
	legh.gh_rev = htole32(GPTREVISION);
	legh.gh_size = htole32(GPTMINHDRSIZE);
	legh.gh_rsvd = 0;
	legh.gh_lba_self = htole64(GPTSECTOR);
	legh.gh_lba_alt = htole64(altgh);
	legh.gh_lba_start = htole64(gh.gh_lba_start);
	legh.gh_lba_end = htole64(gh.gh_lba_end);
	uuid_enc_le(&legh.gh_guid, &gh.gh_guid);
	legh.gh_part_lba = htole64(GPTSECTOR + 1);
	legh.gh_part_num = htole32(gh.gh_part_num);
	legh.gh_part_size = htole32(GPTMINPARTSIZE);

	legp = calloc(1, gpbytes);
	if (legp == NULL)
		err(1, "legp");

	for (pn = 0; pn < gh.gh_part_num; pn++) {
		uuid_enc_le(&legp[pn].gp_type, &gp[pn].gp_type);
		uuid_enc_le(&legp[pn].gp_guid, &gp[pn].gp_guid);
		legp[pn].gp_lba_start = htole64(gp[pn].gp_lba_start);
		legp[pn].gp_lba_end = htole64(gp[pn].gp_lba_end);
		legp[pn].gp_attrs = htole64(gp[pn].gp_attrs);
		memcpy(legp[pn].gp_name, gp[pn].gp_name,
		    sizeof(legp[pn].gp_name));
	}
	legh.gh_part_csum = htole32(crc32((unsigned char *)legp, gpbytes));
	legh.gh_csum = 0;
	legh.gh_csum = htole32(crc32((unsigned char *)&legh, gh.gh_size));

	if (DISK_writebytes(&legh, GPTSECTOR, gh.gh_size) ||
	    DISK_writebytes(legp, GPTSECTOR + 1, gpbytes))
		goto done;

	legh.gh_lba_self = htole64(altgh);
	legh.gh_lba_alt = htole64(GPTSECTOR);
	legh.gh_part_lba = htole64(altgp);
	legh.gh_csum = 0;
	legh.gh_csum = htole32(crc32((unsigned char *)&legh, gh.gh_size));

	if (DISK_writebytes(&legh, altgh, gh.gh_size) ||
	    DISK_writebytes(&gp, altgp, gpbytes))
		goto done;

	/* Refresh in-kernel disklabel from the updated disk information. */
	if (ioctl(disk.dk_fd, DIOCRLDINFO, 0) == -1)
		warn("DIOCRLDINFO");
	rslt = 0;

 done:
	free(legp);
	return rslt;
}

int
gp_lba_start_cmp(const void *e1, const void *e2)
{
	struct gpt_partition	*p1 = *(struct gpt_partition **)e1;
	struct gpt_partition	*p2 = *(struct gpt_partition **)e2;
	uint64_t		 o1;
	uint64_t		 o2;

	o1 = p1->gp_lba_start;
	o2 = p2->gp_lba_start;

	if (o1 < o2)
		return -1;
	else if (o1 > o2)
		return 1;
	else
		return 0;
}

const struct gpt_partition * const *
sort_gpt(void)
{
	static const struct gpt_partition	*sgp[NGPTPARTITIONS+2];
	unsigned int				 i, pn;

	memset(sgp, 0, sizeof(sgp));

	i = 0;
	for (pn = 0; pn < gh.gh_part_num; pn++) {
		if (gp[pn].gp_lba_start >= gh.gh_lba_start)
			sgp[i++] = &gp[pn];
	}

	if (i > 1) {
		if (mergesort(sgp, i, sizeof(sgp[0]), gp_lba_start_cmp) == -1) {
			printf("unable to sort gpt by lba start\n");
			return NULL;
		}
	}

	return sgp;
}

int
lba_free(uint64_t *start, uint64_t *end)
{
	const struct gpt_partition * const *sgp;
	uint64_t			  bs, bigbs, nextbs, ns;
	unsigned int			  i;

	sgp = sort_gpt();
	if (sgp == NULL)
		return -1;

	bs = gh.gh_lba_start;
	ns = gh.gh_lba_end - bs + 1;

	if (sgp[0] != NULL) {
		bigbs = bs;
		ns = 0;
		for (i = 0; sgp[i] != NULL; i++) {
			nextbs = sgp[i]->gp_lba_start;
			if (bs < nextbs && ns < nextbs - bs) {
				ns = nextbs - bs;
				bigbs = bs;
			}
			bs = sgp[i]->gp_lba_end + 1;
		}
		nextbs = gh.gh_lba_end + 1;
		if (bs < nextbs && ns < nextbs - bs) {
			ns = nextbs - bs;
			bigbs = bs;
		}
		bs = bigbs;
	}

	if (ns == 0)
		return -1;

	if (start != NULL)
		*start = bs;
	if (end != NULL)
		*end = bs + ns - 1;

	return 0;
}

int
GPT_get_lba_start(const unsigned int pn)
{
	uint64_t		bs;
	unsigned int		i;
	int			rslt;

	bs = gh.gh_lba_start;

	if (gp[pn].gp_lba_start >= bs) {
		bs = gp[pn].gp_lba_start;
	} else {
		rslt = lba_free(&bs, NULL);
		if (rslt == -1) {
			printf("no space for partition %u\n", pn);
			return -1;
		}
	}

	bs = getuint64("Partition offset", bs, gh.gh_lba_start, gh.gh_lba_end);
	for (i = 0; i < gh.gh_part_num; i++) {
		if (i == pn)
			continue;
		if (bs >= gp[i].gp_lba_start && bs <= gp[i].gp_lba_end) {
			printf("partition %u can't start inside partition %u\n",
			    pn, i);
			return -1;
		}
	}

	gp[pn].gp_lba_start = bs;

	return 0;
}

int
GPT_get_lba_end(const unsigned int pn)
{
	const struct gpt_partition	* const *sgp;
	uint64_t			  bs, nextbs, ns;
	unsigned int			  i;

	sgp = sort_gpt();
	if (sgp == NULL)
		return -1;

	bs = gp[pn].gp_lba_start;
	ns = gh.gh_lba_end - bs + 1;
	for (i = 0; sgp[i] != NULL; i++) {
		nextbs = sgp[i]->gp_lba_start;
		if (nextbs > bs) {
			ns = nextbs - bs;
			break;
		}
	}
	ns = getuint64("Partition size", ns, 1, ns);

	gp[pn].gp_lba_end = bs + ns - 1;

	return 0;
}

int
GPT_get_name(const unsigned int pn)
{
	char			 name[GPTPARTNAMESIZE + 1];

	printf("Partition name: [%s] ", name_to_string(pn));
	string_from_line(name, sizeof(name), UNTRIMMED);

	switch (strlen(name)) {
	case 0:
		break;
	case GPTPARTNAMESIZE:
		printf("partition name must be < %d characters\n",
		    GPTPARTNAMESIZE);
		return -1;
	default:
		string_to_name(pn, name);
		break;
	}

	return 0;
}

/*
 * Adapted from Hacker's Delight crc32b().
 *
 * To quote http://www.hackersdelight.org/permissions.htm :
 *
 * "You are free to use, copy, and distribute any of the code on
 *  this web site, whether modified by you or not. You need not give
 *  attribution. This includes the algorithms (some of which appear
 *  in Hacker's Delight), the Hacker's Assistant, and any code submitted
 *  by readers. Submitters implicitly agree to this."
 */
uint32_t
crc32(const u_char *buf, const uint32_t size)
{
	int			j;
	uint32_t		i, byte, crc, mask;

	crc = 0xFFFFFFFF;

	for (i = 0; i < size; i++) {
		byte = buf[i];			/* Get next byte. */
		crc = crc ^ byte;
		for (j = 7; j >= 0; j--) {	/* Do eight times. */
			mask = -(crc & 1);
			crc = (crc >> 1) ^ (0xEDB88320 & mask);
		}
	}

	return ~crc;
}
