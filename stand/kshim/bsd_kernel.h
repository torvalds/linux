/* $FreeBSD$ */
/*-
 * Copyright (c) 2011 Hans Petter Selasky. All rights reserved.
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
 */

#ifndef _BSD_KERNEL_H_
#define	_BSD_KERNEL_H_

#define	_KERNEL
#undef __FreeBSD_version
#define	__FreeBSD_version 1100000

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/errno.h>

#define	howmany(x, y)	(((x)+((y)-1))/(y))
#define	nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#define	isalpha(x) (((x) >= 'a' && (x) <= 'z') || ((x) >= 'A' && (x) <= 'Z'))
#define	isdigit(x) ((x) >= '0' && (x) <= '9')
#define	panic(...) do { printf("USB PANIC: " __VA_ARGS__); while (1) ; } while (0)
#define	rebooting 0
#define	M_USB 0
#define	M_USBDEV 0
#define	USB_PROC_MAX 3
#define	USB_BUS_GIANT_PROC(bus) (usb_process + 2)
#define	USB_BUS_NON_GIANT_BULK_PROC(bus) (usb_process + 2)
#define	USB_BUS_NON_GIANT_ISOC_PROC(bus) (usb_process + 2)
#define	USB_BUS_EXPLORE_PROC(bus) (usb_process + 0)
#define	USB_BUS_CONTROL_XFER_PROC(bus) (usb_process + 1)
#define	SYSCTL_DECL(...)
struct sysctl_ctx_list {
};
struct sysctl_req {
	void		*newptr;
};
#define	SYSCTL_HANDLER_ARGS void *oidp, void *arg1,	\
	uint32_t arg2, struct sysctl_req *req
#define	SYSCTL_NODE(name,...) struct { } name __used
#define	SYSCTL_INT(...)
#define	SYSCTL_UINT(...)
#define	SYSCTL_PROC(...)
#define	SYSCTL_ADD_NODE(...) NULL
#define	SYSCTL_ADD_U16(...) NULL
#define	SYSCTL_ADD_PROC(...) NULL
#define	sysctl_handle_int(...) EOPNOTSUPP
#define	sysctl_handle_string(...) EOPNOTSUPP
#define	sysctl_ctx_init(ctx) do { (void)(ctx); } while (0)
#define	sysctl_ctx_free(ctx) do { (void)(ctx); } while (0)
#define	TUNABLE_INT(...)
#define	MALLOC_DECLARE(...)
#define	MALLOC_DEFINE(...)
#define	EVENTHANDLER_DECLARE(...)
#define	EVENTHANDLER_INVOKE(...)
#define	KASSERT(...)
#define	SCHEDULER_STOPPED(x) (0)
#define	PI_SWI(...) (0)
#define	UNIQ_NAME(x) x
#define	UNIQ_NAME_STR(x) #x
#define	DEVCLASS_MAXUNIT 32
#define	MOD_LOAD 1
#define	MOD_UNLOAD 2
#define	DEVMETHOD(what,func) { #what, (void *)&func }
#define	DEVMETHOD_END {0,0}
#define	EARLY_DRIVER_MODULE(a, b, c, d, e, f, g)	DRIVER_MODULE(a, b, c, d, e, f)
#define	DRIVER_MODULE(name, busname, driver, devclass, evh, arg)	\
  static struct module_data bsd_##name##_##busname##_driver_mod = {	\
	evh, arg, #busname, #name, #busname "/" #name,			\
	&driver, &devclass, { 0, 0 } };					\
