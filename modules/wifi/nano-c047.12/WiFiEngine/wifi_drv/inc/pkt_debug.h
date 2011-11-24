#ifndef _PKT_DEBUG_H
#define _PKT_DEBUG_H

#ifdef WIFI_DEBUG_ON

void print_pkt_hdr(char *pkt, size_t len);

#else

#define print_pkt_hdr(a,b)

#endif

#ifdef WITH_PACKET_HISTORY

void init_pkt_log(void);

void log_pkt(char *buf, size_t len, int read);

#else

#define init_pkt_log()

#define log_pkt(a,b,c)

#endif

#endif /* _PKT_DEBUG_H */
