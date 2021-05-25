#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>

void helper(int i) {
	printf("%d\n", i);
}

void * thread_start(void *arg) {

	helper((int) arg);
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {

	int pid = 0;
	pthread_t tid[300];
	struct timeval begin, end;

	if (argc < 1) {
		printf("./scheduling <mode>\n");
		return -1;
	}

	gettimeofday(&begin, NULL);

	for (int i = 0; i < 300; i++) {
		if (atoi(argv[1]) == 0) {
			pid = pthread_create(&tid[i], NULL, &thread_start, (void *) i);
			if (pid != 0) {
				break;
			}
		} else {
			pid = fork();
			if (pid == 0) {
				helper(i);
				break;
			}
		}
	}

	gettimeofday(&end, NULL);

	return 0;
}
