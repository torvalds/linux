/*-
 * Copyright (c) 2017-2018 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/vtoc.h>
#include <sys/param.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgeom.h>
#include <paths.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <libgeom.h>
#include <geom/geom.h>
#include <geom/geom_ctl.h>
#include <geom/geom_int.h>

#include <efivar.h>
#include <efiutil.h>
#include <efichar.h>
#include <efivar-dp.h>

#ifndef LOAD_OPTION_ACTIVE
#define LOAD_OPTION_ACTIVE 0x00000001
#endif

#ifndef LOAD_OPTION_CATEGORY_BOOT
#define LOAD_OPTION_CATEGORY_BOOT 0x00000000
#endif

#define BAD_LENGTH	((size_t)-1)

typedef struct _bmgr_opts {
	char	*env;
	char	*loader;
	char	*label;
	char	*kernel;
	char	*name;
	char	*order;
	int     bootnum;
	bool	copy;
	bool    create;
	bool    delete;
	bool    delete_bootnext;
	bool    del_timeout;
	bool    dry_run;
	bool	has_bootnum;
	bool    once;
	int	cp_src;
	bool    set_active;
	bool    set_bootnext;
	bool    set_inactive;
	bool    set_timeout;
	int     timeout;
	bool    verbose;
} bmgr_opts_t;

static struct option lopts[] = {
	{"activate", no_argument, NULL, 'a'},
	{"bootnext", no_argument, NULL, 'n'}, /* set bootnext */
	{"bootnum", required_argument, NULL, 'b'},
	{"bootorder", required_argument, NULL, 'o'}, /* set order */
	{"copy", required_argument, NULL, 'C'},		/* Copy boot method */
	{"create", no_argument, NULL, 'c'},
	{"deactivate", no_argument, NULL, 'A'},
	{"del-timout", no_argument, NULL, 'T'},
	{"delete", no_argument, NULL, 'B'},
	{"delete-bootnext", no_argument, NULL, 'N'},
	{"dry-run", no_argument, NULL, 'D'},
	{"env", required_argument, NULL, 'e'},
	{"help", no_argument, NULL, 'h'},
	{"kernel", required_argument, NULL, 'k'},
	{"label", required_argument, NULL, 'L'},
	{"loader", required_argument, NULL, 'l'},
	{"once", no_argument, NULL, 'O'},
	{"set-timeout", required_argument, NULL, 't'},
	{"verbose", no_argument, NULL, 'v'},
	{ NULL, 0, NULL, 0}
};

/* global efibootmgr opts */
static bmgr_opts_t opts;

static LIST_HEAD(efivars_head, entry) efivars =
	LIST_HEAD_INITIALIZER(efivars);

struct entry {
	efi_guid_t	guid;
	uint32_t	attrs;
	uint8_t		*data;
	size_t		size;
	char		*name;
	char		*label;
	int		idx;
	int		flags;
#define SEEN	1

	LIST_ENTRY(entry) entries;
};

#define MAX_DP_LEN	4096
#define MAX_LOADOPT_LEN	8192


static char *
mangle_loader(char *loader)
{
	char *c;

	for (c = loader; *c; c++)
		if (*c == '/')
			*c = '\\';

	return loader;
}


#define COMMON_ATTRS EFI_VARIABLE_NON_VOLATILE | \
	EFI_VARIABLE_BOOTSERVICE_ACCESS | \
	EFI_VARIABLE_RUNTIME_ACCESS

/*
 * We use global guid, and common var attrs and
 * find it better to just delete and re-create a var.
 */
static int
set_bootvar(const char *name, uint8_t *data, size_t size)
{

	return efi_set_variable(EFI_GLOBAL_GUID, name, data, size,
	    COMMON_ATTRS);
}


#define USAGE \
	"   [-aAnB -b bootnum] [-N] [-t timeout] [-T] [-o bootorder] [-O] [--verbose] [--help]\n\
  [-c -l loader [-k kernel] [-L label] [--dry-run] [-b bootnum]]"

#define CREATE_USAGE \
	"       efibootmgr -c -l loader [-k kernel] [-L label] [--dry-run] [-b bootnum] [-a]"
#define ORDER_USAGE \
	"       efibootmgr -o bootvarnum1,bootvarnum2,..."
#define TIMEOUT_USAGE \
	"       efibootmgr -t seconds"
#define DELETE_USAGE \
	"       efibootmgr -B -b bootnum"
#define ACTIVE_USAGE \
	"       efibootmgr [-a | -A] -b bootnum"
