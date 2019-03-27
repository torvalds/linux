/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

#include <sys/param.h>

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif
#include <sys/un.h>

#include <string.h>
#include <termios.h>

#include "layer.h"
#include "defs.h"
#include "log.h"
#include "timer.h"
#include "descriptor.h"
#include "lqr.h"
#include "mbuf.h"
#include "fsm.h"
#include "throughput.h"
#include "hdlc.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "async.h"
#include "physical.h"
#include "proto.h"
#include "cbcp.h"
#include "mp.h"
#include "chat.h"
#include "auth.h"
#include "chap.h"
#include "datalink.h"

void
cbcp_Init(struct cbcp *cbcp, struct physical *p)
{
  cbcp->required = 0;
  cbcp->fsm.state = CBCP_CLOSED;
  cbcp->fsm.id = 0;
  cbcp->fsm.delay = 0;
  *cbcp->fsm.phone = '\0';
  memset(&cbcp->fsm.timer, '\0', sizeof cbcp->fsm.timer);
  cbcp->p = p;
}

static void cbcp_SendReq(struct cbcp *);
static void cbcp_SendResponse(struct cbcp *);
static void cbcp_SendAck(struct cbcp *);

static void
cbcp_Timeout(void *v)
{
  struct cbcp *cbcp = (struct cbcp *)v;

  timer_Stop(&cbcp->fsm.timer);
  if (cbcp->fsm.restart) {
    switch (cbcp->fsm.state) {
      case CBCP_CLOSED:
      case CBCP_STOPPED:
        log_Printf(LogCBCP, "%s: Urk - unexpected CBCP timeout !\n",
                   cbcp->p->dl->name);
        break;

      case CBCP_REQSENT:
        cbcp_SendReq(cbcp);
        break;
      case CBCP_RESPSENT:
        cbcp_SendResponse(cbcp);
        break;
      case CBCP_ACKSENT:
        cbcp_SendAck(cbcp);
        break;
    }
  } else {
    const char *missed;

    switch (cbcp->fsm.state) {
      case CBCP_STOPPED:
        missed = "REQ";
        break;
      case CBCP_REQSENT:
        missed = "RESPONSE";
        break;
      case CBCP_RESPSENT:
        missed = "ACK";
        break;
      case CBCP_ACKSENT:
        missed = "Terminate REQ";
        break;
      default:
        log_Printf(LogCBCP, "%s: Urk - unexpected CBCP timeout !\n",
                   cbcp->p->dl->name);
        missed = NULL;
        break;
    }
    if (missed)
      log_Printf(LogCBCP, "%s: Timeout waiting for peer %s\n",
                 cbcp->p->dl->name, missed);
    datalink_CBCPFailed(cbcp->p->dl);
  }
}

static void
cbcp_StartTimer(struct cbcp *cbcp, int timeout)
{
  timer_Stop(&cbcp->fsm.timer);
  cbcp->fsm.timer.func = cbcp_Timeout;
  cbcp->fsm.timer.name = "cbcp";
  cbcp->fsm.timer.load = timeout * SECTICKS;
  cbcp->fsm.timer.arg = cbcp;
  timer_Start(&cbcp->fsm.timer);
}

#define CBCP_CLOSED	(0)	/* Not in use */
#define CBCP_STOPPED	(1)	/* Waiting for a REQ */
#define CBCP_REQSENT	(2)	/* Waiting for a RESP */
#define CBCP_RESPSENT	(3)	/* Waiting for an ACK */
#define CBCP_ACKSENT	(4)	/* Waiting for an LCP Term REQ */

static const char * const cbcpname[] = {
  "closed", "stopped", "req-sent", "resp-sent", "ack-sent"
};

static const char *
cbcpstate(unsigned s)
{
  if (s < sizeof cbcpname / sizeof cbcpname[0])
    return cbcpname[s];
  return HexStr(s, NULL, 0);
}

