#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/poll.h>
#include <sys/socket.h>
//#include <net/if.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <err.h>

/* XXX this lives in net/if.h, but you can't include that with
 * linux/netlink.h... */
char *if_indextoname(unsigned ifindex, char *ifname);

#include "nrx_priv.h"

#ifndef IFLA_WIRELESS
#define IFLA_WIRELESS 11
#endif

static void
we_dispatch(nrx_context ctx, uint16_t cmd, void *buf, size_t len)
{
   struct nrx_we_event *ev;

   struct nrx_we_callback_data xx;
   xx.cmd = cmd;
   xx.data = buf;
   xx.len = len;
   
   TAILQ_FOREACH(ev, &ctx->we_handlers, next) {
      if(ev->cmd == 0 || ev->cmd == cmd)
         (*ev->handler)(ctx, NRX_CB_TRIGGER, &xx, sizeof(xx), ev->user_data);
   }
}

static struct nrx_we_event*
find_we_by_userdata(nrx_context ctx, void *user_data)
{
   struct nrx_we_event *ev;
   
   TAILQ_FOREACH(ev, &ctx->we_handlers, next) {
      if(ev->user_data == user_data)
         return ev;
   }
   return NULL;
}

/*!
 * @internal
 * @brief <b>Registers a handler for Wireless Extensions events</b>
 *
 * @param ctx The context.
 * @param cmd The WE event code to match. Zero matches all events.
 * @param handler The callback function to call when an event is received.
 * @param user_data Pointer to additional data passed to the callback
 *
 * @retval Zero on memory allocation failure.
 * @retval Non-zero integer representing the event handler.
 */
nrx_callback_handle
nrx_register_we_event_handler(nrx_context ctx, 
                              uint16_t cmd,
                              nrx_callback_t handler,
                              void *user_data)
{
   struct nrx_we_event *ev;

   nrx_event_connection_number(ctx); /* XXX create socket to receive
                                        netlink packets */
   
   ev = malloc(sizeof(*ev));
   if(ev != NULL) {
      ev->cmd = cmd;
      ev->handler = handler;
      ev->user_data = user_data;
      TAILQ_INSERT_TAIL(&ctx->we_handlers, ev, next);
   }
   return (nrx_callback_handle)ev;
}

/*!
 * @internal
 * @brief <b>Cancels a registered handler for Wireless Extensions events</b>
 *
 * @param ctx The context.
 * @param handle A handle previously obtained from
 * nrx_register_we_event_handler.
 *
 * @retval Zero on success.
 * @retval EINVAL if a callback matching handle was not found.
 */
int
nrx_cancel_we_event_handler(nrx_context ctx, 
                            nrx_callback_handle handle)
{
   struct nrx_we_event *ev;
   ev = TAILQ_FIRST(&ctx->we_handlers);
   while(ev != NULL) {
      if(ev == (struct nrx_we_event*)handle) {
         TAILQ_REMOVE(&ctx->we_handlers, ev, next);
         (*ev->handler)(ctx, NRX_CB_CANCEL, NULL, 0, ev->user_data);
         free(ev);
         return 0;
      }
      ev = TAILQ_NEXT(ev, next);
   }
   return EINVAL;
}

