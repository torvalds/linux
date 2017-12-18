#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <errno.h>

#include "ipcsocket.h"


int opensocket(int *sockfd, const char *name, int connecttype)
{
	int ret, temp = 1;

	if (!name || strlen(name) > MAX_SOCK_NAME_LEN) {
		fprintf(stderr, "<%s>: Invalid socket name.\n", __func__);
		return -1;
	}

	ret = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (ret < 0) {
		fprintf(stderr, "<%s>: Failed socket: <%s>\n",
			__func__, strerror(errno));
		return ret;
	}

	*sockfd = ret;
	if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&temp, sizeof(int)) < 0) {
		fprintf(stderr, "<%s>: Failed setsockopt: <%s>\n",
		__func__, strerror(errno));
		goto err;
	}

	sprintf(sock_name, "/tmp/%s", name);

	if (connecttype == 1) {
		/* This is for Server connection */
		struct sockaddr_un skaddr;
		int clientfd;
		socklen_t sklen;

		unlink(sock_name);
		memset(&skaddr, 0, sizeof(skaddr));
		skaddr.sun_family = AF_LOCAL;
		strcpy(skaddr.sun_path, sock_name);

		ret = bind(*sockfd, (struct sockaddr *)&skaddr,
			SUN_LEN(&skaddr));
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed bind: <%s>\n",
			__func__, strerror(errno));
			goto err;
		}

		ret = listen(*sockfd, 5);
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed listen: <%s>\n",
			__func__, strerror(errno));
			goto err;
		}

		memset(&skaddr, 0, sizeof(skaddr));
		sklen = sizeof(skaddr);

		ret = accept(*sockfd, (struct sockaddr *)&skaddr,
			(socklen_t *)&sklen);
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed accept: <%s>\n",
			__func__, strerror(errno));
			goto err;
		}

		clientfd = ret;
		*sockfd = clientfd;
	} else {
		/* This is for client connection */
		struct sockaddr_un skaddr;

		memset(&skaddr, 0, sizeof(skaddr));
		skaddr.sun_family = AF_LOCAL;
		strcpy(skaddr.sun_path, sock_name);

		ret = connect(*sockfd, (struct sockaddr *)&skaddr,
			SUN_LEN(&skaddr));
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed connect: <%s>\n",
			__func__, strerror(errno));
			goto err;
		}
	}

	return 0;

err:
	if (*sockfd)
		close(*sockfd);

	return ret;
}

int sendtosocket(int sockfd, struct socketdata *skdata)
{
	int ret, buffd;
	unsigned int len;
	char cmsg_b[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg;
	struct msghdr msgh;
	struct iovec iov;
	struct timeval timeout;
	fd_set selFDs;

	if (!skdata) {
		fprintf(stderr, "<%s>: socketdata is NULL\n", __func__);
		return -1;
	}

	FD_ZERO(&selFDs);
	FD_SET(0, &selFDs);
	FD_SET(sockfd, &selFDs);
	timeout.tv_sec = 20;
	timeout.tv_usec = 0;

	ret = select(sockfd+1, NULL, &selFDs, NULL, &timeout);
	if (ret < 0) {
		fprintf(stderr, "<%s>: Failed select: <%s>\n",
		__func__, strerror(errno));
		return -1;
	}

	if (FD_ISSET(sockfd, &selFDs)) {
		buffd = skdata->data;
		len = skdata->len;
		memset(&msgh, 0, sizeof(msgh));
		msgh.msg_control = &cmsg_b;
		msgh.msg_controllen = CMSG_LEN(len);
		iov.iov_base = "OK";
		iov.iov_len = 2;
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(len);
		memcpy(CMSG_DATA(cmsg), &buffd, len);

		ret = sendmsg(sockfd, &msgh, MSG_DONTWAIT);
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed sendmsg: <%s>\n",
			__func__, strerror(errno));
			return -1;
		}
	}

	return 0;
}

int receivefromsocket(int sockfd, struct socketdata *skdata)
{
	int ret, buffd;
	unsigned int len = 0;
	char cmsg_b[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg;
	struct msghdr msgh;
	struct iovec iov;
	fd_set recvFDs;
	char data[32];

	if (!skdata) {
		fprintf(stderr, "<%s>: socketdata is NULL\n", __func__);
		return -1;
	}

	FD_ZERO(&recvFDs);
	FD_SET(0, &recvFDs);
	FD_SET(sockfd, &recvFDs);

	ret = select(sockfd+1, &recvFDs, NULL, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "<%s>: Failed select: <%s>\n",
		__func__, strerror(errno));
		return -1;
	}

	if (FD_ISSET(sockfd, &recvFDs)) {
		len = sizeof(buffd);
		memset(&msgh, 0, sizeof(msgh));
		msgh.msg_control = &cmsg_b;
		msgh.msg_controllen = CMSG_LEN(len);
		iov.iov_base = data;
		iov.iov_len = sizeof(data)-1;
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;
		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(len);

		ret = recvmsg(sockfd, &msgh, MSG_DONTWAIT);
		if (ret < 0) {
			fprintf(stderr, "<%s>: Failed recvmsg: <%s>\n",
			__func__, strerror(errno));
			return -1;
		}

		memcpy(&buffd, CMSG_DATA(cmsg), len);
		skdata->data = buffd;
		skdata->len = len;
	}
	return 0;
}

int closesocket(int sockfd, char *name)
{
	char sockname[MAX_SOCK_NAME_LEN];

	if (sockfd)
		close(sockfd);
	sprintf(sockname, "/tmp/%s", name);
	unlink(sockname);
	shutdown(sockfd, 2);

	return 0;
}
