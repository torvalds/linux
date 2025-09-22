/*
 * File isovfy.c - verify consistency of iso9660 filesystem.
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

static char rcsid[] ="$Id: isovfy.c,v 1.1 2000/10/10 20:40:28 beck Exp $";

#include <stdio.h>
#include <signal.h>

FILE * infile;
int blocksize;

#define PAGE sizeof(buffer)

#define ISODCL(from, to) (to - from + 1)

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
	unsigned char name			[38];
};

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
isonum_721 (char * p)
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

int
isonum_711 (char * p)
{
	return (*p & 0xff);
}

int
isonum_731 (char * p)
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}

int
isonum_722 (char * p)
{
	return ((p[1] & 0xff)
		| ((p[0] & 0xff) << 8));
}

int
isonum_732 (char * p)
{
	return ((p[3] & 0xff)
		| ((p[2] & 0xff) << 8)
		| ((p[1] & 0xff) << 16)
		| ((p[0] & 0xff) << 24));
}

int
isonum_733 (unsigned char * p)
{
	return (isonum_731 ((char *)p));
}

char lbuffer[1024];
int iline;
int rr_goof;


int 
dump_rr(struct iso_directory_record * idr){
	int len;
	char * pnt;

	len = idr->length[0] & 0xff;
	len -= (sizeof(struct iso_directory_record) - sizeof(idr->name));
	len -= idr->name_len[0];
	pnt = (char *) idr;
	pnt += (sizeof(struct iso_directory_record) - sizeof(idr->name));
	pnt += idr->name_len[0];

	if((idr->name_len[0] & 1) == 0){
		pnt++;
		len--;
	};

	rr_goof = 0;
	parse_rr(pnt, len, 0);
	return rr_goof;
}

int parse_rr(unsigned char * pnt, int len, int cont_flag)
{
	int slen;
	int ncount;
	int flag1, flag2;
	int extent;
	unsigned char *pnts;
	int cont_extent, cont_offset, cont_size;
	char symlink[1024];
	sprintf(lbuffer+iline," RRlen=%d ", len);
	iline += strlen(lbuffer+iline);

	cont_extent = cont_offset = cont_size = 0;

	symlink[0] = 0;

	ncount = 0;
	flag1 = flag2 = 0;
	while(len >= 4){
		if(ncount) sprintf(lbuffer+iline,",");
		else sprintf(lbuffer+iline,"[");
		iline += strlen(lbuffer + iline);
		sprintf(lbuffer+iline,"%c%c", pnt[0], pnt[1]);
		iline += strlen(lbuffer + iline);
		if(pnt[0] < 'A' || pnt[0] > 'Z' || pnt[1] < 'A' || 
		   pnt[1] > 'Z') {
			sprintf(lbuffer+iline,"**BAD SUSP %d %d]", 
					 pnt[0], pnt[1], rr_goof++);
			iline += strlen(lbuffer + iline);
			return flag2;
		};

		if(pnt[3] != 1) {
			sprintf(lbuffer+iline,"**BAD RRVERSION", rr_goof++);
			iline += strlen(lbuffer + iline);
			return flag2;
		};
		ncount++;
		if(pnt[0] == 'R' && pnt[1] == 'R') flag1 = pnt[4] & 0xff;
		if(strncmp(pnt, "PX", 2) == 0) flag2 |= 1;
		if(strncmp(pnt, "PN", 2) == 0) flag2 |= 2;
		if(strncmp(pnt, "SL", 2) == 0) flag2 |= 4;
		if(strncmp(pnt, "NM", 2) == 0) flag2 |= 8;
		if(strncmp(pnt, "CL", 2) == 0) flag2 |= 16;
		if(strncmp(pnt, "PL", 2) == 0) flag2 |= 32;
		if(strncmp(pnt, "RE", 2) == 0) flag2 |= 64;
		if(strncmp(pnt, "TF", 2) == 0) flag2 |= 128;

		if(strncmp(pnt, "CE", 2) == 0) {
			cont_extent = isonum_733(pnt+4);
			cont_offset = isonum_733(pnt+12);
			cont_size = isonum_733(pnt+20);
			sprintf(lbuffer+iline, "=[%x,%x,%d]",
					 cont_extent, cont_offset, cont_size);
			iline += strlen(lbuffer + iline);
		      };

		if(strncmp(pnt, "PL", 2) == 0 || strncmp(pnt, "CL", 2) == 0) {
			extent = isonum_733(pnt+4);
		        sprintf(lbuffer+iline,"=%x", extent);
			iline += strlen(lbuffer + iline);
			if(extent == 0) rr_goof++;
		};
		if(strncmp(pnt, "SL", 2) == 0) {
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
					strcat (symlink, "/");
					break;
				case 16:
					strcat(symlink,"/mnt");
					sprintf(lbuffer+iline,"Warning - mount point requested");
					iline += strlen(lbuffer + iline);
					break;
				case 32:
					strcat(symlink,"kafka");
					sprintf(lbuffer+iline,"Warning - host_name requested");
					iline += strlen(lbuffer + iline);
					break;
				default:
					sprintf(lbuffer+iline,"Reserved bit setting in symlink", rr_goof++);
					iline += strlen(lbuffer + iline);
					break;
				};
				if((pnts[0] & 0xfe) && pnts[1] != 0) {
					sprintf(lbuffer+iline,"Incorrect length in symlink component");
					iline += strlen(lbuffer + iline);
				};
				if((pnts[0] & 1) == 0)
				  strcat(symlink,"/");
				slen -= (pnts[1] + 2);
				pnts += (pnts[1] + 2);
				
		       };
			if(symlink[0] != 0) {
			  sprintf(lbuffer+iline,"=%s", symlink);
			  iline += strlen(lbuffer + iline);
			  symlink[0] = 0;
			}
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
	if(ncount) 
	  {
	    sprintf(lbuffer+iline,"]");
	    iline += strlen(lbuffer + iline);
	  }
	if (!cont_flag && flag1 && flag1 != flag2) 
	  {
	    sprintf(lbuffer+iline,"Flag %x != %x", flag1, flag2, rr_goof++);
	    iline += strlen(lbuffer + iline);
	  }
	return flag2;
}


int dir_count = 0;
int dir_size_count = 0;
int ngoof = 0;


check_tree(int file_addr, int file_size, int parent_addr){
  unsigned char buffer[2048];
  unsigned int k;
  int rflag;
  int i, i1, j, goof;
  int extent, size;
  int line;
  int orig_file_addr, parent_file_addr;
  struct iso_directory_record * idr;

  i1 = 0;

  orig_file_addr = file_addr / blocksize;  /* Actual extent of this directory */
  parent_file_addr = parent_addr / blocksize;

  if((dir_count % 100) == 0) printf("[%d %d]\n", dir_count, dir_size_count);
