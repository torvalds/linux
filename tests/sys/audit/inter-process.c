/*-
 * Copyright (c) 2018 Aniket Pandey
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
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/shm.h>
#define _WANT_SEMUN
#include <sys/sem.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"
#define BUFFSIZE 80

struct msgstr {
	long int	 mtype;
	char		 mtext[BUFFSIZE];
};
typedef struct msgstr msgstr_t;

static pid_t pid;
static int msqid, shmid, semid;
static union semun semarg;
static struct pollfd fds[1];
static struct msqid_ds msgbuff;
static struct shmid_ds shmbuff;
static struct semid_ds sembuff;
static char ipcregex[BUFFSIZE];
static const char *auclass = "ip";
static char path[BUFFSIZE] = "/fileforaudit";
static unsigned short semvals[BUFFSIZE];


ATF_TC_WITH_CLEANUP(msgget_success);
ATF_TC_HEAD(msgget_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgget(2) call");
}

ATF_TC_BODY(msgget_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);
	/* Check the presence of message queue ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
			"msgget.*return,success,%d", msqid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgget_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgget_failure);
ATF_TC_HEAD(msgget_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgget(2) call");
}

ATF_TC_BODY(msgget_failure, tc)
{
	const char *regex = "msgget.*return,failure.*No such file or directory";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgget((key_t)(-1), 0));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgget_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgsnd_success);
ATF_TC_HEAD(msgsnd_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgsnd(2) call");
}

ATF_TC_BODY(msgsnd_success, tc)
{
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);

	/* Initialize a msgstr_t structure to store message */
	msgstr_t msg;
	msg.mtype = 1;
	memset(msg.mtext, 0, BUFFSIZE);

	/* Check the presence of message queue ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"msgsnd.*Message IPC.*%d.*return,success", msqid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, msgsnd(msqid, &msg, BUFFSIZE, IPC_NOWAIT));
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgsnd_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgsnd_failure);
ATF_TC_HEAD(msgsnd_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgsnd(2) call");
}

ATF_TC_BODY(msgsnd_failure, tc)
{
	const char *regex = "msgsnd.*Message IPC.*return,failure : Bad address";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgsnd(-1, NULL, 0, IPC_NOWAIT));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgsnd_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgrcv_success);
ATF_TC_HEAD(msgrcv_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgrcv(2) call");
}

ATF_TC_BODY(msgrcv_success, tc)
{
	ssize_t recv_bytes;
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);

	/* Initialize two msgstr_t structures to store respective messages */
	msgstr_t msg1, msg2;
	msg1.mtype = 1;
	memset(msg1.mtext, 0, BUFFSIZE);

	/* Send a message to the queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgsnd(msqid, &msg1, BUFFSIZE, IPC_NOWAIT));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((recv_bytes = msgrcv(msqid, &msg2,
			BUFFSIZE, 0, MSG_NOERROR | IPC_NOWAIT)) != -1);
	/* Check the presence of queue ID and returned bytes in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
	"msgrcv.*Message IPC,*%d.*return,success,%zd", msqid, recv_bytes);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgrcv_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgrcv_failure);
ATF_TC_HEAD(msgrcv_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgrcv(2) call");
}

ATF_TC_BODY(msgrcv_failure, tc)
{
	const char *regex = "msgrcv.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgrcv(-1, NULL, 0, 0, MSG_NOERROR | IPC_NOWAIT));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgrcv_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_rmid_success);
ATF_TC_HEAD(msgctl_rmid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(msgctl_rmid_success, tc)
{
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
	/* Check the presence of queue ID and IPC_RMID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
			"msgctl.*IPC_RMID.*%d.*return,success", msqid);
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(msgctl_rmid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_rmid_failure);
ATF_TC_HEAD(msgctl_rmid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(msgctl_rmid_failure, tc)
{
	const char *regex = "msgctl.*IPC_RMID.*return,failur.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgctl(-1, IPC_RMID, NULL));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgctl_rmid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_stat_success);
ATF_TC_HEAD(msgctl_stat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(msgctl_stat_success, tc)
{
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_STAT, &msgbuff));
	/* Check the presence of queue ID and IPC_STAT in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
			"msgctl.*IPC_STAT.*%d.*return,success", msqid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgctl_stat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_stat_failure);
ATF_TC_HEAD(msgctl_stat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(msgctl_stat_failure, tc)
{
	const char *regex = "msgctl.*IPC_STAT.*return,failur.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgctl(-1, IPC_STAT, &msgbuff));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgctl_stat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_set_success);
ATF_TC_HEAD(msgctl_set_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"msgctl(2) call for IPC_SET command");
}

ATF_TC_BODY(msgctl_set_success, tc)
{
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);
	/* Fill up the msgbuff structure to be used with IPC_SET */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_STAT, &msgbuff));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_SET, &msgbuff));
	/* Check the presence of message queue ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
			"msgctl.*IPC_SET.*%d.*return,success", msqid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgctl_set_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_set_failure);
ATF_TC_HEAD(msgctl_set_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgctl(2) call for IPC_SET command");
}

ATF_TC_BODY(msgctl_set_failure, tc)
{
	const char *regex = "msgctl.*IPC_SET.*return,failure.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgctl(-1, IPC_SET, &msgbuff));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(msgctl_set_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(msgctl_illegal_command);
ATF_TC_HEAD(msgctl_illegal_command, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"msgctl(2) call for illegal cmd value");
}

ATF_TC_BODY(msgctl_illegal_command, tc)
{
	/* Create a message queue and obtain the corresponding identifier */
	ATF_REQUIRE((msqid = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR)) != -1);

	const char *regex = "msgctl.*illegal command.*failur.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, msgctl(msqid, -1, &msgbuff));
	check_audit(fds, regex, pipefd);

	/* Destroy the message queue with ID = msqid */
	ATF_REQUIRE_EQ(0, msgctl(msqid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(msgctl_illegal_command, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmget_success);
ATF_TC_HEAD(shmget_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmget(2) call");
}

ATF_TC_BODY(shmget_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);
	/* Check the presence of shared memory ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex), "shmget.*ret.*success,%d", shmid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmget_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmget_failure);
ATF_TC_HEAD(shmget_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmget(2) call");
}

ATF_TC_BODY(shmget_failure, tc)
{
	const char *regex = "shmget.*return,failure.*No such file or directory";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmget((key_t)(-1), 0, 0));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmget_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmat_success);
ATF_TC_HEAD(shmat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmat(2) call");
}

ATF_TC_BODY(shmat_success, tc)
{
	void *addr;
	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((intptr_t)(addr = shmat(shmid, NULL, 0)) != -1);

	/* Check for shared memory ID and process address in record */
	snprintf(ipcregex, sizeof(ipcregex), "shmat.*Shared Memory "
			"IPC.*%d.*return,success", shmid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmat_failure);
ATF_TC_HEAD(shmat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmat(2) call");
}

ATF_TC_BODY(shmat_failure, tc)
{
	const char *regex = "shmat.*Shared Memory IPC.*return,failure";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, (intptr_t)shmat(-1, NULL, 0));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmdt_success);
ATF_TC_HEAD(shmdt_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmdt(2) call");
}

ATF_TC_BODY(shmdt_success, tc)
{
	void *addr;
	pid = getpid();
	snprintf(ipcregex, sizeof(ipcregex), "shmdt.*%d.*return,success", pid);

	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	/* Attach the shared memory to calling process's address space */
	ATF_REQUIRE((intptr_t)(addr = shmat(shmid, NULL, 0)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shmdt(addr));
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmdt_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmdt_failure);
ATF_TC_HEAD(shmdt_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmdt(2) call");
}

ATF_TC_BODY(shmdt_failure, tc)
{
	const char *regex = "shmdt.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmdt(NULL));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmdt_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_rmid_success);
ATF_TC_HEAD(shmctl_rmid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(shmctl_rmid_success, tc)
{
	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
	/* Check the presence of shmid and IPC_RMID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"shmctl.*IPC_RMID.*%d.*return,success", shmid);
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(shmctl_rmid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_rmid_failure);
ATF_TC_HEAD(shmctl_rmid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(shmctl_rmid_failure, tc)
{
	const char *regex = "shmctl.*IPC_RMID.*return,fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmctl(-1, IPC_RMID, NULL));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmctl_rmid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_stat_success);
ATF_TC_HEAD(shmctl_stat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(shmctl_stat_success, tc)
{
	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_STAT, &shmbuff));
	/* Check if shared memory ID and IPC_STAT are present in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"shmctl.*IPC_STAT.*%d.*return,success", shmid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmctl_stat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_stat_failure);
ATF_TC_HEAD(shmctl_stat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(shmctl_stat_failure, tc)
{
	const char *regex = "shmctl.*IPC_STAT.*return,fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmctl(-1, IPC_STAT, &shmbuff));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmctl_stat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_set_success);
ATF_TC_HEAD(shmctl_set_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shmctl(2) call for IPC_SET command");
}

ATF_TC_BODY(shmctl_set_success, tc)
{
	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);
	/* Fill up the shmbuff structure to be used with IPC_SET */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_STAT, &shmbuff));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_SET, &shmbuff));
	/* Check the presence of shared memory ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"shmctl.*IPC_SET.*%d.*return,success", msqid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmctl_set_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_set_failure);
ATF_TC_HEAD(shmctl_set_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmctl(2) call for IPC_SET command");
}

ATF_TC_BODY(shmctl_set_failure, tc)
{
	const char *regex = "shmctl.*IPC_SET.*return,failure.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmctl(-1, IPC_SET, &shmbuff));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shmctl_set_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shmctl_illegal_command);
ATF_TC_HEAD(shmctl_illegal_command, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shmctl(2) call for illegal cmd value");
}

ATF_TC_BODY(shmctl_illegal_command, tc)
{
	/* Create a shared memory segment and obtain the identifier */
	ATF_REQUIRE((shmid =
		shmget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	const char *regex = "shmctl.*illegal command.*fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shmctl(shmid, -1, &shmbuff));
	check_audit(fds, regex, pipefd);

	/* Destroy the shared memory with ID = shmid */
	ATF_REQUIRE_EQ(0, shmctl(shmid, IPC_RMID, NULL));
}

ATF_TC_CLEANUP(shmctl_illegal_command, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semget_success);
ATF_TC_HEAD(semget_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semget(2) call");
}

ATF_TC_BODY(semget_success, tc)
{
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	/* Check the presence of semaphore set ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semget.*return,success,%d", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semget_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semget_failure);
ATF_TC_HEAD(semget_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semget(2) call");
}

ATF_TC_BODY(semget_failure, tc)
{
	pid = getpid();
	snprintf(ipcregex, sizeof(ipcregex), "semget.*%d.*return,failure", pid);

	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: nsems is a negative number */
	ATF_REQUIRE_EQ(-1, semget(IPC_PRIVATE, -1, 0));
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(semget_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semop_success);
ATF_TC_HEAD(semop_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semop(2) call");
}

ATF_TC_BODY(semop_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	/* Initialize a sembuf structure to operate on semaphore set */
	struct sembuf sop[1] = {{0, 1, 0}};
	/* Check the presence of semaphore set ID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semop.*Semaphore IPC.*%d.*return,success", semid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semop(semid, sop, sizeof(sop)/sizeof(struct sembuf)));
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semop_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semop_failure);
ATF_TC_HEAD(semop_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semop(2) call");
}

ATF_TC_BODY(semop_failure, tc)
{
	const char *regex = "semop.*0xffff.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semop(-1, NULL, 0));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semop_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getval_success);
ATF_TC_HEAD(semctl_getval_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for GETVAL command");
}

ATF_TC_BODY(semctl_getval_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, GETVAL));
	/* Check the presence of semaphore ID and GETVAL in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*GETVAL.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_getval_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getval_failure);
ATF_TC_HEAD(semctl_getval_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for GETVAL command");
}

ATF_TC_BODY(semctl_getval_failure, tc)
{
	const char *regex = "semctl.*GETVAL.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, GETVAL));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_getval_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_setval_success);
ATF_TC_HEAD(semctl_setval_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for SETVAL command");
}

ATF_TC_BODY(semctl_setval_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.val = 1;
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, SETVAL, semarg));
	/* Check the presence of semaphore ID and SETVAL in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*SETVAL.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_setval_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_setval_failure);
ATF_TC_HEAD(semctl_setval_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for SETVAL command");
}

ATF_TC_BODY(semctl_setval_failure, tc)
{
	const char *regex = "semctl.*SETVAL.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, SETVAL, semarg));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_setval_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getpid_success);
ATF_TC_HEAD(semctl_getpid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for GETPID command");
}

ATF_TC_BODY(semctl_getpid_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, GETPID));
	/* Check the presence of semaphore ID and GETVAL in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*GETPID.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_getpid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getpid_failure);
ATF_TC_HEAD(semctl_getpid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for GETPID command");
}

ATF_TC_BODY(semctl_getpid_failure, tc)
{
	const char *regex = "semctl.*GETPID.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, GETPID));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_getpid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getncnt_success);
ATF_TC_HEAD(semctl_getncnt_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for GETNCNT command");
}

ATF_TC_BODY(semctl_getncnt_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, GETNCNT));
	/* Check the presence of semaphore ID and GETNCNT in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*GETNCNT.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_getncnt_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getncnt_failure);
ATF_TC_HEAD(semctl_getncnt_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for GETNCNT command");
}

ATF_TC_BODY(semctl_getncnt_failure, tc)
{
	const char *regex = "semctl.*GETNCNT.*return,failure.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, GETNCNT));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_getncnt_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getzcnt_success);
ATF_TC_HEAD(semctl_getzcnt_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for GETZCNT command");
}

ATF_TC_BODY(semctl_getzcnt_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, GETZCNT));
	/* Check the presence of semaphore ID and GETZCNT in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*GETZCNT.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_getzcnt_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getzcnt_failure);
ATF_TC_HEAD(semctl_getzcnt_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for GETZCNT command");
}

ATF_TC_BODY(semctl_getzcnt_failure, tc)
{
	const char *regex = "semctl.*GETZCNT.*return,failure.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, GETZCNT));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_getzcnt_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getall_success);
ATF_TC_HEAD(semctl_getall_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for GETALL command");
}

ATF_TC_BODY(semctl_getall_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.array = semvals;
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE(semctl(semid, 0, GETALL, semarg) != -1);
	/* Check the presence of semaphore ID and GETALL in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*GETALL.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_getall_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_getall_failure);
ATF_TC_HEAD(semctl_getall_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for GETALL command");
}

ATF_TC_BODY(semctl_getall_failure, tc)
{
	const char *regex = "semctl.*GETALL.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, GETALL, semarg));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_getall_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_setall_success);
ATF_TC_HEAD(semctl_setall_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for SETALL command");
}

ATF_TC_BODY(semctl_setall_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.array = semvals;
	/* Initialize semvals to be used with SETALL */
	ATF_REQUIRE(semctl(semid, 0, GETALL, semarg) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, SETALL, semarg));
	/* Check the presence of semaphore ID and SETALL in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*SETALL.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_setall_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_setall_failure);
ATF_TC_HEAD(semctl_setall_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for SETALL command");
}

ATF_TC_BODY(semctl_setall_failure, tc)
{
	const char *regex = "semctl.*SETALL.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, SETALL, semarg));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_setall_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_stat_success);
ATF_TC_HEAD(semctl_stat_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(semctl_stat_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.buf = &sembuff;
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_STAT, semarg));
	/* Check the presence of semaphore ID and IPC_STAT in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*IPC_STAT.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_stat_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_stat_failure);
ATF_TC_HEAD(semctl_stat_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for IPC_STAT command");
}

ATF_TC_BODY(semctl_stat_failure, tc)
{
	const char *regex = "semctl.*IPC_STAT.*return,fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, IPC_STAT, semarg));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_stat_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_set_success);
ATF_TC_HEAD(semctl_set_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for IPC_SET command");
}

ATF_TC_BODY(semctl_set_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.buf = &sembuff;
	/* Fill up the sembuff structure to be used with IPC_SET */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_STAT, semarg));

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_SET, semarg));
	/* Check the presence of semaphore ID and IPC_SET in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*IPC_SET.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_set_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_set_failure);
ATF_TC_HEAD(semctl_set_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for IPC_SET command");
}

ATF_TC_BODY(semctl_set_failure, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	semarg.buf = &sembuff;
	/* Fill up the sembuff structure to be used with IPC_SET */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_STAT, semarg));

	const char *regex = "semctl.*IPC_SET.*return,failure.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, IPC_SET, semarg));
	check_audit(fds, regex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_set_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_rmid_success);
