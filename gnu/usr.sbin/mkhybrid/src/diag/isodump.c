/*
 * File isodump.c - dump iso9660 directory information.
 *

   Written by Eric Youngdale (1993).

   Copyright 1993 Yggdrasil Computing, Incorporated

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

static char rcsid[] ="$Id: isodump.c,v 1.1 2000/10/10 20:40:28 beck Exp $";

#include "../config.h"

#include <stdio.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#include <sys/ioctl.h>
#else
#include <termio.h>
#endif
#include <signal.h>

FILE * infile;
int file_addr;
unsigned char buffer[2048];
unsigned char search[64];
int blocksize;

#define PAGE sizeof(buffer)

#define ISODCL(from, to) (to - from + 1)


int
isonum_731 (char * p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}

int
isonum_721 (char * p)
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

int
isonum_723 (char * p)
{
#if 0
	if (p[0] != p[3] || p[1] != p[2]) {
		fprintf (stderr, "invalid format 7.2.3 number\n");
		exit (1);
	}
#endif
	return (isonum_721 (p));
}


int
isonum_733 (unsigned char * p)
{
	return (isonum_731 ((char *)p));
}

struct iso_primary_descriptor {
	unsigned char type			[ISODCL (  1,   1)]; /* 711 */
	unsigned char id				[ISODCL (  2,   6)];
	unsigned char version			[ISODCL (  7,   7)]; /* 711 */
	unsigned char unused1			[ISODCL (  8,   8)];
	unsigned char system_id			[ISODCL (  9,  40)]; /* aunsigned chars */
	unsigned char volume_id			[ISODCL ( 41,  72)]; /* dunsigned chars */
	unsigned char unused2			[ISODCL ( 73,  80)];
	unsigned char volume_space_size		[ISODCL ( 81,  88)]; /* 733 */
	unsigned char unused3			[ISODCL ( 89, 120)];
	unsigned char volume_set_size		[ISODCL (121, 124)]; /* 723 */
	unsigned char volume_sequence_number	[ISODCL (125, 128)]; /* 723 */
	unsigned char logical_block_size		[ISODCL (129, 132)]; /* 723 */
	unsigned char path_table_size		[ISODCL (133, 140)]; /* 733 */
	unsigned char type_l_path_table		[ISODCL (141, 144)]; /* 731 */
	unsigned char opt_type_l_path_table	[ISODCL (145, 148)]; /* 731 */
	unsigned char type_m_path_table		[ISODCL (149, 152)]; /* 732 */
	unsigned char opt_type_m_path_table	[ISODCL (153, 156)]; /* 732 */
	unsigned char root_directory_record	[ISODCL (157, 190)]; /* 9.1 */
	unsigned char volume_set_id		[ISODCL (191, 318)]; /* dunsigned chars */
	unsigned char publisher_id		[ISODCL (319, 446)]; /* achars */
	unsigned char preparer_id		[ISODCL (447, 574)]; /* achars */
	unsigned char application_id		[ISODCL (575, 702)]; /* achars */
	unsigned char copyright_file_id		[ISODCL (703, 739)]; /* 7.5 dchars */
	unsigned char abstract_file_id		[ISODCL (740, 776)]; /* 7.5 dchars */
	unsigned char bibliographic_file_id	[ISODCL (777, 813)]; /* 7.5 dchars */
	unsigned char creation_date		[ISODCL (814, 830)]; /* 8.4.26.1 */
	unsigned char modification_date		[ISODCL (831, 847)]; /* 8.4.26.1 */
	unsigned char expiration_date		[ISODCL (848, 864)]; /* 8.4.26.1 */
	unsigned char effective_date		[ISODCL (865, 881)]; /* 8.4.26.1 */
	unsigned char file_structure_version	[ISODCL (882, 882)]; /* 711 */
	unsigned char unused4			[ISODCL (883, 883)];
	unsigned char application_data		[ISODCL (884, 1395)];
	unsigned char unused5			[ISODCL (1396, 2048)];
};

