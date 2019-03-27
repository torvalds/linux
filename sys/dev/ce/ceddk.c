/*
 * Middle-level code for Cronyx Tau32-PCI adapters.
 *
 * Copyright (C) 2004 Cronyx Engineering
 * Copyright (C) 2004 Roman Kurakin <rik@FreeBSD.org>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx: ceddk.c,v 1.2.6.2 2005/11/17 16:04:13 rik Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/cx/machdep.h>
#include <dev/ce/ceddk.h>

#undef CE_DDK_DEBUG_ENABLED
#ifdef CE_DDK_DEBUG_ENABLED
#ifdef __FreeBSD__
# define CE_DDK_DEBUG(b,c,s) \
	do { \
		if (c) { \
			printf("ce%d-%d: ",(b)->num,(c)->num); \
		} else { \
			printf("ce%d-*: ",(b)->num); \
		} \
		printf s; \
	} while (0)
#else
# define CE_DDK_DEBUG(b,c,s)	do {} while (0)
#endif
#else
# define CE_DDK_DEBUG(b,c,s)	do {} while (0)
#endif

#if 0
#define ENTER() \
	static int enter=0; \
	do { \
	enter++; \
	printf ("%s: >> enter (%16llx) %d\n", __FUNCTION__, rdtsc (), enter); \
	} while (0)

#define EXIT(val...) \
	do { \
	enter--; \
	printf ("%s: << exit  (%16llx) %d line %d\n", __FUNCTION__, rdtsc (), enter, __LINE__); \
	return val; \
	} while (0)
#else
#define ENTER() \
	do {} while (0)

#define EXIT(val...) \
	do {return val;} while (0)
#endif

#define CE_ENQUEUE(list,item) \
	do { \
		TAU32_UserRequest **last; \
		last = &(list); \
		while (*last) { \
			last = &(*last)->next; \
		} \
		(*last) = (item); \
		(item)->next = NULL; \
	} while (0)
	
#define CE_ENQUEUE_HEAD(list,item) \
	do { \
		(item)->next = list; \
		list = item; \
	} while (0)
	
#define CE_DEQUEUE(list,item) \
	do { \
		item = list; \
		if (list) { \
			list = (item)->next; \
		} \
	} while (0)
	
#define CE_PREREQUEST(b,c,list,item) \
	do { \
		item = list; \
		if (!item) { \
			CE_DDK_DEBUG (b, c, ("Fatal error, no free structs " \
					     "for UserRequest (%s:%d)\n", \
					     __FUNCTION__, __LINE__)); \
		} \
	} while (0)

#define CE_DUMP_QUEUE(list) \
	do { \
		TAU32_UserRequest *item; \
		int i = 0; \
		item = list; \
		while (item) { \
			printf ("item%d: %p\n", i, item); \
			item = item->next; \
			i++; \
		} \
	} while (0)

#define CE_FIND_ITEM(list,item,flag) \
	do { \
		TAU32_UserRequest *citem; \
		flag = 0; \
		for (citem = list; citem; citem = citem->next) { \
			if (citem == item) { \
				flag = 1; \
				break; \
			} \
		} \
	} while (0)
	
#define CE_LAST_ITEM(list,item) \
	do { \
		TAU32_UserRequest **last; \
		last = &(list); \
		while ((*last) && (*last)->next) { \
			last = &(*last)->next; \
		} \
		(item) = (*last); \
	} while (0)

#define CE_ASSERT(a) \
	do { \
		if (!(a)) { \
			printf ("ASSERT: %d %s\n", __LINE__, #a); \
			__asm __volatile ("int $3"); \
		} \
	} while (0)

static void _ce_set_ts (ce_chan_t *c, unsigned long ts);
static void _ce_submit_configure_e1 (ce_chan_t *c, char *rname);

#ifdef CE_DDK_DEBUG_ENABLED
static char *ce_err2str (unsigned long err)
{
	switch (err) {
	case TAU32_SUCCESSFUL:
		return "Successful";
	case TAU32_ERROR_ALLOCATION:
		return "Allocation error, not enough tx/rx descriptors";
	case TAU32_ERROR_BUS:
		return "PEB could not access to host memory by PCI bus for load/store information";
	case TAU32_ERROR_FAIL:
		return "PEB action request failed";
	case TAU32_ERROR_TIMEOUT:
		return "PEB action request timeout";
	case TAU32_ERROR_CANCELLED:
		return "request has been canceled";
	case TAU32_ERROR_TX_UNDERFLOW:
		return "transmission underflow";
	case TAU32_ERROR_TX_PROTOCOL:
		return "TX_PROTOCOL";
	case TAU32_ERROR_RX_OVERFLOW:
		return "RX_OVERFLOW";
	case TAU32_ERROR_RX_ABORT:
		return "RX_ABORT";
	case TAU32_ERROR_RX_CRC:
		return "RX_CRC";
	case TAU32_ERROR_RX_SHORT:
		return "RX_SHORT";
	case TAU32_ERROR_RX_SYNC:
		return "RX_SYNC";
	case TAU32_ERROR_RX_FRAME:
		return "RX_FRAME";
	case TAU32_ERROR_RX_LONG:
		return "RX_LONG";
	case TAU32_ERROR_RX_SPLIT:
		return "frame has splitted between two requests due rx-gap allocation";
	case TAU32_ERROR_RX_UNFIT:
		return "frame can't be fit into request buffer";
	case TAU32_ERROR_TSP:
		return "ERROR_TSP";
	case TAU32_ERROR_RSP:
		return "ERROR_RSP";
	case TAU32_ERROR_INT_OVER_TX:
		return "ERROR INT OVER TX";
	case TAU32_ERROR_INT_OVER_RX:
		return "ERROR INT OVER RX";
	case TAU32_ERROR_INT_STORM:
		return "irq storm";
	case TAU32_ERROR_INT_E1LOST:
		return "ERROR_E1LOST";
	default:
		return ("Unknown error");
	}
}
#endif

void ce_set_dtr (ce_chan_t *c, int on)
{
	c->dtr = on?1:0;
}

void ce_set_rts (ce_chan_t *c, int on)
{
	c->rts = on?1:0;
}

static void TAU32_CALLBACK_TYPE ce_on_receive
	(TAU32_UserContext *pContext, TAU32_UserRequest *req)
{
	ce_buf_item_t *item = (ce_buf_item_t *)req;
	ce_chan_t *c;
	ce_board_t *b;
	unsigned int error;
	int len;

	ENTER ();
	if (!req || !req->sys) {
		EXIT ();
	}

	c = (ce_chan_t *)req->sys;
	b = c->board;

	len = req->Io.Rx.Received;
	error = req->ErrorCode;
	
	c->rintr++;
	if (error == TAU32_SUCCESSFUL) {
		if (req->Io.Rx.FrameEnd) {
			c->ipkts++;
		} else {
			CE_DDK_DEBUG (b, c, ("No FrameEnd\n"));
			/* probably do something in some cases*/
		}
		c->ibytes += len;
		if (c->receive)
			c->receive (c, item->buf, len);
	} else if (error & TAU32_ERROR_BUS) {
		c->overrun++;
		if (c->error)
			c->error (c, CE_OVERRUN);
	} else {
		CE_DDK_DEBUG (b, c, ("Another receive error: %x\n", error));
		/* Do some procesing */
	}
	
	CE_ASSERT (!req->pInternal);
	CE_ENQUEUE (c->rx_queue, req);
	while (c->rx_queue) {
		CE_DEQUEUE (c->rx_queue, req);
		CE_ASSERT (req);
		item = (ce_buf_item_t *)req;
		req->Command = TAU32_Rx_Data;
		req->Io.Rx.Channel = c->num;
		req->pCallback = ce_on_receive;
		req->Io.Rx.BufferLength = BUFSZ+4;
		req->Io.Rx.PhysicalDataAddress = item->phys;
		if (!TAU32_SubmitRequest (b->ddk.pControllerObject, req)) {
			CE_DDK_DEBUG (b, c, ("RX submition failure\n"));
			c->rx_pending--;
			CE_ENQUEUE_HEAD (c->rx_queue, req);
			break;
		}
	}
	EXIT ();
}

