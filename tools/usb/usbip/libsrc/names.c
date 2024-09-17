// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      names.c  --  USB name database manipulation routines
 *
 *      Copyright (C) 1999, 2000  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	Copyright (C) 2005 Takahiro Hirofuchi
 *		- names_deinit() is added.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#include "names.h"
#include "usbip_common.h"

struct vendor {
	struct vendor *next;
	u_int16_t vendorid;
	char name[1];
};

struct product {
	struct product *next;
	u_int16_t vendorid, productid;
	char name[1];
};

struct class {
	struct class *next;
	u_int8_t classid;
	char name[1];
};

struct subclass {
	struct subclass *next;
	u_int8_t classid, subclassid;
	char name[1];
};

struct protocol {
	struct protocol *next;
	u_int8_t classid, subclassid, protocolid;
	char name[1];
};

struct genericstrtable {
	struct genericstrtable *next;
	unsigned int num;
	char name[1];
};


#define HASH1  0x10
#define HASH2  0x02
#define HASHSZ 16

static unsigned int hashnum(unsigned int num)
{
	unsigned int mask1 = HASH1 << 27, mask2 = HASH2 << 27;

	for (; mask1 >= HASH1; mask1 >>= 1, mask2 >>= 1)
		if (num & mask1)
			num ^= mask2;
	return num & (HASHSZ-1);
}


static struct vendor *vendors[HASHSZ] = { NULL, };
static struct product *products[HASHSZ] = { NULL, };
static struct class *classes[HASHSZ] = { NULL, };
static struct subclass *subclasses[HASHSZ] = { NULL, };
static struct protocol *protocols[HASHSZ] = { NULL, };

const char *names_vendor(u_int16_t vendorid)
{
	struct vendor *v;

	v = vendors[hashnum(vendorid)];
	for (; v; v = v->next)
		if (v->vendorid == vendorid)
			return v->name;
	return NULL;
}

const char *names_product(u_int16_t vendorid, u_int16_t productid)
{
	struct product *p;

	p = products[hashnum((vendorid << 16) | productid)];
	for (; p; p = p->next)
		if (p->vendorid == vendorid && p->productid == productid)
			return p->name;
	return NULL;
}

const char *names_class(u_int8_t classid)
{
	struct class *c;

	c = classes[hashnum(classid)];
	for (; c; c = c->next)
		if (c->classid == classid)
			return c->name;
	return NULL;
}

const char *names_subclass(u_int8_t classid, u_int8_t subclassid)
{
	struct subclass *s;

	s = subclasses[hashnum((classid << 8) | subclassid)];
	for (; s; s = s->next)
		if (s->classid == classid && s->subclassid == subclassid)
			return s->name;
	return NULL;
}

const char *names_protocol(u_int8_t classid, u_int8_t subclassid,
			   u_int8_t protocolid)
{
	struct protocol *p;

	p = protocols[hashnum((classid << 16) | (subclassid << 8)
			      | protocolid)];
	for (; p; p = p->next)
		if (p->classid == classid && p->subclassid == subclassid &&
		    p->protocolid == protocolid)
			return p->name;
	return NULL;
}

/* add a cleanup function by takahiro */
struct pool {
	struct pool *next;
	void *mem;
};

static struct pool *pool_head;

static void *my_malloc(size_t size)
{
	struct pool *p;

	p = calloc(1, sizeof(struct pool));
	if (!p)
		return NULL;

	p->mem = calloc(1, size);
	if (!p->mem) {
		free(p);
		return NULL;
	}

	p->next = pool_head;
	pool_head = p;

	return p->mem;
}

void names_free(void)
{
	struct pool *pool;

	if (!pool_head)
		return;

	for (pool = pool_head; pool != NULL; ) {
		struct pool *tmp;

		if (pool->mem)
			free(pool->mem);

		tmp = pool;
		pool = pool->next;
		free(tmp);
	}
}

static int new_vendor(const char *name, u_int16_t vendorid)
{
	struct vendor *v;
	unsigned int h = hashnum(vendorid);

	v = vendors[h];
	for (; v; v = v->next)
		if (v->vendorid == vendorid)
			return -1;
	v = my_malloc(sizeof(struct vendor) + strlen(name));
	if (!v)
		return -1;
	strcpy(v->name, name);
	v->vendorid = vendorid;
	v->next = vendors[h];
	vendors[h] = v;
	return 0;
}

