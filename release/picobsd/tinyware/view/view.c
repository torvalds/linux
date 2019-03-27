/*-
 * Copyright (c) 1998 Andrzej Bialecki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Small PNG viewer with scripting abilities
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/mouse.h>
#include <vgl.h>
#include <png.h>

#define NUMBER	8

extern char *optarg;
extern int optind;

/* Prototypes */
int kbd_action(int x, int y, char hotkey);

struct action {
	int zoom;
	int rotate;
	int Xshift,Yshift;
};

struct menu_item {
	char *descr;
	char hotkey;
	int (*func)(int x, int y, char hotkey);
};

struct menu_item std_menu[]= {
	{"q  Quit",'q',kbd_action},
	{"n  Next",'n',kbd_action},
	{"p  Previous",'p',kbd_action},
	{"Z  Zoom in",'Z',kbd_action},
	{"z  Zoom out",'z',kbd_action},
	{"r  Rotate",'r',kbd_action},
	{"R  Refresh",'R',kbd_action},
	{"l  Left",'l',kbd_action},
	{"h  Right",'h',kbd_action},
	{"j  Up",'j',kbd_action},
	{"k  Down",'k',kbd_action},
	{NULL,0,NULL}
};

char *progname;
VGLBitmap pic,bkg;
struct action a;
byte pal_red[256];
byte pal_green[256];
byte pal_blue[256];
byte pal_colors;
double screen_gamma;
int max_screen_colors=15;
int quit,changed;
char **pres;
int nimg=0;
int auto_chg=0;
int cur_img=0;
char act;
FILE *log;

void
usage()
{
	fprintf(stderr,"\nVGL graphics viewer, 1.0 (c) Andrzej Bialecki.\n");
	fprintf(stderr,"\nUsage:\n");
	fprintf(stderr,"\t%s [-r n] [-g n.n] filename\n",progname);
	fprintf(stderr,"\nwhere:\n");
	fprintf(stderr,"\t-r n\tchoose resolution:\n");
	fprintf(stderr,"\t\t0 - 640x480x16 (default)\n");
	fprintf(stderr,"\t\t1 - 640x200x256\n");
	fprintf(stderr,"\t\t2 - 320x240x256\n");
	fprintf(stderr,"\t-g n.n\tset screen gamma (1.3 by default)\n");
	fprintf(stderr,"\n");
}