#define BOOTNEXT_USAGE \
	"       efibootmgr [-n | -N] -b bootnum"

static void
parse_args(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt_long(argc, argv, "AaBb:C:cDe:hk:L:l:NnOo:Tt:v",
		    lopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
			opts.set_inactive = true;
			break;
		case 'a':
			opts.set_active = true;
			break;
		case 'b':
			opts.has_bootnum = true;
			opts.bootnum = strtoul(optarg, NULL, 16);
			break;
		case 'B':
			opts.delete = true;
			break;
		case 'C':
			opts.copy = true;
			opts.cp_src = strtoul(optarg, NULL, 16);
		case 'c':
			opts.create = true;
			break;
		case 'D': /* should be remove dups XXX */
			opts.dry_run = true;
			break;
		case 'e':
			free(opts.env);
			opts.env = strdup(optarg);
			break;
		case 'h':
		default:
			errx(1, "%s", USAGE);
			break;
		case 'k':
			free(opts.kernel);
			opts.kernel = strdup(optarg);
			break;
		case 'L':
			free(opts.label);
			opts.label = strdup(optarg);
			break;
		case 'l':
			free(opts.loader);
			opts.loader = strdup(optarg);
			opts.loader = mangle_loader(opts.loader);
			break;
		case 'N':
			opts.delete_bootnext = true;
			break;
		case 'n':
			opts.set_bootnext = true;
			break;
		case 'O':
			opts.once = true;
			break;
		case 'o':
			free(opts.order);
			opts.order = strdup(optarg);
			break;
		case 'T':
			opts.del_timeout = true;
			break;
		case 't':
			opts.set_timeout = true;
			opts.timeout = strtoul(optarg, NULL, 10);
			break;
		case 'v':
			opts.verbose = true;
			break;
		}
	}
	if (opts.create) {
		if (!opts.loader)
			errx(1, "%s",CREATE_USAGE);
		return;
	}

	if (opts.order && !(opts.order))
		errx(1, "%s", ORDER_USAGE);

	if ((opts.set_inactive || opts.set_active) && !opts.has_bootnum)
		errx(1, "%s", ACTIVE_USAGE);

	if (opts.delete && !opts.has_bootnum)
		errx(1, "%s", DELETE_USAGE);

	if (opts.set_bootnext && !opts.has_bootnum)
		errx(1, "%s", BOOTNEXT_USAGE);
}


static void
read_vars(void)
{

	efi_guid_t *guid;
	char *next_name = NULL;
	int ret = 0;

	struct entry *nent;

	LIST_INIT(&efivars);
	while ((ret = efi_get_next_variable_name(&guid, &next_name)) > 0) {
		/*
		 * Only pay attention to EFI:BootXXXX variables to get the list.
		 */
		if (efi_guid_cmp(guid, &EFI_GLOBAL_GUID) != 0 ||
		    strlen(next_name) != 8 ||
		    strncmp(next_name, "Boot", 4) != 0 ||
		    !isxdigit(next_name[4]) ||
		    !isxdigit(next_name[5]) ||
		    !isxdigit(next_name[6]) ||
		    !isxdigit(next_name[7]))
			continue;
		nent = malloc(sizeof(struct entry));
		nent->name = strdup(next_name);

		ret = efi_get_variable(*guid, next_name, &nent->data,
		    &nent->size, &nent->attrs);
		if (ret < 0)
			err(1, "efi_get_variable");
		nent->guid = *guid;
		nent->idx = strtoul(&next_name[4], NULL, 16);
		LIST_INSERT_HEAD(&efivars, nent, entries);
	}
}


static void
set_boot_order(char *order)
{
	uint16_t *new_data;
	size_t size;
	char *next, *cp;
	int cnt;
	int i;

	cp = order;
	cnt = 1;
	while (*cp) {
		if (*cp++ == ',')
			cnt++;
	}
	size = sizeof(uint16_t) * cnt;
	new_data = malloc(size);

	i = 0;
	cp = strdup(order);
	while ((next = strsep(&cp, ",")) != NULL) {
		new_data[i] = strtoul(next, NULL, 16);
		if (new_data[i] == 0 && errno == EINVAL) {
			warnx("can't parse %s as a numb", next);
			errx(1, "%s", ORDER_USAGE);
		}
		i++;
	}
	free(cp);
	if (set_bootvar("BootOrder", (uint8_t*)new_data, size) < 0)
		err(1, "Unabke to set BootOrder to %s", order);
	free(new_data);
}