static int
rtnetlink_event(nrx_context ctx, 
                int operation, 
                void *event_data,
                size_t event_data_size,
                void *user_data)
{
   struct ifinfomsg *ifi;
   size_t rta_len;
   struct rtattr *rta;
   struct nlmsghdr *nh = event_data;

   if(operation == NRX_CB_CANCEL)
      return 0;

   ifi = NLMSG_DATA(nh);
   if(nh->nlmsg_len < NLMSG_ALIGN(sizeof(*ifi))) {
      return EINVAL;
   }
   rta = (void*)(char *) ifi + NLMSG_ALIGN(sizeof(*ifi));
   rta_len = nh->nlmsg_len - NLMSG_ALIGN(sizeof(*ifi));
   while (RTA_OK(rta, rta_len)) {
#if 0
      char ifname[IFNAMSIZ];
      if_indextoname(ifi->ifi_index, ifname);
      printf("rtype = %d, fa = %d, t = %d, i = %d (%s), fl = %x, ch = %x\n", 
             rta->rta_type,
             ifi->ifi_family, 
             ifi->ifi_type, 
             ifi->ifi_index, ifname, 
             ifi->ifi_flags, 
             ifi->ifi_change);
#endif
      /* Check if the Wireless kind */
      if(rta->rta_type == IFLA_WIRELESS) {
         struct iw_event *ev = RTA_DATA(rta);
         if(RTA_PAYLOAD(rta) < IW_EV_LCP_LEN)
            errx(1, "foo %u > %u", sizeof(*ev), RTA_PAYLOAD(rta));
         we_dispatch(ctx, ev->cmd, &ev->u, RTA_PAYLOAD(rta) - IW_EV_LCP_LEN);
         //we_dispatch(ctx, ev->cmd, &ev->u, ev->len - IW_EV_LCP_LEN);
      }
      rta = RTA_NEXT(rta, rta_len);
   }
   return 0;
}

/*!
 * @internal
 * @brief <b>Registers a handler for Netlink messages</b>
 *
 * @param ctx       The context.
 * @param type      Matches the type field of the netlink message. 
 *                  Zero matches any type.
 * @param pid       Matches the pid field of the netlink message.
 *                  Zero matches any pid.
 * @param seq       Matches the seq field of the netlink message.
 *                  Zero matches any seq.
 * @param handler   The callback function to call when a matching 
 *                  message is received.
 * @param user_data Pointer to additional data passed to the callback
 *
 * @return An integer representing the message handler.
 */
nrx_callback_handle
nrx_register_netlink_handler(nrx_context ctx, 
                             uint16_t type, 
                             uint32_t pid, 
                             uint32_t seq,
                             nrx_callback_t handler,
                             void *user_data)
{
   struct nrx_netlink_event *ev = malloc(sizeof(*ev));
   if(ev != NULL) {
      ev->type = type;
      ev->pid = pid;
      ev->seq = seq;
      ev->handler = handler;
      ev->user_data = user_data;
      TAILQ_INSERT_TAIL(&ctx->netlink_handlers, ev, next);
   }
   return (nrx_callback_handle)ev;
}

/*!
 * @internal
 * @brief <b>Cancels a registered handler for Netlink messages</b>
 *
 * @param ctx The context.
 * @param handle A handle previously obtained from
 * nrx_register_netlink_handler.
 *
 * @return Nothing.
 */
void
nrx_cancel_netlink_handler(nrx_context ctx, 
                           nrx_callback_handle handler)
{
   struct nrx_netlink_event *ev;
   ev = TAILQ_FIRST(&ctx->netlink_handlers);
   while(ev != NULL) {
      if(ev == (struct nrx_netlink_event*)handler) {
         TAILQ_REMOVE(&ctx->netlink_handlers, ev, next);
         (*ev->handler)(ctx, NRX_CB_CANCEL, NULL, 0, ev->user_data);
         free(ev);
         return;
      }
      ev = TAILQ_NEXT(ev, next);
   }
}


static int
netlink_dispatch(nrx_context ctx, 
                 struct nlmsghdr *nh)
{
   struct nrx_netlink_event *ev;
   int handled = 0;
   for(ev = TAILQ_FIRST(&ctx->netlink_handlers); 
       ev != NULL; 
       ev = TAILQ_NEXT(ev, next)) {
#define MATCH(EV, NH, F) ((EV)->F == 0 || (EV)->F == (NH)->nlmsg_##F)
      if(MATCH(ev, nh, type) && MATCH(ev, nh, pid) && MATCH(ev, nh, seq)) {
#undef MATCH
         if((*ev->handler)(ctx, NRX_CB_TRIGGER, 
                           nh, sizeof(*nh), 
                           ev->user_data) == 0)
            handled = 1;
      }
   }
   if(handled)
      return 0;
   return ENOENT;
}

