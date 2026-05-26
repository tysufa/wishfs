#include <string.h> //strlen
#include <stdio.h> //fprintf, stdout
#include <stdlib.h> //exit
#include <pthread.h> //thread
#include <sys/ioctl.h> //ioctl

#include "test.h"
#include "ouichefs_ioctl.h"

#define NBTHREADS 2
#define NBREP 5

void* write_several_small_blocks(void *arg){

	pthread_t id_thread = pthread_self();
	char *file = (char*)arg;
	ssize_t res;

	fprintf(stdout, "Thread nb %ld started, output in %s.\n", (unsigned long)id_thread, file);

	//create empty file
	create_file(file);

	//write 4KB each time
	char *s_write = s4KB("a");
	ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	for(size_t i=0; i<NBREP; i++){
		fprintf(stdout, "[thread %ld] -- writes block %ld.\n", (unsigned long)id_thread, i);
		res = measure_write(fd_write, s_write, len_write);
	}

	manage_error(ioctl(fd_write, OUICHEFS_IOC_GET_EXTENTS, 0), "write_several_small_blocks", "ioctl");

	close_file(fd_write);
	return NULL;
}

int main(int argc, char* argv[]){

	fprintf(stdout, "=== TEST SUITE multithread ===\n");

	const char generic_name[] = "multi_%ld";
	pthread_t threads[NBTHREADS];
	char arg[NBTHREADS][20];

	for(size_t i=0; i<NBTHREADS; i++){
		threads[i] = i;
		sprintf(arg[i], generic_name, (unsigned long)threads[i]);
		pthread_create(&threads[i], NULL, write_several_small_blocks, (void*)arg[i]);
	}

	for(size_t i=0; i<NBTHREADS; i++)
		pthread_join(threads[i], NULL);

	return EXIT_SUCCESS;
}