struct iso_directory_record {
	unsigned char length			[ISODCL (1, 1)]; /* 711 */
	unsigned char ext_attr_length		[ISODCL (2, 2)]; /* 711 */
	unsigned char extent			[ISODCL (3, 10)]; /* 733 */
	unsigned char size			[ISODCL (11, 18)]; /* 733 */
	unsigned char date			[ISODCL (19, 25)]; /* 7 by 711 */
	unsigned char flags			[ISODCL (26, 26)];
	unsigned char file_unit_size		[ISODCL (27, 27)]; /* 711 */
	unsigned char interleave			[ISODCL (28, 28)]; /* 711 */
	unsigned char volume_sequence_number	[ISODCL (29, 32)]; /* 723 */
	unsigned char name_len		[ISODCL (33, 33)]; /* 711 */
	unsigned char name			[1];
};

#ifdef HAVE_TERMIOS_H
struct termios savetty;
struct termios newtty;
#else
struct termio savetty;
struct termio newtty;
#endif

reset_tty(){
#ifdef HAVE_TERMIOS_H
  if(tcsetattr(0, TCSANOW, &savetty) == -1)
#else
  if(ioctl(0, TCSETAF, &savetty)==-1)
#endif
    {
      printf("cannot put tty into normal mode\n");
      exit(1);
    }
}

set_tty(){
#ifdef HAVE_TERMIOS_H
  if(tcsetattr(0, TCSANOW, &newtty) == -1)
#else
  if(ioctl(0, TCSETAF, &newtty)==-1)
#endif
    {
      printf("cannot put tty into raw mode\n");
      exit(1);
    }
}

/* Come here when we get a suspend signal from the terminal */

void
onsusp (int signo)
{
    /* ignore SIGTTOU so we don't get stopped if csh grabs the tty */
    signal(SIGTTOU, SIG_IGN);
    reset_tty ();
    fflush (stdout);
    signal(SIGTTOU, SIG_DFL);
    /* Send the TSTP signal to suspend our process group */
    signal(SIGTSTP, SIG_DFL);
/*    sigsetmask(0);*/
    kill (0, SIGTSTP);
    /* Pause for station break */

    /* We're back */
    signal (SIGTSTP, onsusp);
    set_tty ();
}



crsr2(int row, int col){
  printf("\033[%d;%dH",row,col);
}

