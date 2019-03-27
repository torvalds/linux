/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001,2002,2003 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "dev/pst/pst-iop.h"

struct iop_request {
    struct i2o_single_reply *reply;
    u_int32_t mfa;
};

/* local vars */
MALLOC_DEFINE(M_PSTIOP, "PSTIOP", "Promise SuperTrak IOP driver");

int
iop_init(struct iop_softc *sc)
{
    int mfa, timeout = 10000;

    while ((mfa = sc->reg->iqueue) == 0xffffffff && --timeout)
	DELAY(1000);
    if (!timeout) {
	printf("pstiop: no free mfa\n");
	return 0;
    }
    iop_free_mfa(sc, mfa);

    sc->reg->oqueue_intr_mask = 0xffffffff;

    if (!iop_reset(sc)) {
	printf("pstiop: no reset response\n");
	return 0;
    }

    if (!iop_init_outqueue(sc)) {
	printf("pstiop: init outbound queue failed\n");
	return 0;
    }

    /* register iop_attach to be run when interrupts are enabled */
    if (!(sc->iop_delayed_attach = (struct intr_config_hook *)
				   malloc(sizeof(struct intr_config_hook),
				   M_PSTIOP, M_NOWAIT | M_ZERO))) {
	printf("pstiop: malloc of delayed attach hook failed\n");
	return 0;
    }
    sc->iop_delayed_attach->ich_func = iop_attach;
    sc->iop_delayed_attach->ich_arg = sc;
    if (config_intrhook_establish(sc->iop_delayed_attach)) {
	printf("pstiop: config_intrhook_establish failed\n");
	free(sc->iop_delayed_attach, M_PSTIOP);
    }
    return 1;
}

void
iop_attach(void *arg)
{
    struct iop_softc *sc;
    int i;

    sc = arg;
    if (sc->iop_delayed_attach) {
	config_intrhook_disestablish(sc->iop_delayed_attach);
	free(sc->iop_delayed_attach, M_PSTIOP);
	sc->iop_delayed_attach = NULL;
    }

    if (!iop_get_lct(sc)) {
	printf("pstiop: get LCT failed\n");
	return;
    }

    /* figure out what devices are here and config as needed */
    for (i = 0; sc->lct[i].entry_size == I2O_LCT_ENTRYSIZE; i++) {
#ifdef PSTDEBUG
	struct i2o_get_param_reply *reply;

	printf("pstiop: LCT entry %d ", i);
	printf("class=%04x ", sc->lct[i].class);
	printf("sub=%04x ", sc->lct[i].sub_class);
	printf("localtid=%04x ", sc->lct[i].local_tid);
	printf("usertid=%04x ", sc->lct[i].user_tid);
	printf("parentid=%04x\n", sc->lct[i].parent_tid);

	if ((reply = iop_get_util_params(sc, sc->lct[i].local_tid,
					 I2O_PARAMS_OPERATION_FIELD_GET,
					 I2O_UTIL_DEVICE_IDENTITY_GROUP_NO))) {
	    struct i2o_device_identity *ident =
		(struct i2o_device_identity *)reply->result;
	    printf("pstiop: vendor=<%.16s> product=<%.16s>\n",
		   ident->vendor, ident->product);
	    printf("pstiop: description=<%.16s> revision=<%.8s>\n",
		   ident->description, ident->revision);
	    contigfree(reply, PAGE_SIZE, M_PSTIOP);
	}
#endif

	if (sc->lct[i].user_tid != I2O_TID_NONE &&
	    sc->lct[i].user_tid != I2O_TID_HOST)
	    continue;

	switch (sc->lct[i].class) {
	case I2O_CLASS_DDM:
	    if (sc->lct[i].sub_class == I2O_SUBCLASS_ISM)
		sc->ism = sc->lct[i].local_tid;
	    break;

	case I2O_CLASS_RANDOM_BLOCK_STORAGE:
	    pst_add_raid(sc, &sc->lct[i]);
	    break;
	}
    }

    /* setup and enable interrupts */
    bus_setup_intr(sc->dev, sc->r_irq, INTR_TYPE_BIO|INTR_ENTROPY|INTR_MPSAFE,
		   NULL, iop_intr, sc, &sc->handle);
    sc->reg->oqueue_intr_mask = 0x0;
}

