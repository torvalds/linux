/* Copyright (C) Nanoradio AB */
/* $Id: mibtable.c 10372 2008-11-13 09:22:12Z miwi $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <unistd.h>

#include <inttypes.h>

#include "nrx_lib.h"
#include "nrx_priv.h"

int debuglevel = 0;
enum { DEFAULT, BRIEF, BINARY, HEXDUMP } oformat = DEFAULT;

struct mibinfo {
   char *oid;
   char *name;
   size_t size;
   struct mibtype *type;
   struct mibinfo *next;
};

int
getint(const void **data, size_t *data_len, size_t isize)
{
   const unsigned char *p;
   int val = 0;

   if(isize == 0)
      isize = 4;
   if(*data_len < isize) {
      warnx("bad format for data %d < %d", *data_len, isize);
      isize = *data_len;
   }
   for(p = (const unsigned char*)*data + isize - 1; p >= (const unsigned char*)*data; p--) {
      val = val * 256 + *p;
   }
   *data = (const unsigned char*)*data + isize;
   *data_len -= isize;
   return val;
}

#define ALTFORMAT 1
#define ZEROPAD 2
#define ALWAYSSIGN 4
#define PADSIGN 8
#define LEFTADJ 16

static void
make_format(char *buf, int x, size_t len, int flags, int size)
{
   char *p = buf;
   int n;

   *p++ = '%';

   if(flags & ALTFORMAT)
      *p++ = '#';

   if(flags & LEFTADJ)
      *p++ = '-';
   else if(flags & ZEROPAD)
      *p++ = '0';

   if(flags & ALWAYSSIGN)
      *p++ = '+';
   else if(flags & PADSIGN)
      *p++ = ' ';

   if(flags & ZEROPAD) {
      size *= 2;
      for(n = 1; (size / n) >= 10; n *= 10);
      while(size > 0) {
         *p++ = '0' + (size / n);
         size -= (size / n) * n;
         n /= 10;
      }
   }
   *p++ = x;
   *p = '\0';
}

static int
format(const char *format, 
       char *buf, size_t len, 
       const void *data, size_t data_size)
{
   char fbuf[32];
   const char *f;
   char *b;
   size_t l;
   int flags;
   int size;
   
   b = buf;
   
   while(data_size > 0) {
      for(f = format; *f != '\0'; ) {
         if(*f != '%') {
            *b++ = *f;
            len--;
            f++;
            continue;
         }
         f++;
         flags = 0;

         switch(*f) {
            case '#':
               flags |= ALTFORMAT;
               break;
            case '0':
               flags |= ZEROPAD;
               break;
            case '+':
               flags |= ALWAYSSIGN;
               break;
            case ' ':
               flags |= PADSIGN;
               break;
            case '-':
               flags |= LEFTADJ;
               break;
            default:
               break;
         }
         size = 0;
         while(isdigit(*f)) {
            size = size * 10 + (*f - '0');
            f++;
         }
         make_format(fbuf, *f, sizeof(fbuf), flags, size);

         switch(*f) {
            case 'i':
            case 'd':
            case 'x': {
               int val = getint(&data, &data_size, size);
#if 0
               fprintf(stderr, "=== %s %d ===\n", fbuf, val);
#endif
               l = snprintf(b, len, fbuf, val);
               b += l;
               len -= l;
               break;
            }
            case 'c': {
               *b++ = *(const char*)data;
               l--;
               data = (const char*)data + 1;
               data_size -= 1;
               break;
            }
            case 's': {
               *b++ = *(const char*)data;
               l--;
               data = (const char*)data + 1;
               data_size -= 1;
               break;
            }
         }
         f++;
      }
   }
   *b = '\0';
   return 0;
}


static int
x_format(char *s, struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   char f[32];
   if(m->size == 0)
      snprintf(f, sizeof(f), "%%%d%s", data_size, s);
   else
      snprintf(f, sizeof(f), "%%%d%s", m->size, s);

   return format(f, buf, len, data, data_size);
}

static int
decimal_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   return x_format("d", m, data, data_size, buf, len);
}

static int
hex_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   return x_format("x", m, data, data_size, buf, len);
}

static int
hex32_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   return format("%4x ", buf, len, data, data_size);
}

static int
hex16_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   return format("%2x ", buf, len, data, data_size);
}

static int
hexvec_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   return format("%01x ", buf, len, data, data_size);
}

static int
bool_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   const unsigned char *p;
   int f = 0;
   for(p = data; p < (const unsigned char*)data + data_size; p++)
      if(*p != 0) {
         f = 1;
         break;
      }
   if(f)
      strlcpy(buf, "on", len);
   else
      strlcpy(buf, "off", len);
   
   return 0;
}

static int
string_format(struct mibinfo *m, const void *data, size_t data_size, char *buf, size_t len)
{
   if(data_size < len)
      len = data_size;
   memcpy(buf, data, data_size);
   buf[data_size] = '\0';

   return 0;
}

static struct mibtype {
   char *name;
   int (*parse)(const char*);
   int (*format)(struct mibinfo*, const void*, size_t, char*, size_t);
} mibtypes[] = {
/* #define X(Y) { #Y, Y##_parse, Y##_format } */
#define X(Y) { #Y, NULL, Y##_format }
   X(decimal),
   X(bool),
   X(string),
   X(hex),
   X(hexvec),
   X(hex32),
   X(hex16),
   { NULL }
};