void
_nrx_netlink_init(nrx_context ctx)
{
   ctx->netlink_sock = -1;
   TAILQ_INIT(&ctx->netlink_handlers);
   TAILQ_INIT(&ctx->we_handlers);

   nrx_register_netlink_handler(ctx, RTM_NEWLINK, 0, 0, 
                                rtnetlink_event, NULL);
   nrx_register_netlink_handler(ctx, RTM_DELLINK, 0, 0, 
                                rtnetlink_event, NULL);
}

void
_nrx_netlink_free(nrx_context ctx)
{
   struct nrx_we_event *ev;
   struct nrx_netlink_event *nlh;
   if(ctx->netlink_sock >= 0) {
      close(ctx->netlink_sock);
      ctx->netlink_sock = -1;
   }
   while((nlh = TAILQ_FIRST(&ctx->netlink_handlers)) != NULL) {
      nrx_cancel_netlink_handler(ctx, (nrx_callback_handle)nlh);
   }
   while((ev = TAILQ_FIRST(&ctx->we_handlers)) != NULL) {
      nrx_cancel_we_event_handler(ctx, (nrx_callback_handle)ev);
   }
}

static int
netlink_event(nrx_context ctx, void *buf, size_t len)
{
   struct nlmsghdr *nh;

   for(nh = buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
#if 0
      printf("%s: type = %d, pid = %d, seq = %d\n", 
             __func__,
             nh->nlmsg_type, 
             nh->nlmsg_pid, 
             nh->nlmsg_seq);
#endif
      netlink_dispatch(ctx, nh);
   }
   return 0;
}


/*!
 * @internal
 * @brief <b>Create a socket suitable for netlink access</b>
 *
 * @retval non-negative a socket
 * @retval negative a negative errno number
 *
 * @note This functions does not follow the convention of taking a
 * context, and returning zero or errno number.
 */
int
_nrx_create_netlink_socket(void)
{
   int sock;
   struct sockaddr_nl snl;

   sock = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
   if(sock < 0)
      return -errno;

   memset(&snl, 0, sizeof(snl));
   snl.nl_family = AF_NETLINK;
   snl.nl_groups = RTMGRP_LINK;

   if(bind(sock, (struct sockaddr*)&snl, sizeof(snl)) < 0) {
      close(sock);
      return -errno;
   }

   return sock;
}

/*!
 * @ingroup LIB
 * @brief <b>Returns the connection number used for event notification</b>
 *
 * @param ctx The context.
 *
 * @return The connection number, or a negative number on error.
 *
 * @note This functions does not follow the convention of 
 * returning zero or errno number.
 */
int
nrx_event_connection_number(nrx_context ctx)
{
   NRX_ASSERT(ctx);
   if(ctx->netlink_sock < 0)
      ctx->netlink_sock = _nrx_create_netlink_socket();

   return ctx->netlink_sock;   /* _nrx_create_netlink_socket returns
                                  negative errno numbers on failure */
}

/*!
 * @ingroup LIB
 * @brief <b>Waits for an event to arrive</b>
 *
 * @param ctx The context.
 * @param timeout The time to wait (in ms) for an event to occur,
 *                INFTIM (-1) means wait forever. If the value of
 *                timeout is 0, it shall return immediately. Should an
 *                event occur the function will return immediately.
 *                The timeout may be rounded upwards due to system
 *                limitations. Maximum value should be 2^31-1, but may
 *                be subjected to system limitations in the
 *                implementation of poll(2).
 *
 * @retval Zero         Indicates that there is an event to process.
 * @retval EWOULDBLOCK  Indicates that the wait timeout was exceeded.
 * @retval EPIPE        Poll indicates an abnormal condition. This happens
 *                      for instance if the context is destroyed.
 * @retval other        May return error numbers from socket(2), 
 *                      bind(2), or poll(2).
 */
