#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

/*void do_ls(const char *path)
{
	char buf[512];
	DIR *dir;
	struct dirent *dent;
	struct stat st;

	printf("> ls %s\n", path);

	if (!(dir = opendir(path))) {
		perror(path);
		return;
	}

	while ((dent = readdir(dir)) != NULL) {
		sprintf(buf, "%s/%s", path, dent->d_name);
		stat(buf, &st);
		printf("%-9d %s\n", st.st_size, dent->d_name);
	}

	closedir(dir);
}*/

int main(int argc, char *argv[])
{
	printf("\n");
	printf("Hello world Linux userspace!\n");
	printf("Our PID is: %d\n", getpid());

	execl("/bin/bash", "/bin/bash", NULL);

	while (1) sleep(2);

	/*do_ls("/");
	//do_ls("/bin");

	printf("\nWe'll try to get bash running...\n");

	int pid = fork();
	if (pid == 0) {
		printf("Child PID: %d\n", getpid());
		int ret = execlp("/bin/bash", "/bin/bash", NULL);
		printf("execlp returned %d\n", ret);
		if (ret < 0) {
			perror("execlp");
		}
	} else if (pid > 0) {
		printf("Parent PID: %d\n", getpid());
	} else {
		printf("fork() error\n");
	}

	printf("Infinite loop...\n");

	while (1)
		sleep(1);*/

	return 0;
}
