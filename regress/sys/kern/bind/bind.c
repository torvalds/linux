#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <err.h>

int
main(int argc, char **argv)
{
	struct sockaddr_in addr1, addr2;
	int fd1, fd2, enable = 1;

	addr1.sin_family = AF_INET;
	addr1.sin_port = htons(6666);
	addr1.sin_addr.s_addr = INADDR_ANY;
	/* fill sin_zero explicitly with garbage */
	memset(addr1.sin_zero, 0xd0, sizeof(addr1.sin_zero));

	addr2.sin_family = AF_INET;
	addr2.sin_port = htons(6666);
	addr2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* fill sin_zero explicitly with garbage */
	memset(addr2.sin_zero, 0xd0, sizeof(addr2.sin_zero));


	if ((fd1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		err(1, "socket1");

	if (setsockopt(fd1, SOL_SOCKET, SO_REUSEPORT, &enable,
	    sizeof(int)) < 0)
		err(1, "setsockopt1");

	if (bind(fd1, (struct sockaddr *)&addr1, sizeof(addr1)) == -1)
		err(1, "bind1");

	if ((fd2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		err(1, "socket1");

	if (setsockopt(fd2, SOL_SOCKET, SO_REUSEPORT, &enable,
	    sizeof(int)) < 0)
		err(1, "setsockopt2");

	if (bind(fd2, (struct sockaddr *)&addr2, sizeof(addr2)) == -1)
		err(1, "bind2");

	return 0;
}
