/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * SCSI Disk Emulator
 *
 * Copyright (c) 2002 Nate Lawson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <aio.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/event.h>
#include <sys/param.h>
#include <sys/disk.h>
#include <cam/cam_queue.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_targetio.h>
#include <cam/scsi/scsi_message.h>
#include "scsi_target.h"

/* Maximum amount to transfer per CTIO */
#define MAX_XFER	MAXPHYS
/* Maximum number of allocated CTIOs */
#define MAX_CTIOS	64
/* Maximum sector size for emulated volume */
#define MAX_SECTOR	32768

/* Global variables */
int		debug;
int		notaio = 0;
off_t		volume_size;
u_int		sector_size;
size_t		buf_size;

/* Local variables */
static int    targ_fd;
static int    kq_fd;
static int    file_fd;
static int    num_ctios;
static struct ccb_queue		pending_queue;
static struct ccb_queue		work_queue;
static struct ioc_enable_lun	ioc_enlun = {
	CAM_BUS_WILDCARD,
	CAM_TARGET_WILDCARD,
	CAM_LUN_WILDCARD
};

/* Local functions */
static void		cleanup(void);
static int		init_ccbs(void);
static void		request_loop(void);
static void		handle_read(void);
/* static int		work_atio(struct ccb_accept_tio *); */
static void		queue_io(struct ccb_scsiio *);
static int		run_queue(struct ccb_accept_tio *);
static int		work_inot(struct ccb_immediate_notify *);
static struct ccb_scsiio *
			get_ctio(void);
/* static void		free_ccb(union ccb *); */
static cam_status	get_sim_flags(u_int16_t *);
static void		rel_simq(void);
static void		abort_all_pending(void);
static void		usage(void);

