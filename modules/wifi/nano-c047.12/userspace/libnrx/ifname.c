#include "nrx_priv.h"
#include <time.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>

struct nrx_iflist {
   char ifname[IFNAMSIZ+1];
   int ifindex;
   TAILQ_ENTRY(nrx_iflist) next;
};

TAILQ_HEAD(nrx_iflist_head, nrx_iflist);

static void
nrx_free_iflist(nrx_context context, struct nrx_iflist_head *head) 
{
   struct nrx_iflist *ifl;
   while((ifl = TAILQ_FIRST(head)) != NULL) {
      TAILQ_REMOVE(head, ifl, next);
      memset(ifl, 0, sizeof(ifl));
      free(ifl);
   }
}

struct find_ifname_data {
   int done;
   struct nrx_iflist_head ifs;
};

static int
find_ifname_callback(nrx_context ctx, 
                     int operation, 
                     void *event_data, 
                     size_t event_data_size,
                     void *user_data)
{
   struct ifinfomsg *ifi;
   size_t rta_len;
   struct rtattr *rta;
   struct nlmsghdr *nh = event_data;
   struct nrx_iflist *ifl;
   struct find_ifname_data *fid = user_data;
   
   if(operation == NRX_CB_CANCEL)
      return 0;

#if 0
   printf("%s: type = %d, pid = %d, seq = %d\n", 
          __func__,
          nh->nlmsg_type, 
          nh->nlmsg_pid, 
          nh->nlmsg_seq);
#endif

   switch(nh->nlmsg_type) {
      case NLMSG_DONE:
         fid->done = 1;
         break;
      case RTM_NEWLINK:
         ifi = NLMSG_DATA(nh);
         if(nh->nlmsg_len < NLMSG_ALIGN(sizeof(*ifi))) {
            return EINVAL;
         }
         rta = (void*)(char *) ifi + NLMSG_ALIGN(sizeof(*ifi));
         rta_len = nh->nlmsg_len - NLMSG_ALIGN(sizeof(*ifi));
         while (RTA_OK(rta, rta_len)) {
            /* Check if the Wireless kind */
            if(rta->rta_type == IFLA_IFNAME) {
               char *ifname = RTA_DATA(rta);
#if 0
               printf("==> %s\n", ifname);
#endif
               ifl = malloc(sizeof(*ifl));
               strncpy(ifl->ifname, ifname, sizeof(ifl->ifname));
               ifl->ifindex = ifi->ifi_index;
               TAILQ_INSERT_TAIL(&fid->ifs, ifl, next);
            } 
            rta = RTA_NEXT(rta, rta_len);
         }
   }
   return 0;
}

static int 
sendreq(nrx_context ctx, int request, uint32_t seq)
{
   int sock;
   char reqbuf[1024];
   struct nlmsghdr *nh;
   struct rtgenmsg *rt;

   if((sock = nrx_event_connection_number(ctx)) < 0)
      return errno;
   
   memset(&reqbuf, 0, sizeof(reqbuf));
   nh = (struct nlmsghdr *)reqbuf;
   rt = (struct rtgenmsg *)NLMSG_DATA(nh);
   nh->nlmsg_len = NLMSG_LENGTH(sizeof(*rt));
   nh->nlmsg_type = request;
   nh->nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
   nh->nlmsg_pid = getpid();
   nh->nlmsg_seq = seq;
   rt->rtgen_family = AF_UNSPEC;
   if(write(sock, nh, nh->nlmsg_len) < 0) {
      return errno;
   }

   return 0;
}

int
nrx_find_ifname(nrx_context ctx, char *ifname, size_t len)
{
   nrx_callback_handle handle;
   struct find_ifname_data fid;
   struct nrx_iflist *ifl, *match = NULL;
   uint32_t seq = time(NULL);

   memset(&fid, 0, sizeof(fid));
   TAILQ_INIT(&fid.ifs);
   handle = nrx_register_netlink_handler(ctx, 0, getpid(), seq, 
                                         find_ifname_callback, &fid);
   sendreq(ctx, RTM_GETLINK, seq);
   while(fid.done == 0) {
      if(nrx_wait_event(ctx, 100) == 0)
         nrx_next_event(ctx);
   }
   nrx_cancel_netlink_handler(ctx, handle);

   TAILQ_FOREACH(ifl, &fid.ifs, next) {
      char buf[128];
#if 0
      printf("%s %d\n", ifl->ifname, ifl->ifindex);
#endif
      if(nrx_check_wext(ctx, ifl->ifname) != 0)
         continue;
      if(nrx_get_nickname(ctx, ifl->ifname, buf, sizeof(buf)) == 0) {
         if(strstr(buf, "nanoradio") != NULL) {
            match = ifl;
            break;
         }
      }
      if(match == NULL)
         match = ifl;
   }

   if(match != NULL) {
      strlcpy(ifname, match->ifname, len);
   } else
      *ifname = '\0';
   
   nrx_free_iflist(ctx, &fid.ifs);
   return 0;
}