ATF_TC_HEAD(semctl_rmid_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"semctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(semctl_rmid_success, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID, semarg));
	/* Check the presence of semaphore ID and IPC_RMID in audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"semctl.*IPC_RMID.*%d.*return,success", semid);
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(semctl_rmid_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_rmid_failure);
ATF_TC_HEAD(semctl_rmid_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for IPC_RMID command");
}

ATF_TC_BODY(semctl_rmid_failure, tc)
{
	const char *regex = "semctl.*IPC_RMID.*return,fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(-1, 0, IPC_RMID, semarg));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(semctl_rmid_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(semctl_illegal_command);
ATF_TC_HEAD(semctl_illegal_command, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"semctl(2) call for illegal cmd value");
}

ATF_TC_BODY(semctl_illegal_command, tc)
{
	/* Create a semaphore set and obtain the set identifier */
	ATF_REQUIRE((semid =
		semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRUSR)) != -1);

	const char *regex = "semctl.*illegal command.*fail.*Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, semctl(semid, 0, -1));
	check_audit(fds, regex, pipefd);

	/* Destroy the semaphore set with ID = semid */
	ATF_REQUIRE_EQ(0, semctl(semid, 0, IPC_RMID));
}

ATF_TC_CLEANUP(semctl_illegal_command, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shm_open_success);
ATF_TC_HEAD(shm_open_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shm_open(2) call");
}

