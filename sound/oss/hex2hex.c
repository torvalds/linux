/*
 * hex2hex reads stdin in Intel HEX format and produces an
 * (unsigned char) array which contains the bytes and writes it
 * to stdout using C syntax
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ABANDON(why) { fprintf(stderr, "%s\n", why); exit(1); }
#define MAX_SIZE (256*1024)
unsigned char buf[MAX_SIZE];

int loadhex(FILE *inf, unsigned char *buf)
{
	int l=0, c, i;

	while ((c=getc(inf))!=EOF)
	{
		if (c == ':')	/* Sync with beginning of line */
		{
			int n, check;
			unsigned char sum;
			int addr;
			int linetype;

			if (fscanf(inf, "%02x", &n) != 1)
			   ABANDON("File format error");
			sum = n;

			if (fscanf(inf, "%04x", &addr) != 1)
			   ABANDON("File format error");
			sum += addr/256;
			sum += addr%256;

			if (fscanf(inf, "%02x", &linetype) != 1)
			   ABANDON("File format error");
			sum += linetype;

			if (linetype != 0)
			   continue;

			for (i=0;i<n;i++)
			{
				if (fscanf(inf, "%02x", &c) != 1)
			   	   ABANDON("File format error");
				if (addr >= MAX_SIZE)
				   ABANDON("File too large");
				buf[addr++] = c;
				if (addr > l)
				   l = addr;
				sum += c;
			}

			if (fscanf(inf, "%02x", &check) != 1)
			   ABANDON("File format error");

			sum = ~sum + 1;
			if (check != sum)
			   ABANDON("Line checksum error");
		}
	}

	return l;
}

int main( int argc, const char * argv [] )
{
	const char * varline;
	int i,l;
	int id=0;

	if(argv[1] && strcmp(argv[1], "-i")==0)
	{
		argv++;
		argc--;
		id=1;
	}
	if(argv[1]==NULL)
	{
		fprintf(stderr,"hex2hex: [-i] filename\n");
		exit(1);
	}
	varline = argv[1];
	l = loadhex(stdin, buf);

	printf("/*\n *\t Computer generated file. Do not edit.\n */\n");
        printf("static int %s_len = %d;\n", varline, l);
	printf("static unsigned char %s[] %s = {\n", varline, id?"__initdata":"");

	for (i=0;i<l;i++)
	{
		if (i) printf(",");
		if (i && !(i % 16)) printf("\n");
		printf("0x%02x", buf[i]);
	}

	printf("\n};\n\n");
	return 0;
}