int
nrx_wait_event(nrx_context ctx, int timeout)
{
   struct pollfd pfd[2];
   NRX_ASSERT(ctx);
   
   if((pfd[0].fd = nrx_event_connection_number(ctx)) < 0)
      return -pfd[0].fd; /* see nrx_event_connection_number */
   
   if((pfd[1].fd = ctx->sock) < 0)
      return EPIPE; /* see nrx_event_connection_number */
   
   pfd[0].events = POLLIN;
   pfd[1].events = 0;
   
  again:
   if(poll(pfd, 2, timeout) < 0) {
      if (errno == EINTR)       /* Ctrl-Z + fg */
         goto again;
      return errno;
   }
   
   if(pfd[0].revents & POLLIN)
      return 0;

   if(pfd[0].revents & (POLLERR|POLLHUP|POLLNVAL))
      return EPIPE;

   if(pfd[1].revents & (POLLERR|POLLHUP|POLLNVAL))
      return EPIPE;

   return EWOULDBLOCK;
}

/*!
 * @ingroup LIB
 * @brief <b>Retrieves and processes a pending event</b>
 *
 * @param ctx The context.
 *
 * @retval Zero         On success.
 * @retval other        May return error numbers from socket(2), 
 *                      bind(2) or recvfrom(2).
 */
int
nrx_next_event(nrx_context ctx)
{
   int sock;
   char buf[1024];
   ssize_t n;
   NRX_ASSERT(ctx);

   if((sock = nrx_event_connection_number(ctx)) < 0)
      return errno;
   
   n = recvfrom(sock, buf, sizeof(buf), 0, NULL, 0);
   if(n < 0)
      return errno;
   if(n == 0)
      return 0;
   return netlink_event(ctx, buf, n);
}



/***************************/
/*                         */
/*  Netlink custom events  */
/*                         */
/***************************/

struct nrx_custom_event_t {
   char type[32];
   uint32_t id;
   nrx_callback_t handler;
   void *user_data;
   nrx_callback_handle ev_handler;
};

static int CHAR2HEX(char x)
{
   int ret = -1;
   if (x>='0' && x<='9')
      ret = x-'0';
   else if (x>='A' && x<='F') 
      ret = x-'A'+10; 
   else if (x>='a' && x<='f') 
      ret = x-'a'+10; 

   return ret;
}

#define hexlen(x) strspn((x), "0123456789abcdefABCDEF")

/*!  
 * @internal
 * @brief Convert hex string in str into buffer in buf. 
 */
ssize_t nrx_string_to_binary(const char *str, void *buf, size_t size)
{
   int i = 0;
   uint8_t *vect = buf;
   if ((hexlen(str) % 2) != 0) {
      return -EINVAL;
   }
   if ((hexlen(str) / 2) > size) {
      return -E2BIG;
   }
   while (CHAR2HEX(*str) >= 0) {
      vect[i]  = CHAR2HEX(*str++) << 4;
      vect[i] += CHAR2HEX(*str++);
      i++;
   }
   
   return i;
}

static char *
split_token(char **buf, const char *separator, int skip_whitespace)
{
   char *s = *buf;
   char *q;

   /* skip leading whitespace */
   if(skip_whitespace) {
      while(isspace(*s)) 
	 s++;
   }
   /* end-of-string -> return NULL */
   if(*s == '\0')
      return NULL;
   /* find end of token */
   q = s + strcspn(s, separator);
   if(*q == '\0')
      *buf = q;
   else
      /* found new token, skip separator */
      *buf = q + 1;
   
   if(skip_whitespace) {
      while(q > s && isspace(q[-1]))
	 q--;
   }
   *q = '\0';

   return s;
}