ATF_TC_BODY(shm_open_success, tc)
{
	pid = getpid();
	snprintf(ipcregex, sizeof(ipcregex), "shm_open.*%d.*ret.*success", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE(shm_open(SHM_ANON, O_CREAT | O_TRUNC | O_RDWR, 0600) != -1);
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(shm_open_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shm_open_failure);
ATF_TC_HEAD(shm_open_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shm_open(2) call");
}

ATF_TC_BODY(shm_open_failure, tc)
{
	const char *regex = "shm_open.*fileforaudit.*return,failure";
	FILE *pipefd = setup(fds, auclass);
	/* Failure reason: File does not exist */
	ATF_REQUIRE_EQ(-1, shm_open(path, O_TRUNC | O_RDWR, 0600));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shm_open_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shm_unlink_success);
ATF_TC_HEAD(shm_unlink_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"shm_unlink(2) call");
}

ATF_TC_BODY(shm_unlink_success, tc)
{
	/* Build an absolute path to a file in the test-case directory */
	char dirpath[50];
	ATF_REQUIRE(getcwd(dirpath, sizeof(dirpath)) != NULL);
	strlcat(dirpath, path, sizeof(dirpath));
	ATF_REQUIRE(shm_open(dirpath, O_CREAT | O_TRUNC | O_RDWR, 0600) != -1);

	const char *regex = "shm_unlink.*fileforaudit.*return,success";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, shm_unlink(dirpath));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shm_unlink_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(shm_unlink_failure);
ATF_TC_HEAD(shm_unlink_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"shm_unlink(2) call");
}

