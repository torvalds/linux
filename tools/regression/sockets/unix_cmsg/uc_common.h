/*-
 * Copyright (c) 2005 Andrey Simonenko
 * Copyright (c) 2016 Maksym Sobolyev <sobomax@FreeBSD.org>
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

struct uc_cfg {
	int	sock_type;
	const char *sock_type_str;
	bool	debug;
	const char *proc_name;
	int	sync_fd[2][2];
	int	serv_sock_fd;
	bool	server_flag;
	bool	send_data_flag;
	struct sockaddr_un serv_addr_sun;
	bool	send_array_flag;
	pid_t	client_pid;
	struct {
		char	*buf_send;
		char	*buf_recv;
		size_t	buf_size;
		u_int	msg_num;
	} ipc_msg;
	struct {
		uid_t	uid;
		uid_t	euid;
		gid_t	gid;
		gid_t	egid;
		gid_t	*gid_arr;
		int	gid_num;
	} proc_cred;
};

extern struct uc_cfg uc_cfg;

int uc_check_msghdr(const struct msghdr *msghdr, size_t size);
int uc_check_cmsghdr(const struct cmsghdr *cmsghdr, int type, size_t size);
void uc_output(const char *format, ...) __printflike(1, 2);
void uc_logmsgx(const char *format, ...) __printflike(1, 2);
void uc_dbgmsg(const char *format, ...) __printflike(1, 2);
void uc_logmsg(const char *format, ...) __printflike(1, 2);
void uc_vlogmsgx(const char *format, va_list ap);
int uc_message_recv(int fd, struct msghdr *msghdr);
int uc_message_send(int fd, const struct msghdr *msghdr);
int uc_message_sendn(int fd, struct msghdr *msghdr);
void uc_msghdr_init_server(struct msghdr *msghdr, struct iovec *iov,
    void *cmsg_data, size_t cmsg_size);
void uc_msghdr_init_client(struct msghdr *msghdr, struct iovec *iov,
    void *cmsg_data, size_t cmsg_size, int type, size_t arr_size);
int uc_socket_create(void);
int uc_socket_accept(int listenfd);
int uc_socket_close(int fd);
int uc_socket_connect(int fd);
int uc_sync_recv(void);
int uc_sync_send(void);
int uc_client_fork(void);
void uc_client_exit(int rv);
int uc_client_wait(void);
int uc_check_groups(const char *gid_arr_str, const gid_t *gid_arr,
    const char *gid_num_str, int gid_num, bool all_gids);
int uc_check_scm_creds_cmsgcred(struct cmsghdr *cmsghdr);
int uc_check_scm_creds_sockcred(struct cmsghdr *cmsghdr);