static void TAU32_CALLBACK_TYPE ce_on_transmit
	(TAU32_UserContext *pContext, TAU32_UserRequest *req)
{
	int len;
	unsigned int error;
	ce_chan_t *c;
	ENTER ();

	if (!req || !req->sys) {
		EXIT ();
	}

	c = (ce_chan_t *)req->sys;

	len = req->Io.Tx.Transmitted;
	error = req->ErrorCode;
	
	c->tintr++;
	if (error == TAU32_SUCCESSFUL) {
		c->obytes += len;
		c->opkts++;
	} else if (error & TAU32_ERROR_BUS) {
		c->underrun++;
		if (c->error)
			c->error (c, CE_UNDERRUN);		
	} else {
		CE_DDK_DEBUG (c->board, c, ("Another transmit error: %x\n",
				error));
		/* Do some procesing */
	}
	
	CE_ENQUEUE (c->tx_queue, req);
	c->tx_pending--;
	
	if (c->transmit)
		c->transmit (c, 0, len);
	EXIT ();
}

int ce_transmit_space (ce_chan_t *c)
{
	return c->tx_pending < (TAU32_IO_QUEUE);
}

int ce_send_packet (ce_chan_t *c, unsigned char *buf, int len, void *tag)
{
	TAU32_UserRequest *req;
	ce_buf_item_t *item;
	
	ENTER ();

	if (!ce_transmit_space (c)) {
		EXIT (-1);
	}

	if (len <= 0 || len > BUFSZ) {
		EXIT (-2);
	}

	CE_DEQUEUE (c->tx_queue, req);
	CE_ASSERT (req);
	item = (ce_buf_item_t *)req;
		
	if (buf != item->buf)
		memcpy (item->buf, buf, len);
		
	CE_ASSERT (!req->pInternal);
		
	req->Command = TAU32_Tx_Data | TAU32_Tx_FrameEnd;
	req->Io.Tx.Channel = c->num;
	req->pCallback = ce_on_transmit;
	req->Io.Tx.DataLength = len;
	req->Io.Tx.PhysicalDataAddress = item->phys;
	c->tx_pending++;
	if (!TAU32_SubmitRequest (c->board->ddk.pControllerObject, req)) {
		CE_DDK_DEBUG (c->board, c, ("Can't submit packet for "
					    "transmission\n"));
		CE_ENQUEUE_HEAD (c->tx_queue, req);
		c->tx_pending--;
		EXIT (-3);
	}
	EXIT (0);
}

static void TAU32_CALLBACK_TYPE ce_on_config
	(TAU32_UserContext *pContext, TAU32_UserRequest *req)
{
	ce_board_t *b = (ce_board_t *) pContext;
	ENTER ();
	b->cr.pending--;
	if (req->ErrorCode)
		CE_DDK_DEBUG (b, (ce_chan_t*)0, ("Config request failure: %lx\n",
			      req->ErrorCode));
	EXIT ();
}

static void TAU32_CALLBACK_TYPE ce_on_config_stop
	(TAU32_UserContext *pContext, TAU32_UserRequest *req)
{
	int i, first;
	TAU32_UserRequest *rreq;
	ce_board_t *b = (ce_board_t *) pContext;
	ce_chan_t *c = b->chan + req->Io.ChannelNumber;
	
	ENTER ();
	/* Stop all requests */
	CE_ASSERT (0);/* Buggy */
	CE_LAST_ITEM (c->rx_queue, rreq);
	/* A little hacky, try to guess which is a first */
	first = rreq ? (c->rx_item - (ce_buf_item_t *)rreq) + 1 : 0;
	for (i = 0; i < TAU32_IO_QUEUE; i++) {
		int is_pending;
		rreq = &c->rx_item[(i + first) % TAU32_IO_QUEUE].req;
		CE_FIND_ITEM (c->rx_queue, rreq, is_pending);
		if (!is_pending)
			continue;
		TAU32_CancelRequest (b->ddk.pControllerObject, rreq, 1);
		rreq->Command = TAU32_Rx_Data;
		rreq->Io.Rx.Channel = c->num;
		rreq->Io.Rx.BufferLength = BUFSZ+4;
		rreq->Io.Rx.PhysicalDataAddress = ((ce_buf_item_t *)rreq)->phys;
		c->rx_pending++;
		if (!TAU32_SubmitRequest (b->ddk.pControllerObject, rreq)) {
			CE_ASSERT (0);/* Buggy */
			c->rx_pending--;
			break;
		}
	}
	
	c->tx_pending = 0;
/*	c->rx_pending = 0;*/
	EXIT ();
}