ATF_TC_BODY(shm_unlink_failure, tc)
{
	const char *regex = "shm_unlink.*fileforaudit.*return,failure";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, shm_unlink(path));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(shm_unlink_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(pipe_success);
ATF_TC_HEAD(pipe_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"pipe(2) call");
}

ATF_TC_BODY(pipe_success, tc)
{
	int filedesc[2];
	pid = getpid();
	snprintf(ipcregex, sizeof(ipcregex), "pipe.*%d.*return,success", pid);
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(0, pipe(filedesc));
	check_audit(fds, ipcregex, pipefd);

	close(filedesc[0]);
	close(filedesc[1]);
}

ATF_TC_CLEANUP(pipe_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(pipe_failure);
ATF_TC_HEAD(pipe_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"pipe(2) call");
}

ATF_TC_BODY(pipe_failure, tc)
{
	pid = getpid();
	snprintf(ipcregex, sizeof(ipcregex), "pipe.*%d.*return.failure", pid);

	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, pipe(NULL));
	check_audit(fds, ipcregex, pipefd);
}

ATF_TC_CLEANUP(pipe_failure, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(posix_openpt_success);
ATF_TC_HEAD(posix_openpt_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of a successful "
					"posix_openpt(2) call");
}

ATF_TC_BODY(posix_openpt_success, tc)
{
	int filedesc;
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE((filedesc = posix_openpt(O_RDWR | O_NOCTTY)) != -1);
	/* Check for the presence of filedesc in the audit record */
	snprintf(ipcregex, sizeof(ipcregex),
		"posix_openpt.*return,success,%d", filedesc);
	check_audit(fds, ipcregex, pipefd);
	close(filedesc);
}