static void
cbcp_NewPhase(struct cbcp *cbcp, int new)
{
  if (cbcp->fsm.state != new) {
    log_Printf(LogCBCP, "%s: State change %s --> %s\n", cbcp->p->dl->name,
               cbcpstate(cbcp->fsm.state), cbcpstate(new));
    cbcp->fsm.state = new;
  }
}

struct cbcp_header {
  u_char code;
  u_char id;
  u_int16_t length;	/* Network byte order */
};


/* cbcp_header::code values */
#define CBCP_REQ	(1)
#define CBCP_RESPONSE	(2)
#define CBCP_ACK	(3)

struct cbcp_data {
  u_char type;
  u_char length;
  u_char delay;
  char addr_start[253];	/* max cbcp_data length 255 + 1 for NULL */
};

/* cbcp_data::type values */
#define CBCP_NONUM	(1)
#define CBCP_CLIENTNUM	(2)
#define CBCP_SERVERNUM	(3)
#define CBCP_LISTNUM	(4)

static void
cbcp_Output(struct cbcp *cbcp, u_char code, struct cbcp_data *data)
{
  struct cbcp_header *head;
  struct mbuf *bp;

  bp = m_get(sizeof *head + data->length, MB_CBCPOUT);
  head = (struct cbcp_header *)MBUF_CTOP(bp);
  head->code = code;
  head->id = cbcp->fsm.id;
  head->length = htons(sizeof *head + data->length);
  memcpy(MBUF_CTOP(bp) + sizeof *head, data, data->length);
  log_DumpBp(LogDEBUG, "cbcp_Output", bp);
  link_PushPacket(&cbcp->p->link, bp, cbcp->p->dl->bundle,
                  LINK_QUEUES(&cbcp->p->link) - 1, PROTO_CBCP);
}

static const char *
cbcp_data_Type(unsigned type)
{
  static const char * const types[] = {
    "No callback", "User-spec", "Server-spec", "list"
  };

  if (type < 1 || type > sizeof types / sizeof types[0])
    return HexStr(type, NULL, 0);
  return types[type-1];
}

struct cbcp_addr {
  u_char type;
  char addr[sizeof ((struct cbcp_data *)0)->addr_start - 1];	/* ASCIIZ */
};

/* cbcp_data::type values */
#define CBCP_ADDR_PSTN	(1)

static void
cbcp_data_Show(struct cbcp_data *data)
{
  struct cbcp_addr *addr;
  char *end;

  addr = (struct cbcp_addr *)data->addr_start;
  end = (char *)data + data->length;
  *end = '\0';

  log_Printf(LogCBCP, " TYPE %s\n", cbcp_data_Type(data->type));
  if ((char *)&data->delay < end) {
    log_Printf(LogCBCP, " DELAY %d\n", data->delay);
    while (addr->addr < end) {
      if (addr->type == CBCP_ADDR_PSTN)
        log_Printf(LogCBCP, " ADDR %s\n", addr->addr);
      else
        log_Printf(LogCBCP, " ADDR type %d ??\n", (int)addr->type);
      addr = (struct cbcp_addr *)(addr->addr + strlen(addr->addr) + 1);
    }
  }
}