#if 0
  printf("Starting directory %d %d %d\n", file_addr, file_size, parent_addr);
#endif

  dir_count++;
  dir_size_count += file_size / blocksize;

  if(file_size & 0x3ff) printf("********Directory has unusual size\n");

  for(k=0; k < (file_size / sizeof(buffer)); k++){
	  lseek(fileno(infile), file_addr, 0);
	  read(fileno(infile), buffer, sizeof(buffer));
	  i = 0;
	  while(1==1){
		  goof = iline=0;
		  idr = (struct iso_directory_record *) &buffer[i];
		  if(idr->length[0] == 0) break;
		  sprintf(&lbuffer[iline],"%3d ", idr->length[0]);
		  iline += strlen(lbuffer + iline);
		  extent = isonum_733(idr->extent);
		  size = isonum_733(idr->size);
		  sprintf(&lbuffer[iline],"%5x ", extent);
		  iline += strlen(lbuffer + iline);
		  sprintf(&lbuffer[iline],"%8d ", size);
		  iline += strlen(lbuffer + iline);
		  sprintf (&lbuffer[iline], "%c", (idr->flags[0] & 2) ? '*' : ' '); 
		  iline += strlen(lbuffer + iline);

		  if(idr->name_len[0] > 33)
		    {
		      sprintf(&lbuffer[iline],"File name length=(%d)",
			      idr->name_len[0], goof++);
		      iline += strlen(lbuffer + iline);
		    }
		  else if(idr->name_len[0] == 1 && idr->name[0] == 0) {
			  sprintf(&lbuffer[iline],".             ");
			  iline += strlen(lbuffer + iline);
			  rflag = 0;
			  if(orig_file_addr !=isonum_733(idr->extent) + isonum_711((char *) idr->ext_attr_length))
			    {
			      sprintf(&lbuffer[iline],"***** Directory has null extent.", goof++);
			      iline += strlen(lbuffer + iline);
			    }
			  if(i1)
			    {
			      sprintf(&lbuffer[iline],"***** . not  first entry.", rr_goof++);
			      iline += strlen(lbuffer + iline);
			    }
		  } else if(idr->name_len[0] == 1 && idr->name[0] == 1) {
			  sprintf(&lbuffer[iline],"..            ");
			  iline += strlen(lbuffer + iline);
			  rflag = 0;
			  if(parent_file_addr !=isonum_733(idr->extent) + isonum_711((char *) idr->ext_attr_length))
			    {
			      sprintf(&lbuffer[iline],"***** Directory has null extent.", goof++);
			      iline += strlen(lbuffer + iline);
			    }
			  if(i1 != 1)
			    {
			      sprintf(&lbuffer[iline],"***** .. not second entry.", rr_goof++);
			      iline += strlen(lbuffer + iline);
			    }
			  
		  } else {
		          if(i1 < 2) 
			    {
			      sprintf(&lbuffer[iline]," Improper sorting.", rr_goof++);
			    }
			  for(j=0; j<idr->name_len[0]; j++) 
			    {
			      sprintf(&lbuffer[iline],"%c", idr->name[j]);
			    }
			  for(j=0; j<14 - (int) idr->name_len[0]; j++) 
			    {
			      sprintf(&lbuffer[iline]," ");
			      iline += strlen(lbuffer + iline);
			    }
			  rflag = 1;
		  };

		  if(size && extent == 0) 
		    {
		      sprintf(&lbuffer[iline],"****Extent==0, size != 0", goof++);
		      iline += strlen(lbuffer + iline);
		    }
#if 0
		  /* This is apparently legal. */
		  if(size == 0 && extent) 
		    {
		      sprintf(&lbuffer[iline],"****Extent!=0, size == 0", goof++);
		      iline += strlen(lbuffer + iline);
		    }
#endif

		  if(idr->flags[0] & 0xf5)
		    {
			  sprintf(&lbuffer[iline],"Flags=(%x) ", idr->flags[0], goof++);
			  iline += strlen(lbuffer + iline);
		    }
		  if(idr->interleave[0])
		    {
			  sprintf(&lbuffer[iline],"Interleave=(%d) ", idr->interleave[0], goof++);
			  iline += strlen(lbuffer + iline);
		    }

		  if(idr->file_unit_size[0])
		    {
			sprintf(&lbuffer[iline],"File unit size=(%d) ", idr->file_unit_size[0], goof++);
			iline += strlen(lbuffer + iline);
		    }


		  if(idr->volume_sequence_number[0] != 1)
		    {
		      sprintf(&lbuffer[iline],"Volume sequence number=(%d) ", idr->volume_sequence_number[0], goof++);
		      iline += strlen(lbuffer + iline);
		    }

		  goof += dump_rr(idr);
		  sprintf(&lbuffer[iline],"\n");
		  iline += strlen(lbuffer + iline);


		  if(goof){
		          ngoof++;
			  lbuffer[iline++] = 0;
			  printf("%x: %s", orig_file_addr, lbuffer);
		  };



		  if(rflag && (idr->flags[0] & 2)) check_tree((isonum_733(idr->extent) + isonum_711((char *)idr->ext_attr_length)) * blocksize,
						   isonum_733(idr->size),
						   orig_file_addr * blocksize);
		  i += buffer[i];
		  i1++;
		  if (i > 2048 - sizeof(struct iso_directory_record)) break;
	  };
	  file_addr += sizeof(buffer);
  };
  fflush(stdout);
}


