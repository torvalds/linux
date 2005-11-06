/*
 * conmakehash.c
 *
 * Create arrays for initializing the kernel folded tables (using a hash
 * table turned out to be to limiting...)  Unfortunately we can't simply
 * preinitialize the tables at compile time since kfree() cannot accept
 * memory not allocated by kmalloc(), and doing our own memory management
 * just for this seems like massive overkill.
 *
 * Copyright (C) 1995-1997 H. Peter Anvin
 *
 * This program is a part of the Linux kernel, and may be freely
 * copied under the terms of the GNU General Public License (GPL),
 * version 2, or at your option any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <string.h>
#include <ctype.h>

#define MAX_FONTLEN 256

typedef unsigned short unicode;

void usage(char *argv0)
{
  fprintf(stderr, "Usage: \n"
         "        %s chartable [hashsize] [hashstep] [maxhashlevel]\n", argv0);
  exit(EX_USAGE);
}

int getunicode(char **p0)
{
  char *p = *p0;

  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != 'U' || p[1] != '+' ||
      !isxdigit(p[2]) || !isxdigit(p[3]) || !isxdigit(p[4]) ||
      !isxdigit(p[5]) || isxdigit(p[6]))
    return -1;
  *p0 = p+6;
  return strtol(p+2,0,16);
}

unicode unitable[MAX_FONTLEN][255];
				/* Massive overkill, but who cares? */
int unicount[MAX_FONTLEN];

void addpair(int fp, int un)
{
  int i;

  if ( un <= 0xfffe )
    {
      /* Check it isn't a duplicate */

      for ( i = 0 ; i < unicount[fp] ; i++ )
	if ( unitable[fp][i] == un )
	  return;

      /* Add to list */

      if ( unicount[fp] > 254 )
	{
	  fprintf(stderr, "ERROR: Only 255 unicodes/glyph permitted!\n");
	  exit(EX_DATAERR);
	}

      unitable[fp][unicount[fp]] = un;
      unicount[fp]++;
    }

  /* otherwise: ignore */
}