int
pop_up(char *title,int x, int y)
{
	VGLBitmap sav,clr;
	int x1,y1,width,height,i,j;
	int last_pos,cur_pos,max_item;
	char buttons;
	char *t;

	sav.Type=VGLDisplay->Type;
	clr.Type=VGLDisplay->Type;
	width=0;
	height=0;
	max_item=0;
	i=0;
	while(std_menu[i].descr!=NULL) {
		height++;
		max_item++;
		if(strlen(std_menu[i].descr)>width) width=strlen(std_menu[i].descr);
		i++;
	}
	width=width*8+2;
	height=height*9+4+8;
	sav.Xsize=width;
	sav.Ysize=height;
	clr.Xsize=width;
	clr.Ysize=height;
	sav.Bitmap=(byte *)calloc(width*height,1);
	clr.Bitmap=(byte *)calloc(width*height,1);
	if(x>(VGLDisplay->Xsize-width)) x1=VGLDisplay->Xsize-width;
	else x1=x;
	if(y>(VGLDisplay->Ysize-height)) y1=VGLDisplay->Ysize-height;
	else y1=y;
	VGLMouseMode(VGL_MOUSEHIDE);
	VGLBitmapCopy(VGLDisplay,x1,y1,&sav,0,0,width,height);
	VGLFilledBox(VGLDisplay,x1,y1,x1+width-1,y1+height-1,pal_colors-1);
	VGLBitmapString(VGLDisplay,x1+1,y1+1,title,0,pal_colors-1,0,0);
	VGLLine(VGLDisplay,x1,y1+9,x1+width,y1+9,0);
	i=0;
	while(std_menu[i].descr!=NULL) {
		VGLBitmapString(VGLDisplay,x1+1,y1+11+i*9,std_menu[i].descr,0,pal_colors-1,0,0);
		i++;
	}
	last_pos=-1;
	VGLMouseMode(VGL_MOUSESHOW);
	do {
		pause();
		VGLMouseStatus(&x,&y,&buttons);
		cur_pos=(y-y1-11)/9;
		if((cur_pos<0)||(cur_pos>max_item-1)) {
			if(last_pos==-1) last_pos=0;
			VGLBitmapString(VGLDisplay,x1+1,y1+11+last_pos*9,std_menu[last_pos].descr,0,pal_colors-1,0,0);
			last_pos=-1;
		} else if(last_pos!=cur_pos) {
			if(last_pos==-1) last_pos=0;
			VGLBitmapString(VGLDisplay,x1+1,y1+11+last_pos*9,std_menu[last_pos].descr,0,pal_colors-1,0,0);
			VGLBitmapString(VGLDisplay,x1+1,y1+11+cur_pos*9,std_menu[cur_pos].descr,pal_colors/2+1,pal_colors-1,0,0);
			last_pos=cur_pos;
		}
	} while (buttons & MOUSE_BUTTON3DOWN);
	VGLMouseMode(VGL_MOUSEHIDE);
	/* XXX Screws up totally when r==3. Libvgl bug! */
	VGLBitmapCopy(&clr,0,0,VGLDisplay,x1,y1,width,height);
	VGLBitmapCopy(&sav,0,0,VGLDisplay,x1,y1,width,height);
	VGLMouseMode(VGL_MOUSESHOW);
	free(sav.Bitmap);
	free(clr.Bitmap);
	changed++;
	if((cur_pos>=0) && (cur_pos<max_item)) {
		std_menu[cur_pos].func(x,y,std_menu[cur_pos].hotkey);
	}
	changed++;
	return(0);
}

void
display(	VGLBitmap *pic,
		byte *red,
		byte *green,
		byte *blue,
		struct action *e)
{
	VGLBitmap target;
	int x,y,i=0,j=0;

	VGLMouseMode(VGL_MOUSEHIDE);
	VGLRestorePalette();
	/* XXX Broken in r!=2. Libvgl bug. */
	//VGLClear(VGLDisplay,0);
	VGLBitmapCopy(&bkg,0,0,VGLDisplay,0,0,bkg.Xsize,bkg.Ysize);

	if(e!=NULL) {
		if(e->zoom!=1 || e->rotate) {
			target.Bitmap=(byte *)calloc(pic->Xsize*pic->Ysize*e->zoom*e->zoom,1);
			if(e->rotate) {
				target.Xsize=pic->Ysize*e->zoom;
				target.Ysize=pic->Xsize*e->zoom;
			} else {
				target.Xsize=pic->Xsize*e->zoom;
				target.Ysize=pic->Ysize*e->zoom;
			}
			target.Type=pic->Type;
			for(x=0;x<pic->Xsize;x++) {
				for(y=0;y<pic->Ysize;y++) {
					for(i=0;i<e->zoom;i++) {
						for(j=0;j<e->zoom;j++) {
							if(e->rotate) {
								VGLSetXY(&target,target.Xsize-(e->zoom*y+i),e->zoom*x+j,VGLGetXY(pic,x,y));
							} else {
								VGLSetXY(&target,e->zoom*x+i,e->zoom*y+j,VGLGetXY(pic,x,y));
							}
						}
					}
				}
			}
		} else {
			target.Bitmap=(byte *)calloc(pic->Xsize*pic->Ysize,sizeof(byte));
			target.Xsize=pic->Xsize;
			target.Ysize=pic->Ysize;
			target.Type=pic->Type;
			VGLBitmapCopy(pic,0,0,&target,0,0,pic->Xsize,pic->Ysize);
		}
	} else {
		target.Bitmap=(byte *)calloc(pic->Xsize*pic->Ysize,sizeof(byte));
		target.Xsize=pic->Xsize;
		target.Ysize=pic->Ysize;
		target.Type=pic->Type;
		VGLBitmapCopy(pic,0,0,&target,0,0,pic->Xsize,pic->Ysize);
	}
	VGLSetPalette(red, green, blue);
	if(e!=NULL) {
		VGLBitmapCopy(&target,0,0,VGLDisplay,e->Xshift,e->Yshift,target.Xsize,target.Ysize);
	} else {
		VGLBitmapCopy(&target,0,0,VGLDisplay,0,0,target.Xsize,target.Ysize);
	}
	VGLMouseMode(VGL_MOUSESHOW);
	free(target.Bitmap);
}

