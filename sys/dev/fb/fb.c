/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_fb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/uio.h>
#include <sys/fbio.h>
#include <sys/linker_set.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/fb/fbreg.h>

SET_DECLARE(videodriver_set, const video_driver_t);

/* local arrays */

/*
 * We need at least one entry each in order to initialize a video card
 * for the kernel console.  The arrays will be increased dynamically
 * when necessary.
 */

static int		vid_malloc;
static int		adapters = 1;
static video_adapter_t	*adp_ini;
static video_adapter_t	**adapter = &adp_ini;
static video_switch_t	*vidsw_ini;
       video_switch_t	**vidsw = &vidsw_ini;

#ifdef FB_INSTALL_CDEV
static struct cdevsw	*vidcdevsw_ini;
static struct cdevsw	**vidcdevsw = &vidcdevsw_ini;
#endif

#define ARRAY_DELTA	4

static int
vid_realloc_array(void)
{
	video_adapter_t **new_adp;
	video_switch_t **new_vidsw;
#ifdef FB_INSTALL_CDEV
	struct cdevsw **new_cdevsw;
#endif
	int newsize;
	int s;

	if (!vid_malloc)
		return ENOMEM;

	s = spltty();
	newsize = rounddown(adapters + ARRAY_DELTA, ARRAY_DELTA);
	new_adp = malloc(sizeof(*new_adp)*newsize, M_DEVBUF, M_WAITOK | M_ZERO);
	new_vidsw = malloc(sizeof(*new_vidsw)*newsize, M_DEVBUF,
	    M_WAITOK | M_ZERO);
#ifdef FB_INSTALL_CDEV
	new_cdevsw = malloc(sizeof(*new_cdevsw)*newsize, M_DEVBUF,
	    M_WAITOK | M_ZERO);
#endif
	bcopy(adapter, new_adp, sizeof(*adapter)*adapters);
	bcopy(vidsw, new_vidsw, sizeof(*vidsw)*adapters);
#ifdef FB_INSTALL_CDEV
	bcopy(vidcdevsw, new_cdevsw, sizeof(*vidcdevsw)*adapters);
#endif
	if (adapters > 1) {
		free(adapter, M_DEVBUF);
		free(vidsw, M_DEVBUF);
#ifdef FB_INSTALL_CDEV
		free(vidcdevsw, M_DEVBUF);
#endif
	}
	adapter = new_adp;
	vidsw = new_vidsw;
#ifdef FB_INSTALL_CDEV
	vidcdevsw = new_cdevsw;
#endif
	adapters = newsize;
	splx(s);

	if (bootverbose)
		printf("fb: new array size %d\n", adapters);

	return 0;
}

static void
vid_malloc_init(void *arg)
{
	vid_malloc = TRUE;
}

SYSINIT(vid_mem, SI_SUB_KMEM, SI_ORDER_ANY, vid_malloc_init, NULL);

/*
 * Low-level frame buffer driver functions
 * frame buffer subdrivers, such as the VGA driver, call these functions
 * to initialize the video_adapter structure and register it to the virtual
 * frame buffer driver `fb'.
 */

/* initialize the video_adapter_t structure */
void
vid_init_struct(video_adapter_t *adp, char *name, int type, int unit)
{
	adp->va_flags = 0;
	adp->va_name = name;
	adp->va_type = type;
	adp->va_unit = unit;
}

/* Register a video adapter */
int
vid_register(video_adapter_t *adp)
{
	const video_driver_t **list;
	const video_driver_t *p;
	int index;

	for (index = 0; index < adapters; ++index) {
		if (adapter[index] == NULL)
			break;
	}
	if (index >= adapters) {
		if (vid_realloc_array())
			return -1;
	}

	adp->va_index = index;
	adp->va_token = NULL;
	SET_FOREACH(list, videodriver_set) {
		p = *list;
		if (strcmp(p->name, adp->va_name) == 0) {
			adapter[index] = adp;
			vidsw[index] = p->vidsw;
			return index;
		}
	}

	return -1;
}

