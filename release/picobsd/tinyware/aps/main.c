/*-
 * Copyright (c) 1998 Andrzej Bialecki
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
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

/*
 * Ok, I could extract almost anything from /proc, but I'm too lazy...
 * I think it will suffice for now.
 */

int
main(int argc, char *argv[])
{
	DIR *d;
	struct dirent *e;
	FILE *fd;
	char buf[100];
	char *tok, *sep=" ", *sep1=",";
	char *name, *pid, *ppid, *uid, *gid;
	char *pgid, *sid, *tty, *cred;
	char *major, *minor;
	char con[10];

	d=opendir("/proc");
	printf(" PID   PPID  TTY  COMMAND\n");
	while((e=readdir(d))!=NULL) {
		/* Skip '.' and '..' */
		if(e->d_name[0]=='.') continue;
		/* Skip 'curproc' - it's us */
		if(e->d_name[0]=='c') continue;
		sprintf(buf,"/proc/%s/status",e->d_name);
		fd=fopen(buf,"r");
		fgets(buf,99,fd);
		fclose(fd);
		name=strtok(buf,sep);
		pid=strtok(NULL,sep);
		ppid=strtok(NULL,sep);
		pgid=strtok(NULL,sep);
		sid=strtok(NULL,sep);
		tty=strtok(NULL,sep);
		tok=strtok(NULL,sep); /* flags */
		tok=strtok(NULL,sep); /* start */
		tok=strtok(NULL,sep); /* user time */
		tok=strtok(NULL,sep); /* system time */
		tok=strtok(NULL,sep); /* wchan */
		cred=strtok(NULL,sep); /* credentials */
		major=strtok(tty,sep1);
		minor=strtok(NULL,sep1);
		if(strcmp(minor,"-1")==0) {
			minor="?";
		}
		if(strcmp(major,"-1")==0) {
			major="?";
		} else if(strcmp(major,"12")==0) {
			major="v";
		} else if(strcmp(major,"0")==0) {
			major="con";
			minor="-";
		} else if(strcmp(major,"5")==0) {
			major="p";
		} else major="x";
		if((strcmp(major,"v")==0) && (strcmp(minor,"255")==0)) {
			major="con";
			minor="-";
		}
		sprintf(con,"%s%s",major,minor);
		printf("%5s %5s %4s (%s)\n",pid,ppid,con,name);

	}
	closedir(d);
	exit(0);
}