int
png_load(char *filename)
{
	int i,j,k;
	FILE *fd;
	u_char header[NUMBER];
	png_structp png_ptr;
	png_infop info_ptr,end_info;
	png_uint_32 width,height;
	int bit_depth,color_type,interlace_type;
	int compression_type,filter_type;
	int channels,rowbytes;
	double gamma;
	png_colorp palette;
	int num_palette;
	png_bytep *row_pointers;
	char c;
	int res=0;

	fd=fopen(filename,"rb");
	
	if(fd==NULL) {
		VGLEnd();
		perror("fopen");
		exit(1);
	}
	fread(header,1,NUMBER,fd);
	if(!png_check_sig(header,NUMBER)) {
		fprintf(stderr,"Not a PNG file.\n");
		return(-1);
	}
	png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,(void *)NULL,
		NULL,NULL);
	info_ptr=png_create_info_struct(png_ptr);
	end_info=png_create_info_struct(png_ptr);
	if(!png_ptr || !info_ptr || !end_info) {
		VGLEnd();
		fprintf(stderr,"failed to allocate needed structs!\n");
		png_destroy_read_struct(&png_ptr,&info_ptr,&end_info);
		return(-1);
	}
	png_set_sig_bytes(png_ptr,NUMBER);
	png_init_io(png_ptr,fd);
	png_read_info(png_ptr,info_ptr);
	png_get_IHDR(png_ptr,info_ptr,&width,&height,&bit_depth,
		&color_type,&interlace_type,&compression_type,&filter_type);
	png_get_PLTE(png_ptr,info_ptr,&palette,&num_palette);
	channels=png_get_channels(png_ptr,info_ptr);
	rowbytes=png_get_rowbytes(png_ptr,info_ptr);
	if(bit_depth==16)
		png_set_strip_16(png_ptr);
	if(color_type & PNG_COLOR_MASK_ALPHA) 
		png_set_strip_alpha(png_ptr);
	if(png_get_gAMA(png_ptr,info_ptr,&gamma))
		png_set_gamma(png_ptr,screen_gamma,gamma);
	else
	png_set_gamma(png_ptr,screen_gamma,0.45);
	if(res==0) {
		/* Dither */
		if(color_type & PNG_COLOR_MASK_COLOR) {
			if(png_get_valid(png_ptr,info_ptr,PNG_INFO_PLTE)) {
				png_uint_16p histogram;
				png_get_hIST(png_ptr,info_ptr,&histogram);
				png_set_dither(png_ptr,palette,num_palette,max_screen_colors,histogram,0);
			} else {
				png_color std_color_cube[16]={
					{0x00,0x00,0x00},
					{0x02,0x02,0x02},
					{0x04,0x04,0x04},
					{0x06,0x06,0x06},
					{0x08,0x08,0x08},
					{0x0a,0x0a,0x0a},
					{0x0c,0x0c,0x0c},
					{0x0e,0x0e,0x0e},
					{0x10,0x10,0x10},
					{0x12,0x12,0x12},
					{0x14,0x14,0x14},
					{0x16,0x16,0x16},
					{0x18,0x18,0x18},
					{0x1a,0x1a,0x1a},
					{0x1d,0x1d,0x1d},
					{0xff,0xff,0xff},
				};
				png_set_dither(png_ptr,std_color_cube,max_screen_colors,max_screen_colors,NULL,0);
			}
		}
	}
	png_set_packing(png_ptr);
	if(png_get_valid(png_ptr,info_ptr,PNG_INFO_sBIT)) {
		png_color_8p sig_bit;

		png_get_sBIT(png_ptr,info_ptr,&sig_bit);
		png_set_shift(png_ptr,sig_bit);
	}
	png_read_update_info(png_ptr,info_ptr);
	png_get_IHDR(png_ptr,info_ptr,&width,&height,&bit_depth,
		&color_type,&interlace_type,&compression_type,&filter_type);
	png_get_PLTE(png_ptr,info_ptr,&palette,&num_palette);
	channels=png_get_channels(png_ptr,info_ptr);
	rowbytes=png_get_rowbytes(png_ptr,info_ptr);
	row_pointers=malloc(height*sizeof(png_bytep));
	for(i=0;i<height;i++) {
		row_pointers[i]=malloc(rowbytes);
	}
	png_read_image(png_ptr,row_pointers);
	png_read_end(png_ptr,end_info);
	png_destroy_read_struct(&png_ptr,&info_ptr,&end_info);
	fclose(fd);
	/* Set palette */
	if(res) k=2;
	else k=2;
	for(i=0;i<256;i++) {
	 	pal_red[i]=255;
	 	pal_green[i]=255;
	 	pal_blue[i]=255;
	}
	for(i=0;i<num_palette;i++) {
	 	pal_red[i]=(palette+i)->red>>k;
	 	pal_green[i]=(palette+i)->green>>k;
	 	pal_blue[i]=(palette+i)->blue>>k;
	}
	pal_colors=num_palette;
	if(pic.Bitmap!=NULL) free(pic.Bitmap);
	pic.Bitmap=(byte *)calloc(rowbytes*height,sizeof(byte));
	pic.Type=MEMBUF;
	pic.Xsize=rowbytes;
	pic.Ysize=height;
	for(i=0;i<rowbytes;i++) {
		for(j=0;j<height;j++) {
			VGLSetXY(&pic,
			i,j,row_pointers[j][i]);
		}
	}
	a.zoom=1;
	a.Xshift=(VGLDisplay->Xsize-pic.Xsize)/2;
	a.Yshift=(VGLDisplay->Ysize-pic.Ysize)/2;
	a.rotate=0;
	return(0);
}