static void
cbcp_SendReq(struct cbcp *cbcp)
{
  struct cbcp_data data;
  struct cbcp_addr *addr;
  char list[sizeof cbcp->fsm.phone], *next;
  int len, max;

  /* Only callees send REQs */

  log_Printf(LogCBCP, "%s: SendReq(%d) state = %s\n", cbcp->p->dl->name,
             cbcp->fsm.id, cbcpstate(cbcp->fsm.state));
  data.type = cbcp->fsm.type;
  data.delay = 0;
  strncpy(list, cbcp->fsm.phone, sizeof list - 1);
  list[sizeof list - 1] = '\0';

  switch (data.type) {
    case CBCP_CLIENTNUM:
      addr = (struct cbcp_addr *)data.addr_start;
      addr->type = CBCP_ADDR_PSTN;
      *addr->addr = '\0';
      data.length = addr->addr - (char *)&data;
      break;

    case CBCP_LISTNUM:
      addr = (struct cbcp_addr *)data.addr_start;
      for (next = strtok(list, ","); next; next = strtok(NULL, ",")) {
        len = strlen(next);
        max = data.addr_start + sizeof data.addr_start - addr->addr - 1;
        if (len <= max) {
          addr->type = CBCP_ADDR_PSTN;
          strncpy(addr->addr, next, sizeof addr->addr - 1);
          addr->addr[sizeof addr->addr - 1] = '\0';
          addr = (struct cbcp_addr *)((char *)addr + len + 2);
        } else
          log_Printf(LogWARN, "CBCP ADDR \"%s\" skipped - packet too large\n",
                     next);
      }
      data.length = (char *)addr - (char *)&data;
      break;

    case CBCP_SERVERNUM:
      data.length = data.addr_start - (char *)&data;
      break;

    default:
      data.length = (char *)&data.delay - (char *)&data;
      break;
  }

  cbcp_data_Show(&data);
  cbcp_Output(cbcp, CBCP_REQ, &data);
  cbcp->fsm.restart--;
  cbcp_StartTimer(cbcp, cbcp->fsm.delay);
  cbcp_NewPhase(cbcp, CBCP_REQSENT);		/* Wait for a RESPONSE */
}

void
cbcp_Up(struct cbcp *cbcp)
{
  struct lcp *lcp = &cbcp->p->link.lcp;

  cbcp->fsm.delay = cbcp->p->dl->cfg.cbcp.delay;
  if (*cbcp->p->dl->peer.authname == '\0' ||
      !auth_SetPhoneList(cbcp->p->dl->peer.authname, cbcp->fsm.phone,
                         sizeof cbcp->fsm.phone)) {
    strncpy(cbcp->fsm.phone, cbcp->p->dl->cfg.cbcp.phone,
            sizeof cbcp->fsm.phone - 1);
    cbcp->fsm.phone[sizeof cbcp->fsm.phone - 1] = '\0';
  }

  if (lcp->want_callback.opmask) {
    if (*cbcp->fsm.phone == '\0')
      cbcp->fsm.type = CBCP_NONUM;
    else if (!strcmp(cbcp->fsm.phone, "*")) {
      cbcp->fsm.type = CBCP_SERVERNUM;
      *cbcp->fsm.phone = '\0';
    } else
      cbcp->fsm.type = CBCP_CLIENTNUM;
    cbcp_NewPhase(cbcp, CBCP_STOPPED);		/* Wait for a REQ */
    cbcp_StartTimer(cbcp, cbcp->fsm.delay * DEF_FSMTRIES);
  } else {
    if (*cbcp->fsm.phone == '\0')
      cbcp->fsm.type = CBCP_NONUM;
    else if (!strcmp(cbcp->fsm.phone, "*")) {
      cbcp->fsm.type = CBCP_CLIENTNUM;
      *cbcp->fsm.phone = '\0';
    } else if (strchr(cbcp->fsm.phone, ','))
      cbcp->fsm.type = CBCP_LISTNUM;
    else
      cbcp->fsm.type = CBCP_SERVERNUM;
    cbcp->fsm.restart = DEF_FSMTRIES;
    cbcp_SendReq(cbcp);
  }
}