void
iop_intr(void *data)
{
    struct iop_softc *sc = (struct iop_softc *)data;
    struct i2o_single_reply *reply;
    u_int32_t mfa;

    /* we might get more than one finished request pr interrupt */
    mtx_lock(&sc->mtx);
    while (1) {
	if ((mfa = sc->reg->oqueue) == 0xffffffff)
	    if ((mfa = sc->reg->oqueue) == 0xffffffff)
		break;

	reply = (struct i2o_single_reply *)(sc->obase + (mfa - sc->phys_obase));

	/* if this is an event register reply, shout! */
	if (reply->function == I2O_UTIL_EVENT_REGISTER) {
	    struct i2o_util_event_reply_message *event =
		(struct i2o_util_event_reply_message *)reply;

	    printf("pstiop: EVENT!! idx=%08x data=%08x\n",
		   event->event_mask, event->event_data[0]);
	    break;
	}

	/* if reply is a failurenotice we need to free the original mfa */
	if (reply->message_flags & I2O_MESSAGE_FLAGS_FAIL)
	    iop_free_mfa(sc,((struct i2o_fault_reply *)(reply))->preserved_mfa);

	/* reply->initiator_context points to the service routine */
	((void (*)(struct iop_softc *, u_int32_t, struct i2o_single_reply *))
	    (reply->initiator_context))(sc, mfa, reply);
    }
    mtx_unlock(&sc->mtx);
}

int
iop_reset(struct iop_softc *sc)
{
    struct i2o_exec_iop_reset_message *msg;
    int mfa, timeout = 5000;
    volatile u_int32_t reply = 0;

    mfa = iop_get_mfa(sc);
    msg = (struct i2o_exec_iop_reset_message *)(sc->ibase + mfa);
    bzero(msg, sizeof(struct i2o_exec_iop_reset_message));
    msg->version_offset = 0x1;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_exec_iop_reset_message) >> 2;
    msg->target_address = I2O_TID_IOP;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_EXEC_IOP_RESET;
    msg->status_word_low_addr = vtophys(&reply);
    msg->status_word_high_addr = 0;

    sc->reg->iqueue = mfa;

    while (--timeout && !reply)
	DELAY(1000);

    /* wait for iqueue ready */
    timeout = 10000;
    while ((mfa = sc->reg->iqueue) == 0xffffffff && --timeout)
	DELAY(1000);

    iop_free_mfa(sc, mfa);
    return reply;
}

int
iop_init_outqueue(struct iop_softc *sc)
{
    struct i2o_exec_init_outqueue_message *msg;
    int i, mfa, timeout = 5000;
    volatile u_int32_t reply = 0;

    if (!(sc->obase = contigmalloc(I2O_IOP_OUTBOUND_FRAME_COUNT *
				   I2O_IOP_OUTBOUND_FRAME_SIZE,
				   M_PSTIOP, M_NOWAIT,
				   0x00010000, 0xFFFFFFFF,
				   PAGE_SIZE, 0))) {
	printf("pstiop: contigmalloc of outqueue buffers failed!\n");
	return 0;
    }
    sc->phys_obase = vtophys(sc->obase);
    mfa = iop_get_mfa(sc);
    msg = (struct i2o_exec_init_outqueue_message *)(sc->ibase + mfa);
    bzero(msg, sizeof(struct i2o_exec_init_outqueue_message));
    msg->version_offset = 0x61;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_exec_init_outqueue_message) >> 2;
    msg->target_address = I2O_TID_IOP;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_EXEC_OUTBOUND_INIT;
    msg->host_pagesize = PAGE_SIZE;
    msg->init_code = 0x00; /* SOS XXX should be 0x80 == OS */
    msg->queue_framesize = I2O_IOP_OUTBOUND_FRAME_SIZE / sizeof(u_int32_t);
    msg->sgl[0].flags = I2O_SGL_SIMPLE | I2O_SGL_END | I2O_SGL_EOB;
    msg->sgl[0].count = sizeof(reply);
    msg->sgl[0].phys_addr[0] = vtophys(&reply);
    msg->sgl[1].flags = I2O_SGL_END | I2O_SGL_EOB;
    msg->sgl[1].count = 1;
    msg->sgl[1].phys_addr[0] = 0;

    sc->reg->iqueue = mfa;

    /* wait for init to complete */
    while (--timeout && reply != I2O_EXEC_OUTBOUND_INIT_COMPLETE)
	DELAY(1000);

    if (!timeout) {
	printf("pstiop: timeout waiting for init-complete response\n");
	iop_free_mfa(sc, mfa);
	return 0;
    }

    /* now init our oqueue bufs */
    for (i = 0; i < I2O_IOP_OUTBOUND_FRAME_COUNT; i++) {
	sc->reg->oqueue = sc->phys_obase + (i * I2O_IOP_OUTBOUND_FRAME_SIZE);
	DELAY(1000);
    }

    return 1;
}