int parse_rr(unsigned char * pnt, int len, int cont_flag)
{
	int slen;
	int ncount;
	int extent;
	int cont_extent, cont_offset, cont_size;
	int flag1, flag2;
	unsigned char *pnts;
	char symlink[1024];
	char name[1024];
	int goof;
/*	printf(" RRlen=%d ", len); */

	symlink[0] = 0;

	cont_extent = cont_offset = cont_size = 0;

	ncount = 0;
	flag1 = flag2 = 0;
	while(len >= 4){
		if(ncount) printf(",");
		else printf("[");
		printf("%c%c", pnt[0], pnt[1]);
		if(pnt[3] != 1) {
		  printf("**BAD RRVERSION");
		  return;
		};
		ncount++;
		if(pnt[0] == 'R' && pnt[1] == 'R') flag1 = pnt[4] & 0xff;
		if(strncmp(pnt, "PX", 2) == 0) flag2 |= 1;
		if(strncmp(pnt, "PN", 2) == 0) flag2 |= 2;
		if(strncmp(pnt, "SL", 2) == 0) flag2 |= 4;
		if(strncmp(pnt, "NM", 2) == 0) {
		  slen = pnt[2] - 5;
		  pnts = pnt+5;
		  if( (pnt[4] & 6) != 0 )
		    {
		      printf("*");
		    }
		  memset(name, 0, sizeof(name));
		  memcpy(name, pnts, slen);
		  printf("=%s", name);
		  flag2 |= 8;
		}
		if(strncmp(pnt, "CL", 2) == 0) flag2 |= 16;
		if(strncmp(pnt, "PL", 2) == 0) flag2 |= 32;
		if(strncmp(pnt, "RE", 2) == 0) flag2 |= 64;
		if(strncmp(pnt, "TF", 2) == 0) flag2 |= 128;

		if(strncmp(pnt, "PX", 2) == 0) {
			extent = isonum_733(pnt+12);
			printf("=%x", extent);
		};

		if(strncmp(pnt, "CE", 2) == 0) {
			cont_extent = isonum_733(pnt+4);
			cont_offset = isonum_733(pnt+12);
			cont_size = isonum_733(pnt+20);
			printf("=[%x,%x,%d]", cont_extent, cont_offset, 
			       cont_size);
		};

		if(strncmp(pnt, "PL", 2) == 0 || strncmp(pnt, "CL", 2) == 0) {
			extent = isonum_733(pnt+4);
			printf("=%x", extent);
		};

		if(strncmp(pnt, "SL", 2) == 0) {
		        int cflag;

			cflag = pnt[4];
			pnts = pnt+5;
			slen = pnt[2] - 5;
			while(slen >= 1){
				switch(pnts[0] & 0xfe){
				case 0:
					strncat(symlink, pnts+2, pnts[1]);
					break;
				case 2:
					strcat (symlink, ".");
					break;
				case 4:
					strcat (symlink, "..");
					break;
				case 8:
					if((pnts[0] & 1) == 0)strcat (symlink, "/");
					break;
				case 16:
					strcat(symlink,"/mnt");
					printf("Warning - mount point requested");
					break;
				case 32:
					strcat(symlink,"kafka");
					printf("Warning - host_name requested");
					break;
				default:
					printf("Reserved bit setting in symlink", goof++);
					break;
				};
				if((pnts[0] & 0xfe) && pnts[1] != 0) {
					printf("Incorrect length in symlink component");
				};
				if((pnts[0] & 1) == 0) strcat(symlink,"/");

				slen -= (pnts[1] + 2);
				pnts += (pnts[1] + 2);
				
		       };
			if(cflag) strcat(symlink, "+");
			printf("=%s", symlink);
			symlink[0] = 0;
		};

		len -= pnt[2];
		pnt += pnt[2];
		if(len <= 3 && cont_extent) {
		  unsigned char sector[2048];
		  lseek(fileno(infile), cont_extent * blocksize, 0);
		  read(fileno(infile), sector, sizeof(sector));
		  flag2 |= parse_rr(&sector[cont_offset], cont_size, 1);
		};
	};
	if(ncount) printf("]");
	if (!cont_flag && flag1 != flag2) 
	  printf("Flag %x != %x", flag1, flag2, goof++);
	return flag2;
}

int 
dump_rr(struct iso_directory_record * idr)
{
	int len;
	unsigned char * pnt;

	len = idr->length[0] & 0xff;
	len -= sizeof(struct iso_directory_record);
	len += sizeof(idr->name);
	len -= idr->name_len[0];
	pnt = (unsigned char *) idr;
	pnt += sizeof(struct iso_directory_record);
	pnt -= sizeof(idr->name);
	pnt += idr->name_len[0];
	if((idr->name_len[0] & 1) == 0){
		pnt++;
		len--;
	};
	parse_rr(pnt, len, 0);
}