static int
cbcp_AdjustResponse(struct cbcp *cbcp, struct cbcp_data *data)
{
  /*
   * We've received a REQ (data).  Adjust our response (cbcp->fsm.*)
   * so that we (hopefully) agree with the peer
   */
  struct cbcp_addr *addr;

  switch (data->type) {
    case CBCP_NONUM:
      if (cbcp->p->dl->cfg.callback.opmask & CALLBACK_BIT(CALLBACK_NONE))
        /*
         * if ``none'' is a configured callback possibility
         * (ie, ``set callback cbcp none''), go along with the callees
         * request
         */
        cbcp->fsm.type = CBCP_NONUM;

      /*
       * Otherwise, we send our desired response anyway.  This seems to be
       * what Win95 does - although I can't find this behaviour documented
       * in the CBCP spec....
       */

      return 1;

    case CBCP_CLIENTNUM:
      if (cbcp->fsm.type == CBCP_CLIENTNUM) {
        char *ptr;

        if (data->length > data->addr_start - (char *)data) {
          /*
           * The peer has given us an address type spec - make sure we
           * understand !
           */
          addr = (struct cbcp_addr *)data->addr_start;
          if (addr->type != CBCP_ADDR_PSTN) {
            log_Printf(LogPHASE, "CBCP: Unrecognised address type %d !\n",
                       (int)addr->type);
            return 0;
          }
        }
        /* we accept the REQ even if the peer didn't specify an addr->type */
        ptr = strchr(cbcp->fsm.phone, ',');
        if (ptr)
          *ptr = '\0';		/* Just use the first number in our list */
        return 1;
      }
      log_Printf(LogPHASE, "CBCP: no number to pass to the peer !\n");
      return 0;

    case CBCP_SERVERNUM:
      if (cbcp->fsm.type == CBCP_SERVERNUM) {
        *cbcp->fsm.phone = '\0';
        return 1;
      }
      if (data->length > data->addr_start - (char *)data) {
        /*
         * This violates the spec, but if the peer has told us the
         * number it wants to call back, take advantage of this fact
         * and allow things to proceed if we've specified the same
         * number
         */
        addr = (struct cbcp_addr *)data->addr_start;
        if (addr->type != CBCP_ADDR_PSTN) {
          log_Printf(LogPHASE, "CBCP: Unrecognised address type %d !\n",
                     (int)addr->type);
          return 0;
        } else if (cbcp->fsm.type == CBCP_CLIENTNUM) {
          /*
           * If the peer's insisting on deciding the number, make sure
           * it's one of the ones in our list.  If it is, let the peer
           * think it's in control :-)
           */
          char list[sizeof cbcp->fsm.phone], *next;

          strncpy(list, cbcp->fsm.phone, sizeof list - 1);
          list[sizeof list - 1] = '\0';
          for (next = strtok(list, ","); next; next = strtok(NULL, ","))
            if (!strcmp(next, addr->addr)) {
              cbcp->fsm.type = CBCP_SERVERNUM;
              strcpy(cbcp->fsm.phone, next);
              return 1;
            }
        }
      }
      log_Printf(LogPHASE, "CBCP: Peer won't allow local decision !\n");
      return 0;

    case CBCP_LISTNUM:
      if (cbcp->fsm.type == CBCP_CLIENTNUM || cbcp->fsm.type == CBCP_LISTNUM) {
        /*
         * Search through ``data''s addresses and see if cbcp->fsm.phone
         * contains any of them
         */
        char list[sizeof cbcp->fsm.phone], *next, *end;

        addr = (struct cbcp_addr *)data->addr_start;
        end = (char *)data + data->length;

        while (addr->addr < end) {
          if (addr->type == CBCP_ADDR_PSTN) {
            strncpy(list, cbcp->fsm.phone, sizeof list - 1);
            list[sizeof list - 1] = '\0';
            for (next = strtok(list, ","); next; next = strtok(NULL, ","))
              if (!strcmp(next, addr->addr)) {
                cbcp->fsm.type = CBCP_LISTNUM;
                strcpy(cbcp->fsm.phone, next);
                return 1;
              }
          } else
            log_Printf(LogCBCP, "Warning: Unrecognised address type %d !\n",
                       (int)addr->type);
          addr = (struct cbcp_addr *)(addr->addr + strlen(addr->addr) + 1);
        }
      }
      log_Printf(LogPHASE, "CBCP: no good number to pass to the peer !\n");
      return 0;
  }

  log_Printf(LogCBCP, "Unrecognised REQ type %d !\n", (int)data->type);
  return 0;
}