int
iop_get_lct(struct iop_softc *sc)
{
    struct i2o_exec_get_lct_message *msg;
    struct i2o_get_lct_reply *reply;
    int mfa;
#define ALLOCSIZE	 (PAGE_SIZE + (256 * sizeof(struct i2o_lct_entry)))

    if (!(reply = contigmalloc(ALLOCSIZE, M_PSTIOP, M_NOWAIT | M_ZERO,
			       0x00010000, 0xFFFFFFFF, PAGE_SIZE, 0)))
	return 0;

    mfa = iop_get_mfa(sc);
    msg = (struct i2o_exec_get_lct_message *)(sc->ibase + mfa);
    bzero(msg, sizeof(struct i2o_exec_get_lct_message));
    msg->version_offset = 0x61;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_exec_get_lct_message) >> 2;
    msg->target_address = I2O_TID_IOP;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_EXEC_LCT_NOTIFY;
    msg->class = I2O_CLASS_MATCH_ANYCLASS;
    msg->last_change_id = 0;

    msg->sgl.flags = I2O_SGL_SIMPLE | I2O_SGL_END | I2O_SGL_EOB;
    msg->sgl.count = ALLOCSIZE;
    msg->sgl.phys_addr[0] = vtophys(reply);

    if (iop_queue_wait_msg(sc, mfa, (struct i2o_basic_message *)msg)) {
	contigfree(reply, ALLOCSIZE, M_PSTIOP);
	return 0;
    }
    if (!(sc->lct = malloc(reply->table_size * sizeof(struct i2o_lct_entry),
			   M_PSTIOP, M_NOWAIT | M_ZERO))) {
	contigfree(reply, ALLOCSIZE, M_PSTIOP);
	return 0;
    }
    bcopy(&reply->entry[0], sc->lct,
	  reply->table_size * sizeof(struct i2o_lct_entry));
    sc->lct_count = reply->table_size;
    contigfree(reply, ALLOCSIZE, M_PSTIOP);
    return 1;
}

struct i2o_get_param_reply *
iop_get_util_params(struct iop_softc *sc, int target, int operation, int group)
{
    struct i2o_util_get_param_message *msg;
    struct i2o_get_param_operation *param;
    struct i2o_get_param_reply *reply;
    int mfa;

    if (!(param = contigmalloc(PAGE_SIZE, M_PSTIOP, M_NOWAIT | M_ZERO,
			       0x00010000, 0xFFFFFFFF, PAGE_SIZE, 0)))
	return NULL;

    if (!(reply = contigmalloc(PAGE_SIZE, M_PSTIOP, M_NOWAIT | M_ZERO,
			       0x00010000, 0xFFFFFFFF, PAGE_SIZE, 0)))
	return NULL;

    mfa = iop_get_mfa(sc);
    msg = (struct i2o_util_get_param_message *)(sc->ibase + mfa);
    bzero(msg, sizeof(struct i2o_util_get_param_message));
    msg->version_offset = 0x51;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_util_get_param_message) >> 2;
    msg->target_address = target;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_UTIL_PARAMS_GET;
    msg->operation_flags = 0;

    param->operation_count = 1;
    param->operation[0].operation = operation;
    param->operation[0].group = group;
    param->operation[0].field_count = 0xffff;

    msg->sgl[0].flags = I2O_SGL_SIMPLE | I2O_SGL_DIR | I2O_SGL_EOB;
    msg->sgl[0].count = sizeof(struct i2o_get_param_operation);
    msg->sgl[0].phys_addr[0] = vtophys(param);

    msg->sgl[1].flags = I2O_SGL_SIMPLE | I2O_SGL_END | I2O_SGL_EOB;
    msg->sgl[1].count = PAGE_SIZE;
    msg->sgl[1].phys_addr[0] = vtophys(reply);

    if (iop_queue_wait_msg(sc, mfa, (struct i2o_basic_message *)msg) ||
	reply->error_info_size) {
	contigfree(reply, PAGE_SIZE, M_PSTIOP);
	reply = NULL;
    }
    contigfree(param, PAGE_SIZE, M_PSTIOP);
    return reply;
}

