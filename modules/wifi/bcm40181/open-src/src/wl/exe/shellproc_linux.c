/*
 * Remote shell command execution (common for all transports) for linux
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: shellproc_linux.c,v 1.12 2009/08/11 08:51:01 Exp $
 */

/* Linux remote shell command execution
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmcdc.h>
#include "wlu_remote.h"
#include <sys/poll.h>
#include <malloc.h>
#include <miniopt.h>
#include <sys/utsname.h>
#define ASYNC_RESP     0
#define MAX_SHELL_ASYNC_RESP  128  /* Support for maximum 5 async process */
#define MAX_ASYNC_FILE_LENGTH 50
#define MAX_PID_CMD_LENGTH    20
#define MAX_PID_RESP_LENTH    50
#define MAX_SHELL_CMD_LENTH   256
#define PID_TOKEN_SIZE	      50
#define PID_SEARCH_CMD_SIZE   100
#define ASYNC_SHELL_CHAR "%"  /* Async process identifier from the client */
#define FILE_PERMISSION	 777

#define DEFAULT_SHELL_TIMEOUT	0 /* Default TimeOut Value for synchronous shell commands */
#define SHELL_RETURNVALUE_SIZE	2 /* Size of Return Value of the shell command */
#define SHELL_ASYNCCMD_ID	1 /* To identify if it is an async command */
#define REBOOT_MSG      "Rebooting AP  ...\n"

/* Function prototypes */

static int rwl_get_file_size(char *file_name);
static int remote_shell_async_exec(char *buf_ptr);
static int remote_shell_sync_exec(char *cmd_buf_ptr, void *wl);


/* Data structure to hold async shell information */
typedef struct remote_shell_async {
	pid_t PID;
	char file_name[MAX_ASYNC_FILE_LENGTH];
} remote_shell_async_t;

remote_shell_async_t g_async_resp[MAX_SHELL_ASYNC_RESP];

extern int g_shellsync_pid;

extern unsigned char g_return_stat;
extern void rwl_chld_handler(int num);
extern int set_ctrlc;
extern void handle_ctrlc(int unused);

/* Global variable to store the timeout value for the shell commands */
static int g_shellsync_timeout = DEFAULT_SHELL_TIMEOUT;
char globalbuffer[MAX_SHELL_CMD_LENTH];

/* Wait for process termination.
 * This function returns immediately if the child has
 * already exited (zombie process)
 */