static void
cbcp_SendResponse(struct cbcp *cbcp)
{
  struct cbcp_data data;
  struct cbcp_addr *addr;

  /* Only callers send RESPONSEs */

  log_Printf(LogCBCP, "%s: SendResponse(%d) state = %s\n", cbcp->p->dl->name,
             cbcp->fsm.id, cbcpstate(cbcp->fsm.state));

  data.type = cbcp->fsm.type;
  data.delay = cbcp->fsm.delay;
  addr = (struct cbcp_addr *)data.addr_start;
  if (data.type == CBCP_NONUM)
    data.length = (char *)&data.delay - (char *)&data;
  else if (*cbcp->fsm.phone) {
    addr->type = CBCP_ADDR_PSTN;
    strncpy(addr->addr, cbcp->fsm.phone, sizeof addr->addr - 1);
    addr->addr[sizeof addr->addr - 1] = '\0';
    data.length = (addr->addr + strlen(addr->addr) + 1) - (char *)&data;
  } else
    data.length = data.addr_start - (char *)&data;

  cbcp_data_Show(&data);
  cbcp_Output(cbcp, CBCP_RESPONSE, &data);
  cbcp->fsm.restart--;
  cbcp_StartTimer(cbcp, cbcp->fsm.delay);
  cbcp_NewPhase(cbcp, CBCP_RESPSENT);	/* Wait for an ACK */
}

/* What to do after checking an incoming response */
#define CBCP_ACTION_DOWN (0)
#define CBCP_ACTION_REQ (1)
#define CBCP_ACTION_ACK (2)

static int
cbcp_CheckResponse(struct cbcp *cbcp, struct cbcp_data *data)
{
  /*
   * We've received a RESPONSE (data).  Check if it agrees with
   * our REQ (cbcp->fsm)
   */
  struct cbcp_addr *addr;

  addr = (struct cbcp_addr *)data->addr_start;

  if (data->type == cbcp->fsm.type) {
    switch (cbcp->fsm.type) {
      case CBCP_NONUM:
        return CBCP_ACTION_ACK;

      case CBCP_CLIENTNUM:
        if ((char *)data + data->length <= addr->addr)
          log_Printf(LogPHASE, "CBCP: peer didn't respond with a number !\n");
        else if (addr->type != CBCP_ADDR_PSTN)
          log_Printf(LogPHASE, "CBCP: Unrecognised address type %d !\n",
                     addr->type);
        else {
          strncpy(cbcp->fsm.phone, addr->addr, sizeof cbcp->fsm.phone - 1);
          cbcp->fsm.phone[sizeof cbcp->fsm.phone - 1] = '\0';
          cbcp->fsm.delay = data->delay;
          return CBCP_ACTION_ACK;
        }
        return CBCP_ACTION_DOWN;

      case CBCP_SERVERNUM:
        cbcp->fsm.delay = data->delay;
        return CBCP_ACTION_ACK;

      case CBCP_LISTNUM:
        if ((char *)data + data->length <= addr->addr)
          log_Printf(LogPHASE, "CBCP: peer didn't respond with a number !\n");
        else if (addr->type != CBCP_ADDR_PSTN)
          log_Printf(LogPHASE, "CBCP: Unrecognised address type %d !\n",
                     addr->type);
        else {
          char list[sizeof cbcp->fsm.phone], *next;

          strncpy(list, cbcp->fsm.phone, sizeof list - 1);
          list[sizeof list - 1] = '\0';
          for (next = strtok(list, ","); next; next = strtok(NULL, ","))
            if (!strcmp(addr->addr, next)) {
              strcpy(cbcp->fsm.phone, next);
              cbcp->fsm.delay = data->delay;
              return CBCP_ACTION_ACK;
            }
          log_Printf(LogPHASE, "CBCP: peer didn't respond with a "
                     "valid number !\n");
        }
        return CBCP_ACTION_DOWN;
    }
    log_Printf(LogPHASE, "Internal CBCP error - agreed on %d !\n",
               (int)cbcp->fsm.type);
    return CBCP_ACTION_DOWN;
  } else if (data->type == CBCP_NONUM && cbcp->fsm.type == CBCP_CLIENTNUM) {
    /*
     * Client doesn't want CBCP after all....
     * We only allow this when ``set cbcp *'' has been specified.
     */
    cbcp->fsm.type = CBCP_NONUM;
    return CBCP_ACTION_ACK;
  }
  log_Printf(LogCBCP, "Invalid peer RESPONSE\n");
  return CBCP_ACTION_REQ;
}

