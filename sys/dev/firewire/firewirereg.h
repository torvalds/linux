/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

typedef	struct thread fw_proc;
#include <sys/selinfo.h>

#include <sys/uio.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>

#define	splfw splimp

STAILQ_HEAD(fw_xferlist, fw_xfer);

struct fw_device {
	uint16_t dst;
	struct fw_eui64 eui;
	uint8_t speed;
	uint8_t maxrec;
	uint8_t nport;
	uint8_t power;
#define CSRROMOFF 0x400
#define CSRROMSIZE 0x400
	int rommax;	/* offset from 0xffff f000 0000 */
	uint32_t csrrom[CSRROMSIZE / 4];
	int rcnt;
	struct firewire_comm *fc;
	uint32_t status;
#define FWDEVINIT	1
#define FWDEVATTACHED	2
#define FWDEVINVAL	3
	STAILQ_ENTRY(fw_device) link;
};

struct firewire_softc {
	struct cdev *dev;
	struct firewire_comm *fc;
};

#define FW_MAX_DMACH 0x20
#define FW_MAX_DEVCH FW_MAX_DMACH
#define FW_XFERTIMEOUT 1

struct firewire_dev_comm {
	device_t dev;
	struct firewire_comm *fc;
	void (*post_busreset) (void *);
	void (*post_explore) (void *);
};

struct tcode_info {
	u_char hdr_len;	/* IEEE1394 header length */
	u_char flag;
#define FWTI_REQ	(1 << 0)
#define FWTI_RES	(1 << 1)
#define FWTI_TLABEL	(1 << 2)
#define FWTI_BLOCK_STR	(1 << 3)
#define FWTI_BLOCK_ASY	(1 << 4)
	u_char valid_res;
};

struct firewire_comm {
	device_t dev;
	device_t bdev;
	uint16_t busid:10,
		 nodeid:6;
	u_int mode;
	u_int nport;
	u_int speed;
	u_int maxrec;
	u_int irm;
	u_int max_node;
	u_int max_hop;
#define FWPHYASYST (1 << 0)
	uint32_t status;
#define	FWBUSDETACH	(-2)
#define	FWBUSNOTREADY	(-1)
#define	FWBUSRESET	0
#define	FWBUSINIT	1
#define	FWBUSCYMELECT	2
#define	FWBUSMGRELECT	3
#define	FWBUSMGRDONE	4
#define	FWBUSEXPLORE	5
#define	FWBUSPHYCONF	6
#define	FWBUSEXPDONE	7
#define	FWBUSCOMPLETION	10
	int nisodma;
	struct fw_eui64 eui;
	struct fw_xferq
		*arq, *atq, *ars, *ats, *it[FW_MAX_DMACH],*ir[FW_MAX_DMACH];
	struct fw_xferlist tlabels[0x40];
	u_char last_tlabel[0x40];
	struct mtx tlabel_lock;
	STAILQ_HEAD(, fw_bind) binds;
	STAILQ_HEAD(, fw_device) devices;
	u_int  sid_cnt;
#define CSRSIZE 0x4000
	uint32_t csr_arc[CSRSIZE / 4];
#define CROMSIZE 0x400
	uint32_t *config_rom;
	struct crom_src_buf *crom_src_buf;
	struct crom_src *crom_src;
	struct crom_chunk *crom_root;
	struct fw_topology_map *topology_map;
	struct fw_speed_map *speed_map;
	struct callout busprobe_callout;
	struct callout bmr_callout;
	struct callout timeout_callout;
	struct task task_timeout;
	uint32_t (*cyctimer) (struct firewire_comm *);
	void (*ibr) (struct firewire_comm *);
	uint32_t (*set_bmr) (struct firewire_comm *, uint32_t);
	int (*ioctl) (struct cdev *, u_long, caddr_t, int, fw_proc *);
	int (*irx_enable) (struct firewire_comm *, int);
	int (*irx_disable) (struct firewire_comm *, int);
	int (*itx_enable) (struct firewire_comm *, int);
	int (*itx_disable) (struct firewire_comm *, int);
	void (*timeout) (void *);
	void (*poll) (struct firewire_comm *, int, int);
	void (*set_intr) (struct firewire_comm *, int);
	void (*irx_post) (struct firewire_comm *, uint32_t *);
	void (*itx_post) (struct firewire_comm *, uint32_t *);
	struct tcode_info *tcode;
	bus_dma_tag_t dmat;
	struct mtx mtx;
	struct mtx wait_lock;
	struct taskqueue *taskqueue;
	struct proc *probe_thread;
};
#define CSRARC(sc, offset) ((sc)->csr_arc[(offset) / 4])

#define FW_GMTX(fc)		(&(fc)->mtx)
#define FW_GLOCK(fc)		mtx_lock(FW_GMTX(fc))
#define FW_GUNLOCK(fc)		mtx_unlock(FW_GMTX(fc))
#define FW_GLOCK_ASSERT(fc)	mtx_assert(FW_GMTX(fc), MA_OWNED)