static int
nrx_handle_custom_event(nrx_context ctx, 
                        int operation,
                        void *event_data, 
                        size_t event_data_size,
                        void *user_data)
{
   /* Structure of event data vector is partially based on *
    * iw_point with additional text buffer.                *
    * Format: [ length, flags, text message ]              */
   struct iw_point point;
   char msg[IW_CUSTOM_MAX + 1];
   struct nrx_custom_event_t *priv = user_data;
   struct nrx_we_callback_data *d = event_data;
   char *p;
   char *token, *ntoken;
   struct nrx_we_custom_data cdata;
   int hlen;
   int cancel = 0;
   
   if(operation == NRX_CB_CANCEL) {
      (*priv->handler)(ctx, NRX_CB_CANCEL, NULL, 0, priv->user_data);
      free(priv);
      return 0;
   }
   p = d->data;
   
   if(nrx_check_wx_version(ctx, 19) < 0)
      p += sizeof(void*); /* includes a pointer also */
   /* Parse length & flags */
   memcpy(&point.length, p, sizeof(point.length));
   p += sizeof(point.length);
   memcpy(&point.flags, p, sizeof(point.flags));
   p += sizeof(point.flags);

   hlen = p - (char*)d->data;

   /* Check size */
   if (d->len != point.length + hlen || point.length > IW_CUSTOM_MAX) {
      ERROR("Sizes don't agree!! %u != %u + %u\n", d->len, point.length, hlen);
      return 0;
   }

   /* Process text message */
   memcpy(msg, p, point.length);
   msg[point.length] = '\0';

   ntoken = msg;
   token = split_token(&ntoken, ",()", 1);

   if(strcmp(token, "NRX") != 0)
      return 0;

   token = split_token(&ntoken, ",()", 1);

   if (!strcmp("CANCEL", token)) {
      cancel = 1;
      token = split_token(&ntoken, ",()", 1);
   }

   if(*priv->type != '\0' &&
      strcmp(priv->type, token) != 0)
      return 0;
   
   cdata.nvar = 0;
   while((token = split_token(&ntoken, ",()", 1)) != NULL) {
      char *var, *val;
      var = split_token(&token, "=", 1);
      val = split_token(&token, "=", 1);
      if(strcmp(var, "id") == 0) {
         if(priv->id != 0 && 
            val != NULL && 
            strtoul(val, NULL, 0) != priv->id)
            return 0;
      }
      cdata.var[cdata.nvar] = var;
      cdata.val[cdata.nvar] = val;
      cdata.nvar++;
   }

   if (cancel) {
      nrx_cancel_custom_event(ctx, (nrx_callback_handle) priv);
      priv = NULL;
      return 0;
   }

   /* Do da call */
   if (priv->handler) /* Who would register NULL? */
      if ((*priv->handler)(ctx, NRX_CB_TRIGGER, 
                              &cdata, sizeof(cdata), 
                              priv->user_data) != 0)
         return ENOENT;

   return 0;
}


nrx_callback_handle
nrx_register_custom_event(nrx_context ctx, 
                          const char *type, 
                          uint32_t id,
                          nrx_callback_t handler,
                          void *user_data)
{
   struct nrx_custom_event_t *ce = malloc(sizeof(*ce));

   if (ce == NULL)
      return 0;
   memset(ce, 0, sizeof(*ce));

   ce->id = id;
   ce->handler = handler;
   ce->user_data = user_data;
   if (type == NULL) {
      memset(ce->type, 0, sizeof(ce->type));
   } else {
      strlcpy(ce->type, type, sizeof(ce->type));
   }

   ce->ev_handler = nrx_register_we_event_handler(ctx, IWEVCUSTOM, nrx_handle_custom_event, ce);
   if (ce->ev_handler == 0) {
      free(ce);
      return 0;
   }

   return (nrx_callback_handle)ce;
}

int
nrx_cancel_custom_event(nrx_context ctx, 
                        nrx_callback_handle handle)
{
   int ret;
   struct nrx_we_event *ev;
   struct nrx_custom_event_t *ce = (struct nrx_custom_event_t*)handle;
   NRX_ASSERT(ctx);
   NRX_ASSERT(ce);
   ev = find_we_by_userdata(ctx, ce);
   if(ev == NULL)
      return EINVAL;
   ret = nrx_cancel_we_event_handler(ctx, ce->ev_handler); /* Will free ce */
   return ret;
}