ATF_TC_CLEANUP(posix_openpt_success, tc)
{
	cleanup();
}


ATF_TC_WITH_CLEANUP(posix_openpt_failure);
ATF_TC_HEAD(posix_openpt_failure, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests the audit of an unsuccessful "
					"posix_openpt(2) call");
}

ATF_TC_BODY(posix_openpt_failure, tc)
{
	const char *regex = "posix_openpt.*return,failure : Invalid argument";
	FILE *pipefd = setup(fds, auclass);
	ATF_REQUIRE_EQ(-1, posix_openpt(-1));
	check_audit(fds, regex, pipefd);
}

ATF_TC_CLEANUP(posix_openpt_failure, tc)
{
	cleanup();
}


ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, msgget_success);
	ATF_TP_ADD_TC(tp, msgget_failure);
	ATF_TP_ADD_TC(tp, msgsnd_success);
	ATF_TP_ADD_TC(tp, msgsnd_failure);
	ATF_TP_ADD_TC(tp, msgrcv_success);
	ATF_TP_ADD_TC(tp, msgrcv_failure);

	ATF_TP_ADD_TC(tp, msgctl_rmid_success);
	ATF_TP_ADD_TC(tp, msgctl_rmid_failure);
	ATF_TP_ADD_TC(tp, msgctl_stat_success);
	ATF_TP_ADD_TC(tp, msgctl_stat_failure);
	ATF_TP_ADD_TC(tp, msgctl_set_success);
	ATF_TP_ADD_TC(tp, msgctl_set_failure);
	ATF_TP_ADD_TC(tp, msgctl_illegal_command);

	ATF_TP_ADD_TC(tp, shmget_success);
	ATF_TP_ADD_TC(tp, shmget_failure);
	ATF_TP_ADD_TC(tp, shmat_success);
	ATF_TP_ADD_TC(tp, shmat_failure);
	ATF_TP_ADD_TC(tp, shmdt_success);
	ATF_TP_ADD_TC(tp, shmdt_failure);

	ATF_TP_ADD_TC(tp, shmctl_rmid_success);
	ATF_TP_ADD_TC(tp, shmctl_rmid_failure);
	ATF_TP_ADD_TC(tp, shmctl_stat_success);
	ATF_TP_ADD_TC(tp, shmctl_stat_failure);
	ATF_TP_ADD_TC(tp, shmctl_set_success);
	ATF_TP_ADD_TC(tp, shmctl_set_failure);
	ATF_TP_ADD_TC(tp, shmctl_illegal_command);

	ATF_TP_ADD_TC(tp, semget_success);
	ATF_TP_ADD_TC(tp, semget_failure);
	ATF_TP_ADD_TC(tp, semop_success);
	ATF_TP_ADD_TC(tp, semop_failure);

	ATF_TP_ADD_TC(tp, semctl_getval_success);
	ATF_TP_ADD_TC(tp, semctl_getval_failure);
	ATF_TP_ADD_TC(tp, semctl_setval_success);
	ATF_TP_ADD_TC(tp, semctl_setval_failure);
	ATF_TP_ADD_TC(tp, semctl_getpid_success);
	ATF_TP_ADD_TC(tp, semctl_getpid_failure);
	ATF_TP_ADD_TC(tp, semctl_getncnt_success);
	ATF_TP_ADD_TC(tp, semctl_getncnt_failure);
	ATF_TP_ADD_TC(tp, semctl_getzcnt_success);
	ATF_TP_ADD_TC(tp, semctl_getzcnt_failure);
	ATF_TP_ADD_TC(tp, semctl_getall_success);
	ATF_TP_ADD_TC(tp, semctl_getall_failure);
	ATF_TP_ADD_TC(tp, semctl_setall_success);
	ATF_TP_ADD_TC(tp, semctl_setall_failure);
	ATF_TP_ADD_TC(tp, semctl_stat_success);
	ATF_TP_ADD_TC(tp, semctl_stat_failure);
	ATF_TP_ADD_TC(tp, semctl_set_success);
	ATF_TP_ADD_TC(tp, semctl_set_failure);
	ATF_TP_ADD_TC(tp, semctl_rmid_success);
	ATF_TP_ADD_TC(tp, semctl_rmid_failure);
	ATF_TP_ADD_TC(tp, semctl_illegal_command);

	ATF_TP_ADD_TC(tp, shm_open_success);
	ATF_TP_ADD_TC(tp, shm_open_failure);
	ATF_TP_ADD_TC(tp, shm_unlink_success);
	ATF_TP_ADD_TC(tp, shm_unlink_failure);

	ATF_TP_ADD_TC(tp, pipe_success);
	ATF_TP_ADD_TC(tp, pipe_failure);
	ATF_TP_ADD_TC(tp, posix_openpt_success);
	ATF_TP_ADD_TC(tp, posix_openpt_failure);

	return (atf_no_error());
}
