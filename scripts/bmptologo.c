
/*
 *  Convert a logo in ASCII PNM format to C source suitable for inclusion in
 *  the Linux kernel
 *
 *  (C) Copyright 2001-2003 by Geert Uytterhoeven <geert@linux-m68k.org>
 *
 *  --------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 */

#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


static const char *programname;
static const char *filename;
static const char *logoname = "linux_logo";
static const char *outputname;
static FILE *out;

//#define debug 0
#define LINUX_LOGO_MONO		1	/* monochrome black/white */
#define LINUX_LOGO_VGA16	2	/* 16 colors VGA text palette */
#define LINUX_LOGO_CLUT224	3	/* 224 colors */
#define LINUX_LOGO_GRAY256	4	/* 256 levels grayscale */
#define LINUX_LOGO_bmp		5	/* truecolours*/

static const char *logo_types[LINUX_LOGO_bmp+1] = {
    [LINUX_LOGO_MONO] = "LINUX_LOGO_MONO",
    [LINUX_LOGO_VGA16] = "LINUX_LOGO_VGA16",
    [LINUX_LOGO_CLUT224] = "LINUX_LOGO_CLUT224",
    [LINUX_LOGO_GRAY256] = "LINUX_LOGO_GRAY256",
    [LINUX_LOGO_bmp] = "LINUX_LOGO_bmp"
};

#define MAX_LINUX_LOGO_COLORS	224

struct color {
    char blue;
    char green;
    char red;
};

static const struct color clut_vga16[16] = {
    { 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0xaa },
    { 0x00, 0xaa, 0x00 },
    { 0x00, 0xaa, 0xaa },
    { 0xaa, 0x00, 0x00 },
    { 0xaa, 0x00, 0xaa },
    { 0xaa, 0x55, 0x00 },
    { 0xaa, 0xaa, 0xaa },
    { 0x55, 0x55, 0x55 },
    { 0x55, 0x55, 0xff },
    { 0x55, 0xff, 0x55 },
    { 0x55, 0xff, 0xff },
    { 0xff, 0x55, 0x55 },
    { 0xff, 0x55, 0xff },
    { 0xff, 0xff, 0x55 },
    { 0xff, 0xff, 0xff },
};

unsigned char data_name[] = {
	0x6C, 0x6F, 0x67,
	0x6F, 0x5F, 0x52,
	0x4B, 0x6C, 0x6F,
	0x67, 0x6F, 0x5F,
	0x64, 0x61, 0x74,
	0x61
};

unsigned char clut_name[] = {
	0x62, 0x6D, 0x70,
	0x6C, 0x6F, 0x67,
	0x6F, 0x5F, 0x52,
	0x4B, 0x6C, 0x6F,
	0x67, 0x6F, 0x5F,
	0x63, 0x6C, 0x75,
	0x74, 0x00
};

static int logo_type = LINUX_LOGO_CLUT224;
static unsigned long logo_width;
static unsigned long logo_height;
static unsigned long data_long;
static unsigned long data_start;
static unsigned char *logo_data;

static void die(const char *fmt, ...)
    __attribute__ ((noreturn)) __attribute ((format (printf, 1, 2)));
static void usage(void) __attribute ((noreturn));

static void read_image(void)
{
	int fd;
	struct stat s;
	unsigned char *data;
	
	/* open image file */
	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("Cannot open file isll.. %s: %s\n", filename, strerror(errno));

	if (fstat(fd, &s) < 0)
		die("Cannot stat file isll.. %s: %s\n", filename, strerror(errno));

#if 0
	ret = fread(read_buf,1,0x26,fp);
	if (ret != 0x26)
		die("read file %s: error read_buf=%ld\n", filename,ret);

	logo_height = (read_buf[0x19]<<24) + (read_buf[0x18]<<16) +(read_buf[0x17]<<8) +(read_buf[0x16]);
	logo_width  = (read_buf[0x15]<<24) + (read_buf[0x14]<<16) +(read_buf[0x13]<<8) +(read_buf[0x12]);
	data_start = (read_buf[0x0d]<<24) + (read_buf[0x0c]<<16) +(read_buf[0x0b]<<8) +(read_buf[0x0a]);
	data_long  = (read_buf[0x25]<<24) + (read_buf[0x24]<<16) +(read_buf[0x023]<<8) +(read_buf[0x22]);
#endif	
	/* allocate image data */
	//logo_data = (char *)malloc(logo_height * logo_width * 3);
	//data_long = logo_height * logo_width * 3;
//#ifdef debug
#if 0
	die("%s..logo_height=%ld,logo_width=%ld,data_start=%ld,data_long=%ld,sizeof(struct color)=%d,  \
		read_buf[0x17]=%d  read_buf[0x13]=%d\n\n",filename,logo_height,logo_width,data_start,  \
		data_long,sizeof(struct color),read_buf[0x17],read_buf[0x13]);
	if ((logo_width*logo_height*3) != data_long)
		die("something is wront in scripts/bmptologo.c\n");

#endif
#if 0
	fseek(fp,data_start,SEEK_SET);
	ret = fread(logo_data,1,data_long,fp);
	if (ret != data_long)
		die("read file %s: error logo_data=%ld\n", filename,ret);
#else
	data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED)
		die("read file %s: error logo_data\n", filename);
	logo_data = data + 54;
	logo_height = (data[0x19]<<24) + (data[0x18]<<16) +(data[0x17]<<8) +(data[0x16]);
	logo_width  = (data[0x15]<<24) + (data[0x14]<<16) +(data[0x13]<<8) +(data[0x12]);
	data_start = (data[0x0d]<<24) + (data[0x0c]<<16) +(data[0x0b]<<8) +(data[0x0a]);
	data_long  = (data[0x25]<<24) + (data[0x24]<<16) +(data[0x023]<<8) +(data[0x22]);
	data_long = logo_height * logo_width * 3;