/* Can identify event with id alone, or - when id is 0 - together with type */
/* this should probably go away in the future */
int
nrx_cancel_custom_event_by_id(nrx_context ctx, 
                              const char *type,
                              int32_t id)
{
   struct nrx_we_event *ev;
   struct nrx_custom_event_t *ce;
   int ret = 0;

   ev = TAILQ_FIRST(&ctx->we_handlers);
   while(ev != NULL) {
      if(ev->cmd == IWEVCUSTOM && ev->handler == nrx_handle_custom_event) {
         ce = ev->user_data;
         if(ce->id == id && (type == NULL || strcmp(type, ce->type) == 0)) {
            ev = TAILQ_NEXT(ev, next);
            ret = nrx_cancel_custom_event(ctx, (nrx_callback_handle)ce);
            if(ret != 0)
               break;
            continue;
         }
      }
      ev = TAILQ_NEXT(ev, next);
   }
   return ret;
}


/***********************/
/*                     */
/*  MIB Trigger stuff  */
/*                     */
/***********************/

static int
mibtrigger_helper(nrx_context context,
                  int operation,
                  void *event_data,
                  size_t event_data_size,
                  void *user_data)
{
   struct nrx_we_custom_data *cdata = event_data;
   struct nrx_event_mibtrigger mdata;
   struct nrx_event_helper_data *hd = user_data;
   char *end;
   int i;

   if(operation == NRX_CB_CANCEL) {
      int ret;
      ret = (*hd->cb)(context, operation, NULL, 0, hd->user_data);
      free(hd);
      return ret;
   }

   memset(&mdata, 0, sizeof(mdata));
   for(i = 0; i < cdata->nvar; i++) {
      if(strcmp(cdata->var[i], "id") == 0) {
         mdata.id = strtoul(cdata->val[i], &end, 0);
      }
      if(strcmp(cdata->var[i], "type") == 0) { /* ignored */
      }
      if(strcmp(cdata->var[i], "res") == 0) {  /* ignored */
      }
      if(strcmp(cdata->var[i], "value") == 0) {
         mdata.value = strtoul(cdata->val[i], &end, 0);
      }
   }
   return (*hd->cb)(context, operation, &mdata, sizeof(mdata), hd->user_data);
}

nrx_callback_handle
nrx_register_mib_trigger_event_handler(nrx_context ctx,
                                       uint32_t id,
                                       nrx_callback_t handler,
                                       void *user_data)
{
   struct nrx_event_helper_data *hd;

   hd = malloc(sizeof(*hd));
   if(hd == NULL)
      return 0;

   hd->cb = handler;
   hd->helper_data = NULL;
   hd->user_data = user_data;
   return nrx_register_custom_event(ctx, "MIBTRIG", id, 
                                    mibtrigger_helper, hd);
}

int
nrx_register_mib_trigger(nrx_context ctx, 
                         int32_t           *trig_id,
                         char              *mib_id, 
                         int32_t           gating_trig_id,
                         uint32_t          supv_interval, /* timeout depends on mib, may be microseconds / beacons */
                         int32_t           level,
                         uint8_t           dir,       /* rising/falling */
                         uint16_t          event_count,
                         uint16_t          trigmode)
{
   struct nrx_ioc_mib_trigger param;
   int ret;

   strncpy(param.mib_id, mib_id, sizeof(param.mib_id));
   param.mib_id_len = strlen(param.mib_id);
   param.gating_trig_id = gating_trig_id;
   param.supv_interval = supv_interval;
   param.level = level;
   param.dir = dir;
   param.event_count = event_count;
   param.trigmode = trigmode;
   ret = nrx_nrxioctl(ctx, NRXIOWREGMIBTRIG, &param.ioc);    /* Register mib trigger */
   if (ret != 0) 
      return ret;
   *trig_id = param.trig_id;

   return 0;
}

int
nrx_cancel_mib_trigger_event_handler(nrx_context ctx,
                                     nrx_callback_handle handle)
{
   return nrx_cancel_custom_event(ctx, handle);
}

int
nrx_del_mib_trigger(nrx_context ctx, uint32_t trig_id)
{
   struct nrx_ioc_uint32_t param;
   param.value = trig_id;
   return nrx_nrxioctl(ctx, NRXIOWDELMIBTRIG, &param.ioc);
}