static void
handle_activity(int bootnum, bool active)
{
	uint32_t attrs, load_attrs;
	uint8_t *data;
	size_t size;
	char *name;

	asprintf(&name, "%s%04X", "Boot", bootnum);
	if (name == NULL)
		err(1, "asprintf");
	if (efi_get_variable(EFI_GLOBAL_GUID, name, &data, &size, &attrs) < 0)
		err(1, "No such bootvar %s\n", name);

	load_attrs = le32dec(data);

	if (active)
		load_attrs |= LOAD_OPTION_ACTIVE;
	else
		load_attrs &= ~LOAD_OPTION_ACTIVE;

	le32enc(data, load_attrs);

	if (set_bootvar(name, data, size) < 0)
		err(1, "handle activity efi_set_variable");
}


/*
 * add boot var to boot order.
 * called by create boot var. There is no option
 * to add one independent of create.
 *
 * Note: we currently don't support where it goes
 * so it goes on the front, inactive.
 * use -o 2,3,7 etc to affect order, -a to activate.
 */
static void
add_to_boot_order(char *bootvar)
{
	size_t size;
	uint32_t attrs;
	uint16_t val;
	uint8_t *data, *new;

	val = strtoul(&bootvar[4], NULL, 16);

	if (efi_get_variable(EFI_GLOBAL_GUID, "BootOrder", &data, &size, &attrs) < 0) {
		if (errno == ENOENT) { /* create it and set this bootvar to active */
			size = 0;
			data = NULL;
		} else
			err(1, "efi_get_variable BootOrder");
	}

	/*
	 * We have BootOrder with the current order
	 * so grow the array by one, add the value
	 * and write the new variable value.
	 */
	size += sizeof(uint16_t);
	new = malloc(size);
	if (!new)
		err(1, "malloc");

	le16enc(new, val);
	if (size > sizeof(uint16_t))
		memcpy(new + sizeof(uint16_t), data, size - sizeof(uint16_t));

	if (set_bootvar("BootOrder", new, size) < 0)
		err(1, "set_bootvar");
	free(new);
}


static void
remove_from_order(uint16_t bootnum)
{
	uint32_t attrs;
	size_t size, i, j;
	uint8_t *new, *data;

	if (efi_get_variable(EFI_GLOBAL_GUID, "BootOrder", &data, &size, &attrs) < 0)
		return;

	new = malloc(size);
	if (new == NULL)
		err(1, "malloc");

	for (j = i = 0; i < size; i += sizeof(uint16_t)) {
		if (le16dec(data + i) == bootnum)
			continue;
		memcpy(new + j, data + i, sizeof(uint16_t));
		j += sizeof(uint16_t);
	}
	if (i == j)
		warnx("Boot variable %04x not in BootOrder", bootnum);
	else if (set_bootvar("BootOrder", new, j) < 0)
		err(1, "Unable to update BootOrder with new value");
	free(new);
}


static void
delete_bootvar(int bootnum)
{
	char *name;
	int defer = 0;

	/*
	 * Try to delete the boot variable and remocve it
	 * from the boot order. We always do both actions
	 * to make it easy to clean up from oopses.
	 */
	if (bootnum < 0 || bootnum > 0xffff)
		errx(1, "Bad boot variable %#x", bootnum);
	asprintf(&name, "%s%04X", "Boot", bootnum);
	if (name == NULL)
		err(1, "asprintf");
	printf("Removing boot variable '%s'\n", name);
	if (efi_del_variable(EFI_GLOBAL_GUID, name) < 0) {
		defer = 1;
		warn("cannot delete variable %s", name);
	}
	printf("Removing 0x%x from BootOrder\n", bootnum);
	remove_from_order(bootnum);
	free(name);
	if (defer)
		exit(defer);
}


static void
del_bootnext(void)
{

	if (efi_del_variable(EFI_GLOBAL_GUID, "BootNext") < 0)
		err(1, "efi_del_variable");
}

static void
handle_bootnext(uint16_t bootnum)
{
	uint16_t num;

	le16enc(&num, bootnum);
	if (set_bootvar("BootNext", (uint8_t*)&bootnum, sizeof(uint16_t)) < 0)
		err(1, "set_bootvar");
}


static int
compare(const void *a, const void *b)
{
	uint16_t c;
	uint16_t d;

	memcpy(&c, a, sizeof(uint16_t));
	memcpy(&d, b, sizeof(uint16_t));

	if (c < d)
		return -1;
	if (c == d)
		return  0;
	return  1;
}