static struct mibtype*
string_to_type(const char *s)
{
   struct mibtype *p;
   for(p = mibtypes; p->name != NULL; p++)
      if(strcasecmp(s, p->name) == 0)
         return p;
   return NULL;
}

static struct mibinfo *
read_mibinfo(const char *filename)
{
   FILE *f;
   char line[1024];
   char *p;
   char *q;
   struct mibinfo *m, *mhead = NULL, **mtail = &mhead;
   int lineno = 1;
   
   f = fopen(filename, "r");
   if(f == NULL) {
      warn("%s", filename);
      return NULL;
   }

   for(; fgets(line, sizeof(line), f) != NULL; lineno++) {
      char *end;
      line[strcspn(line, "\r\n")] = '\0';
      for(p = line; isspace(*p); p++);
      if(*p == '\0' || *p == '#')
         continue;

      m = calloc(1, sizeof(*m));
      if(m == NULL) {
         warnx("out of memory");
         break;
      }

      if((q = strsep(&p, ",")) == NULL) {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->oid);
         free(m->name);
         free(m);
         continue;
      }
      if((m->oid = strdup(q)) == NULL) {
         warnx("out of memory");
         free(m->oid);
         free(m->name);
         free(m);
         break;
      }

      if((q = strsep(&p, ",")) == NULL) {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->oid);
         free(m->name);
         free(m);
         continue;
      }
      if((m->name = strdup(q)) == NULL) {
         warnx("out of memory");
         free(m->oid);
         free(m->name);
         free(m);
         continue;
      }

      if((q = strsep(&p, ",")) == NULL) {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->oid);
         free(m->name);
         free(m);
         continue;
      }
      m->size = strtol(q, &end, 0);
      if(*end != '\0') {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->name);
         free(m->oid);
         free(m);
         continue;
      }

      if((q = strsep(&p, ",")) == NULL) {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->oid);
         free(m->name);
         free(m);
         continue;
      }
      m->type = string_to_type(q);
      if(m->type == NULL) {
         warnx("%s:%d:malformed entry", filename, lineno);
         free(m->name);
         free(m->oid);
         free(m);
         continue;
      }
      m->next = NULL;
      *mtail = m;
      mtail = &m->next;
   }
   fclose(f);
   return mhead;
}

