/*	$OpenBSD: pcap.c,v 1.6 2016/09/21 06:02:03 otto Exp $ */
/*
 *	Placed in the PUBLIC DOMAIN
 */

#include <pcap.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "test.h"

#define LOOPBACK_IF	"lo0"
#define SNAPLEN		96
#define NO_PROMISC	0
#define	PKTCNT		3

volatile int packet_count = 0;
pthread_mutex_t dummy;
pthread_cond_t syncer;

static void
packet_ignore(u_char *tag, const struct pcap_pkthdr *hdr, const u_char *data)
{
	packet_count += 1;
}

static void *
pcap_thread(void *arg)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *handle;
	int on = 1;

	SET_NAME("pcap_thread");
	CHECKr(pthread_mutex_lock(&dummy));
	handle = pcap_open_live(LOOPBACK_IF, SNAPLEN, NO_PROMISC, 0, errbuf);
	if (!handle)
		PANIC("You may need to run this test as UID 0 (root)");
	ASSERT(ioctl(pcap_fileno(handle), BIOCIMMEDIATE, &on) != -1);
	CHECKr(pthread_mutex_unlock(&dummy));
	CHECKr(pthread_cond_signal(&syncer));
	ASSERT(pcap_loop(handle, PKTCNT, packet_ignore, 0) != -1);
	return 0;
}

static void *
ping_thread(void *arg)
{
	SET_NAME("ping_thread");
	CHECKr(pthread_mutex_lock(&dummy));
	ASSERT(system("ping -c 3 127.0.0.1") == 0);
	CHECKr(pthread_mutex_unlock(&dummy));
	CHECKr(pthread_cond_signal(&syncer));
	return 0;
}

int
main(int argc, char **argv)
{
	pthread_t pcap;
	pthread_t ping;

	CHECKr(pthread_mutex_init(&dummy, NULL));
	CHECKr(pthread_cond_init(&syncer, NULL));
	CHECKr(pthread_mutex_lock(&dummy));
	CHECKr(pthread_create(&pcap, NULL, pcap_thread, NULL));
	CHECKr(pthread_cond_wait(&syncer, &dummy));
	CHECKr(pthread_create(&ping, NULL, ping_thread, NULL));
	CHECKr(pthread_cond_wait(&syncer, &dummy));
	CHECKr(pthread_mutex_unlock(&dummy));
	ASSERT(packet_count == 3);
	SUCCEED;
}