showblock(int flag){
  unsigned int k;
  int i, j;
  int line;
  struct iso_directory_record * idr;
  lseek(fileno(infile), file_addr, 0);
  read(fileno(infile), buffer, sizeof(buffer));
  for(i=0;i<60;i++) printf("\n");
  fflush(stdout);
  i = line = 0;
  if(flag) {
	  while(1==1){
		  crsr2(line+3,1);
		  idr = (struct iso_directory_record *) &buffer[i];
		  if(idr->length[0] == 0) break;
		  printf("%3d ", idr->length[0]);
		  printf("[%2d] ", idr->volume_sequence_number[0]);
		  printf("%5x ", isonum_733(idr->extent));
		  printf("%8d ", isonum_733(idr->size));
		  printf ((idr->flags[0] & 2) ? "*" : " "); 
		  if(idr->name_len[0] == 1 && idr->name[0] == 0)
			  printf(".             ");
		  else if(idr->name_len[0] == 1 && idr->name[0] == 1)
			  printf("..            ");
		  else {
			  for(j=0; j<idr->name_len[0]; j++) printf("%c", idr->name[j]);
			  for(j=0; j<14 -idr->name_len[0]; j++) printf(" ");
		  };
		  dump_rr(idr);
		  printf("\n");
		  i += buffer[i];
		  if (i > 2048 - sizeof(struct iso_directory_record)) break;
		  line++;
	  };
  };
  printf("\n");
  printf(" Zone, zone offset: %6x %4.4x  ",file_addr / blocksize, 
	 file_addr & (blocksize - 1));
  fflush(stdout);
}

getbyte()
{
  char c1;
  c1 = buffer[file_addr & (blocksize-1)];
  file_addr++;
  if ((file_addr & (blocksize-1)) == 0) showblock(0);
  return c1;
}

main(int argc, char * argv[]){
  char c;
  char buffer[2048];
  int nbyte;
  int i,j;
  struct iso_primary_descriptor ipd;
  struct iso_directory_record * idr;

  if(argc < 2) return 0;
  infile = fopen(argv[1],"rb");

  file_addr = 16 << 11;
  lseek(fileno(infile), file_addr, 0);
  read(fileno(infile), &ipd, sizeof(ipd));

  idr = (struct iso_directory_record *) &ipd.root_directory_record;

  blocksize = isonum_723((char *)ipd.logical_block_size);
  if( blocksize != 512 && blocksize != 1024 && blocksize != 2048 )
    {
      blocksize = 2048;
    }

  file_addr = isonum_733(idr->extent);

  file_addr = file_addr * blocksize;

/* Now setup the keyboard for single character input. */
#ifdef HAVE_TERMIOS_H 
	if(tcgetattr(0, &savetty) == -1)
#else
	if(ioctl(0, TCGETA, &savetty) == -1)
#endif
	  {
	    printf("stdin must be a tty\n");
	    exit(1);
	  }
	newtty=savetty;
	newtty.c_lflag&=~ICANON;
	newtty.c_lflag&=~ECHO;
	newtty.c_cc[VMIN]=1;
  	set_tty();
	signal(SIGTSTP, onsusp);

  do{
    if(file_addr < 0) file_addr = 0;
    showblock(1);
    read (0, &c, 1);
    if (c == 'a') file_addr -= blocksize;
    if (c == 'b') file_addr += blocksize;
    if (c == 'g') {
      crsr2(20,1);
      printf("Enter new starting block (in hex):");
      scanf("%x",&file_addr);
      file_addr = file_addr * blocksize;
      crsr2(20,1);
      printf("                                     ");
    };
    if (c == 'f') {
      crsr2(20,1);
      printf("Enter new search string:");
      fgets((char *)search,sizeof(search),stdin);
      while(search[strlen(search)-1] == '\n') search[strlen(search)-1] = 0;
      crsr2(20,1);
      printf("                                     ");
    };
    if (c == '+') {
      while(1==1){
	while(1==1){
	  c = getbyte(&file_addr);
	  if (c == search[0]) break;
	};
	for (j=1;j<strlen(search);j++) 
	  if(search[j] != getbyte()) break;
	if(j==strlen(search)) break;
      };
      file_addr &= ~(blocksize-1);
      showblock(1);
    };
    if (c == 'q') break;
  } while(1==1);
  reset_tty();
  fclose(infile);
}
  



