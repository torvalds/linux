// 
//  Copyright 2008, George V. Neville-Neil
//  All rights reserved.
//
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
//  This is a relatively simple multicast test which can act as a
//  source and sink.  The purpose of this test is to determine the
//  latency between two hosts, the source and the sink.  The programs
//  expect to be run somewhat unsynchronized hosts.  The source and
//  the sink both record the time on their own machine and then the
//  sink will correlate the data at the end of the run.
//

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

// C++ STL and other related includes
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

// Operating System and other C based includes
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

// Private include files
#include "mctest.h"

using namespace std;

//
// usage - just the program's usage line
// 
//
void usage()
{
    cout << "mctest [-r] -M clients -m client number -i interface -g multicast group -s packet size -n number -t inter-packet gap\n";
    exit(-1);
}

//
// usage - print out the usage with a possible message and exit
//
// \param message optional string
// 
//
void usage(string message)
{

    cerr << message << endl;
    usage();
}


//
// absorb and record packets
// 
// @param interface             ///< text name of the interface (em0 etc.)
// @param group			///< multicast group
// @param pkt_size		///< packet Size
// @param number                ///< number of packets we're expecting
// @param clients               ///< total number of clients  (N)
// @param client		///< our client number (0..N)
// 
// @return 0 for 0K, -1 for error, sets errno
//
int sink(char *interface, struct in_addr *group, int pkt_size, int number,
	 int clients, int client, short base_port) {

    
    int sock, backchan;
    socklen_t recvd_len;
    struct sockaddr_in local, recvd;
    struct ip_mreq mreq;
    struct ifreq ifreq;
    struct in_addr lgroup;
    struct timeval timeout;
    
    if (group == NULL) {
	group = &lgroup;
	if (inet_pton(AF_INET, DEFAULT_GROUP, group) < 1)
	    return (-1);
    }
    
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("failed to open datagram socket");
	return (-1);
    }

    if ((backchan = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("failed to open back channel socket");
	return (-1);
    }

    strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFADDR, &ifreq) < 0) {
	perror("no such interface");
	return (-1);
    }

    memcpy(&mreq.imr_interface, 
	   &((struct sockaddr_in*) &ifreq.ifr_addr)->sin_addr,
	   sizeof(struct in_addr));

    mreq.imr_multiaddr.s_addr = group->s_addr;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, 
		   sizeof(mreq)) < 0) {
	perror("failed to add membership");
	return (-1);
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, 
		   &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr, 
		   sizeof(struct in_addr)) < 0) {
	perror("failed to bind interface");
	return (-1);
    }
		   
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = group->s_addr;
    local.sin_port = htons(DEFAULT_PORT);
    local.sin_len = sizeof(local);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
	perror("could not bind socket");
	return (-1);
    }

    timeval packets[number];
    timeval result;
    char *packet;
    packet = new char[pkt_size];
    int n = 0;
    
    timerclear(&timeout);
    timeout.tv_sec = TIMEOUT;
    
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, 
		   sizeof(timeout)) < 0) 
	perror("setsockopt failed");
    
    while (n < number) { 
	recvd_len = sizeof(recvd);
	if (recvfrom(sock, packet, pkt_size, 0, (struct sockaddr *)&recvd, 
		     &recvd_len) < 0) {
	    if (errno == EWOULDBLOCK)
		break;
	    perror("recvfrom failed");
	    return -1;
	}
	/*
	 * Bandwidth limiting.  If there are N clients then we want
	 * 1/N packets from each, otherwise the clients will overwhelm
	 * the sender. 
	 */
	if (n % clients == client) {
		recvd.sin_port = htons(base_port + client);
		if (sendto(backchan, packet, pkt_size, 0, 
			   (struct sockaddr *)&recvd, sizeof(recvd)) < 0) {
		    perror("sendto failed");
		    return -1;
		}
	}
	gettimeofday(&packets[ntohl(*(int *)packet)], 0);
	n++;
    }
    
    cout << "Packet run complete\n";
    if (n < number)
	cout << "Missed " << number - n << " packets." << endl;
    long maxgap = 0, mingap= INT_MAX;
    for (int i = 0; i < number; i++) {
	cout << "sec: " << packets[i].tv_sec << "  usec: " << 
	    packets[i].tv_usec << endl;
	if (i < number - 1) {
	    timersub(&packets[i+1], &packets[i], &result);
	    long gap = (result.tv_sec * 1000000) + result.tv_usec;
	    if (gap > maxgap)
		maxgap = gap;
	    if (gap < mingap)
		mingap = gap;
	}
    }
    
    cout << "maximum gap (usecs): " << maxgap << endl;
    cout << "minimum gap (usecs): " << mingap << endl;
    return 0;
    
}

