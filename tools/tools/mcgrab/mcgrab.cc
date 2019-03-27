// 
//  Copyright (c) 2008, Neville-Neil Consulting
//  All rights reserved.
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
//  This test simply grabs N multicast addresses starting 
//  from a base address.  The purpose is to make sure that switching a device
//  from using a multicast filtering table or function to promiscuous
//  multicast listening mode does not cause deleterious side effects.
//

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

// C++ STL and other related includes
#include <stdlib.h>
#include <limits.h>
#include <iostream>
#include <string.h>
#include <string>

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
    cout << "mcgrab -i interface -g multicast group -n number of groups\n";
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
// grab a set of addresses
// 
// @param interface             ///< text name of the interface (em0 etc.)
// @param group			///< multicast group
// @param number		///< number of addresses to grab
// 
// @return 0 for 0K, -1 for error, sets errno
//
void grab(char *interface, struct in_addr *group, int number, int block) {

    
    int sock;
    struct ip_mreq mreq;
    struct ifreq ifreq;
    struct in_addr lgroup;
    
    if (group == NULL) {
	group = &lgroup;
	if (inet_pton(AF_INET, DEFAULT_GROUP, group) < 1)
	    return;
    }
    
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	perror("failed to open datagram socket");
	return;
    }

    for (int i = 0; i < number; i++) {
	bzero((struct ip_mreq *)&mreq, sizeof(mreq));
	bzero((struct ifreq *)&ifreq, sizeof(ifreq));

	strncpy(ifreq.ifr_name, interface, IFNAMSIZ);
	if (ioctl(sock, SIOCGIFADDR, &ifreq) < 0) {
	    perror("no such interface");
	    return;
	}
	
	memcpy(&mreq.imr_interface, 
	       &((struct sockaddr_in*) &ifreq.ifr_addr)->sin_addr,
	       sizeof(struct in_addr));
	
	mreq.imr_multiaddr.s_addr = group->s_addr;
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, 
		       sizeof(mreq)) < 0) {
	    
	    perror("failed to add membership");
	    return;
	}
	
	if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, 
		       &((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr, 
		       sizeof(struct in_addr)) < 0) {
	    perror("failed to bind interface");
	    return;
	}
	
	group->s_addr = htonl(ntohl(group->s_addr) + 1);
    }
    if (block > 0) {
	    printf("Press Control-C to exit.\n");
	    sleep(INT_MAX);
    }

}


//
// main - the main program
//
// \param -g multicast group address which we will hold
// \param -i interface on which we're holding the address
//
//
int main(int argc, char**argv)
{
    
	char ch;		///< character from getopt()
	extern char* optarg;	///< option argument
	
	char* interface = 0;    ///< Name of the interface
	struct in_addr *group = NULL;	///< the multicast group address
	int number = 0;		///< Number of addresses to grab
	int block = 1;		///< Do we block or just return?
	
	if ((argc < 7) || (argc > 8))
		usage();
	
	while ((ch = getopt(argc, argv, "g:i:n:bh")) != -1) {
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
			break;
		case 'b':
			block = 0;
			break;
		case 'h':
			usage(string("Help\n"));
			break;
		}
	}
	
	grab(interface, group, number, block);
	
}
