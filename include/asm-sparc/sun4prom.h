/*
 * sun4prom.h -- interface to sun4 PROM monitor.  We don't use most of this,
 *               so most of these are just placeholders.
 */

#ifndef _SUN4PROM_H_
#define _SUN4PROM_H_

/*
 * Although this looks similar to an romvec for a OpenProm machine, it is 
 * actually closer to what was used in the Sun2 and Sun3.
 *
 * V2 entries exist only in version 2 PROMs and later, V3 in version 3 and later.
 * 
 * Many of the function prototypes are guesses.  Some are certainly wrong.
 * Use with care.
 */

typedef struct {
	char		*initSP;		/* Initial system stack ptr */
	void		(*startmon)(void);	/* Initial PC for hardware */
	int		*diagberr;		/* Bus err handler for diags */
	struct linux_arguments_v0 **bootParam; /* Info for bootstrapped pgm */
 	unsigned int	*memorysize;		/* Usable memory in bytes */
	unsigned char	(*getchar)(void);	/* Get char from input device */ 
	void		(*putchar)(char);	/* Put char to output device */
	int		(*mayget)(void);	/* Maybe get char, or -1 */
	int		(*mayput)(int);		/* Maybe put char, or -1 */
	unsigned char	*echo;			/* Should getchar echo? */
	unsigned char	*insource;		/* Input source selector */
	unsigned char	*outsink;		/* Output sink selector */
	int		(*getkey)(void);	/* Get next key if one exists */
	void		(*initgetkey)(void);	/* Initialize get key */
	unsigned int	*translation;		/* Kbd translation selector */
	unsigned char	*keybid;		/* Keyboard ID byte */
	int		*screen_x;		/* V2: Screen x pos (r/o) */
	int		*screen_y;		/* V2: Screen y pos (r/o) */
	struct keybuf	*keybuf;		/* Up/down keycode buffer */
	char		*monid;			/* Monitor version ID */
	void		(*fbwritechar)(char);	/* Write a character to FB */
	int		*fbAddr;		/* Address of frame buffer */
	char		**font;			/* Font table for FB */
	void		(*fbwritestr)(char *);	/* Write string to FB */
	void		(*reboot)(char *);	/* e.g. reboot("sd()vmlinux") */
	unsigned char	*linebuf;		/* The line input buffer */
	unsigned char	**lineptr;		/* Cur pointer into linebuf */
	int		*linesize;		/* length of line in linebuf */
	void		(*getline)(char *);	/* Get line from user */
	unsigned char	(*getnextchar)(void);	/* Get next char from linebuf */
	unsigned char	(*peeknextchar)(void);	/* Peek at next char */
	int		*fbthere;		/* =1 if frame buffer there */
	int		(*getnum)(void);	/* Grab hex num from line */
	int		(*printf)(char *, ...);	/* See prom_printf() instead */ 
	void		(*printhex)(int);	/* Format N digits in hex */
	unsigned char	*leds;			/* RAM copy of LED register */
	void		(*setLEDs)(unsigned char *);	/* Sets LED's and RAM copy */
	void		(*NMIaddr)(void *);	/* Addr for level 7 vector */
	void		(*abortentry)(void);	/* Entry for keyboard abort */
	int		*nmiclock;		/* Counts up in msec */
	int		*FBtype;		/* Frame buffer type */
 	unsigned int	romvecversion;		/* Version number for this romvec */
	struct globram  *globram;		/* monitor global variables ??? */
	void *		kbdaddr;		/* Addr of keyboard in use */
	int		*keyrinit;		/* ms before kbd repeat */
	unsigned char	*keyrtick; 		/* ms between repetitions */
	unsigned int	*memoryavail;		/* V1: Main mem usable size */
	long		*resetaddr;		/* where to jump on a reset */
	long		*resetmap;		/* pgmap entry for resetaddr */
	void		(*exittomon)(void);	/* Exit from user program */
	unsigned char	**memorybitmap;		/* V1: &{0 or &bits} */
	void		(*setcxsegmap)(int ctxt, char *va, int pmeg);	/* Set seg in any context */
	void		(**vector_cmd)(void *);	/* V2: Handler for 'v' cmd */
	unsigned long	*expectedtrapsig;	/* V3: Location of the expected trap signal */
	unsigned long	*trapvectorbasetable;	/* V3: Address of the trap vector table */
	int		unused1;
	int		unused2;
	int		unused3;
	int		unused4;
} linux_sun4_romvec;

extern linux_sun4_romvec *sun4_romvec;

#endif /* _SUN4PROM_H_ */
