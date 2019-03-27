/*
 * This code lifted from:
 * 	Simple `echo' pseudo-device KLD
 * 	Murray Stokely
 * 	Converted to 5.X by Søren (Xride) Straarup
 */

/*
 * /bin/echo "server,port=9999,addr=192.168.69.142,validate" > /dev/krping
 * /bin/echo "client,port=9999,addr=192.168.69.142,validate" > /dev/krping
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>  /* uprintf */
#include <sys/errno.h>
#include <sys/param.h>  /* defines used in kernel.h */
#include <sys/kernel.h> /* types used in module initialization */
#include <sys/conf.h>   /* cdevsw struct */
#include <sys/uio.h>    /* uio struct */
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>

#include "krping.h"

#define BUFFERSIZE 512

SYSCTL_NODE(_dev, OID_AUTO, krping, CTLFLAG_RW, 0, "kernel rping module");

int krping_debug = 0;
SYSCTL_INT(_dev_krping, OID_AUTO, debug, CTLFLAG_RW, &krping_debug, 0 , "");

/* Function prototypes */
static d_open_t      krping_open;
static d_close_t     krping_close;
static d_read_t      krping_read;
static d_write_t     krping_write;

/* Character device entry points */
static struct cdevsw krping_cdevsw = {
	.d_version = D_VERSION,
	.d_open = krping_open,
	.d_close = krping_close,
	.d_read = krping_read,
	.d_write = krping_write,
	.d_name = "krping",
};

typedef struct s_krping {
	char msg[BUFFERSIZE];
	int len;
} krping_t;

struct stats_list_entry {
	STAILQ_ENTRY(stats_list_entry) link;
	struct krping_stats *stats;
};
STAILQ_HEAD(stats_list, stats_list_entry);

/* vars */
static struct cdev *krping_dev;

static int
krping_loader(struct module *m, int what, void *arg)
{
	int err = 0;

	switch (what) {
	case MOD_LOAD:                /* kldload */
		krping_dev = make_dev(&krping_cdevsw, 0, UID_ROOT, GID_WHEEL,
					0600, "krping");
		printf("Krping device loaded.\n");
		break;
	case MOD_UNLOAD:
		destroy_dev(krping_dev);
		printf("Krping device unloaded.\n");
		break;
	default:
		err = EOPNOTSUPP;
		break;
	}

	return (err);
}

static int
krping_open(struct cdev *dev, int oflags, int devtype, struct thread *p)
{

	return (0);
}

static int
krping_close(struct cdev *dev, int fflag, int devtype, struct thread *p)
{

	return 0;
}

static void
krping_copy_stats(struct krping_stats *stats, void *arg)
{
	struct stats_list_entry *s;
	struct stats_list *list = arg;

	s = malloc(sizeof(*s), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (s == NULL)
		return;
	if (stats != NULL) {
		s->stats = malloc(sizeof(*stats), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (s->stats == NULL) {
			free(s, M_DEVBUF);
			return;
		}
		*s->stats = *stats;
	}
	STAILQ_INSERT_TAIL(list, s, link);
}

static int
krping_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int num = 1;
	struct stats_list list;
	struct stats_list_entry *e;

	STAILQ_INIT(&list);
	krping_walk_cb_list(krping_copy_stats, &list);

	if (STAILQ_EMPTY(&list))
		return (0);

	uprintf("krping: %4s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n",
	    "num", "device", "snd bytes", "snd msgs", "rcv bytes", "rcv msgs",
	    "wr bytes", "wr msgs", "rd bytes", "rd msgs");

	while (!STAILQ_EMPTY(&list)) {
		e = STAILQ_FIRST(&list);
		STAILQ_REMOVE_HEAD(&list, link);
		if (e->stats == NULL)
			uprintf("krping: %d listen\n", num);
		else {
			struct krping_stats *stats = e->stats;

			uprintf("krping: %4d %10s %10llu %10llu %10llu %10llu "
			    "%10llu %10llu %10llu %10llu\n", num, stats->name,
			    stats->send_bytes, stats->send_msgs,
			    stats->recv_bytes, stats->recv_msgs,
			    stats->write_bytes, stats->write_msgs,
			    stats->read_bytes, stats->read_msgs);
			free(stats, M_DEVBUF);
		}
		num++;
		free(e, M_DEVBUF);
	}

	return (0);
}

static int
krping_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int err = 0;
	int amt;
	int remain = BUFFERSIZE;
	char *cp;
	krping_t *krpingmsg;

	krpingmsg = malloc(sizeof *krpingmsg, M_DEVBUF, M_WAITOK|M_ZERO);
	if (!krpingmsg) {
		uprintf("Could not malloc mem!\n");
		return ENOMEM;
	}

	cp = krpingmsg->msg;
	while (uio->uio_resid) {
		amt = MIN(uio->uio_resid, remain);
		if (amt == 0)
			break;

		/* Copy the string in from user memory to kernel memory */
		err = uiomove(cp, amt, uio);
		if (err) {
			uprintf("Write failed: bad address!\n");
			goto done;
		}
		cp += amt;
		remain -= amt;
	}

	if (uio->uio_resid != 0) {
		uprintf("Message too big. max size is %d!\n", BUFFERSIZE);
		err = EMSGSIZE;
		goto done;
	}

	/* null terminate and remove the \n */
	cp--;
	*cp = 0;
	krpingmsg->len = (unsigned long)(cp - krpingmsg->msg);
	uprintf("krping: write string = |%s|\n", krpingmsg->msg);
	err = krping_doit(krpingmsg->msg);
done:
	free(krpingmsg, M_DEVBUF);
	return(err);
}

int
krping_sigpending(void)
{

	return (SIGPENDING(curthread));
}

DEV_MODULE(krping, krping_loader, NULL);
MODULE_DEPEND(krping, ibcore, 1, 1, 1);