int
main(int argc, char *argv[])
{
	int ch;
	char *file_name;
	u_int16_t req_flags, sim_flags;
	off_t user_size;

	/* Initialize */
	debug = 0;
	req_flags = sim_flags = 0;
	user_size = 0;
	targ_fd = file_fd = kq_fd = -1;
	num_ctios = 0;
	sector_size = SECTOR_SIZE;
	buf_size = DFLTPHYS;

	/* Prepare resource pools */
	TAILQ_INIT(&pending_queue);
	TAILQ_INIT(&work_queue);

	while ((ch = getopt(argc, argv, "AdSTYb:c:s:W:")) != -1) {
		switch(ch) {
		case 'A':
			req_flags |= SID_Addr16;
			break;
		case 'd':
			debug = 1;
			break;
		case 'S':
			req_flags |= SID_Sync;
			break;
		case 'T':
			req_flags |= SID_CmdQue;
			break;
		case 'b':
			buf_size = atoi(optarg);
			if (buf_size < 256 || buf_size > MAX_XFER)
				errx(1, "Unreasonable buf size: %s", optarg);
			break;
		case 'c':
			sector_size = atoi(optarg);
			if (sector_size < 512 || sector_size > MAX_SECTOR)
				errx(1, "Unreasonable sector size: %s", optarg);
			break;
		case 's':
		{
			int last, shift = 0;

			last = strlen(optarg) - 1;
			if (last > 0) {
				switch (tolower(optarg[last])) {
				case 'e':
					shift += 10;
					/* FALLTHROUGH */
				case 'p':
					shift += 10;
					/* FALLTHROUGH */
				case 't':
					shift += 10;
					/* FALLTHROUGH */
				case 'g':
					shift += 10;
					/* FALLTHROUGH */
				case 'm':
					shift += 10;
					/* FALLTHROUGH */
				case 'k':
					shift += 10;
					optarg[last] = 0;
					break;
				}
			}
			user_size = strtoll(optarg, (char **)NULL, /*base*/10);
			user_size <<= shift;
			if (user_size < 0)
				errx(1, "Unreasonable volume size: %s", optarg);
			break;
		}
		case 'W':
			req_flags &= ~(SID_WBus16 | SID_WBus32);
			switch (atoi(optarg)) {
			case 8:
				/* Leave req_flags zeroed */
				break;
			case 16:
				req_flags |= SID_WBus16;
				break;
			case 32:
				req_flags |= SID_WBus32;
				break;
			default:
				warnx("Width %s not supported", optarg);
				usage();
				/* NOTREACHED */
			}
			break;
		case 'Y':
			notaio = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;
	
	if (argc != 2)
		usage();

	sscanf(argv[0], "%u:%u:%u", &ioc_enlun.path_id, &ioc_enlun.target_id,
	       &ioc_enlun.lun_id);
	file_name = argv[1];

	if (ioc_enlun.path_id == CAM_BUS_WILDCARD ||
	    ioc_enlun.target_id == CAM_TARGET_WILDCARD ||
	    ioc_enlun.lun_id == CAM_LUN_WILDCARD) {
		warnx("Incomplete target path specified");
		usage();
		/* NOTREACHED */
	}
	/* We don't support any vendor-specific commands */
	ioc_enlun.grp6_len = 0;
	ioc_enlun.grp7_len = 0;

	/* Open backing store for IO */
	file_fd = open(file_name, O_RDWR);
	if (file_fd < 0)
		errx(EX_NOINPUT, "open backing store file");

	/* Check backing store size or use the size user gave us */
	if (user_size == 0) {
		struct stat st;

		if (fstat(file_fd, &st) < 0)
			err(1, "fstat file");
#if __FreeBSD_version >= 500000
		if ((st.st_mode & S_IFCHR) != 0) {
			/* raw device */
			off_t mediasize;
			if (ioctl(file_fd, DIOCGMEDIASIZE, &mediasize) < 0)
				err(1, "DIOCGMEDIASIZE"); 

			/* XXX get sector size by ioctl()?? */
			volume_size = mediasize / sector_size;
		} else
#endif
			volume_size = st.st_size / sector_size;
	} else {
		volume_size = user_size / sector_size;
	}
	if (debug)
		warnx("volume_size: %d bytes x " OFF_FMT " sectors",
		    sector_size, volume_size);

	if (volume_size <= 0)
		errx(1, "volume must be larger than %d", sector_size);

	if (notaio == 0) {
		struct aiocb aio, *aiop;
		
		/* See if we have we have working AIO support */
		memset(&aio, 0, sizeof(aio));
		aio.aio_buf = malloc(sector_size);
		if (aio.aio_buf == NULL)
			err(1, "malloc");
		aio.aio_fildes = file_fd;
		aio.aio_offset = 0;
		aio.aio_nbytes = sector_size;
		signal(SIGSYS, SIG_IGN);
		if (aio_read(&aio) != 0) {
			printf("AIO support is not available- switchin to"
			       " single-threaded mode.\n");
			notaio = 1;
		} else {
			if (aio_waitcomplete(&aiop, NULL) != sector_size)
				err(1, "aio_waitcomplete");
			assert(aiop == &aio);
			signal(SIGSYS, SIG_DFL);
		}
		free((void *)aio.aio_buf);
		if (debug && notaio == 0)
			warnx("aio support tested ok");
	}

	targ_fd = open("/dev/targ", O_RDWR);
	if (targ_fd < 0)
    	    err(1, "/dev/targ");
	else
	    warnx("opened /dev/targ");

	/* The first three are handled by kevent() later */
	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPROF, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGSTOP, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	/* Register a cleanup handler to run when exiting */
	atexit(cleanup);

	/* Enable listening on the specified LUN */
	if (ioctl(targ_fd, TARGIOCENABLE, &ioc_enlun) != 0)
		err(1, "TARGIOCENABLE");

	/* Enable debugging if requested */
	if (debug) {
		if (ioctl(targ_fd, TARGIOCDEBUG, &debug) != 0)
			warnx("TARGIOCDEBUG");
	}

	/* Set up inquiry data according to what SIM supports */
	if (get_sim_flags(&sim_flags) != CAM_REQ_CMP)
		errx(1, "get_sim_flags");

	if (tcmd_init(req_flags, sim_flags) != 0)
		errx(1, "Initializing tcmd subsystem failed");

	/* Queue ATIOs and INOTs on descriptor */
	if (init_ccbs() != 0)
		errx(1, "init_ccbs failed");

	if (debug)
		warnx("main loop beginning");

	request_loop();

	exit(0);
}

static void
cleanup()
{
	struct ccb_hdr *ccb_h;

	if (debug) {
		warnx("cleanup called");
		debug = 0;
		ioctl(targ_fd, TARGIOCDEBUG, &debug);
	}
	ioctl(targ_fd, TARGIOCDISABLE, NULL);
	close(targ_fd);

	while ((ccb_h = TAILQ_FIRST(&pending_queue)) != NULL) {
		TAILQ_REMOVE(&pending_queue, ccb_h, periph_links.tqe);
		free_ccb((union ccb *)ccb_h);
	}
	while ((ccb_h = TAILQ_FIRST(&work_queue)) != NULL) {
		TAILQ_REMOVE(&work_queue, ccb_h, periph_links.tqe);
		free_ccb((union ccb *)ccb_h);
	}

	if (kq_fd != -1)
		close(kq_fd);
}

/* Allocate ATIOs/INOTs and queue on HBA */
static int
init_ccbs()
{
	int i;

	for (i = 0; i < MAX_INITIATORS; i++) {
		struct ccb_accept_tio *atio;
		struct atio_descr *a_descr;
		struct ccb_immediate_notify *inot;

		atio = (struct ccb_accept_tio *)malloc(sizeof(*atio));
		if (atio == NULL) {
			warn("malloc ATIO");
			return (-1);
		}
		a_descr = (struct atio_descr *)malloc(sizeof(*a_descr));
		if (a_descr == NULL) {
			free(atio);
			warn("malloc atio_descr");
			return (-1);
		}
		atio->ccb_h.func_code = XPT_ACCEPT_TARGET_IO;
		atio->ccb_h.targ_descr = a_descr;
		send_ccb((union ccb *)atio, /*priority*/1);

		inot = (struct ccb_immediate_notify *)malloc(sizeof(*inot));
		if (inot == NULL) {
			warn("malloc INOT");
			return (-1);
		}
		inot->ccb_h.func_code = XPT_IMMEDIATE_NOTIFY;
		send_ccb((union ccb *)inot, /*priority*/1);
	}

	return (0);
}

static void
request_loop()
{
	struct kevent events[MAX_EVENTS];
	struct timespec ts, *tptr;
	int quit;

	/* Register kqueue for event notification */
	if ((kq_fd = kqueue()) < 0)
		err(1, "init kqueue");

	/* Set up some default events */
	EV_SET(&events[0], SIGHUP, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
	EV_SET(&events[1], SIGINT, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
	EV_SET(&events[2], SIGTERM, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, 0);
	EV_SET(&events[3], targ_fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, 0);
	if (kevent(kq_fd, events, 4, NULL, 0, NULL) < 0)
		err(1, "kevent signal registration");

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	tptr = NULL;
	quit = 0;

	/* Loop until user signal */
	while (quit == 0) {
		int retval, i, oo;
		struct ccb_hdr *ccb_h;

		/* Check for the next signal, read ready, or AIO completion */
		retval = kevent(kq_fd, NULL, 0, events, MAX_EVENTS, tptr);
		if (retval < 0) {
			if (errno == EINTR) {
				if (debug)
					warnx("EINTR, looping");
				continue;
            		}
			else {
				err(1, "kevent failed");
			}
		} else if (retval > MAX_EVENTS) {
			errx(1, "kevent returned more events than allocated?");
		}

		/* Process all received events. */
		for (oo = i = 0; i < retval; i++) {
			if ((events[i].flags & EV_ERROR) != 0)
				errx(1, "kevent registration failed");

			switch (events[i].filter) {
			case EVFILT_READ:
				if (debug)
					warnx("read ready");
				handle_read();
				break;
			case EVFILT_AIO:
			{
				struct ccb_scsiio *ctio;
				struct ctio_descr *c_descr;
				if (debug)
					warnx("aio ready");

				ctio = (struct ccb_scsiio *)events[i].udata;
				c_descr = (struct ctio_descr *)
					  ctio->ccb_h.targ_descr;
				c_descr->event = AIO_DONE;
				/* Queue on the appropriate ATIO */
				queue_io(ctio);
				/* Process any queued completions. */
				oo += run_queue(c_descr->atio);
				break;
			}
			case EVFILT_SIGNAL:
				if (debug)
					warnx("signal ready, setting quit");
				quit = 1;
				break;
			default:
				warnx("unknown event %d", events[i].filter);
				break;
			}

			if (debug)
				warnx("event %d done", events[i].filter);
		}

		if (oo) {
			tptr = &ts;
			continue;
		}

		/* Grab the first CCB and perform one work unit. */
		if ((ccb_h = TAILQ_FIRST(&work_queue)) != NULL) {
			union ccb *ccb;

			ccb = (union ccb *)ccb_h;
			switch (ccb_h->func_code) {
			case XPT_ACCEPT_TARGET_IO:
				/* Start one more transfer. */
				retval = work_atio(&ccb->atio);
				break;
			case XPT_IMMEDIATE_NOTIFY:
				retval = work_inot(&ccb->cin1);
				break;
			default:
				warnx("Unhandled ccb type %#x on workq",
				      ccb_h->func_code);
				abort();
				/* NOTREACHED */
			}

			/* Assume work function handled the exception */
			if ((ccb_h->status & CAM_DEV_QFRZN) != 0) {
				if (debug) {
					warnx("Queue frozen receiving CCB, "
					      "releasing");
				}
				rel_simq();
			}

			/* No more work needed for this command. */
			if (retval == 0) {
				TAILQ_REMOVE(&work_queue, ccb_h,
					     periph_links.tqe);
			}
		}

		/*
		 * Poll for new events (i.e. completions) while we
		 * are processing CCBs on the work_queue. Once it's
		 * empty, use an infinite wait.
		 */
		if (!TAILQ_EMPTY(&work_queue))
			tptr = &ts;
		else
			tptr = NULL;
	}
}

/* CCBs are ready from the kernel */
static void
handle_read()
{
	union ccb *ccb_array[MAX_INITIATORS], *ccb;
	int ccb_count, i, oo;

	ccb_count = read(targ_fd, ccb_array, sizeof(ccb_array));
	if (ccb_count <= 0) {
		warn("read ccb ptrs");
		return;
	}
	ccb_count /= sizeof(union ccb *);
	if (ccb_count < 1) {
		warnx("truncated read ccb ptr?");
		return;
	}

	for (i = 0; i < ccb_count; i++) {
		ccb = ccb_array[i];
		TAILQ_REMOVE(&pending_queue, &ccb->ccb_h, periph_links.tqe);

		switch (ccb->ccb_h.func_code) {
		case XPT_ACCEPT_TARGET_IO:
		{
			struct ccb_accept_tio *atio;
			struct atio_descr *a_descr;

			/* Initialize ATIO descr for this transaction */
			atio = &ccb->atio;
			a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;
			bzero(a_descr, sizeof(*a_descr));
			TAILQ_INIT(&a_descr->cmplt_io);
			a_descr->flags = atio->ccb_h.flags &
				(CAM_DIS_DISCONNECT | CAM_TAG_ACTION_VALID);
			/* XXX add a_descr->priority */
			if ((atio->ccb_h.flags & CAM_CDB_POINTER) == 0)
				a_descr->cdb = atio->cdb_io.cdb_bytes;
			else
				a_descr->cdb = atio->cdb_io.cdb_ptr;

			/* ATIOs are processed in FIFO order */
			TAILQ_INSERT_TAIL(&work_queue, &ccb->ccb_h,
					  periph_links.tqe);
			break;
		}
		case XPT_CONT_TARGET_IO:
		{
			struct ccb_scsiio *ctio;
			struct ctio_descr *c_descr;

			ctio = &ccb->ctio;
			c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;
			c_descr->event = CTIO_DONE;
			/* Queue on the appropriate ATIO */
			queue_io(ctio);
			/* Process any queued completions. */
			oo += run_queue(c_descr->atio);
			break;
		}
		case XPT_IMMEDIATE_NOTIFY:
			/* INOTs are handled with priority */
			TAILQ_INSERT_HEAD(&work_queue, &ccb->ccb_h,
					  periph_links.tqe);
			break;
		default:
			warnx("Unhandled ccb type %#x in handle_read",
			      ccb->ccb_h.func_code);
			break;
		}
	}
}

/* Process an ATIO CCB from the kernel */
int
work_atio(struct ccb_accept_tio *atio)
{
	struct ccb_scsiio *ctio;
	struct atio_descr *a_descr;
	struct ctio_descr *c_descr;
	cam_status status;
	int ret;

	if (debug)
		warnx("Working on ATIO %p", atio);

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;

	/* Get a CTIO and initialize it according to our known parameters */
	ctio = get_ctio();
	if (ctio == NULL) {
		return (1);
	}
	ret = 0;
	ctio->ccb_h.flags = a_descr->flags;
	ctio->tag_id = atio->tag_id;
	ctio->init_id = atio->init_id;
	/* XXX priority needs to be added to a_descr */
	c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;
	c_descr->atio = atio;
	if ((a_descr->flags & CAM_DIR_IN) != 0)
		c_descr->offset = a_descr->base_off + a_descr->targ_req;
	else if ((a_descr->flags & CAM_DIR_MASK) == CAM_DIR_OUT)
		c_descr->offset = a_descr->base_off + a_descr->init_req;
	else
		c_descr->offset = a_descr->base_off;

	/* 
	 * Return a check condition if there was an error while
	 * receiving this ATIO.
	 */
	if (atio->sense_len != 0) {
		struct scsi_sense_data_fixed *sense;

		if (debug) {
			warnx("ATIO with %u bytes sense received",
			      atio->sense_len);
		}
		sense = (struct scsi_sense_data_fixed *)&atio->sense_data;
		tcmd_sense(ctio->init_id, ctio, sense->flags,
			   sense->add_sense_code, sense->add_sense_code_qual);
		send_ccb((union ccb *)ctio, /*priority*/1);
		return (0);
	}

	status = atio->ccb_h.status & CAM_STATUS_MASK;
	switch (status) {
	case CAM_CDB_RECVD:
		ret = tcmd_handle(atio, ctio, ATIO_WORK);
		break;
	case CAM_REQ_ABORTED:
		warn("ATIO %p aborted", a_descr);
		/* Requeue on HBA */
		TAILQ_REMOVE(&work_queue, &atio->ccb_h, periph_links.tqe);
		send_ccb((union ccb *)atio, /*priority*/1);
		ret = 1;
		break;
	default:
		warnx("ATIO completed with unhandled status %#x", status);
		abort();
		/* NOTREACHED */
		break;
	}

	return (ret);
}

static void
queue_io(struct ccb_scsiio *ctio)
{
	struct ccb_hdr *ccb_h;
	struct io_queue *ioq;
	struct ctio_descr *c_descr;
	
	c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;
	if (c_descr->atio == NULL) {
		errx(1, "CTIO %p has NULL ATIO", ctio);
	}
	ioq = &((struct atio_descr *)c_descr->atio->ccb_h.targ_descr)->cmplt_io;

	if (TAILQ_EMPTY(ioq)) {
		TAILQ_INSERT_HEAD(ioq, &ctio->ccb_h, periph_links.tqe);
		return;
	}

	TAILQ_FOREACH_REVERSE(ccb_h, ioq, io_queue, periph_links.tqe) {
		struct ctio_descr *curr_descr = 
		    (struct ctio_descr *)ccb_h->targ_descr;
		if (curr_descr->offset <= c_descr->offset) {
			break;
		}
	}

	if (ccb_h) {
		TAILQ_INSERT_AFTER(ioq, ccb_h, &ctio->ccb_h, periph_links.tqe);
	} else {
		TAILQ_INSERT_HEAD(ioq, &ctio->ccb_h, periph_links.tqe);
	}
}

/*
 * Go through all completed AIO/CTIOs for a given ATIO and advance data
 * counts, start continuation IO, etc.
 */
static int
run_queue(struct ccb_accept_tio *atio)
{
	struct atio_descr *a_descr;
	struct ccb_hdr *ccb_h;
	int sent_status, event;

	if (atio == NULL)
		return (0);

	a_descr = (struct atio_descr *)atio->ccb_h.targ_descr;

	while ((ccb_h = TAILQ_FIRST(&a_descr->cmplt_io)) != NULL) {
		struct ccb_scsiio *ctio;
		struct ctio_descr *c_descr;

		ctio = (struct ccb_scsiio *)ccb_h;
		c_descr = (struct ctio_descr *)ctio->ccb_h.targ_descr;

		if (ctio->ccb_h.status == CAM_REQ_ABORTED) {
			TAILQ_REMOVE(&a_descr->cmplt_io, ccb_h,
				     periph_links.tqe);
			free_ccb((union ccb *)ctio);
			send_ccb((union ccb *)atio, /*priority*/1);
			continue;
		}

		/* If completed item is in range, call handler */
		if ((c_descr->event == AIO_DONE &&
		    c_descr->offset == a_descr->base_off + a_descr->targ_ack)
		 || (c_descr->event == CTIO_DONE &&
		    c_descr->offset == a_descr->base_off + a_descr->init_ack)) {
			sent_status = (ccb_h->flags & CAM_SEND_STATUS) != 0;
			event = c_descr->event;

			TAILQ_REMOVE(&a_descr->cmplt_io, ccb_h,
				     periph_links.tqe);
			tcmd_handle(atio, ctio, c_descr->event);

			/* If entire transfer complete, send back ATIO */
			if (sent_status != 0 && event == CTIO_DONE)
				send_ccb((union ccb *)atio, /*priority*/1);
		} else {
			/* Gap in offsets so wait until later callback */
			if (/* debug */ 1)
				warnx("IO %p:%p out of order %s",  ccb_h,
				    a_descr, c_descr->event == AIO_DONE?
				    "aio" : "ctio");
			return (1);
		}
	}
	return (0);
}

static int
work_inot(struct ccb_immediate_notify *inot)
{
	cam_status status;

	if (debug)
		warnx("Working on INOT %p", inot);

	status = inot->ccb_h.status;
	status &= CAM_STATUS_MASK;

	switch (status) {
	case CAM_SCSI_BUS_RESET:
		tcmd_ua(CAM_TARGET_WILDCARD, UA_BUS_RESET);
		abort_all_pending();
		break;
	case CAM_BDR_SENT:
		tcmd_ua(CAM_TARGET_WILDCARD, UA_BDR);
		abort_all_pending();
		break;
	case CAM_MESSAGE_RECV:
		switch (inot->arg) {
		case MSG_TASK_COMPLETE:
		case MSG_INITIATOR_DET_ERR:
		case MSG_ABORT_TASK_SET:
		case MSG_MESSAGE_REJECT:
		case MSG_NOOP:
		case MSG_PARITY_ERROR:
		case MSG_TARGET_RESET:
		case MSG_ABORT_TASK:
		case MSG_CLEAR_TASK_SET:
		default:
			warnx("INOT message %#x", inot->arg);
			break;
		}
		break;
	case CAM_REQ_ABORTED:
		warnx("INOT %p aborted", inot);
		break;
	default:
		warnx("Unhandled INOT status %#x", status);
		break;
	}

	/* Requeue on SIM */
	TAILQ_REMOVE(&work_queue, &inot->ccb_h, periph_links.tqe);
	send_ccb((union ccb *)inot, /*priority*/1);

	return (1);
}

void
send_ccb(union ccb *ccb, int priority)
{
	if (debug)
		warnx("sending ccb (%#x)", ccb->ccb_h.func_code);
	ccb->ccb_h.pinfo.priority = priority;
	if (XPT_FC_IS_QUEUED(ccb)) {
		TAILQ_INSERT_TAIL(&pending_queue, &ccb->ccb_h,
				  periph_links.tqe);
	}
	if (write(targ_fd, &ccb, sizeof(ccb)) != sizeof(ccb)) {
		warn("write ccb");
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
	}
}

/* Return a CTIO/descr/buf combo from the freelist or malloc one */
static struct ccb_scsiio *
get_ctio()
{
	struct ccb_scsiio *ctio;
	struct ctio_descr *c_descr;
	struct sigevent *se;

	if (num_ctios == MAX_CTIOS) {
		warnx("at CTIO max");
		return (NULL);
	}

	ctio = (struct ccb_scsiio *)malloc(sizeof(*ctio));
	if (ctio == NULL) {
		warn("malloc CTIO");
		return (NULL);
	}
	c_descr = (struct ctio_descr *)malloc(sizeof(*c_descr));
	if (c_descr == NULL) {
		free(ctio);
		warn("malloc ctio_descr");
		return (NULL);
	}
	c_descr->buf = malloc(buf_size);
	if (c_descr->buf == NULL) {
		free(c_descr);
		free(ctio);
		warn("malloc backing store");
		return (NULL);
	}
	num_ctios++;

	/* Initialize CTIO, CTIO descr, and AIO */
	ctio->ccb_h.func_code = XPT_CONT_TARGET_IO;
	ctio->ccb_h.retry_count = 2;
	ctio->ccb_h.timeout = CAM_TIME_INFINITY;
	ctio->data_ptr = c_descr->buf;
	ctio->ccb_h.targ_descr = c_descr;
	c_descr->aiocb.aio_buf = c_descr->buf;
	c_descr->aiocb.aio_fildes = file_fd;
	se = &c_descr->aiocb.aio_sigevent;
	se->sigev_notify = SIGEV_KEVENT;
	se->sigev_notify_kqueue = kq_fd;
	se->sigev_value.sival_ptr = ctio;

	return (ctio);
}

void
free_ccb(union ccb *ccb)
{
	switch (ccb->ccb_h.func_code) {
	case XPT_CONT_TARGET_IO:
	{
		struct ctio_descr *c_descr;

		c_descr = (struct ctio_descr *)ccb->ccb_h.targ_descr;
		free(c_descr->buf);
		num_ctios--;
		/* FALLTHROUGH */
	}
	case XPT_ACCEPT_TARGET_IO:
		free(ccb->ccb_h.targ_descr);
		/* FALLTHROUGH */
	case XPT_IMMEDIATE_NOTIFY:
	default:
		free(ccb);
		break;
	}
}

static cam_status
get_sim_flags(u_int16_t *flags)
{
	struct ccb_pathinq cpi;
	cam_status status;

	/* Find SIM capabilities */
	bzero(&cpi, sizeof(cpi));
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	send_ccb((union ccb *)&cpi, /*priority*/1);
	status = cpi.ccb_h.status & CAM_STATUS_MASK;
	if (status != CAM_REQ_CMP) {
		fprintf(stderr, "CPI failed, status %#x\n", status);
		return (status);
	}

	/* Can only enable on controllers that support target mode */
	if ((cpi.target_sprt & PIT_PROCESSOR) == 0) {
		fprintf(stderr, "HBA does not support target mode\n");
		status = CAM_PATH_INVALID;
		return (status);
	}

	*flags = cpi.hba_inquiry;
	return (status);
}

static void
rel_simq()
{
	struct ccb_relsim crs;

	bzero(&crs, sizeof(crs));
	crs.ccb_h.func_code = XPT_REL_SIMQ;
	crs.release_flags = RELSIM_RELEASE_AFTER_QEMPTY;
	crs.openings = 0;
	crs.release_timeout = 0;
	crs.qfrozen_cnt = 0;
	send_ccb((union ccb *)&crs, /*priority*/0);
}

/* Cancel all pending CCBs. */
static void
abort_all_pending()
{
	struct ccb_abort	 cab;
	struct ccb_hdr		*ccb_h;

	if (debug)
		  warnx("abort_all_pending");

	bzero(&cab, sizeof(cab));
	cab.ccb_h.func_code = XPT_ABORT;
	TAILQ_FOREACH(ccb_h, &pending_queue, periph_links.tqe) {
		if (debug)
			  warnx("Aborting pending CCB %p\n", ccb_h);
		cab.abort_ccb = (union ccb *)ccb_h;
		send_ccb((union ccb *)&cab, /*priority*/1);
		if (cab.ccb_h.status != CAM_REQ_CMP) {
			warnx("Unable to abort CCB, status %#x\n",
			       cab.ccb_h.status);
		}
	}
}

static void
usage()
{
	fprintf(stderr,
		"Usage: scsi_target [-AdSTY] [-b bufsize] [-c sectorsize]\n"
		"\t\t[-r numbufs] [-s volsize] [-W 8,16,32]\n"
		"\t\tbus:target:lun filename\n");
	exit(1);
}