struct mibinfo*
find_mib(struct mibinfo *mhead, const char *name)
{
   struct mibinfo *m;
   for(m = mhead; m != NULL; m = m->next) {
      if(strcasecmp(name, m->oid) == 0 ||
         strcasecmp(name, m->name) == 0)
         return m;
   }
   return NULL;
}

static struct mibinfo *
foo(struct mibinfo *mhead, const char *id)
{
   struct mibinfo *m;
   static struct mibinfo fake;
   m = find_mib(mhead, id);
   if(m == NULL) {
      m = &fake;
      m->oid = (char*)id;
      m->name = (char*)id;
      m->size = 0;
      m->type = &mibtypes[4]; /* XXX */
   }
   return m;
}

static void
printmib(nrx_context ctx, struct mibinfo *mhead, const char *id)
{
   char buf[512];
   char data[512];
   size_t len = sizeof(data);
   struct mibinfo *m;
   int ret;

   m = foo(mhead, id);
   
   ret = nrx_get_mib_val(ctx, m->oid, data, &len);
   if(ret != 0) {
      warnx("%s: %d", id, ret);
      return;
   }

   switch(oformat) {
      case DEFAULT:
      case BRIEF:
         (*m->type->format)(m, data, len, buf, sizeof(buf));
         if(oformat == BRIEF)
            printf("%s\n", buf);
         else
            printf("%s (%s) = %s\n", m->name, m->oid, buf);
         break;
      case HEXDUMP:
         nrx_printbuf(data, len, " ");
         break;
      case BINARY:
         ret = write(STDOUT_FILENO, data, len);
         break;
   }
}

static int
mib_set(nrx_context ctx, 
        struct mibinfo *mhead, 
        const char *id, 
        void *value, 
        size_t len)
{
   struct mibinfo *m;
   int ret;

   m = foo(mhead, id);
 
   ret = nrx_set_mib_val(ctx, m->oid, value, len);
   if(ret != 0) {
      warnx("%s: %d", id, ret);
   }
   return ret;
}


void
usage(void)
{
   fprintf(stderr, "mibtable [-d][-i ifname][-m mibtable.dat][-o format] oid...\n");
   exit(1);
}



int
main(int argc, char **argv)
{
   nrx_context ctx;
   struct mibinfo *mhead;
   int i;
   int opt_s = 0;

   int ch;
   char *ifname = "eth1";
   char *mibtable = "mibtable.dat";

   while((ch = getopt(argc, argv, "di:m:o:qs")) != -1) {
      switch(ch) {
         case 'd':
            debuglevel++;
            break;
         case 'i':
            ifname = optarg;
            break;
         case 'm':
            mibtable = optarg;
            break;
         case 'o':
            if(strcasecmp(optarg, "hexdump") == 0)
               oformat = HEXDUMP;
            else if(strcasecmp(optarg, "binary") == 0)
               oformat = BINARY;
            else if(strcasecmp(optarg, "default") == 0)
               oformat = DEFAULT;
            else if(strcasecmp(optarg, "brief") == 0)
               oformat = BRIEF;
            else {
               errx(1, "unrecognized format \"%s\"", optarg);
            }
               
            break;
         case 'q':
            oformat = BRIEF;
            break;
         case 's':
            opt_s = 1;
            break;
         case '?':
         default:
            usage();
      }
   }

   if(opt_s && oformat != BINARY)
      errx(1, "set requires binary format");

   mhead = read_mibinfo(mibtable);
   
   nrx_init_context(&ctx, ifname);

   if(opt_s) {
      char buf[1024];
      ssize_t n;
      if(argc != optind + 1)
         errx(1, "need exactly one oid");
      n = read(STDIN_FILENO, buf, sizeof(buf));
      if(n < 0)
         err(1, "stdin");
      mib_set(ctx, mhead, argv[optind], buf, n);
   } else {
      for(i = optind; i < argc; i++)
         printmib(ctx, mhead, argv[i]);
   }
   nrx_free_context(ctx);

   return 0;
}