static int ce_cfg_submit (ce_board_t *b)
{
	TAU32_UserRequest *req;
	ENTER ();

	CE_DEQUEUE (b->cr.queue, req);
	CE_ASSERT (req);
	CE_ASSERT (!req->pInternal);

	req->pCallback = ce_on_config;
	b->cr.pending++;
	
	CE_DDK_DEBUG (b, (ce_chan_t *)0, ("config request pending: %d\n",
		      b->cr.pending));

	if (!TAU32_SubmitRequest (b->ddk.pControllerObject, req)) {
		CE_ENQUEUE_HEAD (b->cr.queue, req);
		CE_DDK_DEBUG (b, (ce_chan_t *)0, ("Fail to submit config request\n"));
		b->cr.pending--;
		EXIT (0);
	}
	
	EXIT (1);
}

void ce_init_board (ce_board_t *b)
{
	int i;

	b->cr.queue = NULL;

	for (i = 0; i < CONFREQSZ; i++) {
		CE_ENQUEUE (b->cr.queue, b->cr.req + i);
	}

	b->chan[0].config = TAU32_ais_on_loss;

	/* lloop = off, rloop = off */
	b->chan[0].config |= TAU32_LineNormal;
	b->chan[0].lloop = 0;
	b->chan[0].rloop = 0;

	/* unfram=off, scrambler=off, use16=off, crc4=off,
	   higain=off, monitor=off*/
	b->chan[0].config |= (b->ddk.Interfaces == 2 ? TAU32_framed_cas_cross :
						       TAU32_framed_cas_set);
	b->chan[0].unfram = 0;
	b->chan[0].scrambler = 0;
	b->chan[0].use16 = 0;
	b->chan[0].crc4 = 0;
	b->chan[0].higain = 0;
	b->chan[0].monitor = 0;

	if (b->ddk.Interfaces == 2) {
		b->chan[1].config = TAU32_ais_on_loss;
		/* lloop = off, rloop = off */
		b->chan[1].config |= TAU32_LineNormal;
		/* unfram=off, scrambler=off, use16=off, crc4=off,
		   higain=off, monitor=off*/
		b->chan[1].config |= TAU32_framed_cas_cross;
		b->chan[1].unfram = 0;
		b->chan[1].scrambler = 0;
		b->chan[1].use16 = 0;
		b->chan[1].crc4 = 0;
		b->chan[1].higain = 0;
		b->chan[1].monitor = 0;
	}

	for (i = 0; i < NCHAN; i++) {
		/* Chan0 ts=1-15,17-31, Chan1 ts=1-2 */
		b->chan[i].type = i < b->ddk.Interfaces ? T_E1 : T_DATA;
		b->chan[i].ts = (i == 0 ? 0xfffefffe :
				(i != 1 ? 0 : 
				(b->ddk.Interfaces == 2 ? 0x6: 0)));
		b->chan[i].dir = (b->ddk.Interfaces == 2) ? (i%2) : 0;
		b->chan[i].mtu = 1504;
	}
#if 0
	/* c->num == 0 */
	req = b->cr.queue;
	/* We must have some here */
	CE_ASSERT (req);
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = TAU32_E1_A;
	req->Io.InterfaceConfig.Config = b->chan[0].config;
	req->Io.InterfaceConfig.UnframedTsMask = 0;
	if (!ce_cfg_submit (b)) {
		CE_DDK_DEBUG (b, b->chan + 0,
			      ("Submit request failure, line %d\n",
			      __LINE__));
	}
	/* c->num == 1 */
	if (b->ddk.Interfaces == 2) {
		req = b->cr.queue;
		/* We must have some here */
		CE_ASSERT (req);
		req->Command = TAU32_Configure_E1;
		req->Io.InterfaceConfig.Interface = TAU32_E1_B;
		req->Io.InterfaceConfig.Config = b->chan[1].config;
		req->Io.InterfaceConfig.UnframedTsMask = 0;
		if (!ce_cfg_submit (b)) {
			CE_DDK_DEBUG (b, b->chan + 1,
				      ("Submit request failure, line %d\n",
				      __LINE__));
		}
	}
#endif
	/* Set default cross matrix */
	for (i = 0; i < 32; i++) {
		/* -X-> Peb */
		b->dxc[i] = TAU32_CROSS_OFF;
		/* Link2 -> Link1 */
		b->dxc[i + 32] = i + 64;
		/* Link1 -> Link2 */
		b->dxc[i + 64] = i + 32;
	}

	/* We have only mux mode for now. Later we will also have cross mode */
	b->mux = 1;
}

