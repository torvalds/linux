/*-
 * Simple_HTTPd v1.1 - a very small, barebones HTTP server
 * 
 * Copyright (c) 1998-1999 Marc Nicholas <marc@netstor.com>
 * All rights reserved.
 *
 * Major rewrite by William Lloyd <wlloyd@slap.net>
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

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int             http_port = 80;
int             daemonize = 1;
int             verbose = 0;
int             http_sock, con_sock;

const char     *fetch_mode = NULL;
char            homedir[100];
char            logfile[80];
char           *adate(void);
void            init_servconnection(void);
void		http_date(void);
void            http_output(const char *html);
void            http_request(void);
void            log_line(char *req);
void            wait_connection(void);

struct hostent *hst;
struct sockaddr_in source;

/* HTTP basics */
static char httpd_server_ident[] = "Server: FreeBSD/PicoBSD simple_httpd 1.1\r";

static char http_200[] = "HTTP/1.0 200 OK\r";

const char *default_mime_type = "application/octet-stream";

const char *mime_type[][2] = {
    { "txt",      "text/plain"            },
    { "htm",      "text/html"             },
    { "html",     "text/html"             },
    { "gif",      "image/gif"             },
    { "jpg",      "image/jpeg"            },
    { "mp3",      "audio/mpeg"            }
};

const int mime_type_max = sizeof(mime_type) / sizeof(mime_type[0]) - 1;

/* Two parts, HTTP Header and then HTML */
static const char *http_404[2] = 
    {"HTTP/1.0 404 Not found\r\n", 
"<HTML><HEAD><TITLE>Error</TITLE></HEAD><BODY><H1>Error 404</H1>\
Not found - file doesn't exist or you do not have permission.\n</BODY></HTML>\r\n"
};

static const char *http_405[2] = 
    {"HTTP/1.0 405 Method Not allowed\r\nAllow: GET,HEAD\r\n",
"<HTML><HEAD><TITLE>Error</TITLE></HEAD><BODY><H1>Error 405</H1>\
This server only supports GET and HEAD requests.\n</BODY></HTML>\r\n"
};

/*
 * Only called on initial invocation
 */
void
init_servconnection(void)
{
	struct sockaddr_in server;

	/* Create a socket */
	http_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (http_sock < 0) {
		perror("socket");
		exit(1);
	}
	server.sin_family = AF_INET;
	server.sin_port = htons(http_port);
	server.sin_addr.s_addr = INADDR_ANY;
	if (bind(http_sock, (struct sockaddr *) & server, sizeof(server)) < 0) {
		perror("bind socket");
		exit(1);
	}
        if (verbose) printf("simple_httpd:%d\n",http_port);
}

/*
 * Wait here until we see an incoming http request
 */
void
wait_connection(void)
{
	socklen_t lg;

	lg = sizeof(struct sockaddr_in);

	con_sock = accept(http_sock, (struct sockaddr *) & source, &lg);
	if (con_sock <= 0) {
		perror("accept");
		exit(1);
	}
}

/*
 * Print timestamp for HTTP HEAD and GET
 */
void
http_date(void)
{
	time_t	tl;
	char	buff[50];

	tl = time(NULL);
	strftime(buff, 50, "Date: %a, %d %h %Y %H:%M:%S %Z\r\n", gmtime(&tl));
	write(con_sock, buff, strlen(buff));
	/* return(buff); */
}

/*
 * Send data to the open socket
 */
void
http_output(const char *html)
{
	write(con_sock, html, strlen(html));
	write(con_sock, "\r\n", 2);
}


/*
 * Create and write the log information to file
 * Log file format is one line per entry
 */
void
log_line(char *req)
{
	char	log_buff[256];
	char	msg[1024];
	char	env_host[80], env_addr[80];
	long	addr;
	FILE	*log;

	strcpy(log_buff,inet_ntoa(source.sin_addr));
	sprintf(env_addr, "REMOTE_ADDR=%s",log_buff);

	addr=inet_addr(log_buff);

	strcpy(msg,adate());
	strcat(msg,"    ");
	hst=gethostbyaddr((char*) &addr, 4, AF_INET);

	/* If DNS hostname exists */
	if (hst) {
	  strcat(msg,hst->h_name);
	  sprintf(env_host, "REMOTE_HOST=%s",hst->h_name);
	}
	strcat(msg," (");
	strcat(msg,log_buff);
	strcat(msg,")   ");
	strcat(msg,req);

	if (daemonize) {
	  log=fopen(logfile,"a");
	  fprintf(log,"%s\n",msg);
	  fclose(log);
	} else
	  printf("%s\n",msg);

	/* This is for CGI scripts */
	putenv(env_addr);
	putenv(env_host);
}