static void
cbcp_SendAck(struct cbcp *cbcp)
{
  struct cbcp_data data;
  struct cbcp_addr *addr;

  /* Only callees send ACKs */

  log_Printf(LogCBCP, "%s: SendAck(%d) state = %s\n", cbcp->p->dl->name,
             cbcp->fsm.id, cbcpstate(cbcp->fsm.state));

  data.type = cbcp->fsm.type;
  switch (data.type) {
    case CBCP_NONUM:
      data.length = (char *)&data.delay - (char *)&data;
      break;
    case CBCP_CLIENTNUM:
      addr = (struct cbcp_addr *)data.addr_start;
      addr->type = CBCP_ADDR_PSTN;
      strncpy(addr->addr, cbcp->fsm.phone, sizeof addr->addr - 1);
      addr->addr[sizeof addr->addr - 1] = '\0';
      data.delay = cbcp->fsm.delay;
      data.length = addr->addr + strlen(addr->addr) + 1 - (char *)&data;
      break;
    default:
      data.delay = cbcp->fsm.delay;
      data.length = data.addr_start - (char *)&data;
      break;
  }

  cbcp_data_Show(&data);
  cbcp_Output(cbcp, CBCP_ACK, &data);
  cbcp->fsm.restart--;
  cbcp_StartTimer(cbcp, cbcp->fsm.delay);
  cbcp_NewPhase(cbcp, CBCP_ACKSENT);	/* Wait for an ACK */
}