static char *
make_next_boot_var_name(void)
{
	struct entry *v;
	uint16_t *vals, next_free = 0;
	char *name;
	int cnt = 0;
	int i;

	LIST_FOREACH(v, &efivars, entries) {
		cnt++;
	}

	vals = malloc(sizeof(uint16_t) * cnt);
	if (!vals)
		return NULL;

	i = 0;
	LIST_FOREACH(v, &efivars, entries) {
		vals[i++] = v->idx;
	}
	qsort(vals, cnt, sizeof(uint16_t), compare);
	/* if the hole is at the beginning, just return zero */
	if (vals[0] > 0) {
		next_free = 0;
	} else {
		/* now just run the list looking for the first hole */
		for (i = 0; i < cnt - 1 && next_free == 0; i++)
			if (vals[i] + 1 != vals[i + 1])
				next_free = vals[i] + 1;
		if (next_free == 0)
			next_free = vals[cnt - 1] + 1;
		/* In theory we could have used all 65k slots -- what to do? */
	}
	free(vals);

	asprintf(&name, "%s%04X", "Boot", next_free);
	if (name == NULL)
		err(1, "asprintf");
	return name;
}

static char *
make_boot_var_name(uint16_t bootnum)
{
	struct entry *v;
	char *name;

	LIST_FOREACH(v, &efivars, entries) {
		if (v->idx == bootnum)
			return NULL;
	}

	asprintf(&name, "%s%04X", "Boot", bootnum);
	if (name == NULL)
		err(1, "asprintf");
	return name;
}

static size_t
create_loadopt(uint8_t *buf, size_t bufmax, uint32_t attributes, efidp dp, size_t dp_size,
    const char *description, const uint8_t *optional_data, size_t optional_data_size)
{
	efi_char *bbuf = NULL;
	uint8_t *pos = buf;
	size_t desc_len = 0;
	size_t len;

	if (optional_data == NULL && optional_data_size != 0)
		return BAD_LENGTH;
	if (dp == NULL && dp_size != 0)
		return BAD_LENGTH;

	/*
	 * Compute the length to make sure the passed in buffer is long enough.
	 */
	utf8_to_ucs2(description, &bbuf, &desc_len);
	len = sizeof(uint32_t) + sizeof(uint16_t) + desc_len + dp_size + optional_data_size;
	if (len > bufmax) {
		free(bbuf);
		return BAD_LENGTH;
	}

	le32enc(pos, attributes);
	pos += sizeof (attributes);

	le16enc(pos, dp_size);
	pos += sizeof (uint16_t);

	memcpy(pos, bbuf, desc_len);	/* NB:desc_len includes strailing NUL */
	pos += desc_len;
	free(bbuf);

	memcpy(pos, dp, dp_size);
	pos += dp_size;

	if (optional_data && optional_data_size > 0) {
		memcpy(pos, optional_data, optional_data_size);
		pos += optional_data_size;
	}

	return pos - buf;
}