//
// Structure to hold thread arguments
//
struct server_args {
    struct timeval *packets; 	///< The timestamps of returning packets
    int number;			///< Number of packets to expect.
    int pkt_size;		///< Size of the packets
    int client;			///< Which client we listen for
};

//
// server receives packets sent back from the sink
//
// @param passed		///< Arguments passed from the caller
//
// 0return  always NULL
void* server(void *passed) {

    int sock, n =0;
    struct timeval timeout;
    struct sockaddr_in addr;
    server_args *args = (server_args *)passed;

    timerclear(&timeout);
    timeout.tv_sec = TIMEOUT;
    
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("could not open server socket");
	return NULL;
    }

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(args->client);
    addr.sin_len = sizeof(addr);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror("could not bind server socket");
	return NULL;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, 
		   sizeof(timeout)) < 0) 
	perror("setsockopt failed");

    char packet[args->pkt_size];
    while (n < args->number) { 
	if (recvfrom(sock, &packet, args->pkt_size, 0, NULL, 0) < 0) {
	    if (errno == EWOULDBLOCK)
		break;
	    perror("recvfrom failed");
	    return NULL;
	}
	gettimeofday(&args->packets[ntohl(*(int *)packet)], 0);
	n++;
    }

    cout << "Packet Reflection Complete" << endl;

    if (n < args->number)
	cout << "Missed " << args->number - n << " packets." << endl;

    return NULL;

}