int main(int argc, char *argv[])
{
  FILE *ctbl;
  char *tblname;
  char buffer[65536];
  int fontlen;
  int i, nuni, nent;
  int fp0, fp1, un0, un1;
  char *p, *p1;

  if ( argc < 2 || argc > 5 )
    usage(argv[0]);

  if ( !strcmp(argv[1],"-") )
    {
      ctbl = stdin;
      tblname = "stdin";
    }
  else
    {
      ctbl = fopen(tblname = argv[1], "r");
      if ( !ctbl )
	{
	  perror(tblname);
	  exit(EX_NOINPUT);
	}
    }

  /* For now we assume the default font is always 256 characters. */    
  fontlen = 256;

  /* Initialize table */

  for ( i = 0 ; i < fontlen ; i++ )
    unicount[i] = 0;

  /* Now we come to the tricky part.  Parse the input table. */

  while ( fgets(buffer, sizeof(buffer), ctbl) != NULL )
    {
      if ( (p = strchr(buffer, '\n')) != NULL )
	*p = '\0';
      else
	fprintf(stderr, "%s: Warning: line too long\n", tblname);

      p = buffer;

/*
 * Syntax accepted:
 *	<fontpos>	<unicode> <unicode> ...
 *	<range>		idem
 *	<range>		<unicode range>
 *
 * where <range> ::= <fontpos>-<fontpos>
 * and <unicode> ::= U+<h><h><h><h>
 * and <h> ::= <hexadecimal digit>
 */

      while (*p == ' ' || *p == '\t')
	p++;
      if (!*p || *p == '#')
	continue;	/* skip comment or blank line */

      fp0 = strtol(p, &p1, 0);
      if (p1 == p)
	{
	  fprintf(stderr, "Bad input line: %s\n", buffer);
	  exit(EX_DATAERR);
        }
      p = p1;

      while (*p == ' ' || *p == '\t')
	p++;
      if (*p == '-')
	{
	  p++;
	  fp1 = strtol(p, &p1, 0);
	  if (p1 == p)
	    {
	      fprintf(stderr, "Bad input line: %s\n", buffer);
	      exit(EX_DATAERR);
	    }
	  p = p1;
        }
      else
	fp1 = 0;

      if ( fp0 < 0 || fp0 >= fontlen )
	{
	    fprintf(stderr,
		    "%s: Glyph number (0x%x) larger than font length\n",
		    tblname, fp0);
	    exit(EX_DATAERR);
	}
      if ( fp1 && (fp1 < fp0 || fp1 >= fontlen) )
	{
	    fprintf(stderr,
		    "%s: Bad end of range (0x%x)\n",
		    tblname, fp1);
	    exit(EX_DATAERR);
	}

      if (fp1)
	{
	  /* we have a range; expect the word "idem" or a Unicode range of the
	     same length */
	  while (*p == ' ' || *p == '\t')
	    p++;
	  if (!strncmp(p, "idem", 4))
	    {
	      for (i=fp0; i<=fp1; i++)
		addpair(i,i);
	      p += 4;
	    }
	  else
	    {
	      un0 = getunicode(&p);
	      while (*p == ' ' || *p == '\t')
		p++;
	      if (*p != '-')
		{
		  fprintf(stderr,
"%s: Corresponding to a range of font positions, there should be a Unicode range\n",
			  tblname);
		  exit(EX_DATAERR);
	        }
	      p++;
	      un1 = getunicode(&p);
	      if (un0 < 0 || un1 < 0)
		{
		  fprintf(stderr,
"%s: Bad Unicode range corresponding to font position range 0x%x-0x%x\n",
			  tblname, fp0, fp1);
		  exit(EX_DATAERR);
	        }
	      if (un1 - un0 != fp1 - fp0)
		{
		  fprintf(stderr,
"%s: Unicode range U+%x-U+%x not of the same length as font position range 0x%x-0x%x\n",
			  tblname, un0, un1, fp0, fp1);
		  exit(EX_DATAERR);
	        }
	      for(i=fp0; i<=fp1; i++)
		addpair(i,un0-fp0+i);
	    }
        }
      else
	{
	    /* no range; expect a list of unicode values for a single font position */

	    while ( (un0 = getunicode(&p)) >= 0 )
	      addpair(fp0, un0);
	}
      while (*p == ' ' || *p == '\t')
	p++;
      if (*p && *p != '#')
	fprintf(stderr, "%s: trailing junk (%s) ignored\n", tblname, p);
    }

  /* Okay, we hit EOF, now output hash table */
  
  fclose(ctbl);
  

  /* Compute total size of Unicode list */
  nuni = 0;
  for ( i = 0 ; i < fontlen ; i++ )
    nuni += unicount[i];
  
  printf("\
/*\n\
 * Do not edit this file; it was automatically generated by\n\
 *\n\
 * conmakehash %s > [this file]\n\
 *\n\
 */\n\
\n\
#include <linux/types.h>\n\
\n\
u8 dfont_unicount[%d] = \n\
{\n\t", argv[1], fontlen);

  for ( i = 0 ; i < fontlen ; i++ )
    {
      printf("%3d", unicount[i]);
      if ( i == fontlen-1 )
        printf("\n};\n");
      else if ( i % 8 == 7 )
        printf(",\n\t");
      else
        printf(", ");
    }
  
  printf("\nu16 dfont_unitable[%d] = \n{\n\t", nuni);
  
  fp0 = 0;
  nent = 0;
  for ( i = 0 ; i < nuni ; i++ )
    {
      while ( nent >= unicount[fp0] )
	{
	  fp0++;
	  nent = 0;
	}
      printf("0x%04x", unitable[fp0][nent++]);
      if ( i == nuni-1 )
         printf("\n};\n");
       else if ( i % 8 == 7 )
         printf(",\n\t");
       else
         printf(", ");
    }

  exit(EX_OK);
}