u_int32_t
iop_get_mfa(struct iop_softc *sc)
{
    u_int32_t mfa;
    int timeout = 10000;

    while ((mfa = sc->reg->iqueue) == 0xffffffff && timeout) {
	DELAY(1000);
	timeout--;
    }
    if (!timeout)
	printf("pstiop: no free mfa\n");
    return mfa;
}

void
iop_free_mfa(struct iop_softc *sc, int mfa)
{
    struct i2o_basic_message *msg = (struct i2o_basic_message *)(sc->ibase+mfa);

    bzero(msg, sizeof(struct i2o_basic_message));
    msg->version = 0x01;
    msg->message_flags = 0x0;
    msg->message_size = sizeof(struct i2o_basic_message) >> 2;
    msg->target_address = I2O_TID_IOP;
    msg->initiator_address = I2O_TID_HOST;
    msg->function = I2O_UTIL_NOP;
    sc->reg->iqueue = mfa;
}

static void
iop_done(struct iop_softc *sc, u_int32_t mfa, struct i2o_single_reply *reply)
{
    struct iop_request *request =
        (struct iop_request *)reply->transaction_context;
    
    request->reply = reply;
    request->mfa = mfa;
    wakeup(request);
}

int
iop_queue_wait_msg(struct iop_softc *sc, int mfa, struct i2o_basic_message *msg)
{
    struct i2o_single_reply *reply;
    struct iop_request request;
    u_int32_t out_mfa;
    int status, timeout = 10000;

    mtx_lock(&sc->mtx);
    if (!(sc->reg->oqueue_intr_mask & 0x08)) {
        msg->transaction_context = (u_int32_t)&request;
        msg->initiator_context = (u_int32_t)iop_done;
        sc->reg->iqueue = mfa;
        if (msleep(&request, &sc->mtx, PRIBIO, "pstwt", 10 * hz)) {
	    printf("pstiop: timeout waiting for message response\n");
	    iop_free_mfa(sc, mfa);
	    mtx_unlock(&sc->mtx);
	    return -1;
	}
        status = request.reply->status;
        sc->reg->oqueue = request.mfa;
    }
    else {
	sc->reg->iqueue = mfa;
	while (--timeout && ((out_mfa = sc->reg->oqueue) == 0xffffffff))
	    DELAY(1000);
	if (!timeout) {
	    printf("pstiop: timeout waiting for message response\n");
	    iop_free_mfa(sc, mfa);
	    mtx_unlock(&sc->mtx);
	    return -1;
	}
	reply = (struct i2o_single_reply *)(sc->obase+(out_mfa-sc->phys_obase));
	status = reply->status;
	sc->reg->oqueue = out_mfa;
    }
    mtx_unlock(&sc->mtx);
    return status;
}

int
iop_create_sgl(struct i2o_basic_message *msg, caddr_t data, int count, int dir)
{
    struct i2o_sgl *sgl = (struct i2o_sgl *)((int32_t *)msg + msg->offset);
    u_int32_t sgl_count, sgl_phys;
    int i = 0;

    if (((uintptr_t)data & 3) || (count & 3)) {
	printf("pstiop: non aligned DMA transfer attempted\n");
	return 0;
    }
    if (!count) {
	printf("pstiop: zero length DMA transfer attempted\n");
	return 0;
    }

    sgl_count = min(count, (PAGE_SIZE - ((uintptr_t)data & PAGE_MASK)));
    sgl_phys = vtophys(data);
    sgl->flags = dir | I2O_SGL_PAGELIST | I2O_SGL_EOB | I2O_SGL_END;
    sgl->count = count;
    data += sgl_count;
    count -= sgl_count;

    while (count) {
	sgl->phys_addr[i] = sgl_phys;
	sgl_phys = vtophys(data);
	data += min(count, PAGE_SIZE);
	count -= min(count, PAGE_SIZE);
	if (++i >= I2O_SGL_MAX_SEGS) {
	    printf("pstiop: too many segments in SGL\n");
	    return 0;
	}
    }
    sgl->phys_addr[i] = sgl_phys;
    msg->message_size += i;
    return 1;
}