static int new_product(const char *name, u_int16_t vendorid,
		       u_int16_t productid)
{
	struct product *p;
	unsigned int h = hashnum((vendorid << 16) | productid);

	p = products[h];
	for (; p; p = p->next)
		if (p->vendorid == vendorid && p->productid == productid)
			return -1;
	p = my_malloc(sizeof(struct product) + strlen(name));
	if (!p)
		return -1;
	strcpy(p->name, name);
	p->vendorid = vendorid;
	p->productid = productid;
	p->next = products[h];
	products[h] = p;
	return 0;
}

static int new_class(const char *name, u_int8_t classid)
{
	struct class *c;
	unsigned int h = hashnum(classid);

	c = classes[h];
	for (; c; c = c->next)
		if (c->classid == classid)
			return -1;
	c = my_malloc(sizeof(struct class) + strlen(name));
	if (!c)
		return -1;
	strcpy(c->name, name);
	c->classid = classid;
	c->next = classes[h];
	classes[h] = c;
	return 0;
}

static int new_subclass(const char *name, u_int8_t classid, u_int8_t subclassid)
{
	struct subclass *s;
	unsigned int h = hashnum((classid << 8) | subclassid);

	s = subclasses[h];
	for (; s; s = s->next)
		if (s->classid == classid && s->subclassid == subclassid)
			return -1;
	s = my_malloc(sizeof(struct subclass) + strlen(name));
	if (!s)
		return -1;
	strcpy(s->name, name);
	s->classid = classid;
	s->subclassid = subclassid;
	s->next = subclasses[h];
	subclasses[h] = s;
	return 0;
}

static int new_protocol(const char *name, u_int8_t classid, u_int8_t subclassid,
			u_int8_t protocolid)
{
	struct protocol *p;
	unsigned int h = hashnum((classid << 16) | (subclassid << 8)
				 | protocolid);

	p = protocols[h];
	for (; p; p = p->next)
		if (p->classid == classid && p->subclassid == subclassid
		    && p->protocolid == protocolid)
			return -1;
	p = my_malloc(sizeof(struct protocol) + strlen(name));
	if (!p)
		return -1;
	strcpy(p->name, name);
	p->classid = classid;
	p->subclassid = subclassid;
	p->protocolid = protocolid;
	p->next = protocols[h];
	protocols[h] = p;
	return 0;
}