void ce_start_chan (ce_chan_t *c, int tx, int rx, ce_buf_t *cb,
	unsigned long phys)
{
	int i;
	ce_board_t *b = c->board;

/*	c->config = TAU32_ais_on_loss | TAU32_framed_cas_cross;*/

	if (cb) {
		CE_DDK_DEBUG (b, c, ("ce_buf_t virt:%p phys:%p\n", cb,
			      (void *)phys));
		c->tx_item = cb->tx_item;
		c->rx_item = cb->rx_item;
		c->tx_queue = NULL;
		c->rx_queue = NULL;
		for (i = 0; i < TAU32_IO_QUEUE; i++) {
			c->tx_item[i].phys = phys +
				((char *)(c->tx_item[i].buf)-(char *)cb);
			c->rx_item[i].phys = phys +
				((char *)(c->rx_item[i].buf)-(char *)cb);
			cb->tx_item[i].req.sys = c;
			cb->rx_item[i].req.sys = c;
			CE_DDK_DEBUG (b, c, ("tx_item[%d].buf virt:%p phys:%p\n",
				      i, c->tx_item[i].buf,
				      (void *)c->tx_item[i].phys));
			CE_DDK_DEBUG (b, c, ("rx_item[%d].buf virt:%p phys:%p\n",
				      i, c->rx_item[i].buf,
				      (void *)c->rx_item[i].phys));
			CE_ENQUEUE (c->rx_queue, &c->rx_item[i].req);
			CE_ENQUEUE (c->tx_queue, &c->tx_item[i].req);
		}
		c->tx_pending = 0;
		c->rx_pending = 0;
	}
	
	/* submit rx */
	while (1) {
		ce_buf_item_t *item;
		TAU32_UserRequest *req;
		
		CE_DEQUEUE (c->rx_queue, req);
		if (!req)
			break;
		item = (ce_buf_item_t *) req;
		CE_ASSERT (c->rx_pending < TAU32_IO_QUEUE);
		req->Command = TAU32_Rx_Data;
		req->Io.Rx.Channel = c->num;
		req->pCallback = ce_on_receive;
		req->Io.Rx.BufferLength = c->mtu + (c->phony ? 0 : 4);
		req->Io.Rx.PhysicalDataAddress = item->phys;
		c->rx_pending++;
		if (!TAU32_SubmitRequest (b->ddk.pControllerObject, req)) {
			CE_DDK_DEBUG (b, c, ("Faild to submit rx request\n"));
			/*XXXRIK: shouldn't happen, but ... */
			CE_ASSERT (0);
			c->rx_pending--;
			break;
		}	
	}
	
	if (tx | rx) {
		TAU32_UserRequest *req;
		CE_PREREQUEST (b, c, b->cr.queue, req);
		if (!req)
 			return;
		req->Command = TAU32_Configure_Commit |
			       (tx ? TAU32_Tx_Start : 0) |
			       (rx ? TAU32_Rx_Start : 0);
		req->Io.ChannelNumber = c->num;
		if (!ce_cfg_submit (b)) {
			CE_DDK_DEBUG (b, c, ("Can't start chan\n"));
			/* Do some error processing */
			return;
		}
	}
	
	/* If we run just after ce_board_init we have prope values.
	 * Else I hope you didn't set ts to incorrect value.
	 */
	_ce_set_ts (c, c->ts);
	if (c->num < b->ddk.Interfaces) {
		/* The same for other modes. We don't check them.
		 * We hope that config is correctly set. Just as we have
		 * after ce_board_init. If channel was stoped we hope that
		 * it's config was not broken just after it and we didn't
		 * brake it before start.
		 */
		_ce_submit_configure_e1 (c, "start_init");
	}
}

void ce_stop_chan (ce_chan_t *c)
{
	ce_board_t *b = c->board;
	TAU32_UserRequest *req;
	CE_DEQUEUE (b->cr.queue, req);

	/* XXXRIK: This function should be for completeness, but for now I
	 * don't use it. I just started to write and haven't finished it yet.
	 * It is VERY BUGGY!!! Do not use it. If you really need
	 * it ask me to fix it or rewrite it by yourself.
	 * Note: most buggy part of it in ce_on_config_stop!
	 */
	if (!req) {
		CE_DDK_DEBUG (b, c, ("Fatal error, no free structs for "
			      "UserRequest (%s:%d)\n", __FUNCTION__, __LINE__));
		return;
	}
//	req->Command = TAU32_Configure_Commit |
//		       TAU32_Tx_Stop | TAU32_Rx_Stop;
	req->Command = 0;
	req->Io.ChannelNumber = c->num;
	req->pCallback = ce_on_config_stop;
	b->cr.pending++;
	
	if (!TAU32_SubmitRequest (b->ddk.pControllerObject, req)) {
		CE_ENQUEUE_HEAD (b->cr.queue, req);
		CE_DDK_DEBUG (b, c, ("Can't stop chan\n"));
		b->cr.pending--;
	}
}	


void ce_register_transmit (ce_chan_t *c,
	void (*func) (ce_chan_t*, void*, int))
{
	c->transmit = func;
}

void ce_register_receive (ce_chan_t *c,
	void (*func) (ce_chan_t*, unsigned char*, int))
{
	c->receive = func;
}

void ce_register_error (ce_chan_t *c,
	void (*func) (ce_chan_t*, int))
{
	c->error = func;
}

void TAU32_CALLBACK_TYPE ce_error_callback (TAU32_UserContext *pContext,
						int Item, unsigned NotifyBits)
{
	ce_board_t *b = (ce_board_t *) pContext;
	ENTER ();
	if (NotifyBits & (TAU32_ERROR_FAIL | TAU32_ERROR_TIMEOUT
		| TAU32_ERROR_INT_OVER_TX | TAU32_ERROR_INT_OVER_RX
		| TAU32_ERROR_INT_STORM)) {
		/* Fatal: adapter failure, need reset & restart */
		/* RIKXXX: probably I should add CE_FAILURE for ce_error */
		CE_DDK_DEBUG (b, (ce_chan_t *)0, ("Err, disable interrupts: %s\n",
				ce_err2str (NotifyBits)));
/*		TAU32_DisableInterrupts (b->ddk.pControllerObject);*/
		EXIT ();
	}
	if (Item >= 0) {
		/* channel error */
		ce_chan_t *c = b->chan + Item;
		if (NotifyBits & TAU32_ERROR_TX_UNDERFLOW) {
			c->underrun++;
			if (c->error)
				c->error (c, CE_UNDERRUN);
		}
		if (NotifyBits & TAU32_ERROR_RX_OVERFLOW) {
			c->overrun++;
			if (c->error)
				c->error (c, CE_OVERFLOW);
		}
		if (NotifyBits & (TAU32_ERROR_RX_FRAME | TAU32_ERROR_RX_ABORT |
		    TAU32_ERROR_RX_SHORT | TAU32_ERROR_RX_LONG |
		    TAU32_ERROR_RX_SYNC | TAU32_ERROR_RX_SPLIT |
		    TAU32_ERROR_RX_UNFIT)) {
			c->frame++;
			CE_DDK_DEBUG (b, c, ("Frame error: %x\n", NotifyBits));
			if (c->error)
				c->error (c, CE_FRAME);
		}
		if(NotifyBits & TAU32_ERROR_RX_CRC) {
			c->crc++;
			if (c->error)
				c->error (c, CE_CRC);
		}
	} else {
		CE_DDK_DEBUG (b, (ce_chan_t *)0, ("Another error: %x\n",
			      NotifyBits));
		/* Adapter error, do smth */
	}
	EXIT ();
}