/* This function simply dumps the contents of the path tables.  No
   consistency checking takes place, although this would proably be a good
   idea. */

struct path_table_info{
  char * name;
  unsigned int extent;
  unsigned short index;
  unsigned short parent;
};


check_path_tables(int typel_extent, int typem_extent, int path_table_size){
  int file_addr;
  int count;
  int j;
  char * pnt;
  char * typel, *typem;

  /* Now read in the path tables */

  typel = (char *) malloc(path_table_size);
  lseek(fileno(infile), typel_extent * blocksize, 0);
  read(fileno(infile), typel, path_table_size);

  typem = (char *) malloc(path_table_size);
  lseek(fileno(infile), typem_extent * blocksize, 0);
  read(fileno(infile), typem, path_table_size);

  j = path_table_size;
  pnt = typel;
  count = 1;
  while(j){
	  int namelen, extent, index;
	  char name[32];
	  namelen = *pnt++; pnt++;
	  extent = isonum_731(pnt); pnt += 4;
	  index = isonum_721(pnt); pnt+= 2;
	  j -= 8+namelen;
	  memset(name, 0, sizeof(name));

	  strncpy(name, pnt, namelen);
	  pnt += namelen;
	  if(j & 1) { j--; pnt++;};
	  printf("%4.4d %4.4d %8.8x %s\n",count++, index, extent, name);
  };

  j = path_table_size;
  pnt = typem;
  count = 1;
  while(j){
	  int namelen, extent, index;
	  char name[32];
	  namelen = *pnt++; pnt++;
	  extent = isonum_732(pnt); pnt += 4;
	  index = isonum_722(pnt); pnt+= 2;
	  j -= 8+namelen;
	  memset(name, 0, sizeof(name));

	  strncpy(name, pnt, namelen);
	  pnt += namelen;
	  if(j & 1) { j--; pnt++;};
	  printf("%4.4d %4.4d %8.8x %s\n", count++, index, extent, name);
  };

}