static int
make_boot_var(const char *label, const char *loader, const char *kernel, const char *env, bool dry_run,
    int bootnum, bool activate)
{
	struct entry *new_ent;
	uint32_t load_attrs = 0;
	uint8_t *load_opt_buf;
	size_t lopt_size, llen, klen;
	efidp dp, loaderdp, kerneldp;
	char *bootvar = NULL;
	int ret;

	assert(label != NULL);

	if (bootnum == -1)
		bootvar = make_next_boot_var_name();
	else
		bootvar = make_boot_var_name((uint16_t)bootnum);
	if (bootvar == NULL)
		err(1, "bootvar creation");
	if (loader == NULL)
		errx(1, "Must specify boot loader");
	if (efivar_unix_path_to_device_path(loader, &loaderdp) != 0)
		err(1, "Cannot translate unix loader path '%s' to UEFI", loader);
	if (kernel != NULL) {
		if (efivar_unix_path_to_device_path(kernel, &kerneldp) != 0)
			err(1, "Cannot translate unix kernel path '%s' to UEFI", kernel);
	} else {
		kerneldp = NULL;
	}
	llen = efidp_size(loaderdp);
	if (llen > MAX_DP_LEN)
		errx(1, "Loader path too long.");
	klen = efidp_size(kerneldp);
	if (klen > MAX_DP_LEN)
		errx(1, "Kernel path too long.");
	dp = malloc(llen + klen);
	if (dp == NULL)
		errx(1, "Can't allocate memory for new device paths");
	memcpy(dp, loaderdp, llen);
	if (kerneldp != NULL)
		memcpy((char *)dp + llen, kerneldp, klen);

	/* don't make the new bootvar active by default, use the -a option later */
	load_attrs = LOAD_OPTION_CATEGORY_BOOT;
	if (activate)
		load_attrs |= LOAD_OPTION_ACTIVE;
	load_opt_buf = malloc(MAX_LOADOPT_LEN);
	if (load_opt_buf == NULL)
		err(1, "malloc");

	lopt_size = create_loadopt(load_opt_buf, MAX_LOADOPT_LEN, load_attrs,
	    dp, llen + klen, label, env, env ? strlen(env) + 1 : 0);
	if (lopt_size == BAD_LENGTH)
		errx(1, "Can't crate loadopt");

	ret = 0;
	if (!dry_run) {
		ret = efi_set_variable(EFI_GLOBAL_GUID, bootvar,
		    (uint8_t*)load_opt_buf, lopt_size, COMMON_ATTRS);
	}

	if (ret)
		err(1, "efi_set_variable");

	add_to_boot_order(bootvar); /* first, still not active */
	new_ent = malloc(sizeof(struct entry));
	if (new_ent == NULL)
		err(1, "malloc");
	memset(new_ent, 0, sizeof(struct entry));
	new_ent->name = bootvar;
	new_ent->guid = EFI_GLOBAL_GUID;
	LIST_INSERT_HEAD(&efivars, new_ent, entries);
	free(load_opt_buf);
	free(dp);

	return 0;
}


static void
print_loadopt_str(uint8_t *data, size_t datalen)
{
	char *dev, *relpath, *abspath;
	uint32_t attr;
	uint16_t fplen;
	efi_char *descr;
	uint8_t *ep = data + datalen;
	uint8_t *walker = data;
	efidp dp, edp;
	char buf[1024];
	int len;
	int rv;
	int indent;

	if (datalen < sizeof(attr) + sizeof(fplen) + sizeof(efi_char))
		return;
	// First 4 bytes are attribute flags
	attr = le32dec(walker);
	walker += sizeof(attr);
	// Next two bytes are length of the file paths
	fplen = le16dec(walker);
	walker += sizeof(fplen);
	// Next we have a 0 terminated UCS2 string that we know to be aligned
	descr = (efi_char *)(intptr_t)(void *)walker;
	len = ucs2len(descr); // XXX need to sanity check that len < (datalen - (ep - walker) / 2)
	walker += (len + 1) * sizeof(efi_char);
	if (walker > ep)
		return;
	// Now we have fplen bytes worth of file path stuff
	dp = (efidp)walker;
	walker += fplen;
	if (walker > ep)
		return;
	edp = (efidp)walker;
	/*
	 * Everything left is the binary option args
	 * opt = walker;
	 * optlen = ep - walker;
	 */
	indent = 1;
	while (dp < edp) {
		efidp_format_device_path(buf, sizeof(buf), dp,
		    (intptr_t)(void *)edp - (intptr_t)(void *)dp);
		printf("%*s%s\n", indent, "", buf);
		indent = 10 + len + 1;
		rv = efivar_device_path_to_unix_path(dp, &dev, &relpath, &abspath);
		if (rv == 0) {
			printf("%*s%s:%s %s\n", indent + 4, "", dev, relpath, abspath);
			free(dev);
			free(relpath);
			free(abspath);
		}
		dp = (efidp)((char *)dp + efidp_size(dp));
	}
}

static char *
get_descr(uint8_t *data)
{
	uint8_t *pos = data;
	efi_char *desc;
	int  len;
	char *buf;
	int i = 0;

	pos += sizeof(uint32_t) + sizeof(uint16_t);
	desc = (efi_char*)(intptr_t)(void *)pos;
	len = ucs2len(desc);
	buf = malloc(len + 1);
	memset(buf, 0, len + 1);
	while (desc[i]) {
		buf[i] = desc[i];
		i++;
	}
	return (char*)buf;
}


static bool
print_boot_var(const char *name, bool verbose, bool curboot)
{
	size_t size;
	uint32_t load_attrs;
	uint8_t *data;
	int ret;
	char *d;

	ret = efi_get_variable(EFI_GLOBAL_GUID, name, &data, &size, NULL);
	if (ret < 0)
		return false;
	load_attrs = le32dec(data);
	d = get_descr(data);
	printf("%c%s%c %s", curboot ? '+' : ' ', name,
	    ((load_attrs & LOAD_OPTION_ACTIVE) ? '*': ' '), d);
	free(d);
	if (verbose)
		print_loadopt_str(data, size);
	else
		printf("\n");
	return true;
}