void TAU32_CALLBACK_TYPE ce_status_callback(TAU32_UserContext *pContext,
						int Item, unsigned NotifyBits)
{
	ce_board_t *b = (ce_board_t *) pContext;
	ENTER ();
	if(Item >= 0) {
		/* e1 status */
		ce_chan_t *c = b->chan + Item;
		c->acc_status |= b->ddk.InterfacesInfo[Item].Status;
/*		CE_DDK_DEBUG (b, c, ("Current status: %x\n", c->acc_status));*/
	} else {
		CE_DDK_DEBUG (b, (ce_chan_t *)0, ("Another status: %x\n", NotifyBits));
		/* Adapter status, do smth. */
	}
	EXIT ();
}

int ce_get_cd (ce_chan_t *c)
{
	unsigned int e1status = c->board->ddk.InterfacesInfo[c->dir].Status;

	return (c->ts && !(e1status & (TAU32_RCL | TAU32_E1OFF)));
}

int ce_get_cts (ce_chan_t *c)
{
	return 0;
}

int ce_get_dsr (ce_chan_t *c)
{
	return 0;
}

void ce_e1_timer (ce_chan_t *c)
{
	unsigned bpv, fas, crc4, ebit, pcv, oof, css;
	unsigned int acc_status;
	ce_board_t *b = c->board;
	TAU32_E1_State	*state;

	if (c->num >= b->ddk.Interfaces)
		return;
	
	state = &b->ddk.InterfacesInfo[c->num];
	acc_status = c->acc_status;
	
	/* Clear acc_status */
	c->acc_status = b->ddk.InterfacesInfo[c->num].Status;

	/* Count seconds.
	 * During the first second after the channel startup
	 * the status registers are not stable yet,
	 * we will so skip the first second. */
	++c->cursec;
	if (! c->totsec && c->cursec <= 1)
		return;

	c->status = 0;

	/* Compute the SNMP-compatible channel status. */
	oof = 0;

	if (acc_status & TAU32_RCL)
		c->status |= ESTS_LOS;		/* loss of signal */
	if (acc_status & TAU32_RUA1)
		c->status |= ESTS_AIS;		/* receiving all ones */

	/* Get error counters. */
	bpv = state->RxViolations;
	fas = 0;
	crc4 = 0;
	ebit = 0;
	css = 0;

	if (! c->unfram) {
		if (! c->use16 && (acc_status & TAU32_RSA1))
			c->status |= ESTS_AIS16;	/* signaling all ones */
		if (! c->use16 && (acc_status & TAU32_RDMA))
			c->status |= ESTS_FARLOMF;	/* alarm in timeslot 16 */
		if (acc_status & TAU32_RRA)
			c->status |= ESTS_FARLOF;	/* far loss of framing */

		if (acc_status & TAU32_RFAS) {
			c->status |= ESTS_LOF;		/* loss of framing */
			++oof;				/* out of framing */
		}
		if ((! c->use16 && (acc_status & TAU32_RCAS)) ||
		    (c->crc4 && (acc_status & TAU32_RCRC4))) {
			c->status |= ESTS_LOMF;		/* loss of multiframing */
			++oof;				/* out of framing */
		}
		fas = state->FasErrors;
		crc4 = state->Crc4Errors;
		ebit = state->FarEndBlockErrors;

		/* Controlled slip second -- any slip event. */
		css = state->TransmitSlips + state->ReceiveSlips;
	}
	
	/* Clear state */
	state->RxViolations		= 0;
	state->FasErrors		= 0;
	state->Crc4Errors		= 0;
	state->FarEndBlockErrors	= 0;
	state->TransmitSlips		= 0;
	state->ReceiveSlips		= 0;

	if (c->status & ESTS_LOS)
		c->status = ESTS_LOS;
	else if (c->status & ESTS_AIS)
		c->status = ESTS_AIS;
	else if (c->status & ESTS_LOF)
		c->status = ESTS_LOF;
	else if (c->status & ESTS_LOMF)
		c->status &= ~(ESTS_FARLOMF | ESTS_AIS16);

	if (! c->status)
		c->status = ESTS_NOALARM;

	c->currnt.bpv += bpv;
	c->currnt.fse += fas;
	if (c->crc4) {
		c->currnt.crce += crc4;
		c->currnt.rcrce += ebit;
	}

	/* Path code violation is frame sync error if CRC4 disabled,
	 * or CRC error if CRC4 enabled. */
	pcv = fas;
	if (c->crc4)
		pcv += crc4;

	/* Unavaiable second -- receiving all ones, or
	 * loss of carrier, or loss of signal. */
	if (acc_status & (TAU32_RUA1 | TAU32_RCL))
		/* Unavailable second -- no other counters. */
		++c->currnt.uas;
	else {
		/* Line errored second -- any BPV. */
		if (bpv)
			++c->currnt.les;

		/* Errored second -- any PCV, or out of frame sync,
		 * or any slip events. */
		if (pcv || oof || css)
			++c->currnt.es;

		/* Severely errored framing second -- out of frame sync. */
		if (oof)
			++c->currnt.oofs;

		/* Severely errored seconds --
		 * 832 or more PCVs, or 2048 or more BPVs. */
		if (bpv >= 2048 || pcv >= 832)
			++c->currnt.ses;
		else {
			/* Bursty errored seconds --
			 * no SES and more than 1 PCV. */
			if (pcv > 1)
				++c->currnt.bes;

			/* Collect data for computing
			 * degraded minutes. */
			++c->degsec;
			c->degerr += bpv + pcv;
		}
	}

	/* Degraded minutes -- having error rate more than 10e-6,
	 * not counting unavailable and severely errored seconds. */
	if (c->cursec % 60 == 0) {
		if (c->degerr > c->degsec * 2048 / 1000)
			++c->currnt.dm;
		c->degsec = 0;
		c->degerr = 0;
	}

	/* Rotate statistics every 15 minutes. */
	if (c->cursec > 15*60) {
		int i;

		for (i=47; i>0; --i)
			c->interval[i] = c->interval[i-1];
		c->interval[0] = c->currnt;

		/* Accumulate total statistics. */
		c->total.bpv   += c->currnt.bpv;
		c->total.fse   += c->currnt.fse;
		c->total.crce  += c->currnt.crce;
		c->total.rcrce += c->currnt.rcrce;
		c->total.uas   += c->currnt.uas;
		c->total.les   += c->currnt.les;
		c->total.es    += c->currnt.es;
		c->total.bes   += c->currnt.bes;
		c->total.ses   += c->currnt.ses;
		c->total.oofs  += c->currnt.oofs;
		c->total.css   += c->currnt.css;
		c->total.dm    += c->currnt.dm;
		c->currnt.bpv	= 0;
		c->currnt.fse	= 0;
		c->currnt.crce	= 0;
		c->currnt.rcrce	= 0;
		c->currnt.uas	= 0;
		c->currnt.les	= 0;
		c->currnt.es	= 0;
		c->currnt.bes	= 0;
		c->currnt.ses	= 0;
		c->currnt.oofs	= 0;
		c->currnt.css	= 0;
		c->currnt.dm	= 0;

		c->totsec += c->cursec;
		c->cursec = 0;
	}
}