int
vid_unregister(video_adapter_t *adp)
{
	if ((adp->va_index < 0) || (adp->va_index >= adapters))
		return ENOENT;
	if (adapter[adp->va_index] != adp)
		return ENOENT;

	adapter[adp->va_index] = NULL;
	vidsw[adp->va_index] = NULL;
	return 0;
}

/* Get video I/O function table */
video_switch_t
*vid_get_switch(char *name)
{
	const video_driver_t **list;
	const video_driver_t *p;

	SET_FOREACH(list, videodriver_set) {
		p = *list;
		if (strcmp(p->name, name) == 0)
			return p->vidsw;
	}

	return NULL;
}

/*
 * Video card client functions
 * Video card clients, such as the console driver `syscons' and the frame
 * buffer cdev driver, use these functions to claim and release a card for
 * exclusive use.
 */

/* find the video card specified by a driver name and a unit number */
int
vid_find_adapter(char *driver, int unit)
{
	int i;

	for (i = 0; i < adapters; ++i) {
		if (adapter[i] == NULL)
			continue;
		if (strcmp("*", driver) && strcmp(adapter[i]->va_name, driver))
			continue;
		if ((unit != -1) && (adapter[i]->va_unit != unit))
			continue;
		return i;
	}
	return -1;
}

/* allocate a video card */
int
vid_allocate(char *driver, int unit, void *id)
{
	int index;
	int s;

	s = spltty();
	index = vid_find_adapter(driver, unit);
	if (index >= 0) {
		if (adapter[index]->va_token) {
			splx(s);
			return -1;
		}
		adapter[index]->va_token = id;
	}
	splx(s);
	return index;
}

int
vid_release(video_adapter_t *adp, void *id)
{
	int error;
	int s;

	s = spltty();
	if (adp->va_token == NULL) {
		error = EINVAL;
	} else if (adp->va_token != id) {
		error = EPERM;
	} else {
		adp->va_token = NULL;
		error = 0;
	}
	splx(s);
	return error;
}

/* Get a video adapter structure */
video_adapter_t
*vid_get_adapter(int index)
{
	if ((index < 0) || (index >= adapters))
		return NULL;
	return adapter[index];
}

/* Configure drivers: this is a backdoor for the console driver XXX */
int
vid_configure(int flags)
{
	const video_driver_t **list;
	const video_driver_t *p;

	SET_FOREACH(list, videodriver_set) {
		p = *list;
		if (p->configure != NULL)
			(*p->configure)(flags);
	}

	return 0;
}

/*
 * Virtual frame buffer cdev driver functions
 * The virtual frame buffer driver dispatches driver functions to
 * appropriate subdrivers.
 */

#define FB_DRIVER_NAME	"fb"

#ifdef FB_INSTALL_CDEV

#if 0 /* experimental */

static devclass_t	fb_devclass;

static int		fbprobe(device_t dev);
static int		fbattach(device_t dev);

static device_method_t fb_methods[] = {
	DEVMETHOD(device_probe,		fbprobe),
	DEVMETHOD(device_attach,	fbattach),

	DEVMETHOD_END
};

static driver_t fb_driver = {
	FB_DRIVER_NAME,
	fb_methods,
	0,
};

static int
fbprobe(device_t dev)
{
	int unit;

	unit = device_get_unit(dev);
	if (unit >= adapters)
		return ENXIO;
	if (adapter[unit] == NULL)
		return ENXIO;

	device_set_desc(dev, "generic frame buffer");
	return 0;
}

static int
fbattach(device_t dev)
{
	printf("fbattach: about to attach children\n");
	bus_generic_attach(dev);
	return 0;
}

#endif

#define FB_UNIT(dev)	dev2unit(dev)
#define FB_MKMINOR(unit) (u)

#if 0 /* experimental */
static d_open_t		fbopen;
static d_close_t	fbclose;
static d_read_t		fbread;
static d_write_t	fbwrite;
static d_ioctl_t	fbioctl;
static d_mmap_t		fbmmap;


static struct cdevsw fb_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	fbopen,
	.d_close =	fbclose,
	.d_read =	fbread,
	.d_write =	fbwrite,
	.d_ioctl =	fbioctl,
	.d_mmap =	fbmmap,
	.d_name =	FB_DRIVER_NAME,
};
#endif