static void
sigchld_handler(int s)
{
	UNUSED_PARAMETER(s);

	while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* Create a main directory \tmp\RWL\ for the shell response files */
int
rwl_create_dir(void)
{
	if (mkdir(SHELL_RESP_PATH, FILE_PERMISSION) < 0) {
		if (errno != EEXIST)
			return BCME_ERROR;
	}

	return SUCCESS;
}

/* Main function for shell command execution */
int
remote_shell_execute(char* buf_ptr, void *wl)
{
	char *async_cmd_flag;
	int msg_len;

	/* Check for the "%" token in the buffer from client
	 * If "%" token is present, execute asynchronous process
	 * else, execute synchronous shell process
	 */
	async_cmd_flag = strstr((char*)buf_ptr, ASYNC_SHELL_CHAR);

	if ((async_cmd_flag != NULL) && (!strcmp(async_cmd_flag, ASYNC_SHELL_CHAR))) {
		g_shellsync_pid = SHELL_ASYNCCMD_ID;
		msg_len = remote_shell_async_exec(buf_ptr);
	}
	else {
		msg_len = remote_shell_sync_exec(buf_ptr, wl);
		strcpy(buf_ptr, globalbuffer);
	}
	return msg_len;
}

/* Function to get the shell response from the file */
int
remote_shell_async_get_resp(char* shell_fname, char* buf_ptr, int msg_len)
{
	int sts = 0;
	FILE *shell_fpt;

	shell_fpt = fopen(shell_fname, "rb");

	if (shell_fpt == NULL) {
		DPRINT_ERR(ERR, "\nShell Cmd:File open error\n");
		return sts;
	}

	/* If there is any response from the shell, Read the file and 
	 * update the buffer for the shell response
	 * else Just send the return value of the command executed
	 */
	if (g_shellsync_pid != SHELL_ASYNCCMD_ID) {
		if (msg_len)
			sts  = fread(buf_ptr, sizeof(char), msg_len, shell_fpt);
		fscanf(shell_fpt, "%2x", &sts);
	}
	else
		sts  = fread(buf_ptr, sizeof(char), MAX_SHELL_CMD_LENTH, shell_fpt);

	fclose(shell_fpt);

	remove(shell_fname);

	DPRINT_DBG(OUTPUT, "\n Resp buff from shell cmdis %s\n", buf_ptr);

	return sts;
}

/* 
 * Function to get the shell response length
 * by opening the file containing the shell response
 * and get the total file size.
 * For a given input file name it returns File size.
 */
static int
rwl_get_file_size(char *file_name)
{
	FILE *shell_fpt;
	int filesize = 0;

	shell_fpt = fopen(file_name, "rb");

	if (shell_fpt == NULL) {
		DPRINT_DBG(OUTPUT, "\nShell Cmd:File open error\n");
		return filesize;
	}

	/* obtain file size */
	if (fseek(shell_fpt, 0, SEEK_END) < 0)
		return filesize;

	filesize = ftell(shell_fpt);
	fclose(shell_fpt);

	return filesize;
}

/* 
 * Function for executing asynchronous shell comamnd
 * Stores the results in async temp file and returns the PID
 */
static int
remote_shell_async_exec(char *buf_ptr)
{
	int PID_val, val, msg_len, sts;
	FILE *fpt;
	int async_count = 0; /* counter needs to be initialized */
	struct sigaction sa;
	char pid_search_cmd[MAX_PID_CMD_LENGTH];
	char pid_resp_buf[MAX_PID_RESP_LENTH];
	char temp_async_file_name[MAX_ASYNC_FILE_LENGTH];
	pid_t pid;
	char *pid_token, next_pid[PID_TOKEN_SIZE][PID_TOKEN_SIZE];
	struct utsname name;

	/* Call the signal handler for reaping defunct or zombie process */
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction:");
	}

	/* Store the async file name if that async process is not killed.
	 * Async file name: async_temp_0...5
	 */
	for (val = 0; val < MAX_SHELL_ASYNC_RESP; val++) {
		if (g_async_resp[val].PID > 0) {
			async_count++;
		} else {
			sprintf(g_async_resp[val].file_name, "%s%d", "async_temp_", val);
			break;
		}
	}
	sprintf(temp_async_file_name, "%s%s", SHELL_RESP_PATH,
		g_async_resp[val].file_name);

	DPRINT_DBG(OUTPUT, "\nasync_count:%d\n", async_count);
	if (async_count >= MAX_SHELL_ASYNC_RESP) {
		sprintf(buf_ptr, "\n%s\n", "Exceeded max async process forking");
		return BCME_ERROR;
	}

	/* Open a child process. The fork will return the PID of the child process
	 * (i.e) defunct process PID in parent's thread of execution. Zero is returned
	 * for child's thread of execution.
	 */
	if ((pid = fork()) == 0) {
		/* Redirect the async process output to the async file
		 * Then after the client executes the kill command for that
		 * async process, the file will give the status of async process
		 */
		strtok(buf_ptr, ASYNC_SHELL_CHAR); /* Remove % character from the command buf */
		uname(&name);
		/*
		* Checking for mips architecture
		* different command for mips and x86
		*/
		if (strncmp(name.machine, "mips", sizeof(name.machine)) != 0) {
			strcat(buf_ptr, "&> "); /* buf_ptr is now "ping 127.0.0.1&> " */
			strcat(buf_ptr, temp_async_file_name); /* Add path \tmp\RWL\async_temp_* */
		}
		else {
			strcat(buf_ptr, " > "); /* buf_ptr is now "ping 127.0.0.1> " */
			strcat(buf_ptr, temp_async_file_name); /* Add path \tmp\RWL\async_temp_* */
			strcat(buf_ptr, " 2>&1 &");
		}
		if ((sts = execl(SH_PATH, "sh", "-c", buf_ptr, NULL)) == -1) {
			sprintf(buf_ptr, "%s\n", "Not able to execute shell cmd");
			return BCME_ERROR;
		}
		exit(0);
	} /* end of fork */

	if (pid < 0) {
		perror("\nFork error:");
		sprintf(buf_ptr, "%s\n", "Forking async process failed");
		return BCME_ERROR;
	}

	/* Find the PID of the running process (for ex: ping)
	 * pidof -s options returns latest PID of the command.
	 */
	strtok(buf_ptr, " ");

	uname(&name);
	/* Checking for mips architecture */
	if (strncmp(name.machine, "mips", sizeof(name.machine)) != 0)
		sprintf(pid_search_cmd, "pidof -s %s", buf_ptr);
	else
		sprintf(pid_search_cmd, "pidof %s", buf_ptr);

	sleep(1);

	/* Execute the command e.g "pidof ping" */
	if ((fpt = popen(pid_search_cmd, "r")) == NULL) {
		sprintf(buf_ptr, "%s\n", "Can't return PID");
		return BCME_ERROR;
	}

	/* Get the PID and copy the PID in buf_ptr to send to the client */
	fgets(pid_resp_buf, sizeof(pid_resp_buf), fpt);

	/* Checking for mips architecture */
	if (strncmp(name.machine, "mips", sizeof(name.machine)) != 0) {
		PID_val = atoi(pid_resp_buf);
	}
	else {
		/* code to extract the correct PID */
		pid_token = strtok_r(pid_resp_buf, " ", (char **)next_pid);
		if (pid_token != NULL) {
			while (pid_token != NULL) {
			/* the pid buffer will terminate with a '\n'
			 * It will affect the string tokenizing logic
			 * To avoid this we're using the if case
			 */
				if (strncmp(pid_token, "\n", sizeof(pid_token)) == 0)
					break;
				PID_val = atoi(pid_token);
				pid_token = strtok_r(NULL, " ", (char **)next_pid);
			}
		}
		else
			PID_val = atoi(pid_token);
	}
	if (PID_val == 0) {
		msg_len = rwl_get_file_size(temp_async_file_name);
		remote_shell_async_get_resp(temp_async_file_name, buf_ptr, msg_len);

	} else {
		g_async_resp[val].PID = PID_val;
		/* Update PID value in buffer to send it to client */
		sprintf(buf_ptr, "%d", PID_val);
		msg_len = strlen(buf_ptr);
	}

	pclose(fpt);
	/* In async case, the PID value will be copied to the input buffer only
	 * and there is no need of getting the response from the file. So return
	 * value can be -1.
	 */
	return msg_len;
}