void ce_set_baud (ce_chan_t *c, unsigned long baud)
{
	TAU32_UserRequest *req;
	ce_board_t *b = c->board;
	unsigned long cfg = c->config & ~TAU32_framing_mode_mask;
	unsigned long ts;
	unsigned long kbps = (baud + 32000) / 64000 * 64;
	
	if (!c->unfram || c->num != 0 ||
		baud == c->baud || b->cr.pending >= CONFREQSZ)
		return;
	
	if (!kbps || kbps > 1024) {
		ts = 0xffffffffUL;
		cfg |= TAU32_unframed_2048;
	} else if (kbps > 512) {
		ts = 0x0000ffffUL;
		cfg |= TAU32_unframed_1024;
	} else if (kbps > 256) {
		ts = 0x000000ffUL;
		cfg |= TAU32_unframed_512;
	} else if (kbps > 128) {
		ts = 0x0000000fUL;
		cfg |= TAU32_unframed_256;
	} else if (kbps > 64) {
		ts = 0x00000003UL;
		cfg |= TAU32_unframed_128;
	} else {
		ts = 0x00000001UL;
		cfg |= TAU32_unframed_64;
	}

	/* _ce_set_ts () will set proper baud */
	_ce_set_ts (c, ts);
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = ts;
	if (ce_cfg_submit (b)) {
		c->baud = baud;
		c->ts = ts;
		c->config = cfg;
	}
}

void ce_set_lloop (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~(TAU32_line_mode_mask | TAU32_ais_on_loss);
	ce_board_t *b = c->board;

	if (c->num >= b->ddk.Interfaces || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->lloop)
		return;

	cfg |= on ? TAU32_LineLoopInt : (TAU32_LineNormal | TAU32_ais_on_loss);
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit lloop\n"));
	if (ce_cfg_submit (b)) {
		c->lloop = on ? 1 : 0;
		c->config = cfg;
	} 
}

void ce_set_rloop (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~TAU32_line_mode_mask;
	ce_board_t *b = c->board;

	if (c->num >= b->ddk.Interfaces || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->rloop)
		return;

	cfg |= on ? TAU32_LineLoopExt : TAU32_LineNormal;
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit rloop\n"));
	if (ce_cfg_submit (b)) {
		c->rloop = on ? 1 : 0;
		c->config = cfg;
	}
}

void ce_set_higain (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~TAU32_higain;
	ce_board_t *b = c->board;

	if (c->num >= b->ddk.Interfaces || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->higain)
		return;

	cfg |= on ? TAU32_higain : 0;
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit higain\n"));
	if (ce_cfg_submit (b)) {
		c->higain = on ? 1 : 0;
		c->config = cfg;
	}
}

static void _ce_set_ts (ce_chan_t *c, unsigned long ts)
{
	TAU32_UserRequest *req;
	ce_board_t *b = c->board;
	unsigned long mask = 0, omask = 0;
	int nts = 0, ots = 0, pts = 0;
	int i, k;

	if (b->cr.pending >= CONFREQSZ)
		return;

	/*
	 * pts - number of busy "peb" ts
	 * ots - current (old) busy ts
	 * nts - new busy ts
	 */
	for (i = 0; i < 32; i++) {
		if (c->ts & (1ul << i))
			ots++;
		if (ts & (1ul << i))
			nts++;
		if (b->dxc[i] != TAU32_CROSS_OFF)
			pts++;
	}
	
	CE_DDK_DEBUG (b, c, ("pts: %d ots: %d nts: %d ts: %lx\n", pts, ots, nts,
		      ts));
	/* 32 - all busy + my old busy == free */
	if (32 - pts + ots - nts < 0)
		return;
		
	/* Ok. We have enougth "peb" ts. Clean old. */
	/* We start from zero, cause this is peb cells */
	for (i = 0; i < 32; i++) {
		int tin = b->dxc[i];
		int t = tin % 32;
		if (tin < (c->dir?64:32) || tin > (c->dir?95:63))
			continue;
		if (c->ts & (1ul << t)) {
			b->dxc[tin] = TAU32_CROSS_OFF;
			b->dxc[i] = TAU32_CROSS_OFF;
			if (b->dxc[t + 32] == TAU32_CROSS_OFF &&
			    b->dxc[t + 64] == TAU32_CROSS_OFF) {
				b->dxc[t + 32] = t + 64;
				b->dxc[t + 64] = t + 32;
			}
			omask |= (1ul << t);
		}
	}
	
	k = 0;
	/* Set */
	for (i = 0; i < 32; i++) {
		if ((ts & (1ul << i)) == 0)
			continue;
		while (b->dxc[k] != TAU32_CROSS_OFF) {
			k++;
			/* Paranoic */
			if (k >= 32) {
				CE_DDK_DEBUG (b, c, ("TS count overflow\n"));
				return;
			}
		}
		b->dxc[k] = (c->dir?64:32) + i;
		b->dxc[(c->dir?64:32) + i] = k;
		if (b->dxc[(c->dir?32:64) + i] == (c->dir?64:32) + i)
			b->dxc[(c->dir?32:64) + i] = TAU32_CROSS_OFF;
		mask |= (1ul << k);
	}
	
	c->ts = ts;
	c->baud = nts*64000;

	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;

	req->Command = TAU32_Timeslots_Channel | TAU32_Configure_Commit;
	req->Io.ChannelNumber = c->num;
	req->Io.ChannelConfig.AssignedTsMask = mask;	
	
	if (c->phony) {
		b->pmask &= ~omask;
		b->pmask |= mask;
	}
	
	CE_DDK_DEBUG (b, c, ("ts=%lx mask=%lx omask=%lx pmask=%lx\n", c->ts,
		      mask, omask, b->pmask));
	CE_DDK_DEBUG (b, c, ("Crossmatrix table:\n"));

#ifdef CE_DDK_DEBUG_ENABLED
	for (i = 0; i < 32*3; i++) {
		printf ("%3d\t%s", b->dxc[i], (i%8==7)?"\n":"");
		printf ("%s",(i%32==31)?"\n":"");
	}
#endif

	CE_DDK_DEBUG (b, c, ("Submit tsmask\n"));
	if (!ce_cfg_submit (b)) {
		CE_DDK_DEBUG (b, c, ("Fail to submit tsmask\n"));
		/* Do some error processing */
		return;
	}
	
	CE_DDK_DEBUG (b, c, ("SetCrossMatrix\n"));
	if (!TAU32_SetCrossMatrix(b->ddk.pControllerObject, b->dxc, b->pmask)) {
		CE_DDK_DEBUG (b, c, ("Faild to SetCrossMatrix\n"));
		/* Do some error processing */
		return;
	}
}