void
kbd_handler(int sig)
{
	u_char buf[10];
	int res;

	res=read(0,&buf,10);
	changed++;
	act=buf[res-1];
}

int
kbd_action(int x, int y, char key)
{
	changed=0;
	if(key!='n') auto_chg=0;
	switch(key) {
	case 'q':
		quit=1;
		break;
	case 'Z':
		a.zoom++;
		changed++;
		break;
	case 'z':
		a.zoom--;
		if(a.zoom<1) a.zoom=1;
		changed++;
		break;
	case 'l':
		a.Xshift+=VGLDisplay->Xsize/5;
		changed++;
		break;
	case 'h':
		a.Xshift-=VGLDisplay->Xsize/5;
		changed++;
		break;
	case 'k':
		a.Yshift+=VGLDisplay->Ysize/5;
		changed++;
		break;
	case 'j':
		a.Yshift-=VGLDisplay->Ysize/5;
		changed++;
		break;
	case 'R':
		changed++;
		break;
	case 'r':
		if(a.rotate) a.rotate=0;
		else a.rotate=1;
		changed++;
		break;
	case '\n':
	case 'n':
		if(nimg>0) {
			if(cur_img<nimg-1) {
				cur_img++;
			} else {
				cur_img=0;
			}
			png_load(pres[cur_img]);
			changed++;
		}
		break;
	case 'p':
		if(nimg>0) {
			if(cur_img>0) {
				cur_img--;
			} else {
				cur_img=nimg-1;
			}
			png_load(pres[cur_img]);
			changed++;
		}
		break;
	}
	act=0;
}