/* Process for 'kill' command. 
 * Kill command can also be used from the client to get the
 * result of asynchronous command and actually kill the mentioned process
 */
static int
remote_kill_cmd_exec(char *cmd_buf_ptr)
{
	char file_name[MAX_ASYNC_FILE_LENGTH];
	int  PID_val, val, msg_len;
	FILE *fpt;
	char *pid_token, next_pid[PID_TOKEN_SIZE][PID_TOKEN_SIZE];

	system(cmd_buf_ptr);

	/* Parse the PID val from the kill command. 
	 */
	pid_token = strtok_r(cmd_buf_ptr, " ", (char **)next_pid);
	while (pid_token != NULL) {
		/* to extract the PID from the kill command */
		if (strncmp(pid_token, "\n", sizeof(pid_token)) == 0)
			break;
		PID_val = atoi(pid_token);
		pid_token = strtok_r(NULL, " ", (char **)next_pid);
	}

	/* Check for the matching PID from the async structure and 
	 * give the last 256 bytes statistics of the async process
	 * that was running
	 */
	for (val = 0; val < MAX_SHELL_ASYNC_RESP; ++val) {
		if (g_async_resp[val].PID == PID_val) {
			/* We found a match here. Hence get the response now from the
			 * corresponding async response file
			 */
			sprintf(file_name, "%s%s", SHELL_RESP_PATH, g_async_resp[val].file_name);
			msg_len = rwl_get_file_size(file_name);
			if (msg_len > 0) {
				if ((fpt = fopen(file_name, "rb")) == NULL) {
					DPRINT_DBG(OUTPUT, "\nShell Cmd:File open error\n");
					return BCME_ERROR;
				}

				if (fseek(fpt, 0, SEEK_SET) < 0) {
					fclose(fpt);
					return BCME_ERROR;
				}

				if (fread(cmd_buf_ptr, sizeof(char), MAX_SHELL_CMD_LENTH,
					fpt) <= 0) {
					sprintf(cmd_buf_ptr, "%s\n", "Shell Resp:Reading error");
					fclose(fpt);
					return BCME_ERROR;
				}

			fclose(fpt);
			}
			else
				sprintf(cmd_buf_ptr, "ed %d: No Response\n", PID_val);
			remove(g_async_resp[val].file_name);

			g_async_resp[val].PID = 0;
			break;
		}
	}
	return MAX_SHELL_CMD_LENTH;
}