void ce_set_ts (ce_chan_t *c, unsigned long ts)
{
	ce_board_t *b = c->board;
	ce_chan_t *x;

	if (c->ts == ts || b->chan->unfram)
		return;

	ts &= ~(1ul);

	if (!b->chan[c->dir].use16)
		ts &= ~(1ul << 16);
		
	for (x = b->chan; x < b->chan + NCHAN; x++) {
		if (x == c || x->dir != c->dir)
			continue;
		ts &= ~x->ts;
	}
	
	_ce_set_ts (c, ts);
}

void ce_set_unfram (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	ce_board_t *b = c->board;
	unsigned long cfg = c->config & ~TAU32_framing_mode_mask;
	unsigned long i;
	
	if (c->num != 0 || b->cr.pending + 2*32 + 3>= CONFREQSZ)
		return;

	on = on ? 1 : 0;
	
	if (on == c->unfram)
		return;
		
	if (on) {
		ce_set_dir (c, 0);
		for (i = 1; i < TAU32_CHANNELS; i++) {
			ce_set_ts (b->chan + i, 0);
			ce_set_phony (b->chan + i, 0);
		}
		ce_set_use16 (b->chan + 0, 0);
		ce_set_use16 (b->chan + 1, 0);
		/* Get current value, previous ce_set request may change it */
		cfg = c->config & ~TAU32_framing_mode_mask;
		cfg |= TAU32_unframed_2048;
		c->unfram = on;
		_ce_set_ts (b->chan, ~0ul);
		c->config = cfg;
		/* XXXRIK: Do extra checks on config queue size*/
		if (b->ddk.Interfaces) {
			CE_PREREQUEST (b, c, b->cr.queue, req);
			if (!req)
				return;
			req->Command = TAU32_Configure_E1;
			req->Io.InterfaceConfig.Interface = TAU32_E1_B;
			req->Io.InterfaceConfig.Config = TAU32_LineOff;
			req->Io.InterfaceConfig.UnframedTsMask = 0;
			CE_DDK_DEBUG (b, c, ("unfram: B line off\n"));
			ce_cfg_submit (b);
		}
		CE_PREREQUEST (b, c, b->cr.queue, req);
		if (!req)
			return;
		req->Command = TAU32_Configure_E1;
		req->Io.InterfaceConfig.Interface = TAU32_E1_A;
		req->Io.InterfaceConfig.Config = cfg;
		req->Io.InterfaceConfig.UnframedTsMask = c->ts;
		CE_DDK_DEBUG (b, c, ("Submit unfram\n"));
		ce_cfg_submit (b);
	} else {
		cfg |= TAU32_framed_cas_cross;
		CE_PREREQUEST (b, c, b->cr.queue, req);
		if (!req)
			return;
		req->Command = TAU32_Configure_E1;
		req->Io.InterfaceConfig.Interface = TAU32_E1_ALL;
		req->Io.InterfaceConfig.Config = cfg;
		req->Io.InterfaceConfig.UnframedTsMask = 0;
		CE_DDK_DEBUG (b, c, ("Submit framed\n"));
		ce_cfg_submit (b);
		ce_set_ts (c, 0);
	}
	c->unfram = on;
}

void ce_set_phony (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	ce_board_t *b = c->board;
	unsigned long mask = 0;
	int i;
	
	if ((c->phony && on) || (c->phony == 0 && on == 0) ||
	    b->cr.pending >= CONFREQSZ)
		return;
	
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;

	req->Command = TAU32_Configure_Channel;
	req->Io.InterfaceConfig.Config = on ? TAU32_TMA :
		(TAU32_HDLC | TAU32_fr_rx_splitcheck | TAU32_fr_rx_fitcheck);
	req->Io.ChannelNumber = c->num;
	CE_DDK_DEBUG (b, c, ("Submit phony\n"));
	if (!ce_cfg_submit (b)) {
		/* Do some error processing */
		return;
	}

	for (i = 0; i < 32; i++) {
		int t = b->dxc[i] % 32;
		if (b->dxc[i] < (c->dir?64:32) || b->dxc[i] > (c->dir?95:63))
			continue;
		if (c->ts & (1ul << t))
			mask |= (1ul << t);
	}
	
	CE_DDK_DEBUG (b, c, ("phony mask:%lx\n", mask));
	
	if (on) {
		b->pmask |= mask;
	} else {
		b->pmask &= ~mask;
	}

	c->phony = on ? 1 : 0;
	
	CE_DDK_DEBUG (b, c, ("Submit (setcrosmatrix) phony\n"));
	if (!TAU32_SetCrossMatrix(b->ddk.pControllerObject, b->dxc, b->pmask)) {
		/* Do some error processing */
		return;
	}
}