static int
fb_modevent(module_t mod, int type, void *data) 
{ 

	switch (type) { 
	case MOD_LOAD: 
		break; 
	case MOD_UNLOAD: 
		printf("fb module unload - not possible for this module type\n"); 
		return EINVAL; 
	default:
		return EOPNOTSUPP;
	} 
	return 0; 
} 

static moduledata_t fb_mod = { 
	"fb", 
	fb_modevent, 
	NULL
}; 

DECLARE_MODULE(fb, fb_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

int
fb_attach(int unit, video_adapter_t *adp, struct cdevsw *cdevsw)
{
	int s;

	if (adp->va_index >= adapters)
		return EINVAL;
	if (adapter[adp->va_index] != adp)
		return EINVAL;

	s = spltty();
	adp->va_minor = unit;
	vidcdevsw[adp->va_index] = cdevsw;
	splx(s);

	printf("fb%d at %s%d\n", adp->va_index, adp->va_name, adp->va_unit);
	return 0;
}

int
fb_detach(int unit, video_adapter_t *adp, struct cdevsw *cdevsw)
{
	int s;

	if (adp->va_index >= adapters)
		return EINVAL;
	if (adapter[adp->va_index] != adp)
		return EINVAL;
	if (vidcdevsw[adp->va_index] != cdevsw)
		return EINVAL;

	s = spltty();
	vidcdevsw[adp->va_index] = NULL;
	splx(s);
	return 0;
}

/*
 * Generic frame buffer cdev driver functions
 * Frame buffer subdrivers may call these functions to implement common
 * driver functions.
 */

int genfbopen(genfb_softc_t *sc, video_adapter_t *adp, int flag, int mode,
	      struct thread *td)
{
	int s;

	s = spltty();
	if (!(sc->gfb_flags & FB_OPEN))
		sc->gfb_flags |= FB_OPEN;
	splx(s);
	return 0;
}

int genfbclose(genfb_softc_t *sc, video_adapter_t *adp, int flag, int mode,
	       struct thread *td)
{
	int s;

	s = spltty();
	sc->gfb_flags &= ~FB_OPEN;
	splx(s);
	return 0;
}

int genfbread(genfb_softc_t *sc, video_adapter_t *adp, struct uio *uio,
	      int flag)
{
	int size;
	int offset;
	int error;
	int len;

	error = 0;
	size = adp->va_buffer_size/adp->va_info.vi_planes;
	while (uio->uio_resid > 0) {
		if (uio->uio_offset >= size)
			break;
		offset = uio->uio_offset%adp->va_window_size;
		len = imin(uio->uio_resid, size - uio->uio_offset);
		len = imin(len, adp->va_window_size - offset);
		if (len <= 0)
			break;
		vidd_set_win_org(adp, uio->uio_offset);
		error = uiomove((caddr_t)(adp->va_window + offset), len, uio);
		if (error)
			break;
	}
	return error;
}

int genfbwrite(genfb_softc_t *sc, video_adapter_t *adp, struct uio *uio,
	       int flag)
{
	return ENODEV;
}

int genfbioctl(genfb_softc_t *sc, video_adapter_t *adp, u_long cmd,
	       caddr_t arg, int flag, struct thread *td)
{
	int error;

	if (adp == NULL)	/* XXX */
		return ENXIO;
	error = vidd_ioctl(adp, cmd, arg);
	if (error == ENOIOCTL)
		error = ENODEV;
	return error;
}

int genfbmmap(genfb_softc_t *sc, video_adapter_t *adp, vm_ooffset_t offset,
	      vm_paddr_t *paddr, int prot, vm_memattr_t *memattr)
{
	return vidd_mmap(adp, offset, paddr, prot, memattr);
}

#endif /* FB_INSTALL_CDEV */

static char
*adapter_name(int type)
{
    static struct {
	int type;
	char *name;
    } names[] = {
	{ KD_MONO,	"MDA" },
	{ KD_HERCULES,	"Hercules" },
	{ KD_CGA,	"CGA" },
	{ KD_EGA,	"EGA" },
	{ KD_VGA,	"VGA" },
	{ KD_TGA,	"TGA" },
	{ -1,		"Unknown" },
    };
    int i;

    for (i = 0; names[i].type != -1; ++i)
	if (names[i].type == type)
	    break;
    return names[i].name;
}

/*
 * Generic low-level frame buffer functions
 * The low-level functions in the frame buffer subdriver may use these
 * functions.
 */

void
fb_dump_adp_info(char *driver, video_adapter_t *adp, int level)
{
    if (level <= 0)
	return;

    printf("%s%d: %s%d, %s, type:%s (%d), flags:0x%x\n", 
	   FB_DRIVER_NAME, adp->va_index, driver, adp->va_unit, adp->va_name,
	   adapter_name(adp->va_type), adp->va_type, adp->va_flags);
    printf("%s%d: port:0x%lx-0x%lx, crtc:0x%lx, mem:0x%lx 0x%x\n",
	   FB_DRIVER_NAME, adp->va_index, (u_long)adp->va_io_base, 
	   (u_long)adp->va_io_base + adp->va_io_size - 1,
	   (u_long)adp->va_crtc_addr, (u_long)adp->va_mem_base, 
	   adp->va_mem_size);
    printf("%s%d: init mode:%d, bios mode:%d, current mode:%d\n",
	   FB_DRIVER_NAME, adp->va_index,
	   adp->va_initial_mode, adp->va_initial_bios_mode, adp->va_mode);
    printf("%s%d: window:%p size:%dk gran:%dk, buf:%p size:%dk\n",
	   FB_DRIVER_NAME, adp->va_index, 
	   (void *)adp->va_window, (int)adp->va_window_size/1024,
	   (int)adp->va_window_gran/1024, (void *)adp->va_buffer,
	   (int)adp->va_buffer_size/1024);
}

void
fb_dump_mode_info(char *driver, video_adapter_t *adp, video_info_t *info,
		  int level)
{
    if (level <= 0)
	return;

    printf("%s%d: %s, mode:%d, flags:0x%x ", 
	   driver, adp->va_unit, adp->va_name, info->vi_mode, info->vi_flags);
    if (info->vi_flags & V_INFO_GRAPHICS)
	printf("G %dx%dx%d, %d plane(s), font:%dx%d, ",
	       info->vi_width, info->vi_height, 
	       info->vi_depth, info->vi_planes, 
	       info->vi_cwidth, info->vi_cheight); 
    else
	printf("T %dx%d, font:%dx%d, ",
	       info->vi_width, info->vi_height, 
	       info->vi_cwidth, info->vi_cheight); 
    printf("win:0x%lx\n", (u_long)info->vi_window);
}

int
fb_type(int adp_type)
{
	static struct {
		int	fb_type;
		int	va_type;
	} types[] = {
		{ FBTYPE_MDA,		KD_MONO },
		{ FBTYPE_HERCULES,	KD_HERCULES },
		{ FBTYPE_CGA,		KD_CGA },
		{ FBTYPE_EGA,		KD_EGA },
		{ FBTYPE_VGA,		KD_VGA },
		{ FBTYPE_TGA,		KD_TGA },
	};
	int i;

	for (i = 0; i < nitems(types); ++i) {
		if (types[i].va_type == adp_type)
			return types[i].fb_type;
	}
	return -1;
}

int
fb_commonioctl(video_adapter_t *adp, u_long cmd, caddr_t arg)
{
	int error;
	int s;

	/* assert(adp != NULL) */

	error = 0;
	s = spltty();

	switch (cmd) {

	case FBIO_ADAPTER:	/* get video adapter index */
		*(int *)arg = adp->va_index;
		break;

	case FBIO_ADPTYPE:	/* get video adapter type */
		*(int *)arg = adp->va_type;
		break;

	case FBIO_ADPINFO:	/* get video adapter info */
	        ((video_adapter_info_t *)arg)->va_index = adp->va_index;
		((video_adapter_info_t *)arg)->va_type = adp->va_type;
		bcopy(adp->va_name, ((video_adapter_info_t *)arg)->va_name,
		      imin(strlen(adp->va_name) + 1,
			   sizeof(((video_adapter_info_t *)arg)->va_name))); 
		((video_adapter_info_t *)arg)->va_unit = adp->va_unit;
		((video_adapter_info_t *)arg)->va_flags = adp->va_flags;
		((video_adapter_info_t *)arg)->va_io_base = adp->va_io_base;
		((video_adapter_info_t *)arg)->va_io_size = adp->va_io_size;
		((video_adapter_info_t *)arg)->va_crtc_addr = adp->va_crtc_addr;
		((video_adapter_info_t *)arg)->va_mem_base = adp->va_mem_base;
		((video_adapter_info_t *)arg)->va_mem_size = adp->va_mem_size;
		((video_adapter_info_t *)arg)->va_window
#if defined(__amd64__) || defined(__i386__)
			= vtophys(adp->va_window);
#else
			= adp->va_window;
#endif
		((video_adapter_info_t *)arg)->va_window_size
			= adp->va_window_size;
		((video_adapter_info_t *)arg)->va_window_gran
			= adp->va_window_gran;
		((video_adapter_info_t *)arg)->va_window_orig
			= adp->va_window_orig;
		((video_adapter_info_t *)arg)->va_unused0
#if defined(__amd64__) || defined(__i386__)
			= adp->va_buffer != 0 ? vtophys(adp->va_buffer) : 0;
#else
			= adp->va_buffer;
#endif
		((video_adapter_info_t *)arg)->va_buffer_size
			= adp->va_buffer_size;
		((video_adapter_info_t *)arg)->va_mode = adp->va_mode;
		((video_adapter_info_t *)arg)->va_initial_mode
			= adp->va_initial_mode;
		((video_adapter_info_t *)arg)->va_initial_bios_mode
			= adp->va_initial_bios_mode;
		((video_adapter_info_t *)arg)->va_line_width
			= adp->va_line_width;
		((video_adapter_info_t *)arg)->va_disp_start.x
			= adp->va_disp_start.x;
		((video_adapter_info_t *)arg)->va_disp_start.y
			= adp->va_disp_start.y;
		break;

	case FBIO_MODEINFO:	/* get mode information */
		error = vidd_get_info(adp,
		    ((video_info_t *)arg)->vi_mode,
		    (video_info_t *)arg);
		if (error)
			error = ENODEV;
		break;

	case FBIO_FINDMODE:	/* find a matching video mode */
		error = vidd_query_mode(adp, (video_info_t *)arg);
		break;

	case FBIO_GETMODE:	/* get video mode */
		*(int *)arg = adp->va_mode;
		break;

	case FBIO_SETMODE:	/* set video mode */
		error = vidd_set_mode(adp, *(int *)arg);
		if (error)
			error = ENODEV;	/* EINVAL? */
		break;

	case FBIO_GETWINORG:	/* get frame buffer window origin */
		*(u_int *)arg = adp->va_window_orig;
		break;

	case FBIO_GETDISPSTART:	/* get display start address */
		((video_display_start_t *)arg)->x = adp->va_disp_start.x;
		((video_display_start_t *)arg)->y = adp->va_disp_start.y;
		break;

	case FBIO_GETLINEWIDTH:	/* get scan line width in bytes */
		*(u_int *)arg = adp->va_line_width;
		break;

	case FBIO_BLANK:	/* blank display */
		error = vidd_blank_display(adp, *(int *)arg);
		break;

	case FBIO_GETPALETTE:	/* get color palette */
	case FBIO_SETPALETTE:	/* set color palette */
		/* XXX */

	case FBIOPUTCMAP:
	case FBIOGETCMAP:
	case FBIOPUTCMAPI:
	case FBIOGETCMAPI:
		/* XXX */

	case FBIO_SETWINORG:	/* set frame buffer window origin */
	case FBIO_SETDISPSTART:	/* set display start address */
	case FBIO_SETLINEWIDTH:	/* set scan line width in pixel */

	case FBIOGTYPE:
	case FBIOGATTR:
	case FBIOSVIDEO:
	case FBIOGVIDEO:
	case FBIOVERTICAL:
	case FBIOSCURSOR:
	case FBIOGCURSOR:
	case FBIOSCURPOS:
	case FBIOGCURPOS:
	case FBIOGCURMAX:
	case FBIOMONINFO:
	case FBIOGXINFO:

	default:
		error = ENODEV;
		break;
	}

	splx(s);
	return error;
}