SYSINIT(bsd_##name##_##busname##_driver_mod, SI_SUB_DRIVERS,		\
  SI_ORDER_MIDDLE, module_register,					\
  &bsd_##name##_##busname##_driver_mod)
#define	SYSINIT(uniq, subs, order, _func, _data)	\
const struct sysinit UNIQ_NAME(sysinit_##uniq) = {	\
	.func = (_func),				\
	.data = __DECONST(void *, _data)		\
};							\
SYSINIT_ENTRY(uniq##_entry, "sysinit", (subs),		\
    (order), "const struct sysinit",			\
    UNIQ_NAME_STR(sysinit_##uniq), "SYSINIT")

#define	SYSUNINIT(uniq, subs, order, _func, _data)	\
const struct sysinit UNIQ_NAME(sysuninit_##uniq) = {	\
	.func = (_func),				\
	.data = __DECONST(void *, _data)		\
};							\
SYSINIT_ENTRY(uniq##_entry, "sysuninit", (subs),	\
    (order), "const struct sysuninit",			\
    UNIQ_NAME_STR(sysuninit_##uniq), "SYSUNINIT")
#define	MODULE_DEPEND(...)
#define	MODULE_VERSION(...)
#define	NULL ((void *)0)
#define	BUS_SPACE_BARRIER_READ 0x01
#define	BUS_SPACE_BARRIER_WRITE 0x02
#define	hz 1000
#undef PAGE_SIZE
#define	PAGE_SIZE 4096
#undef MIN
#define	MIN(a,b) (((a) < (b)) ? (a) : (b))
#undef MAX
#define	MAX(a,b) (((a) > (b)) ? (a) : (b))
#define	MTX_DEF 0
#define	MTX_SPIN 0
#define	MTX_RECURSE 0
#define	SX_DUPOK 0
#define	SX_NOWITNESS 0
#define	WITNESS_WARN(...)
#define	cold 0
#define	BUS_PROBE_GENERIC 0
#define	BUS_PROBE_DEFAULT (-20)
#define	CALLOUT_RETURNUNLOCKED 0x1
#undef ffs
#define	ffs(x) __builtin_ffs(x)
#undef va_list
#define	va_list __builtin_va_list
#undef va_size
#define	va_size(type) __builtin_va_size(type)
#undef va_start
#define	va_start(ap, last) __builtin_va_start(ap, last)
#undef va_end
#define	va_end(ap) __builtin_va_end(ap)
#undef va_arg
#define	va_arg(ap, type) __builtin_va_arg((ap), type)
#define	DEVICE_ATTACH(dev, ...) \
  (((device_attach_t *)(device_get_method(dev, "device_attach")))(dev,## __VA_ARGS__))
#define	DEVICE_DETACH(dev, ...) \
  (((device_detach_t *)(device_get_method(dev, "device_detach")))(dev,## __VA_ARGS__))
#define	DEVICE_PROBE(dev, ...) \
  (((device_probe_t *)(device_get_method(dev, "device_probe")))(dev,## __VA_ARGS__))
#define	DEVICE_RESUME(dev, ...) \
  (((device_resume_t *)(device_get_method(dev, "device_resume")))(dev,## __VA_ARGS__))
#define	DEVICE_SHUTDOWN(dev, ...) \
  (((device_shutdown_t *)(device_get_method(dev, "device_shutdown")))(dev,## __VA_ARGS__))
#define	DEVICE_SUSPEND(dev, ...) \
  (((device_suspend_t *)(device_get_method(dev, "device_suspend")))(dev,## __VA_ARGS__))
#define	USB_HANDLE_REQUEST(dev, ...) \
  (((usb_handle_request_t *)(device_get_method(dev, "usb_handle_request")))(dev,## __VA_ARGS__))
#define	USB_TAKE_CONTROLLER(dev, ...) \
  (((usb_take_controller_t *)(device_get_method(dev, "usb_take_controller")))(dev,## __VA_ARGS__))
#define	GPIO_PIN_SET(dev, ...) \
  (((gpio_pin_set_t *)(device_get_method(dev, "gpio_pin_set")))(dev,## __VA_ARGS__))
#define	GPIO_PIN_SETFLAGS(dev, ...) \
  (((gpio_pin_setflags_t *)(device_get_method(dev, "gpio_pin_setflags")))(dev,## __VA_ARGS__))

enum {
	SI_SUB_DUMMY = 0x0000000,
	SI_SUB_LOCK = 0x1B00000,
	SI_SUB_KLD = 0x2000000,
	SI_SUB_DRIVERS = 0x3100000,
	SI_SUB_PSEUDO = 0x7000000,
	SI_SUB_KICK_SCHEDULER = 0xa000000,
	SI_SUB_RUN_SCHEDULER = 0xfffffff
};

enum {
	SI_ORDER_FIRST = 0x0000000,
	SI_ORDER_SECOND = 0x0000001,
	SI_ORDER_THIRD = 0x0000002,
	SI_ORDER_FOURTH = 0x0000003,
	SI_ORDER_MIDDLE = 0x1000000,
	SI_ORDER_ANY = 0xfffffff	/* last */
};

struct uio;
struct thread;
struct malloc_type;
struct usb_process;

#ifndef INT32_MAX
#define	INT32_MAX 0x7fffffff
#endif

#ifndef HAVE_STANDARD_DEFS
#define	_UINT8_T_DECLARED
typedef unsigned char uint8_t;
#define	_INT8_T_DECLARED
typedef signed char int8_t;
#define	_UINT16_T_DECLARED
typedef unsigned short uint16_t;
#define	_INT16_T_DECLARED
typedef signed short int16_t;
#define	_UINT32_T_DECLARED
typedef unsigned int uint32_t;
#define	_INT32_T_DECLARED
typedef signed int int32_t;
#define	_UINT64_T_DECLARED
typedef unsigned long long uint64_t;
#define	_INT16_T_DECLARED
typedef signed long long int64_t;

typedef uint16_t uid_t;
typedef uint16_t gid_t;
typedef uint16_t mode_t;

typedef uint8_t *caddr_t;
#define	_UINTPTR_T_DECLARED
typedef unsigned long uintptr_t;

#define	_SIZE_T_DECLARED
typedef unsigned long size_t;
typedef unsigned long u_long;
#endif

typedef unsigned long bus_addr_t;
typedef unsigned long bus_size_t;

typedef struct bus_dma_segment {
	bus_addr_t	ds_addr;	/* DMA address */
	bus_size_t	ds_len;		/* length of transfer */
} bus_dma_segment_t;

struct bus_dma_tag {
	uint32_t	alignment;
	uint32_t	maxsize;
};

typedef void *bus_dmamap_t;
typedef struct bus_dma_tag *bus_dma_tag_t;

typedef enum {
	BUS_DMA_LOCK	= 0x01,
	BUS_DMA_UNLOCK	= 0x02,
} bus_dma_lock_op_t;

typedef void *bus_space_tag_t;
typedef uint8_t *bus_space_handle_t;
typedef int bus_dma_filter_t(void *, bus_addr_t);
typedef void bus_dma_lock_t(void *, bus_dma_lock_op_t);

typedef uint32_t bool;

/* SYSINIT API */

#include <sysinit.h>

struct sysinit {
	void    (*func) (void *arg);
	void   *data;
};

/* MUTEX API */

struct mtx {
	int	owned;
	struct mtx *parent;
};

#define	mtx_assert(...) do { } while (0)
void	mtx_init(struct mtx *, const char *, const char *, int);
void	mtx_lock(struct mtx *);
void	mtx_unlock(struct mtx *);
#define	mtx_lock_spin(x) mtx_lock(x)
#define	mtx_unlock_spin(x) mtx_unlock(x)
int	mtx_owned(struct mtx *);
void	mtx_destroy(struct mtx *);

extern struct mtx Giant;

/* SX API */

struct sx {
	int	owned;
};

#define	sx_assert(...) do { } while (0)
#define	sx_init(...) sx_init_flags(__VA_ARGS__, 0)
void	sx_init_flags(struct sx *, const char *, int);
void	sx_destroy(struct sx *);
void	sx_xlock(struct sx *);
void	sx_xunlock(struct sx *);
int	sx_xlocked(struct sx *);

/* CONDVAR API */

struct cv {
	int	sleeping;
};

void	cv_init(struct cv *, const char *desc);
void	cv_destroy(struct cv *);
void	cv_wait(struct cv *, struct mtx *);
int	cv_timedwait(struct cv *, struct mtx *, int);
void	cv_signal(struct cv *);
void	cv_broadcast(struct cv *);

/* CALLOUT API */

typedef void callout_fn_t (void *);

extern volatile int ticks;

struct callout {
	LIST_ENTRY(callout) entry;
	callout_fn_t *c_func;
	void   *c_arg;
	struct mtx *mtx;
	int	flags;
	int	timeout;
};

void	callout_init_mtx(struct callout *, struct mtx *, int);
void	callout_reset(struct callout *, int, callout_fn_t *, void *);
void	callout_stop(struct callout *);
void	callout_drain(struct callout *);
int	callout_pending(struct callout *);
void	callout_process(int timeout);

/* DEVICE API */

struct driver;
struct devclass;
struct device;
struct module;
struct module_data;

typedef struct driver driver_t;
typedef struct devclass *devclass_t;
typedef struct device *device_t;
typedef void (driver_intr_t)(void *arg);
typedef int (driver_filter_t)(void *arg);
#define	FILTER_STRAY		0x01
#define	FILTER_HANDLED		0x02
#define	FILTER_SCHEDULE_THREAD	0x04

typedef int device_attach_t (device_t dev);
typedef int device_detach_t (device_t dev);
typedef int device_resume_t (device_t dev);
typedef int device_shutdown_t (device_t dev);
typedef int device_probe_t (device_t dev);
typedef int device_suspend_t (device_t dev);
typedef int gpio_pin_set_t (device_t dev, uint32_t, unsigned int);
typedef int gpio_pin_setflags_t (device_t dev, uint32_t, uint32_t);

typedef int bus_child_location_str_t (device_t parent, device_t child, char *buf, size_t buflen);
typedef int bus_child_pnpinfo_str_t (device_t parent, device_t child, char *buf, size_t buflen);
typedef void bus_driver_added_t (device_t dev, driver_t *driver);

struct device_method {
	const char *desc;
	void   *const func;
};

typedef struct device_method device_method_t;

struct device {
	TAILQ_HEAD(device_list, device) dev_children;
	TAILQ_ENTRY(device) dev_link;

	struct device *dev_parent;
	const struct module_data *dev_module;
	void   *dev_sc;
	void   *dev_aux;
	driver_filter_t *dev_irq_filter;
	driver_intr_t *dev_irq_fn;
	void   *dev_irq_arg;

	uint16_t dev_unit;

	char	dev_nameunit[64];
	char	dev_desc[64];

	uint8_t	dev_res_alloc:1;
	uint8_t	dev_quiet:1;
	uint8_t	dev_softc_set:1;
	uint8_t	dev_softc_alloc:1;
	uint8_t	dev_attached:1;
	uint8_t	dev_fixed_class:1;
	uint8_t	dev_unit_manual:1;
};

struct devclass {
	device_t dev_list[DEVCLASS_MAXUNIT];
};

struct driver {
	const char *name;
	const struct device_method *methods;
	uint32_t size;
};

struct module_data {
	int     (*callback) (struct module *, int, void *arg);
	void   *arg;
	const char *bus_name;
	const char *mod_name;
	const char *long_name;
	const struct driver *driver;
	struct devclass **devclass_pp;
	TAILQ_ENTRY(module_data) entry;
};

device_t device_get_parent(device_t dev);
void   *device_get_method(device_t dev, const char *what);
const char *device_get_name(device_t dev);
const char *device_get_nameunit(device_t dev);

#define	device_printf(dev, fmt,...) \
	printf("%s: " fmt, device_get_nameunit(dev),## __VA_ARGS__)
device_t device_add_child(device_t dev, const char *name, int unit);
void	device_quiet(device_t dev);
void	device_set_interrupt(device_t dev, driver_filter_t *, driver_intr_t *, void *);
void	device_run_interrupts(device_t parent);
void	device_set_ivars(device_t dev, void *ivars);
void   *device_get_ivars(device_t dev);
const char *device_get_desc(device_t dev);
int	device_probe_and_attach(device_t dev);
int	device_detach(device_t dev);
void   *device_get_softc(device_t dev);
void	device_set_softc(device_t dev, void *softc);
int	device_delete_child(device_t dev, device_t child);
int	device_delete_children(device_t dev);
int	device_is_attached(device_t dev);
void	device_set_desc(device_t dev, const char *desc);
void	device_set_desc_copy(device_t dev, const char *desc);
int	device_get_unit(device_t dev);
void   *devclass_get_softc(devclass_t dc, int unit);
int	devclass_get_maxunit(devclass_t dc);
device_t devclass_get_device(devclass_t dc, int unit);
devclass_t devclass_find(const char *classname);

#define	bus_get_dma_tag(...) (NULL)
int	bus_generic_detach(device_t dev);
int	bus_generic_resume(device_t dev);
int	bus_generic_shutdown(device_t dev);
int	bus_generic_suspend(device_t dev);
int	bus_generic_print_child(device_t dev, device_t child);
void	bus_generic_driver_added(device_t dev, driver_t *driver);
int	bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp);

/* BUS SPACE API */

void	bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint8_t data);
void	bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint16_t data);
void	bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint32_t data);

uint8_t	bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset);
uint16_t bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset);
uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset);

void	bus_space_read_multi_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint8_t *datap, bus_size_t count);
void	bus_space_read_multi_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint16_t *datap, bus_size_t count);
void	bus_space_read_multi_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint32_t *datap, bus_size_t count);

void	bus_space_write_multi_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint8_t *datap, bus_size_t count);
void	bus_space_write_multi_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint16_t *datap, bus_size_t count);
void	bus_space_write_multi_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset, uint32_t *datap, bus_size_t count);

void	bus_space_read_region_1(bus_space_tag_t space, bus_space_handle_t handle, bus_size_t offset, uint8_t *datap, bus_size_t count);
void	bus_space_write_region_1(bus_space_tag_t space, bus_space_handle_t handle, bus_size_t offset, uint8_t *datap, bus_size_t count);
void	bus_space_read_region_4(bus_space_tag_t space, bus_space_handle_t handle, bus_size_t offset, uint32_t *datap, bus_size_t count);
void	bus_space_write_region_4(bus_space_tag_t space, bus_space_handle_t handle, bus_size_t offset, uint32_t *datap, bus_size_t count);

void	bus_space_barrier(bus_space_tag_t space, bus_space_handle_t handle, bus_size_t offset, bus_size_t length, int flags);

void	module_register(void *);

/* LIB-C */

void   *memset(void *, int, size_t len);
void   *memcpy(void *, const void *, size_t len);
int	printf(const char *,...) __printflike(1, 2);
int	snprintf(char *restrict str, size_t size, const char *restrict format,...) __printflike(3, 4);
size_t	strlen(const char *s);

/* MALLOC API */

#undef malloc
#define	malloc(s,x,f) usb_malloc(s)
void   *usb_malloc(size_t);

#undef free
#define	free(p,x) usb_free(p)
void	usb_free(void *);

#define	strdup(p,x) usb_strdup(p)
char   *usb_strdup(const char *str);

/* ENDIANNESS */

#ifndef HAVE_ENDIAN_DEFS

/* Assume little endian */

#define	htole64(x) ((uint64_t)(x))
#define	le64toh(x) ((uint64_t)(x))

#define	htole32(x) ((uint32_t)(x))
#define	le32toh(x) ((uint32_t)(x))

#define	htole16(x) ((uint16_t)(x))
#define	le16toh(x) ((uint16_t)(x))

#define	be32toh(x) ((uint32_t)(x))
#define	htobe32(x) ((uint32_t)(x))

#else
#include <sys/endian.h>
#endif

/* USB */

typedef int usb_handle_request_t (device_t dev, const void *req, void **pptr, uint16_t *plen, uint16_t offset, uint8_t *pstate);
typedef int usb_take_controller_t (device_t dev);

void	usb_idle(void);
void	usb_init(void);
void	usb_uninit(void);

/* set some defaults */

#ifndef USB_POOL_SIZE
#define	USB_POOL_SIZE (1024*1024)	/* 1 MByte */
#endif

int	pause(const char *, int);
void	DELAY(unsigned int);

/* OTHER */

struct selinfo {
};

/* SYSTEM STARTUP API */

extern const void *sysinit_data[];
extern const void *sysuninit_data[];

/* Resources */

enum intr_type {
	INTR_TYPE_TTY = 1,
	INTR_TYPE_BIO = 2,
	INTR_TYPE_NET = 4,
	INTR_TYPE_CAM = 8,
	INTR_TYPE_MISC = 16,
	INTR_TYPE_CLK = 32,
	INTR_TYPE_AV = 64,
	INTR_EXCL = 256,		/* exclusive interrupt */
	INTR_MPSAFE = 512,		/* this interrupt is SMP safe */
	INTR_ENTROPY = 1024,		/* this interrupt provides entropy */
	INTR_MD1 = 4096,		/* flag reserved for MD use */
	INTR_MD2 = 8192,		/* flag reserved for MD use */
	INTR_MD3 = 16384,		/* flag reserved for MD use */
	INTR_MD4 = 32768		/* flag reserved for MD use */
};

struct resource_i {
	u_long		r_start;	/* index of the first entry in this resource */
	u_long		r_end;		/* index of the last entry (inclusive) */
};

struct resource {
	struct resource_i	*__r_i;
	bus_space_tag_t		r_bustag; /* bus_space tag */
	bus_space_handle_t	r_bushandle;	/* bus_space handle */
};

struct resource_spec {
	int	type;
	int	rid;
	int	flags;
};

#define	SYS_RES_IRQ	1	/* interrupt lines */
#define	SYS_RES_DRQ	2	/* isa dma lines */
#define	SYS_RES_MEMORY	3	/* i/o memory */
#define	SYS_RES_IOPORT	4	/* i/o ports */

#define	RF_ALLOCATED	0x0001	/* resource has been reserved */
#define	RF_ACTIVE	0x0002	/* resource allocation has been activated */
#define	RF_SHAREABLE	0x0004	/* resource permits contemporaneous sharing */
#define	RF_SPARE1	0x0008
#define	RF_SPARE2	0x0010
#define	RF_FIRSTSHARE	0x0020	/* first in sharing list */
#define	RF_PREFETCHABLE	0x0040	/* resource is prefetchable */
#define	RF_OPTIONAL	0x0080	/* for bus_alloc_resources() */

int bus_alloc_resources(device_t, struct resource_spec *, struct resource **);
int bus_release_resource(device_t, int, int, struct resource *);
void bus_release_resources(device_t, const struct resource_spec *,
    struct resource **);
struct resource *bus_alloc_resource_any(device_t, int, int *, unsigned int);
int bus_generic_attach(device_t);
bus_space_tag_t rman_get_bustag(struct resource *);
bus_space_handle_t rman_get_bushandle(struct resource *);
u_long rman_get_size(struct resource *);
int bus_setup_intr(device_t, struct resource *, int, driver_filter_t,
    driver_intr_t, void *, void **);
int bus_teardown_intr(device_t, struct resource *, void *);
int ofw_bus_status_okay(device_t);
int ofw_bus_is_compatible(device_t, char *);

extern int (*bus_alloc_resource_any_cb)(struct resource *res, device_t dev,
    int type, int *rid, unsigned int flags);
extern int (*ofw_bus_status_ok_cb)(device_t dev);
extern int (*ofw_bus_is_compatible_cb)(device_t dev, char *name);

#ifndef strlcpy
#define	strlcpy(d,s,n) snprintf((d),(n),"%s",(s))
#endif

/* Should be defined in user application since it is machine-dependent */
extern int delay(unsigned int);

/* BUS dma */
#define	BUS_SPACE_MAXADDR_24BIT	0xFFFFFF
#define	BUS_SPACE_MAXADDR_32BIT	0xFFFFFFFF
#define	BUS_SPACE_MAXADDR	0xFFFFFFFF
#define	BUS_SPACE_MAXSIZE_24BIT	0xFFFFFF
#define	BUS_SPACE_MAXSIZE_32BIT	0xFFFFFFFF
#define	BUS_SPACE_MAXSIZE	0xFFFFFFFF

#define	BUS_DMA_WAITOK		0x00	/* safe to sleep (pseudo-flag) */
#define	BUS_DMA_NOWAIT		0x01	/* not safe to sleep */
#define	BUS_DMA_ALLOCNOW	0x02	/* perform resource allocation now */
#define	BUS_DMA_COHERENT	0x04	/* hint: map memory in a coherent way */
#define	BUS_DMA_ZERO		0x08	/* allocate zero'ed memory */
#define	BUS_DMA_BUS1		0x10	/* placeholders for bus functions... */
#define	BUS_DMA_BUS2		0x20
#define	BUS_DMA_BUS3		0x40
#define	BUS_DMA_BUS4		0x80

typedef void bus_dmamap_callback_t(void *, bus_dma_segment_t *, int, int);

int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
		   bus_size_t boundary, bus_addr_t lowaddr,
		   bus_addr_t highaddr, bus_dma_filter_t *filter,
		   void *filterarg, bus_size_t maxsize, int nsegments,
		   bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
		   void *lockfuncarg, bus_dma_tag_t *dmat);

int bus_dmamem_alloc(bus_dma_tag_t dmat, void** vaddr, int flags,
    bus_dmamap_t *mapp);
void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);
int bus_dma_tag_destroy(bus_dma_tag_t dmat);

#endif					/* _BSD_KERNEL_H_ */