void ce_set_scrambler (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~TAU32_scrambler;
	ce_board_t *b = c->board;

	if (c->num != 0 || c->unfram == 0 || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->scrambler)
		return;

	cfg |= on ? TAU32_scrambler : 0;
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit scrambler\n"));
	if (ce_cfg_submit (b)) {
		c->scrambler = on ? 1 : 0;
		c->config = cfg;
	}
}

void ce_set_monitor (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~TAU32_monitor;
	ce_board_t *b = c->board;

	if (c->num >= b->ddk.Interfaces || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->monitor)
		return;

	cfg |= on ? TAU32_monitor : 0;
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit monitor\n"));
	if (ce_cfg_submit (b)) {
		c->monitor = on ? 1 : 0;
		c->config = cfg;
	}
}

static void _ce_submit_configure_e1 (ce_chan_t *c, char *rname)
{
	TAU32_UserRequest *req;
	ce_board_t *b = c->board;

	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num == 0 ? TAU32_E1_A : TAU32_E1_B;
	req->Io.InterfaceConfig.Config = c->config;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit %s\n", rname ? rname : ""));
	if (!ce_cfg_submit (b)) {
		CE_DDK_DEBUG (b, c, ("Fail to submit %s\n", rname?rname:""));
		/* Do some error processing */
		return;	
	}
}

void ce_set_use16 (ce_chan_t *c, unsigned char on)
{
	ce_board_t *b = c->board;
	ce_chan_t *x;
	unsigned long cfg[2];
	int use[2];

	if (c->num >= b->ddk.Interfaces || b->cr.pending + 2 >= CONFREQSZ)
		return;

	cfg[0] = b->chan[0].config & ~TAU32_framing_mode_mask;
	cfg[1] = b->chan[1].config & ~TAU32_framing_mode_mask;
	
	on = on ? 1 : 0;
	
	if (c->use16 == on || b->chan->unfram)
		return;
		
	use[0] = b->chan[0].use16;
	use[1] = b->chan[1].use16;
	
	/* Correct value */
	use[c->num] = on;

	if (b->ddk.Interfaces == 1) {
		cfg[0] |= on ? TAU32_framed_cas_set : TAU32_framed_no_cas;
	} else {
		if (use[0] == 0 && use[1] == 0) {
			cfg[0] |= TAU32_framed_cas_cross;
			cfg[1] |= TAU32_framed_cas_cross;
		} else if (use[0] == 0) {
			cfg[0] |= TAU32_framed_cas_set;
			cfg[1] |= TAU32_framed_no_cas;
		} else if (use[1] == 0) {
			cfg[0] |= TAU32_framed_no_cas;
			cfg[1] |= TAU32_framed_cas_set;
		} else {
			cfg[0] |= TAU32_framed_no_cas;
			cfg[1] |= TAU32_framed_no_cas;
		}
	}

	c->use16 = on;

	for (x = b->chan; !on && x < b->chan + NCHAN; x++) {
		if (x->dir == c->num && x->ts & (1ul<<16)) {
			ce_set_ts (x, x->ts);
			break;
		}
	}

	if (cfg[0] != b->chan[0].config) {
		b->chan[0].config = cfg[0];
		_ce_submit_configure_e1 (b->chan + 0, "use16");
	}

	if (cfg[1] != b->chan[1].config) {
		b->chan[1].config = cfg[1];
		_ce_submit_configure_e1 (b->chan + 1, "use16");
	}
}

void ce_set_crc4 (ce_chan_t *c, unsigned char on)
{
	TAU32_UserRequest *req;
	unsigned long cfg = c->config & ~TAU32_crc4_mf;
	ce_board_t *b = c->board;

	if (c->num >= b->ddk.Interfaces || b->cr.pending >= CONFREQSZ)
		return;
	on = on ? 1 : 0;
	if (on == c->crc4 || b->chan->unfram)
		return;

	cfg |= on ? TAU32_crc4_mf : 0;
	CE_PREREQUEST (b, c, b->cr.queue, req);
	if (!req)
		return;
	req->Command = TAU32_Configure_E1;
	req->Io.InterfaceConfig.Interface = c->num ? TAU32_E1_B : TAU32_E1_A;
	req->Io.InterfaceConfig.Config = cfg;
	req->Io.InterfaceConfig.UnframedTsMask = c->ts;
	CE_DDK_DEBUG (b, c, ("Submit crc4\n"));
	if (ce_cfg_submit (b)) {
		c->crc4 = on ? 1 : 0;
		c->config = cfg;
	}
}

void ce_set_gsyn (ce_chan_t *c, int syn)
{
	ce_board_t *b = c->board;
	unsigned int mode;

	if (c->num >= b->ddk.Interfaces)
		return;
	
	if (syn == GSYN_RCV)
		syn = c->num ? GSYN_RCV1 : GSYN_RCV0;

	switch (syn) {
	default:	mode = TAU32_SYNC_INTERNAL;	break;
	case GSYN_RCV0:	mode = TAU32_SYNC_RCV_A;	break;
	case GSYN_RCV1:	mode = TAU32_SYNC_RCV_B;	break;
	}

	CE_DDK_DEBUG (b, c, ("Set Sync Mode\n"));
	if (TAU32_SetSyncMode (b->ddk.pControllerObject, mode)) {
		b->chan->gsyn = syn;
		if (b->ddk.Interfaces > 1)
			(b->chan + 1)->gsyn = syn;
	}
}

int ce_get_cable (ce_chan_t *c)
{
	ce_board_t *b = c->board;
	if (c->num >= b->ddk.Interfaces)
		return 0;

	return CABLE_TP;
}

void ce_set_dir (ce_chan_t *c, int dir)
{
	ce_board_t *b = c->board;
	unsigned long ts;
	if (b->cr.pending + 1>= CONFREQSZ || c->dir == dir)
		return;

	ts = c->ts;
	ce_set_ts (c, 0);
	c->dir = dir;
	ce_set_ts (c, ts);
}