/* Handle --timeout command line option for linux servers */
int
shell_timeout_cmd(char *cmd_buf_ptr, char *sync_file_name)
{
	char *token1, *token2, *nexttoken;
	FILE* fp;
	int msg_len;

	token1 = strtok_r(cmd_buf_ptr, "--timeout ", &nexttoken);
	if (token1)
		token2 = strtok_r(NULL, token1, &nexttoken);
	if (token1 == NULL || atoi(token1) <= 0 || token2 == NULL) {
		fp = fopen(sync_file_name, "w+");
		fprintf(fp, "Usage: ./wl --<transport> <ip/mac> sh"
			"--timeout <timeout value> <shell command>\n");
		fprintf(fp, "Eg: ./wl --socket 172.22.65.226 sh --timeout 15 ls\n");
		fflush(fp);
		msg_len = rwl_get_file_size(sync_file_name);
		strcpy(cmd_buf_ptr, sync_file_name);
		fclose(fp);
		strcpy(globalbuffer, sync_file_name);
		printf("Fix timeout problem in socket!!!!!\n");
		return msg_len;
	}
	else
		g_shellsync_timeout = atoi(token1);
	return BCME_OK;
}

/* Handle synchronous shell commands here */
static int
remote_shell_sync_exec(char *cmd_buf_ptr, void *wl)
{
	char *kill_cmd_token;
	char sync_file_name[] = TEMPLATE;
	int fd, msg_len;
	char cmd[(strlen(cmd_buf_ptr) + 1)];
	int pid, status, pid_final;
	char buf[SHELL_RESP_SIZE], cmd_find_lastpid[PID_SEARCH_CMD_SIZE];
	int nbytes = 0;
	int child_status;
	static int sent_once = 0;
	struct utsname name;
	FILE *fpt;

	/* Default Size of Return Value of the shell command is 2bytes */

	kill_cmd_token =  strstr(cmd_buf_ptr, "kill");

	/* Synchronous Kill command processing is handled separately */
	if (kill_cmd_token != NULL) {
		msg_len = remote_kill_cmd_exec(cmd_buf_ptr);
		remote_tx_response(wl, cmd_buf_ptr, msg_len);
		return 0;
	}


	/* Process synchronous command other than kill command */
	if ((fd = mkstemp(sync_file_name)) < 0) {
		perror("mkstemp failed");
		DPRINT_ERR(ERR, "\n errno:%d\n", errno);
		sprintf(cmd_buf_ptr, "%s\n", "mkstemp failed");
		return BCME_ERROR;
	}

	close(fd);

	strcpy(cmd, cmd_buf_ptr);
	/* Synchronous timeout command processing is handled separately */
	if (strstr(cmd_buf_ptr, "--timeout") != NULL) {
		if ((msg_len = shell_timeout_cmd (cmd, sync_file_name) > 0)) {
			/* Signal end of command output */
			g_rem_ptr->msg.len = 0;
			g_rem_ptr->msg.cmd = g_return_stat;
			remote_tx_response(wl, NULL, 0);
			return msg_len;
		} else {
			/* Parse out --timeout <val> since command is successful
			 * point buffer to the shell command
			 */
			strcpy(cmd, cmd_buf_ptr);
			strtok_r(cmd, " ", &cmd_buf_ptr);
			strcpy(cmd, cmd_buf_ptr);
			strtok_r(cmd, " ", &cmd_buf_ptr);
		}
	}

	/* Schedule an ALARM in case of timeout value of SHELL_TIMEOUT seconds */
	/* Defalut time out only in case of Non socket transport */
	alarm(g_shellsync_timeout);
	/* registering the relevant signals to handle end of child process,
	 * the ctrl+c event on the server side and the kill command on the 
	 * server process
	 */
	signal(SIGCHLD, rwl_chld_handler);
	signal(SIGINT, handle_ctrlc);
	signal(SIGTERM, handle_ctrlc);

	/* Set g_sig_chld before forking */
	g_sig_chld = 1;

	if (strcmp("reboot", cmd_buf_ptr) == 0) { /* reboot command */
		memset(buf, 0, sizeof(buf));
		strncpy(buf, REBOOT_MSG, sizeof(REBOOT_MSG));
		remote_tx_response(wl, buf, 0);

		/* Signal end of command output */
		g_rem_ptr->msg.len = 0;
		g_rem_ptr->msg.cmd = 0;
		remote_tx_response(wl, NULL, 0);
		sleep(1);

		/* Clean up the temp file */
		remove(sync_file_name);
	}

	if ((pid = fork()) == 0) {
		close(STDOUT_FILENO);
		fd = open(sync_file_name, O_WRONLY|O_SYNC);
		/* Redirect stdin to dev/null. This handles un usual commands like
		 * sh cat from the client side
		 */
		close(STDIN_FILENO);
		open("/dev/null", O_RDONLY);
		close(STDERR_FILENO);
		fcntl(fd, F_DUPFD, STDERR_FILENO);
		if ((status = execl(SH_PATH, "sh", "-c", cmd_buf_ptr, NULL)) == -1) {
			perror("Exec error");

		}
	} /* end of fork */

	g_shellsync_pid = pid;
	/* The g_return_stat is being set for short commands */
	waitpid(g_shellsync_pid, &child_status, WNOHANG);
	if (WIFEXITED(child_status))
		g_return_stat = WEXITSTATUS(child_status);
	else
		g_return_stat = 1;

	/* Read file in the interim from a temp file and send back the results */
	fd = open(sync_file_name, O_RDONLY|O_SYNC);

	while (1) {
		/* read file in the interim and send back the results */
		nbytes = read(fd, buf, SHELL_RESP_SIZE);
		g_rem_ptr->msg.len = nbytes;
		if (nbytes > 0) {
			remote_tx_response(wl, buf, 0);
#ifdef RWL_SERIAL
			/* usleep introduced for flooding of data over serial port */
			usleep(1);
#endif
		}
		if (get_ctrlc_header(wl) >= 0) {
			if (g_rem_ptr->msg.flags == (unsigned)CTRLC_FLAG) {
				uname(&name);
				/* Checking for mips architecture 
				 * The mips machine responds differently to
				 * execl command. so the pid is incremented
				 * to kill the right command.
				 */
				if (strncmp(name.machine, "mips", sizeof(name.machine)) == 0)
					pid++;
				if (strncmp(name.machine, "armv5tel", sizeof(name.machine)) == 0) {
					snprintf(cmd_find_lastpid, sizeof(cmd_find_lastpid),
						"ps | awk \'PRINT $1\' | tail -n 1");
					if ((fpt = popen(cmd_find_lastpid, "r")) == NULL) {
						sprintf(buf, "%s\n", "Can't return PID");
						return BCME_ERROR;
					}
					fgets(cmd_find_lastpid, sizeof(cmd_find_lastpid), fpt);
					pid_final = atoi(cmd_find_lastpid);
					while (pid <= pid_final) {
						kill(pid, SIGKILL);
						pid++;
					}
					pclose(fpt);
				}
				else {
					kill(pid, SIGKILL);
				}
				break;
			}
		}
		if (get_ctrlc_header(wl) >= 0) {
			if (g_rem_ptr->msg.flags == (unsigned)CTRLC_FLAG) {
				uname(&name);
				/* Checking for mips architecture 
				 * The mips machine responds differently to
				 * execl command. so the pid is incremented
				 * to kill the right command.
				 */
				if (strncmp(name.machine, "mips", sizeof(name.machine)) == 0) {
					pid++;
					kill(pid, SIGKILL);
				}
				/* Checking for arm architecture
				 * The multiple commands would not work
				 * for ctrl+C. So we kill the processes 
				 * spawned after the parent. This method has 
				 * its own limitations but the busybox in pxa
				 * doesnot have many options to implement it better 
				 */
				else {
					if (strncmp(name.machine, "armv5tel",
					sizeof(name.machine)) == 0) {
						/* The command below is used to get the
						 * PIDs and they are killed 
						 */
						snprintf(cmd_find_lastpid,
							sizeof(cmd_find_lastpid),
							"ps | awk \'PRINT $1\' | tail -n 1");
						if ((fpt = popen(cmd_find_lastpid, "r")) == NULL) {
							sprintf(buf, "%s\n", "Can't return PID");
							return BCME_ERROR;
						}
						fgets(cmd_find_lastpid, sizeof(cmd_find_lastpid),
						fpt);
						pid_final = atoi(cmd_find_lastpid);
						while (pid <= pid_final) {
							kill(pid, SIGKILL);
							pid++;
						}
						pclose(fpt);
					}
					/* In the case of x86, on receiving ctrl+C
					 * the child PIDs are obtained by searching 
					 * the parent PID to obtain the PIDs of the
					 * and kill them
					 */
					else {
						while (pid != 0) {
							/* The commad below is used to get the 
							 * child PIDs by using their parent PID
							 */
							snprintf(cmd_find_lastpid,
							sizeof(cmd_find_lastpid),
							"ps al | awk \"{ if (\\$4 == %d)"
							" {print \\$3}}\"| head -n 1",
							g_shellsync_pid);
							if ((fpt = popen(cmd_find_lastpid, "r"))
								== NULL) {
								sprintf(buf, "%s\n",
									"Can't return PID");
								return BCME_ERROR;
							}
							fgets(cmd_find_lastpid,
								sizeof(cmd_find_lastpid),
								fpt);
							pid = atoi(cmd_find_lastpid);
							if (pid == 0)
								kill(g_shellsync_pid, SIGKILL);
							else
								kill(pid, SIGKILL);
							pclose(fpt);
						}
					}
				}
				break;
			}
		}
		if (set_ctrlc == 1) {
			g_rem_ptr->msg.len = 0;
			g_rem_ptr->msg.cmd = g_return_stat;
			remote_tx_response(wl, NULL, g_return_stat);
			unlink(sync_file_name);
			kill(0, SIGKILL);
		}
		/* It is possible that the child would have exited
		 * However we did not get a chance to read the file
		 * In this case go once again and check the file
		 */
		if (!sent_once && !g_sig_chld) {
			sent_once = 1;
			continue;
		}

		if (!(g_sig_chld || nbytes))
			break;
	}
	wait(NULL);
	close(fd);

	/* Signal end of command output */
	g_rem_ptr->msg.len = 0;
	g_rem_ptr->msg.cmd = g_return_stat;

	remote_tx_response(wl, NULL, g_return_stat);
	/* Cancel the time out alarm if any */
	alarm(0);
	sent_once = 0;
	/* Clean up the temp file */
	unlink(sync_file_name);
	g_shellsync_timeout = DEFAULT_SHELL_TIMEOUT;
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);

	return BCME_OK;
}
