
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static const char *programname;
static const char *filename;
static const char *logoname = "linux_logo";
static const char *outputname;
static FILE *out;


#define LINUX_LOGO_MONO		1	/* monochrome black/white */
#define LINUX_LOGO_VGA16	2	/* 16 colors VGA text palette */
#define LINUX_LOGO_CLUT224	3	/* 224 colors */
#define LINUX_LOGO_GRAY256	4	/* 256 levels grayscale */

static const char *logo_types[LINUX_LOGO_GRAY256+1] = {
    [LINUX_LOGO_MONO] = "LINUX_LOGO_MONO",
    [LINUX_LOGO_VGA16] = "LINUX_LOGO_VGA16",
    [LINUX_LOGO_CLUT224] = "LINUX_LOGO_CLUT224",
    [LINUX_LOGO_GRAY256] = "LINUX_LOGO_GRAY256"
};

#define MAX_LINUX_LOGO_COLORS	224

struct color {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
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


static int logo_type = LINUX_LOGO_CLUT224;
static unsigned int logo_width;
static unsigned int logo_height;
static struct color **logo_data;
static struct color logo_clut[MAX_LINUX_LOGO_COLORS];
static unsigned int logo_clutsize;

static void die(const char *fmt, ...)
    __attribute__ ((noreturn)) __attribute ((format (printf, 1, 2)));
static void usage(void) __attribute ((noreturn));


static unsigned int get_number(FILE *fp)
{
    int c, val;

    /* Skip leading whitespace */
    do {
	c = fgetc(fp);
	if (c == EOF)
	    die("%s: end of file\n", filename);
	if (c == '#') {
	    /* Ignore comments 'till end of line */
	    do {
		c = fgetc(fp);
		if (c == EOF)
		    die("%s: end of file\n", filename);
	    } while (c != '\n');
	}
    } while (isspace(c));

    /* Parse decimal number */
    val = 0;
    while (isdigit(c)) {
	val = 10*val+c-'0';
	c = fgetc(fp);
	if (c == EOF)
	    die("%s: end of file\n", filename);
    }
    return val;
}

static unsigned int get_number255(FILE *fp, unsigned int maxval)
{
    unsigned int val = get_number(fp);
    return (255*val+maxval/2)/maxval;
}

static void read_image(void)
{
    FILE *fp;
    unsigned int i, j;
    int magic;
    unsigned int maxval;

    /* open image file */
    fp = fopen(filename, "r");
    if (!fp)
	die("Cannot open file %s: %s\n", filename, strerror(errno));

    /* check file type and read file header */
    magic = fgetc(fp);
    if (magic != 'P')
	die("%s is not a PNM file\n", filename);
    magic = fgetc(fp);
    switch (magic) {
	case '1':
	case '2':
	case '3':
	    /* Plain PBM/PGM/PPM */
	    break;

	case '4':
	case '5':
	case '6':
	    /* Binary PBM/PGM/PPM */
	    die("%s: Binary PNM is not supported\n"
		"Use pnmnoraw(1) to convert it to ASCII PNM\n", filename);

	default:
	    die("%s is not a PNM file\n", filename);
    }
    logo_width = get_number(fp);
    logo_height = get_number(fp);

    /* allocate image data */
    logo_data = (struct color **)malloc(logo_height*sizeof(struct color *));
    if (!logo_data)
	die("%s\n", strerror(errno));
    for (i = 0; i < logo_height; i++) {
	logo_data[i] = malloc(logo_width*sizeof(struct color));
	if (!logo_data[i])
	    die("%s\n", strerror(errno));
    }

    /* read image data */
    switch (magic) {
	case '1':
	    /* Plain PBM */
	    for (i = 0; i < logo_height; i++)
		for (j = 0; j < logo_width; j++)
		    logo_data[i][j].red = logo_data[i][j].green =
			logo_data[i][j].blue = 255*(1-get_number(fp));
	    break;

	case '2':
	    /* Plain PGM */
	    maxval = get_number(fp);
	    for (i = 0; i < logo_height; i++)
		for (j = 0; j < logo_width; j++)
		    logo_data[i][j].red = logo_data[i][j].green =
			logo_data[i][j].blue = get_number255(fp, maxval);
	    break;

	case '3':
	    /* Plain PPM */
	    maxval = get_number(fp);
	    for (i = 0; i < logo_height; i++)
		for (j = 0; j < logo_width; j++) {
		    logo_data[i][j].red = get_number255(fp, maxval);
		    logo_data[i][j].green = get_number255(fp, maxval);
		    logo_data[i][j].blue = get_number255(fp, maxval);
		}
	    break;
    }

    /* close file */
    fclose(fp);
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
    fprintf(out, "\t.width\t\t= %d,\n", logo_width);
    fprintf(out, "\t.height\t\t= %d,\n", logo_height);
    if (logo_type == LINUX_LOGO_CLUT224) {
	fprintf(out, "\t.clutsize\t= %d,\n", logo_clutsize);
	fprintf(out, "\t.clut\t\t= %s_clut,\n", logoname);
    }
    fprintf(out, "\t.data\t\t= %s_data\n", logoname);
    fputs("};\n\n", out);

    /* close logo file */
    if (outputname)
	fclose(out);
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

static void write_logo_mono(void)
{
    unsigned int i, j;
    unsigned char val, bit;

    /* validate image */
    for (i = 0; i < logo_height; i++)
	for (j = 0; j < logo_width; j++)
	    if (!is_black(logo_data[i][j]) && !is_white(logo_data[i][j]))
		die("Image must be monochrome\n");

    /* write file header */
    write_header();

    /* write logo data */
    for (i = 0; i < logo_height; i++) {
	for (j = 0; j < logo_width;) {
	    for (val = 0, bit = 0x80; bit && j < logo_width; j++, bit >>= 1)
		if (logo_data[i][j].red)
		    val |= bit;
	    write_hex(val);
	}
    }

    /* write logo structure and file footer */
    write_footer();
}

static void write_logo_vga16(void)
{
    unsigned int i, j, k;
    unsigned char val;

    /* validate image */
    for (i = 0; i < logo_height; i++)
	for (j = 0; j < logo_width; j++) {
	    for (k = 0; k < 16; k++)
		if (is_equal(logo_data[i][j], clut_vga16[k]))
		    break;
	    if (k == 16)
		die("Image must use the 16 console colors only\n"
		    "Use ppmquant(1) -map clut_vga16.ppm to reduce the number "
		    "of colors\n");
	}

    /* write file header */
    write_header();

    /* write logo data */
    for (i = 0; i < logo_height; i++)
	for (j = 0; j < logo_width; j++) {
	    for (k = 0; k < 16; k++)
		if (is_equal(logo_data[i][j], clut_vga16[k]))
		    break;
	    val = k<<4;
	    if (++j < logo_width) {
		for (k = 0; k < 16; k++)
		    if (is_equal(logo_data[i][j], clut_vga16[k]))
			break;
		val |= k;
	    }
	    write_hex(val);
	}

    /* write logo structure and file footer */
    write_footer();
}

static void write_logo_clut224(void)
{
    unsigned int i, j, k;

    /* validate image */
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

    /* write file header */
    write_header();

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
    for (i = 0; i < logo_clutsize; i++) {
	write_hex(logo_clut[i].red);
	write_hex(logo_clut[i].green);
	write_hex(logo_clut[i].blue);
    }

    /* write logo structure and file footer */
    write_footer();
}

static void write_logo_gray256(void)
{
    unsigned int i, j;

    /* validate image */
    for (i = 0; i < logo_height; i++)
	for (j = 0; j < logo_width; j++)
	    if (!is_gray(logo_data[i][j]))
		die("Image must be grayscale\n");

    /* write file header */
    write_header();

    /* write logo data */
    for (i = 0; i < logo_height; i++)
	for (j = 0; j < logo_width; j++)
	    write_hex(logo_data[i][j].red);

    /* write logo structure and file footer */
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
	"                      mono    : monochrome black/white\n"
	"                      vga16   : 16 colors VGA text palette\n"
	"                      clut224 : 224 colors (default)\n"
	"                      gray256 : 256 levels grayscale\n"
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
		if (!strcmp(optarg, "mono"))
		    logo_type = LINUX_LOGO_MONO;
		else if (!strcmp(optarg, "vga16"))
		    logo_type = LINUX_LOGO_VGA16;
		else if (!strcmp(optarg, "clut224"))
		    logo_type = LINUX_LOGO_CLUT224;
		else if (!strcmp(optarg, "gray256"))
		    logo_type = LINUX_LOGO_GRAY256;
		else
		    usage();
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
	case LINUX_LOGO_MONO:
	    write_logo_mono();
	    break;

	case LINUX_LOGO_VGA16:
	    write_logo_vga16();
	    break;

	case LINUX_LOGO_CLUT224:
	    write_logo_clut224();
	    break;

	case LINUX_LOGO_GRAY256:
	    write_logo_gray256();
	    break;
    }
    exit(0);
}