main(int argc, char * argv[]){
  int file_addr, file_size;
  char c;
  int nbyte;
  struct iso_primary_descriptor ipd;
  struct iso_directory_record * idr;
  int typel_extent, typem_extent;
  int path_table_size;
  int i,j;
  if(argc < 2) return 0;
  infile = fopen(argv[1],"rb");


  file_addr = 32768;
  lseek(fileno(infile), file_addr, 0);
  read(fileno(infile), &ipd, sizeof(ipd));

  idr = (struct iso_directory_record *) &ipd.root_directory_record;

  blocksize = isonum_723((char *)ipd.logical_block_size);
  if( blocksize != 512 && blocksize != 1024 && blocksize != 2048 )
    {
      blocksize = 2048;
    }

  file_addr = isonum_733(idr->extent) + isonum_711((char *)idr->ext_attr_length);
  file_size = isonum_733(idr->size);

  printf("Root at extent %x, %d bytes\n", file_addr, file_size);
  file_addr = file_addr * blocksize;

  check_tree(file_addr, file_size, file_addr);

  typel_extent = isonum_731((char *)ipd.type_l_path_table);
  typem_extent = isonum_732((char *)ipd.type_m_path_table);
  path_table_size = isonum_733(ipd.path_table_size);

  /* Enable this to get the dump of the path tables */
#if 0
  check_path_tables(typel_extent, typem_extent, path_table_size);
#endif

  fclose(infile);

  if(!ngoof) printf("No errors found\n");
}
  