/*
 * We have a connection.  Identify what type of request GET, HEAD, CGI, etc 
 * and do what needs to be done
 */
void
http_request(void)
{
	int             fd, lg, i; 
	int             cmd = 0;
	char           *p, *par;
	const char     *filename, *c, *ext, *type;
	struct stat     file_status;
	char            req[1024];
	char            buff[8192];

	lg = read(con_sock, req, 1024);

	if ((p=strstr(req,"\n"))) *p=0;
	if ((p=strstr(req,"\r"))) *p=0;

	log_line(req);

	c = strtok(req, " ");

	/* Error msg if request is nothing */
	if (c == NULL) {
	  http_output(http_404[0]);
	  http_output(http_404[1]);
	  goto end_request;
	}

	if (strncmp(c, "GET", 3) == 0) cmd = 1;
	if (strncmp(c, "HEAD", 4) == 0) cmd = 2;

	/* Do error msg for any other type of request */
	if (cmd == 0) {	        
	  http_output(http_405[0]);
	  http_output(http_405[1]);
	  goto end_request;
	}

	filename = strtok(NULL, " ");

	c = strtok(NULL, " ");
	if (fetch_mode != NULL) filename=fetch_mode;
	if (filename == NULL ||
	    strlen(filename)==1) filename="/index.html";

	while (filename[0]== '/') filename++;
	
	/* CGI handling.  Untested */
	if (!strncmp(filename,"cgi-bin/",8))
	   {
	   par=0;
	   if ((par=strstr(filename,"?")))
	      {
	       *par=0;
	        par++;
	      } 
	   if (access(filename,X_OK)) goto conti;
	   stat (filename,&file_status);
	   if (setuid(file_status.st_uid)) return;
	   if (seteuid(file_status.st_uid)) return;
	   if (!fork())
	      {
	       close(1);
	       dup(con_sock);
	       /*printf("HTTP/1.0 200 OK\nContent-type: text/html\n\n\n");*/
	       printf("HTTP/1.0 200 OK\r\n");
	       /* Plug in environment variable, others in log_line */
	       setenv("SERVER_SOFTWARE", "FreeBSD/PicoBSD", 1);

	       execlp (filename,filename,par,(char *)0);
	      } 
	    wait(&i);
	    return;
	    }
	conti:
	if (filename == NULL) {
	  http_output(http_405[0]);
	  http_output(http_405[1]);
	  goto end_request;
	}
	/* End of CGI handling */
	
	/* Reject any request with '..' in it, bad hacker */
	c = filename;
	while (*c != '\0')
	  if (c[0] == '.' && c[1] == '.') {
	    http_output(http_404[0]);
	    http_output(http_404[1]); 
	    goto end_request;
	  } else
	    c++;
	
	/* Open filename */
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		http_output(http_404[0]);
		http_output(http_404[1]);
		goto end_request;
	}

	/* Get file status information */
	if (fstat(fd, &file_status) < 0) {
	  http_output(http_404[0]);
	  http_output(http_404[1]);
	  goto end_request2;
	}

	/* Is it a regular file? */
	if (!S_ISREG(file_status.st_mode)) {
	  http_output(http_404[0]);
	  http_output(http_404[1]);
	  goto end_request2;
	}

	/* Past this point we are serving either a GET or HEAD */
	/* Print all the header info */
	http_output(http_200);
	http_output(httpd_server_ident);
	http_date();

	sprintf(buff, "Content-length: %jd\r\n", (intmax_t)file_status.st_size);
	write(con_sock, buff, strlen(buff));

	strcpy(buff, "Content-type: ");
	type = default_mime_type;
	if ((ext = strrchr(filename, '.')) != NULL) {
	  for (i = mime_type_max; i >= 0; i--)
	    if (strcmp(ext + 1, mime_type[i][0]) == 0) {
	      type = mime_type[i][1];
	      break;
	    }
	}
	strcat(buff, type);
	http_output(buff);
	
	strftime(buff, 50, "Last-Modified: %a, %d %h %Y %H:%M:%S %Z\r\n\r\n", gmtime(&file_status.st_mtime));
	write(con_sock, buff, strlen(buff));

	/* Send data only if GET request */
	if (cmd == 1) {
	  while ((lg = read(fd, buff, 8192)) > 0)
	    write(con_sock, buff, lg);
	} 

