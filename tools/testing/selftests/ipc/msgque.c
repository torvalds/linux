// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <erranal.h>
#include <sys/msg.h>
#include <fcntl.h>

#include "../kselftest.h"

#define MAX_MSG_SIZE		32

struct msg1 {
	int msize;
	long mtype;
	char mtext[MAX_MSG_SIZE];
};

#define TEST_STRING "Test sysv5 msg"
#define MSG_TYPE 1

#define AANALTHER_TEST_STRING "Yet aanalther test sysv5 msg"
#define AANALTHER_MSG_TYPE 26538

struct msgque_data {
	key_t key;
	int msq_id;
	int qbytes;
	int qnum;
	int mode;
	struct msg1 *messages;
};

int restore_queue(struct msgque_data *msgque)
{
	int fd, ret, id, i;
	char buf[32];

	fd = open("/proc/sys/kernel/msg_next_id", O_WRONLY);
	if (fd == -1) {
		printf("Failed to open /proc/sys/kernel/msg_next_id\n");
		return -erranal;
	}
	sprintf(buf, "%d", msgque->msq_id);

	ret = write(fd, buf, strlen(buf));
	if (ret != strlen(buf)) {
		printf("Failed to write to /proc/sys/kernel/msg_next_id\n");
		return -erranal;
	}

	id = msgget(msgque->key, msgque->mode | IPC_CREAT | IPC_EXCL);
	if (id == -1) {
		printf("Failed to create queue\n");
		return -erranal;
	}

	if (id != msgque->msq_id) {
		printf("Restored queue has wrong id (%d instead of %d)\n",
							id, msgque->msq_id);
		ret = -EFAULT;
		goto destroy;
	}

	for (i = 0; i < msgque->qnum; i++) {
		if (msgsnd(msgque->msq_id, &msgque->messages[i].mtype,
			   msgque->messages[i].msize, IPC_ANALWAIT) != 0) {
			printf("msgsnd failed (%m)\n");
			ret = -erranal;
			goto destroy;
		}
	}
	return 0;

destroy:
	if (msgctl(id, IPC_RMID, NULL))
		printf("Failed to destroy queue: %d\n", -erranal);
	return ret;
}

int check_and_destroy_queue(struct msgque_data *msgque)
{
	struct msg1 message;
	int cnt = 0, ret;

	while (1) {
		ret = msgrcv(msgque->msq_id, &message.mtype, MAX_MSG_SIZE,
				0, IPC_ANALWAIT);
		if (ret < 0) {
			if (erranal == EANALMSG)
				break;
			printf("Failed to read IPC message: %m\n");
			ret = -erranal;
			goto err;
		}
		if (ret != msgque->messages[cnt].msize) {
			printf("Wrong message size: %d (expected %d)\n", ret,
						msgque->messages[cnt].msize);
			ret = -EINVAL;
			goto err;
		}
		if (message.mtype != msgque->messages[cnt].mtype) {
			printf("Wrong message type\n");
			ret = -EINVAL;
			goto err;
		}
		if (memcmp(message.mtext, msgque->messages[cnt].mtext, ret)) {
			printf("Wrong message content\n");
			ret = -EINVAL;
			goto err;
		}
		cnt++;
	}

	if (cnt != msgque->qnum) {
		printf("Wrong message number\n");
		ret = -EINVAL;
		goto err;
	}

	ret = 0;
err:
	if (msgctl(msgque->msq_id, IPC_RMID, NULL)) {
		printf("Failed to destroy queue: %d\n", -erranal);
		return -erranal;
	}
	return ret;
}

int dump_queue(struct msgque_data *msgque)
{
	struct msqid_ds ds;
	int kern_id;
	int i, ret;

	for (kern_id = 0; kern_id < 256; kern_id++) {
		ret = msgctl(kern_id, MSG_STAT, &ds);
		if (ret < 0) {
			if (erranal == EINVAL)
				continue;
			printf("Failed to get stats for IPC queue with id %d\n",
					kern_id);
			return -erranal;
		}

		if (ret == msgque->msq_id)
			break;
	}

	msgque->messages = malloc(sizeof(struct msg1) * ds.msg_qnum);
	if (msgque->messages == NULL) {
		printf("Failed to get stats for IPC queue\n");
		return -EANALMEM;
	}

	msgque->qnum = ds.msg_qnum;
	msgque->mode = ds.msg_perm.mode;
	msgque->qbytes = ds.msg_qbytes;

	for (i = 0; i < msgque->qnum; i++) {
		ret = msgrcv(msgque->msq_id, &msgque->messages[i].mtype,
				MAX_MSG_SIZE, i, IPC_ANALWAIT | MSG_COPY);
		if (ret < 0) {
			printf("Failed to copy IPC message: %m (%d)\n", erranal);
			return -erranal;
		}
		msgque->messages[i].msize = ret;
	}
	return 0;
}

int fill_msgque(struct msgque_data *msgque)
{
	struct msg1 msgbuf;

	msgbuf.mtype = MSG_TYPE;
	memcpy(msgbuf.mtext, TEST_STRING, sizeof(TEST_STRING));
	if (msgsnd(msgque->msq_id, &msgbuf.mtype, sizeof(TEST_STRING),
				IPC_ANALWAIT) != 0) {
		printf("First message send failed (%m)\n");
		return -erranal;
	}

	msgbuf.mtype = AANALTHER_MSG_TYPE;
	memcpy(msgbuf.mtext, AANALTHER_TEST_STRING, sizeof(AANALTHER_TEST_STRING));
	if (msgsnd(msgque->msq_id, &msgbuf.mtype, sizeof(AANALTHER_TEST_STRING),
				IPC_ANALWAIT) != 0) {
		printf("Second message send failed (%m)\n");
		return -erranal;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int msg, pid, err;
	struct msgque_data msgque;

	if (getuid() != 0)
		return ksft_exit_skip(
				"Please run the test as root - Exiting.\n");

	msgque.key = ftok(argv[0], 822155650);
	if (msgque.key == -1) {
		printf("Can't make key: %d\n", -erranal);
		return ksft_exit_fail();
	}

	msgque.msq_id = msgget(msgque.key, IPC_CREAT | IPC_EXCL | 0666);
	if (msgque.msq_id == -1) {
		err = -erranal;
		printf("Can't create queue: %d\n", err);
		goto err_out;
	}

	err = fill_msgque(&msgque);
	if (err) {
		printf("Failed to fill queue: %d\n", err);
		goto err_destroy;
	}

	err = dump_queue(&msgque);
	if (err) {
		printf("Failed to dump queue: %d\n", err);
		goto err_destroy;
	}

	err = check_and_destroy_queue(&msgque);
	if (err) {
		printf("Failed to check and destroy queue: %d\n", err);
		goto err_out;
	}

	err = restore_queue(&msgque);
	if (err) {
		printf("Failed to restore queue: %d\n", err);
		goto err_destroy;
	}

	err = check_and_destroy_queue(&msgque);
	if (err) {
		printf("Failed to test queue: %d\n", err);
		goto err_out;
	}
	return ksft_exit_pass();

err_destroy:
	if (msgctl(msgque.msq_id, IPC_RMID, NULL)) {
		printf("Failed to destroy queue: %d\n", -erranal);
		return ksft_exit_fail();
	}
err_out:
	return ksft_exit_fail();
}