int
main(int argc, char *argv[])
{
	int i,j,k;
	char c;
	int res=0;
	int x,y;
	char buttons;
	struct termios t_new,t_old;
	FILE *fsc;

	char buf[100];

	progname=argv[0];
	screen_gamma=1.5;
#ifdef DEBUG
	log=fopen("/png/view.log","w");
#endif
	while((c=getopt(argc,argv,"r:g:"))!=-1) {
		switch(c) {
		case 'r':
			res=atoi(optarg);
			if(res>0) max_screen_colors=256;
			break;
		case 'g':
			screen_gamma=atof(optarg);
			break;
		case '?':
		default:
			usage();
			exit(0);
		}
	}
	switch(res) {
	case 0:
		VGLInit(SW_CG640x480);
		break;
	case 1:
		VGLInit(SW_VGA_CG320);
		break;
	case 2:
		VGLInit(SW_VGA_MODEX);
		break;
	default:
		fprintf(stderr,"No such resolution!\n");
		usage();
		exit(-1);
	}
#ifdef DEBUG
	fprintf(log,"VGL initialised\n");
#endif
	VGLSavePalette();
	if(argc>optind) {
		res=png_load(argv[optind]);
	} else {
		VGLEnd();
		usage();
		exit(0);
	}
	if(res) {
		/* Hmm... Script? */
		fsc=fopen(argv[optind],"r");
#ifdef DEBUG
		fprintf(log,"Trying script %s\n",argv[optind]);
#endif
		fgets(buf,99,fsc);
		buf[strlen(buf)-1]='\0';
		if(strncmp("VIEW SCRIPT",buf,11)!=NULL) {
			VGLEnd();
			usage();
		}
		if(strlen(buf)>12) {
			auto_chg=atoi(buf+12);
		}
		fgets(buf,99,fsc);
		buf[strlen(buf)-1]='\0';
		nimg=atoi(buf);
		if(nimg==0) {
			VGLEnd();
			usage();
		}
		pres=(char **)calloc(nimg,sizeof(char *));
		for(i=0;i<nimg;i++) {
			fgets(buf,99,fsc);
			buf[strlen(buf)-1]='\0';
			pres[i]=strdup(buf);
		}
		fclose(fsc);
		cur_img=0;
#ifdef DEBUG
		fprintf(log,"Script with %d entries\n",nimg);
#endif
		png_load(pres[cur_img]);
	}
	VGLMouseInit(VGL_MOUSEHIDE);
	/* Prepare the keyboard */
	tcgetattr(0,&t_old);
	memcpy(&t_new,&t_old,sizeof(struct termios));
	cfmakeraw(&t_new);
	tcsetattr(0,TCSAFLUSH,&t_new);
	fcntl(0,F_SETFL,O_ASYNC);
	/* XXX VGLClear doesn't work.. :-(( Prepare a blank background */
	bkg.Bitmap=(byte *)calloc(VGLDisplay->Xsize*VGLDisplay->Ysize,1);
	bkg.Xsize=VGLDisplay->Xsize;
	bkg.Ysize=VGLDisplay->Ysize;
	bkg.Type=VGLDisplay->Type;
	signal(SIGIO,kbd_handler);
	a.zoom=1;
	a.Xshift=(VGLDisplay->Xsize-pic.Xsize)/2;
	a.Yshift=(VGLDisplay->Ysize-pic.Ysize)/2;
	a.rotate=0;
	quit=0;
	changed=0;
	display(&pic,pal_red,pal_green,pal_blue,&a);
	while(!quit) {
		if(act) {
#ifdef DEBUG
			fprintf(log,"kbd_action(%c)\n",act);
#endif
			kbd_action(x,y,act);
		}
		if(quit) break;
		if(changed) {
#ifdef DEBUG
			fprintf(log,"changed, redisplaying\n");
#endif
			display(&pic,pal_red,pal_green,pal_blue,&a);
			changed=0;
		}
		if(auto_chg) {
			sleep(auto_chg);
			kbd_action(x,y,'n');
		} else {
			pause();
		}
		VGLMouseStatus(&x,&y,&buttons);
		if(buttons & MOUSE_BUTTON3DOWN) {
#ifdef DEBUG
			fprintf(log,"pop_up called\n");
#endif
			pop_up("View",x,y);
		}
	}
	VGLEnd();
#ifdef DEBUG
	fclose(log);
#endif
	exit(0);
}