#if 0
	die("%s..logo_height=%ld,logo_width=%ld,data_start=%ld,data_long=%ld,sizeof(struct color)=%d,  \
		read_buf[0x17]=%d  read_buf[0x13]=%d\n\n",filename,logo_height,logo_width,data_start,  \
		data_long,sizeof(struct color),read_buf[0x17],read_buf[0x13]);
	if ((logo_width*logo_height*3) != data_long)
		die("something is wront in scripts/bmptologo.c\n");
#endif	
#endif
#ifdef  debug
	die("logo_data is:%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x:over\n", \
		logo_data[0],logo_data[1],logo_data[2],logo_data[3],logo_data[4],logo_data[5],logo_data[6],logo_data[7],logo_data[8], \
logo_data[9],logo_data[10],logo_data[11]);
#endif
    /* close file */
    close(fd);
}


static inline int is_black(struct color c)
{
    return c.red == 0 && c.green == 0 && c.blue == 0;
}

static inline int is_white(struct color c)
{
    return c.red == 255 && c.green == 255 && c.blue == 255;
}

static inline int is_gray(struct color c)
{
    return c.red == c.green && c.red == c.blue;
}

static inline int is_equal(struct color c1, struct color c2)
{
    return c1.red == c2.red && c1.green == c2.green && c1.blue == c2.blue;
}

static int write_hex_cnt;

static void write_hex(unsigned char byte)
{
    if (write_hex_cnt % 12)
	fprintf(out, ", 0x%02x", byte);
    else if (write_hex_cnt)
	fprintf(out, ",\n\t0x%02x", byte);
    else
	fprintf(out, "\t0x%02x", byte);
    write_hex_cnt++;
}

static void write_header(void)
{
	/* open logo file */
	if (outputname) {
		out = fopen(outputname, "w");
		if (!out)
			die("Cannot create file %s: %s\n", outputname, strerror(errno));
	} else {
		out = stdout;
	}

	fputs("/*\n", out);
	fputs(" *  DO NOT EDIT THIS FILE!\n", out);
	fputs(" *\n", out);
	fprintf(out, " *  It was automatically generated from %s\n", filename);
	fputs(" *\n", out);
	fprintf(out, " *  Linux logo %s\n", logoname);
	fputs(" */\n\n", out);
	fputs("#include <linux/linux_logo.h>\n\n", out);
	fprintf(out, "static unsigned char %s_data[] __initdata = {\n",
		logoname);
}

static void write_footer(void)
{
	fputs("\n};\n\n", out);
	fprintf(out, "const struct linux_logo %s __initconst = {\n", logoname);
	fprintf(out, "\t.type\t\t= %s,\n", logo_types[logo_type]);

	if (logo_type == LINUX_LOGO_bmp) {
		fprintf(out, "\t.width\t\t= %ld,\n",  logo_width);
		fprintf(out, "\t.height\t\t= %ld,\n",  logo_height);
		//fprintf(out, "\t.data\t\t= %s_data,\n", logoname);
		fprintf(out, "\t.data\t\t= &(%s_data[%ld]),\n", logoname,sizeof(data_name) + 8);
		fprintf(out, "\t.clut\t\t= %s_clut\n", logoname);
	}  

	fputs("};\n\n", out);

	/* close logo file */
	if (outputname)
		fclose(out);
}