extern struct mbuf *
cbcp_Input(struct bundle *bundle __unused, struct link *l, struct mbuf *bp)
{
  struct physical *p = link2physical(l);
  struct cbcp_header *head;
  struct cbcp_data *data;
  struct cbcp *cbcp = &p->dl->cbcp;
  size_t len;

  if (p == NULL) {
    log_Printf(LogERROR, "cbcp_Input: Not a physical link - dropped\n");
    m_freem(bp);
    return NULL;
  }

  bp = m_pullup(bp);
  len = m_length(bp);
  if (len < sizeof(struct cbcp_header)) {
    m_freem(bp);
    return NULL;
  }
  head = (struct cbcp_header *)MBUF_CTOP(bp);
  if (ntohs(head->length) != len) {
    log_Printf(LogWARN, "Corrupt CBCP packet (code %d, length %u not %zu)"
               " - ignored\n", head->code, ntohs(head->length), len);
    m_freem(bp);
    return NULL;
  }
  m_settype(bp, MB_CBCPIN);

  /* XXX check the id */

  bp->m_offset += sizeof(struct cbcp_header);
  bp->m_len -= sizeof(struct cbcp_header);
  data = (struct cbcp_data *)MBUF_CTOP(bp);

  switch (head->code) {
    case CBCP_REQ:
      log_Printf(LogCBCP, "%s: RecvReq(%d) state = %s\n",
                 p->dl->name, head->id, cbcpstate(cbcp->fsm.state));
      cbcp_data_Show(data);
      if (cbcp->fsm.state == CBCP_STOPPED || cbcp->fsm.state == CBCP_RESPSENT) {
        timer_Stop(&cbcp->fsm.timer);
        if (cbcp_AdjustResponse(cbcp, data)) {
          cbcp->fsm.restart = DEF_FSMTRIES;
          cbcp->fsm.id = head->id;
          cbcp_SendResponse(cbcp);
        } else
          datalink_CBCPFailed(cbcp->p->dl);
      } else
        log_Printf(LogCBCP, "%s: unexpected REQ dropped\n", p->dl->name);
      break;

    case CBCP_RESPONSE:
      log_Printf(LogCBCP, "%s: RecvResponse(%d) state = %s\n",
	         p->dl->name, head->id, cbcpstate(cbcp->fsm.state));
      cbcp_data_Show(data);
      if (cbcp->fsm.id != head->id) {
        log_Printf(LogCBCP, "Warning: Expected id was %d, not %d\n",
                   cbcp->fsm.id, head->id);
        cbcp->fsm.id = head->id;
      }
      if (cbcp->fsm.state == CBCP_REQSENT || cbcp->fsm.state == CBCP_ACKSENT) {
        timer_Stop(&cbcp->fsm.timer);
        switch (cbcp_CheckResponse(cbcp, data)) {
          case CBCP_ACTION_REQ:
            cbcp_SendReq(cbcp);
            break;

          case CBCP_ACTION_ACK:
            cbcp->fsm.restart = DEF_FSMTRIES;
            cbcp_SendAck(cbcp);
            if (cbcp->fsm.type == CBCP_NONUM) {
              /*
               * Don't change state in case the peer doesn't get our ACK,
               * just bring the layer up.
               */
              timer_Stop(&cbcp->fsm.timer);
              datalink_NCPUp(cbcp->p->dl);
            }
            break;

          default:
            datalink_CBCPFailed(cbcp->p->dl);
            break;
        }
      } else
        log_Printf(LogCBCP, "%s: unexpected RESPONSE dropped\n", p->dl->name);
      break;

    case CBCP_ACK:
      log_Printf(LogCBCP, "%s: RecvAck(%d) state = %s\n",
	         p->dl->name, head->id, cbcpstate(cbcp->fsm.state));
      cbcp_data_Show(data);
      if (cbcp->fsm.id != head->id) {
        log_Printf(LogCBCP, "Warning: Expected id was %d, not %d\n",
                   cbcp->fsm.id, head->id);
        cbcp->fsm.id = head->id;
      }
      if (cbcp->fsm.type == CBCP_NONUM) {
        /*
         * Don't change state in case the peer doesn't get our ACK,
         * just bring the layer up.
         */
        timer_Stop(&cbcp->fsm.timer);
        datalink_NCPUp(cbcp->p->dl);
      } else if (cbcp->fsm.state == CBCP_RESPSENT) {
        timer_Stop(&cbcp->fsm.timer);
        datalink_CBCPComplete(cbcp->p->dl);
        log_Printf(LogPHASE, "%s: CBCP: Peer will dial back\n", p->dl->name);
      } else
        log_Printf(LogCBCP, "%s: unexpected ACK dropped\n", p->dl->name);
      break;

    default:
      log_Printf(LogWARN, "Unrecognised CBCP packet (code %d, length %zd)\n",
               head->code, len);
      break;
  }

  m_freem(bp);
  return NULL;
}

void
cbcp_Down(struct cbcp *cbcp)
{
  timer_Stop(&cbcp->fsm.timer);
  cbcp_NewPhase(cbcp, CBCP_CLOSED);
  cbcp->required = 0;
}

void
cbcp_ReceiveTerminateReq(struct physical *p)
{
  if (p->dl->cbcp.fsm.state == CBCP_ACKSENT) {
    /* Don't change our state in case the peer doesn't get the ACK */
    p->dl->cbcp.required = 1;
    log_Printf(LogPHASE, "%s: CBCP: Will dial back on %s\n", p->dl->name,
               p->dl->cbcp.fsm.phone);
  } else
    cbcp_NewPhase(&p->dl->cbcp, CBCP_CLOSED);
}