//
// transmit packets for the multicast test
// 
// @param interface             ///< text name of the interface (em0 etc.)
// @param group			///< multicast group
// @param pkt_size		///< packet size
// @param number                ///< number of packets
// @param gap			///< inter packet gap in nano-seconds
// @param clients		///< number of clients we intend to run
// 
// @return 0 for OK, -1 for error, sets errno
//
int source(char *interface, struct in_addr *group, int pkt_size, 
	   int number, int gap, int clients, short base_port) {

    int sock;
    struct sockaddr_in addr;
    struct ip_mreq mreq;
    struct ifreq ifreq;
    struct in_addr lgroup;

    if (group == NULL) {
	group = &lgroup;
	if (inet_pton(AF_INET, DEFAULT_GROUP, group) < 1)
	    return (-1);
    }
    
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("could not open dgram socket");
	return (-1);
    }
    
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_PORT);
    addr.sin_addr.s_addr = group->s_addr;
    addr.sin_len = sizeof(addr);

    strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFADDR, &ifreq) < 0) {
	perror("no such interface");
	return (-1);
    }

    memcpy(&mreq.imr_interface, 
	   &((struct sockaddr_in*) &ifreq.ifr_addr)->sin_addr,
	   sizeof(struct in_addr));

    mreq.imr_multiaddr.s_addr = group->s_addr;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, 
		   sizeof(mreq)) < 0) {
	perror("failed to add membership");
	return (-1);
    }

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, 
		   &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr, 
		   sizeof(struct in_addr)) < 0) {
	perror("failed to bind interface");
	return (-1);
    }
		   
    u_char ttl = 64;

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
		   &ttl, sizeof(ttl)) < 0) {
	perror("failed to set TTL");
	return (-1);
    }

    char *packets[number];
    for (int i = 0;i < number; i++) {
	packets[i] = new char[pkt_size];
	*(int *)packets[i] = htonl(i);
    }
    
    struct timeval sent[number];
    struct timeval received[clients][number];
    server_args args[clients];
    pthread_t thread[clients];

    for (int i = 0;i < clients; i++) {
        args[i].pkt_size = pkt_size;
        args[i].packets = received[i];
        args[i].number = number / clients;
	args[i].client = base_port + i;
	if (pthread_create(&thread[i], NULL, server, &args[i]) != 0) {
	    perror("failed to create server thread");
	    return -1;
        }
    }

    struct timespec sleeptime;
    sleeptime.tv_sec = 0;
    sleeptime.tv_nsec = gap;
    
    for (int i = 0;i < number; i++) {
	if (sendto(sock, (void *)packets[i], pkt_size, 0, 
		   (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    perror("sendto failed");
	    return -1;
	}
	gettimeofday(&sent[i], 0);
	if (gap > 0) 
	    if (nanosleep(&sleeptime, NULL) < 0) {
		perror("nanosleep failed");
		return -1;
	    }
    }

    for (int i = 0; i < clients; i++) {
        if (pthread_join(thread[i], NULL) != 0) {
 	    perror("failed to join thread");
 	    return -1;
        }
    }

    timeval result;
    vector<int> deltas;
    double idx[] = { .0001, .001, .01, .1, .5, .9, .99, .999, .9999, 0.0 };

    for (int client = 0;client < clients; client++) {
	deltas.clear();
	cout << "Results from client #" << client << endl;
	cout << "in usecs" << endl;
        for (int i = 0; i < number; i++) {
// 	    if (i % clients != client) 
// 		continue;
            if (&args[client].packets[i].tv_sec == 0)
			continue;
	    timersub(&args[client].packets[i], &sent[i], &result);
	    deltas.push_back(result.tv_usec);
// 	    cout << "sec: " << result.tv_sec;
// 	    cout << " usecs: " << result.tv_usec << endl;
            }
	cout << "comparing " << long(deltas.size()) << " deltas" << endl;
	cout << "number represents usecs of round-trip time" << endl;
	sort(deltas.begin(), deltas.end());
	for (int i = 0; idx[i] != 0; ++i) {
		printf("%s% 5d", (i == 0) ? "" : " ", 
		       deltas[(int) (idx[i] * deltas.size())]); 
	}
	printf("\n");		
    }

    return 0;
}


//
// main - the main program
//
// \param -g multicast group address to which to send/recv packets on
// \param -n the number of packets to send
// \param -s packet size in bytes
// \param -t inter-packet gap, in nanoseconds
//
//
int main(int argc, char**argv)
{
    
	const int MAXNSECS = 999999999; ///< Must be < 1.0 x 10**9 nanoseconds

	char ch;		///< character from getopt()
	extern char* optarg;	///< option argument
	
	char* interface = 0;    ///< Name of the interface
	struct in_addr *group = NULL;	///< the multicast group address
	int pkt_size = 0;       ///< packet size
	int gap = 0;		///< inter packet gap (in nanoseconds)
	int number = 0;         ///< number of packets to transmit
	bool server = false;	///< are we on he receiving end of multicast
	int client = 0;		///< for receivers which client are we
	int clients = 1;	///< for senders how many clients are there
	short base_port = SERVER_PORT; ///< to have multiple copies running at once
	
	if (argc < 2 || argc > 16)
		usage();
	
	while ((ch = getopt(argc, argv, "M:m:g:i:n:s:t:b:rh")) != -1) {
		switch (ch) {
		case 'g':
			group = new (struct in_addr );
			if (inet_pton(AF_INET, optarg, group) < 1) 
				usage(argv[0] + string(" Error: invalid multicast group") + 
				      optarg);
			break;
		case 'i':
			interface = optarg;
			break;
		case 'n':
			number = atoi(optarg);
			if (number < 0 || number > INT_MAX)
				usage(argv[0] + string(" Error: ") + optarg + 
				      string(" number of packets out of range"));
			break;
		case 's':
			pkt_size = atoi(optarg);
			if (pkt_size < 0 || pkt_size > 65535)
				usage(argv[0] + string(" Error: ") + optarg + 
				      string(" packet size out of range"));
			break;
		case 't':
			gap = atoi(optarg);
			if (gap < 0 || gap > MAXNSECS)
				usage(argv[0] + string(" Error: ") + optarg + 
				      string(" gap out of range"));
			break;
		case 'r':
			server = true;
			break;
		case 'm':
			client = atoi(optarg);
			break;
		case 'M':
			clients = atoi(optarg);
			break;
		case 'b':
			base_port = atoi(optarg);
			break;
		case 'h':
			usage(string("Help\n"));
			break;
		}
	}
	
	if (server) {
	    if (clients <= 0 || client < 0)
		usage("must specify client (-m) and number of clients (-M)");
	    sink(interface, group, pkt_size, number, clients, client,
		 base_port);
	} else {
	    if (clients <= 0)
		usage("must specify number of clients (-M)");
	    source(interface, group, pkt_size, number, gap, clients, 
		   base_port);
	}
	
}