end_request2:
	close(fd);
end_request:
	close(con_sock);

}

/*
 * Simple httpd server for use in PicoBSD or other embedded application. 
 * Should satisfy simple httpd needs.  For more demanding situations
 * apache is probably a better (but much larger) choice.
 */
int
main(int argc, char *argv[])
{
	int ch, ld;
	pid_t httpd_group = 65534;
	pid_t server_pid;

	/* Default for html directory */
	strcpy (homedir,getenv("HOME"));
	if (!geteuid())	strcpy (homedir,"/httphome");
	   else		strcat (homedir,"/httphome");

	/* Defaults for log file */
	if (geteuid()) {
	    strcpy(logfile,getenv("HOME"));
	    strcat(logfile,"/");
	    strcat(logfile,"jhttp.log");
	} else 
	  strcpy(logfile,"/var/log/jhttpd.log");

	/* Parse command line arguments */
	while ((ch = getopt(argc, argv, "d:f:g:l:p:vDh")) != -1)
	  switch (ch) {
	  case 'd':
	    strcpy(homedir,optarg);
	    break;	  
	  case 'f':
	    daemonize = 0;
	    verbose = 1;
	    fetch_mode = optarg;
	    break;
	  case 'g':
	    httpd_group = atoi(optarg);
	    break;
	  case 'l':
	    strcpy(logfile,optarg);
	    break;
	  case 'p':
	    http_port = atoi(optarg);
	    break;
	  case 'v':
	    verbose = 1;
	    break;
	  case 'D':
	    daemonize = 0;
	    break;
	  case '?':
	  case 'h':
	  default:
	    printf("usage: simple_httpd [[-d directory][-g grpid][-l logfile][-p port][-vD]]\n");
	    exit(1);
	    /* NOTREACHED */
	  }

	/* Not running as root and no port supplied, assume 1080 */
	if ((http_port == 80) && geteuid()) {
	  http_port = 1080;
	}

	/* Do we really have rights in the html directory? */
	if (fetch_mode == NULL) {
	  if (chdir(homedir)) {
	    perror("chdir");
	    puts(homedir);
	    exit(1);
	  }
	}

	/* Create log file if it doesn't exit */
	if ((access(logfile,W_OK)) && daemonize) {
	  ld = open (logfile,O_WRONLY);
	  chmod (logfile,00600);
	  close(ld);
	}

	init_servconnection();

	if (verbose) {
	  printf("Server started with options \n"); 
	  printf("port: %d\n",http_port);
	  if (fetch_mode == NULL) printf("html home: %s\n",homedir);
	  if (daemonize) printf("logfile: %s\n",logfile);
	}

	/* httpd is spawned */
	if (daemonize) {
	  if ((server_pid = fork()) != 0) {
	    wait3(0,WNOHANG,0);
	    if (verbose) printf("pid: %d\n",server_pid);
	    exit(0);
	  }
	  wait3(0,WNOHANG,0);
	}

	if (fetch_mode == NULL)
		setpgrp((pid_t)0, httpd_group);

	/* How many connections do you want? 
	 * Keep this lower than the available number of processes
	 */
	if (listen(http_sock,15) < 0) exit(1);

	label:	
	wait_connection();

	if (fork()) {
	  wait3(0,WNOHANG,0);
	  close(con_sock);
	  goto label;
	}

	http_request();

	wait3(0,WNOHANG,0);
	exit(0);
}


char *
adate(void)
{
	static char out[50];
	time_t now;
	struct tm *t;
	time(&now);
	t = localtime(&now);
	sprintf(out, "%02d:%02d:%02d %02d/%02d/%02d",
		     t->tm_hour, t->tm_min, t->tm_sec,
		     t->tm_mday, t->tm_mon+1, t->tm_year );
	return out;
}