struct fw_xferq {
	int flag;
#define FWXFERQ_CHTAGMASK 0xff
#define FWXFERQ_RUNNING (1 << 8)
#define FWXFERQ_STREAM (1 << 9)

#define FWXFERQ_BULK (1 << 11)
#define FWXFERQ_MODEMASK (7 << 10)

#define FWXFERQ_EXTBUF (1 << 13)
#define FWXFERQ_OPEN (1 << 14)

#define FWXFERQ_HANDLER (1 << 16)
#define FWXFERQ_WAKEUP (1 << 17)
	void (*start) (struct firewire_comm *);
	int dmach;
	struct fw_xferlist q;
	u_int queued;
	u_int maxq;
	u_int psize;
	struct fwdma_alloc_multi *buf;
	u_int bnchunk;
	u_int bnpacket;
	struct fw_bulkxfer *bulkxfer;
	STAILQ_HEAD(, fw_bulkxfer) stvalid;
	STAILQ_HEAD(, fw_bulkxfer) stfree;
	STAILQ_HEAD(, fw_bulkxfer) stdma;
	struct fw_bulkxfer *stproc;
	struct selinfo rsel;
	caddr_t sc;
	void (*hand) (struct fw_xferq *);
};

struct fw_bulkxfer {
	int poffset;
	struct mbuf *mbuf;
	STAILQ_ENTRY(fw_bulkxfer) link;
	caddr_t start;
	caddr_t end;
	int resp;
};

struct fw_bind {
	u_int64_t start;
	u_int64_t end;
	struct fw_xferlist xferlist;
	STAILQ_ENTRY(fw_bind) fclist;
	STAILQ_ENTRY(fw_bind) chlist;
	void *sc;
};

struct fw_xfer {
	caddr_t sc;
	struct firewire_comm *fc;
	struct fw_xferq *q;
	struct timeval tv;
	int8_t resp;
#define FWXF_INIT	0x00
#define FWXF_INQ	0x01
#define FWXF_START	0x02
#define FWXF_SENT	0x04
#define FWXF_SENTERR	0x08
#define FWXF_BUSY	0x10
#define FWXF_RCVD	0x20

#define FWXF_WAKE	0x80
	uint8_t flag;
	int8_t tl;
	void (*hand) (struct fw_xfer *);
	struct {
		struct fw_pkt hdr;
		uint32_t *payload;
		uint16_t pay_len;
		uint8_t spd;
	} send, recv;
	struct mbuf *mbuf;
	STAILQ_ENTRY(fw_xfer) link;
	STAILQ_ENTRY(fw_xfer) tlabel;
	struct malloc_type *malloc;
};

struct fw_rcv_buf {
	struct firewire_comm *fc;
	struct fw_xfer *xfer;
	struct iovec *vec;
	u_int nvec;
	uint8_t spd;
};

void fw_sidrcv (struct firewire_comm *, uint32_t *, u_int);
void fw_rcv (struct fw_rcv_buf *);
void fw_xfer_unload (struct fw_xfer *);
void fw_xfer_free_buf (struct fw_xfer *);
void fw_xfer_free (struct fw_xfer*);
struct fw_xfer *fw_xfer_alloc (struct malloc_type *);
struct fw_xfer *fw_xfer_alloc_buf (struct malloc_type *, int, int);
void fw_init (struct firewire_comm *);
int fw_tbuf_update (struct firewire_comm *, int, int);
int fw_rbuf_update (struct firewire_comm *, int, int);
int fw_bindadd (struct firewire_comm *, struct fw_bind *);
int fw_bindremove (struct firewire_comm *, struct fw_bind *);
int fw_xferlist_add (struct fw_xferlist *, struct malloc_type *, int, int, int,
    struct firewire_comm *, void *, void (*)(struct fw_xfer *));
void fw_xferlist_remove (struct fw_xferlist *);
int fw_asyreq (struct firewire_comm *, int, struct fw_xfer *);
void fw_busreset (struct firewire_comm *, uint32_t);
uint16_t fw_crc16 (uint32_t *, uint32_t);
void fw_xfer_timeout (void *);
void fw_xfer_done (struct fw_xfer *);
void fw_xferwake (struct fw_xfer *);
int fw_xferwait (struct fw_xfer *);
void fw_asy_callback_free (struct fw_xfer *);
struct fw_device *fw_noderesolve_nodeid (struct firewire_comm *, int);
struct fw_device *fw_noderesolve_eui64 (struct firewire_comm *, struct fw_eui64 *);
struct fw_bind *fw_bindlookup (struct firewire_comm *, uint16_t, uint32_t);
void fw_drain_txq (struct firewire_comm *);
int fwdev_makedev (struct firewire_softc *);
int fwdev_destroydev (struct firewire_softc *);
void fwdev_clone (void *, struct ucred *, char *, int, struct cdev **);
int fw_open_isodma(struct firewire_comm *, int);

extern int firewire_debug;
extern devclass_t firewire_devclass;
extern int firewire_phydma_enable;

#define	FWPRI		((PZERO + 8) | PCATCH)

#define CALLOUT_INIT(x) callout_init(x, 1 /* mpsafe */)

MALLOC_DECLARE(M_FW);
MALLOC_DECLARE(M_FWXFER);
