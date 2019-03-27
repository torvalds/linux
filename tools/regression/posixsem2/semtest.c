/* $FreeBSD$ */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define SEM_NAME "/semtst"

int test_unnamed(void);
int test_named(void);
int test_named2(void);

int
test_unnamed(void)
{
	sem_t *s;
	pid_t pid;
	int status;

	printf("testing unnamed process-shared semaphore\n");
	s = (sem_t *)mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED,
		-1, 0);
	if (s == MAP_FAILED)
		err(1, "mmap failed");
	if (sem_init(s, 1, 0))
		err(2, "sem_init failed");
	if ((pid = fork()) == 0) {
		printf("child: sem_wait()\n");
		if (sem_wait(s))
			err(3, "sem_wait failed");
		printf("child: sem_wait() returned\n");
		exit(0);
	} else {
		sleep(1);
		printf("parent: sem_post()\n");
		if (sem_post(s))
			err(4, "sem_post failed");
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			printf("OK.\n");
		else
			printf("Failure.");
	}
	return (0);
}

int
test_named(void)
{
	sem_t *s, *s2;
	pid_t pid;
	int status;

	printf("testing named process-shared semaphore\n");
	sem_unlink(SEM_NAME);
	s = sem_open(SEM_NAME, O_CREAT, 0777, 0);
	if (s == SEM_FAILED)
		err(1, "sem_open failed");
	s2 = sem_open(SEM_NAME, O_CREAT, 0777, 0);
	if (s2 == SEM_FAILED)
		err(2, "second sem_open call failed");
	if (s != s2)
		errx(3,
"two sem_open calls for same semaphore do not return same address");
	if (sem_close(s2))
		err(4, "sem_close failed");
	if ((pid = fork()) == 0) {
		printf("child: sem_wait()\n");
		if (sem_wait(s))
			err(5, "sem_wait failed");
		printf("child: sem_wait() returned\n");
		exit(0);
	} else {
		sleep(1);
		printf("parent: sem_post()\n");
		if (sem_post(s))
			err(6, "sem_post failed");
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			printf("OK.\n");
		else
			printf("Failure.");
	}

	if (sem_close(s))
		err(7, "sem_close failed");
	
	return (0);
}

int
test_named2(void)
{
	sem_t *s, *s2, *s3;

	printf("testing named process-shared semaphore, O_EXCL cases\n");
	sem_unlink(SEM_NAME);
	s = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0777, 0);
	if (s == SEM_FAILED)
		err(1, "sem_open failed");
	s2 = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0777, 0);
	if (s2 != SEM_FAILED)
		errx(2, "second sem_open call wrongly succeeded");
	if (errno != EEXIST)
		err(3, "second sem_open call failed with wrong errno");

	s3 = sem_open(SEM_NAME, 0);
	if (s3 == SEM_FAILED)
		err(4, "third sem_open call failed");
	if (s != s3)
		errx(5,
"two sem_open calls for same semaphore do not return same address");
	if (sem_close(s3))
		err(6, "sem_close failed");

	if (sem_close(s))
		err(7, "sem_close failed");
	
	printf("OK.\n");
	return (0);
}

int
main(void)
{
	test_unnamed();
	test_named();
	test_named2();
	return (0);
}