static void write_logo_bmp(void)
{
	unsigned long  i=0, j=0;
	unsigned char *position ;
	
	/* validate image */
/*statistics how many colours ,and if have over 224
	logo_clutsize = 0;
	for (i = 0; i < logo_height; i++)
		for (j = 0; j < logo_width; j++) {
			for (k = 0; k < logo_clutsize; k++)
				if (is_equal(logo_data[i][j], logo_clut[k]))
					break;
			if (k == logo_clutsize) {
				if (logo_clutsize == MAX_LINUX_LOGO_COLORS)
					die("Image has more than %d colors\n"
						"Use ppmquant(1) to reduce the number of colors\n",
						MAX_LINUX_LOGO_COLORS);
					logo_clut[logo_clutsize++] = logo_data[i][j];
			}
		}

*/
	write_hex_cnt = 0;

	
	/* write file header */
	write_header();
#if 1
	write_hex((unsigned char)(logo_width >> 8));
	write_hex((unsigned char)logo_width);
	write_hex((unsigned char)(logo_height >> 8));
	write_hex((unsigned char)logo_height);

	for (i = 0; i < sizeof(data_name); i++){
		write_hex(data_name[i]);
	}
	write_hex((unsigned char)(logo_width >> 8));
	write_hex((unsigned char)logo_width);
	write_hex((unsigned char)(logo_height >> 8));
	write_hex((unsigned char)logo_height);
#endif
	
#if 0
	/* write logo data */
	for (i = 0; i < logo_height; i++)
		for (j = 0; j < logo_width; j++) {
	 		for (k = 0; k < logo_clutsize; k++)
				if (is_equal(logo_data[i][j], logo_clut[k]))
					break;
			write_hex(k+32);
		}
	fputs("\n};\n\n", out);

	

	/* write logo clut */
	fprintf(out, "static unsigned char %s_clut[] __initdata = {\n",
		logoname);

	write_hex_cnt = 0;

	for (i = 0; i < sizeof(clut_name); i++){
		write_hex(clut_name[i]);
	}
	write_hex(logo_clutsize);

	for (i = 0; i < logo_clutsize; i++) {
		write_hex(logo_clut[i].red);
		write_hex(logo_clut[i].green);
		write_hex(logo_clut[i].blue);
	}

	for (i = logo_clutsize; i < (MAX_LINUX_LOGO_COLORS * 3); i++)
	{
		write_hex(32);
	}

	/* write logo structure and file footer */
#endif

#if 1
	for (i=logo_height; i>0; i--)
	{
		for (j=0; j<logo_width; j++)
		{	
				position = logo_data + (i-1)* logo_width * 3 + 3 * j;
#if 0
			write_hex(*(position));
			write_hex(*(position+1));
			write_hex(*(position+2));
#else
			write_hex(*(position));
			write_hex(*(position+1));
			write_hex(*(position+2));		
			write_hex(0);
#endif
		}
	}
#endif

	fputs("\n};\n\n", out);
	/* write logo clut */
	fprintf(out, "static unsigned char %s_clut[] __initdata = {\n",
		logoname);

	write_hex_cnt = 0;
	for (i = 0; i < sizeof(clut_name); i++){
		write_hex(clut_name[i]);
	}
	
	write_footer();
}

static void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(1);
}

static void usage(void)
{
    die("\n"
	"Usage: %s [options] <filename>\n"
	"\n"
	"Valid options:\n"
	"    -h          : display this usage information\n"
	"    -n <name>   : specify logo name (default: linux_logo)\n"
	"    -o <output> : output to file <output> instead of stdout\n"
	"    -t <type>   : specify logo type, one of\n"	                      
	"                      bmp : truecolour\n"
	"\n", programname);
}

int main(int argc, char *argv[])
{
    int opt;

    programname = argv[0];

    opterr = 0;
    while (1) {
	opt = getopt(argc, argv, "hn:o:t:");
	if (opt == -1)
	    break;

	switch (opt) {
	    case 'h':
		usage();
		break;

	    case 'n':
		logoname = optarg;
		break;

	    case 'o':
		outputname = optarg;
		break;

	    case 't':
		if (!strcmp(optarg, "bmp"))
		    logo_type = LINUX_LOGO_bmp;		
		else
			die("logo_type is wrong without bmp\n");
		break;

	    default:
		usage();
		break;
	}
    }
    if (optind != argc-1)
	usage();

    filename = argv[optind];

  	read_image();
    switch (logo_type) {
	case LINUX_LOGO_bmp:
	  	write_logo_bmp();
	    break;
	default :
		die("logo_type is wrong\n");
    }
    exit(0);
}