static void parse(FILE *f)
{
	char buf[512], *cp;
	unsigned int linectr = 0;
	int lastvendor = -1;
	int lastclass = -1;
	int lastsubclass = -1;
	int lasthut = -1;
	int lastlang = -1;
	unsigned int u;

	while (fgets(buf, sizeof(buf), f)) {
		linectr++;
		/* remove line ends */
		cp = strchr(buf, '\r');
		if (cp)
			*cp = 0;
		cp = strchr(buf, '\n');
		if (cp)
			*cp = 0;
		if (buf[0] == '#' || !buf[0])
			continue;
		cp = buf;
		if (buf[0] == 'P' && buf[1] == 'H' && buf[2] == 'Y' &&
		    buf[3] == 'S' && buf[4] == 'D' &&
		    buf[5] == 'E' && buf[6] == 'S' && /*isspace(buf[7])*/
		    buf[7] == ' ') {
			continue;
		}
		if (buf[0] == 'P' && buf[1] == 'H' &&
		    buf[2] == 'Y' && /*isspace(buf[3])*/ buf[3] == ' ') {
			continue;
		}
		if (buf[0] == 'B' && buf[1] == 'I' && buf[2] == 'A' &&
		    buf[3] == 'S' && /*isspace(buf[4])*/ buf[4] == ' ') {
			continue;
		}
		if (buf[0] == 'L' && /*isspace(buf[1])*/ buf[1] == ' ') {
			lasthut = lastclass = lastvendor = lastsubclass = -1;
			/*
			 * set 1 as pseudo-id to indicate that the parser is
			 * in a `L' section.
			 */
			lastlang = 1;
			continue;
		}
		if (buf[0] == 'C' && /*isspace(buf[1])*/ buf[1] == ' ') {
			/* class spec */
			cp = buf+2;
			while (isspace(*cp))
				cp++;
			if (!isxdigit(*cp)) {
				err("Invalid class spec at line %u", linectr);
				continue;
			}
			u = strtoul(cp, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				err("Invalid class spec at line %u", linectr);
				continue;
			}
			if (new_class(cp, u))
				err("Duplicate class spec at line %u class %04x %s",
				    linectr, u, cp);
			dbg("line %5u class %02x %s", linectr, u, cp);
			lasthut = lastlang = lastvendor = lastsubclass = -1;
			lastclass = u;
			continue;
		}
		if (buf[0] == 'A' && buf[1] == 'T' && isspace(buf[2])) {
			/* audio terminal type spec */
			continue;
		}
		if (buf[0] == 'H' && buf[1] == 'C' && buf[2] == 'C'
		    && isspace(buf[3])) {
			/* HID Descriptor bCountryCode */
			continue;
		}
		if (isxdigit(*cp)) {
			/* vendor */
			u = strtoul(cp, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				err("Invalid vendor spec at line %u", linectr);
				continue;
			}
			if (new_vendor(cp, u))
				err("Duplicate vendor spec at line %u vendor %04x %s",
				    linectr, u, cp);
			dbg("line %5u vendor %04x %s", linectr, u, cp);
			lastvendor = u;
			lasthut = lastlang = lastclass = lastsubclass = -1;
			continue;
		}
		if (buf[0] == '\t' && isxdigit(buf[1])) {
			/* product or subclass spec */
			u = strtoul(buf+1, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				err("Invalid product/subclass spec at line %u",
				    linectr);
				continue;
			}
			if (lastvendor != -1) {
				if (new_product(cp, lastvendor, u))
					err("Duplicate product spec at line %u product %04x:%04x %s",
					    linectr, lastvendor, u, cp);
				dbg("line %5u product %04x:%04x %s", linectr,
				    lastvendor, u, cp);
				continue;
			}
			if (lastclass != -1) {
				if (new_subclass(cp, lastclass, u))
					err("Duplicate subclass spec at line %u class %02x:%02x %s",
					    linectr, lastclass, u, cp);
				dbg("line %5u subclass %02x:%02x %s", linectr,
				    lastclass, u, cp);
				lastsubclass = u;
				continue;
			}
			if (lasthut != -1) {
				/* do not store hut */
				continue;
			}
			if (lastlang != -1) {
				/* do not store langid */
				continue;
			}
			err("Product/Subclass spec without prior Vendor/Class spec at line %u",
			    linectr);
			continue;
		}
		if (buf[0] == '\t' && buf[1] == '\t' && isxdigit(buf[2])) {
			/* protocol spec */
			u = strtoul(buf+2, &cp, 16);
			while (isspace(*cp))
				cp++;
			if (!*cp) {
				err("Invalid protocol spec at line %u",
				    linectr);
				continue;
			}
			if (lastclass != -1 && lastsubclass != -1) {
				if (new_protocol(cp, lastclass, lastsubclass,
						 u))
					err("Duplicate protocol spec at line %u class %02x:%02x:%02x %s",
					    linectr, lastclass, lastsubclass,
					    u, cp);
				dbg("line %5u protocol %02x:%02x:%02x %s",
				    linectr, lastclass, lastsubclass, u, cp);
				continue;
			}
			err("Protocol spec without prior Class and Subclass spec at line %u",
			    linectr);
			continue;
		}
		if (buf[0] == 'H' && buf[1] == 'I' &&
		    buf[2] == 'D' && /*isspace(buf[3])*/ buf[3] == ' ') {
			continue;
		}
		if (buf[0] == 'H' && buf[1] == 'U' &&
		    buf[2] == 'T' && /*isspace(buf[3])*/ buf[3] == ' ') {
			lastlang = lastclass = lastvendor = lastsubclass = -1;
			/*
			 * set 1 as pseudo-id to indicate that the parser is
			 * in a `HUT' section.
			 */
			lasthut = 1;
			continue;
		}
		if (buf[0] == 'R' && buf[1] == ' ')
			continue;

		if (buf[0] == 'V' && buf[1] == 'T')
			continue;

		err("Unknown line at line %u", linectr);
	}
}


int names_init(char *n)
{
	FILE *f;

	f = fopen(n, "r");
	if (!f)
		return errno;

	parse(f);
	fclose(f);
	return 0;
}