/* Cmd epilogue, or just the default with no args.
 * The order is [bootnext] bootcurrent, timeout, order, and the bootvars [-v]
 */
static int
print_boot_vars(bool verbose)
{
	/*
	 * just read and print the current values
	 * as a command epilogue
	 */
	struct entry *v;
	uint8_t *data;
	size_t size;
	uint32_t attrs;
	int ret, bolen;
	uint16_t *boot_order = NULL, current;

	ret = efi_get_variable(EFI_GLOBAL_GUID, "BootNext", &data, &size, &attrs);
	if (ret > 0) {
		printf("BootNext : %04x\n", le16dec(data));
	}

	ret = efi_get_variable(EFI_GLOBAL_GUID, "BootCurrent", &data, &size,&attrs);
	current = le16dec(data);
	printf("BootCurrent: %04x\n", current);

	ret = efi_get_variable(EFI_GLOBAL_GUID, "Timeout", &data, &size, &attrs);
	if (ret > 0) {
		printf("Timeout    : %d seconds\n", le16dec(data));
	}

	if (efi_get_variable(EFI_GLOBAL_GUID, "BootOrder", &data, &size, &attrs) > 0) {
		if (size % 2 == 1)
			warn("Bad BootOrder variable: odd length %d", (int)size);
		boot_order = malloc(size);
		bolen = size / 2;
		printf("BootOrder  : ");
		for (size_t i = 0; i < size; i += 2) {
			boot_order[i / 2] = le16dec(data + i);
			printf("%04X%s", boot_order[i / 2], i == size - 2 ? "\n" : ", ");
		}
	}

	if (boot_order == NULL) {
		/*
		 * now we want to fetch 'em all fresh again
		 * which possibly includes a newly created bootvar
		 */
		LIST_FOREACH(v, &efivars, entries) {
			print_boot_var(v->name, verbose, v->idx == current);
		}
	} else {
		LIST_FOREACH(v, &efivars, entries) {
			v->flags = 0;
		}
		for (int i = 0; i < bolen; i++) {
			char buffer[10];

			snprintf(buffer, sizeof(buffer), "Boot%04X", boot_order[i]);
			if (!print_boot_var(buffer, verbose, boot_order[i] == current))
				printf("%s: MISSING!\n", buffer);
			LIST_FOREACH(v, &efivars, entries) {
				if (v->idx == boot_order[i]) {
					v->flags |= SEEN;
					break;
				}
			}
		}
		if (verbose) {
			printf("\n\nUnreferenced Variables:\n");
			LIST_FOREACH(v, &efivars, entries) {
				if (v->flags == 0)
					print_boot_var(v->name, verbose, v->idx == current);
			}
		}
	}
	return 0;
}

static void
delete_timeout(void)
{

	efi_del_variable(EFI_GLOBAL_GUID,"Timeout");
}

static void
handle_timeout(int to)
{
	uint16_t timeout;

	le16enc(&timeout, to);
	if (set_bootvar("Timeout", (uint8_t *)&timeout, sizeof(timeout)) < 0)
		errx(1, "Can't set Timeout for booting.");
}

int
main(int argc, char *argv[])
{

	if (!efi_variables_supported())
		errx(1, "efi variables not supported on this system. root? kldload efirt?");

	memset(&opts, 0, sizeof (bmgr_opts_t));
	parse_args(argc, argv);
	read_vars();

	if (opts.create)
		/*
		 * side effect, adds to boot order, but not yet active.
		 */
		make_boot_var(opts.label ? opts.label : "",
		    opts.loader, opts.kernel, opts.env, opts.dry_run,
		    opts.has_bootnum ? opts.bootnum : -1, opts.set_active);
	else if (opts.set_active || opts.set_inactive )
		handle_activity(opts.bootnum, opts.set_active);
	else if (opts.order != NULL)
		set_boot_order(opts.order); /* create a new bootorder with opts.order */
	else if (opts.set_bootnext)
		handle_bootnext(opts.bootnum);
	else if (opts.delete_bootnext)
		del_bootnext();
	else if (opts.delete)
		delete_bootvar(opts.bootnum);
	else if (opts.del_timeout)
		delete_timeout();
	else if (opts.set_timeout)
		handle_timeout(opts.timeout);

	print_boot_vars(opts.verbose);
}
