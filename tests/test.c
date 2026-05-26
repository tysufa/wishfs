#include <fcntl.h> //open
#include <unistd.h> //read, llseek
#include <string.h> //strlen
#include <stdio.h> //fprintf, stdout
#include <stdlib.h> //exit

#include <linux/ioctl.h> //ioctl

#include "test.h"

void read_write_simple(const char *file){

	fprintf(stdout, "--- Test write on a simple case, and read too much bytes. ---\n");

	const char *s_write = "Hi, this is a test.";
	const ssize_t len_write = strlen(s_write);

	const size_t len_read = v1KB;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	check_test(len_write == 19, "len_write must be 19B");

	//write
	const int fd_write = open_file(file, O_WRONLY);
	const ssize_t res_write = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);
	//tests
	if (failed(res_write == len_write))
		fprintf(stdout, "Wrote '%s'.\n", s_write);

	//read
	const int fd_read = open_file(file, O_RDONLY);
	const ssize_t res_read = measure_read(fd_read, s_read, len_read);
	close_file(fd_read);
	//tests
	if (failed(res_read == len_write)
		|| failed(strcmp(s_write, s_read) == 0)
	)
		fprintf(stdout, "Should read '%s'. Read: '%s'.\n", s_write, s_read);
}

void read_write_several_blocs(char *file){
	fprintf(stdout, "--- Test read/write on several blocks. ---\n");

	const char *s_write = s4KB("a") s4KB("b") s4KB("c") s4KB("d"); //total 16KB
	const ssize_t len_write = strlen(s_write);

	const size_t len_read = (v1KB << 2 << 2) + 100;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	check_test(len_write == 16384, "len_write must be 16KB");

	//write
	const int fd_write = open_file(file, O_WRONLY);
	const ssize_t res_write = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);
	//tests
	if (failed(res_write == len_write))
		fprintf(stdout, "Should write 16384 Bytes.\n", s_write);

	//read
	const int fd_read = open_file(file, O_RDONLY);
	const ssize_t res_read = measure_read(fd_read, s_read, len_read);
	close_file(fd_read);
	//tests
	if (failed(res_read == len_write)
		|| failed(s_read[0] == 'a')
		|| failed(s_read[v1KB << 2] == 'b')
		|| failed(s_read[(v1KB << 2)*2] == 'c')
		|| failed(s_read[(v1KB << 2)*3] == 'd')
	)
		fprintf(stdout, "Should read 1KB of 'a', 'b', 'c', and 'd' each. Read '%s'.\n", s_read);
}

void write_too_big(const char* file){
	fprintf(stdout, "--- Test >4MB write. ---\n");

	const char *s_write = s4MB("a") s32B("b"); //total 16KB
	const ssize_t len_write = strlen(s_write);

	check_test(len_write == 4194304 + 32, "len_write must be 4MB+32B");

	//write
	fprintf(stdout, "This test must provoke an error or possibly write only 4MB out of 4MB and 32B\n    -> ");
	const int fd_write = open_file(file, O_WRONLY);
	const ssize_t res_write = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);
}

void write_middle_simple(const char* file){
	fprintf(stdout, "--- Test write in the middle of a simple file. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a") s8B("b") s8B("c"));

	//tests on reads & writes
	const char *s_write = s8B("B");
	const ssize_t len_write = strlen(s_write);

	const size_t len_read = 8;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	ssize_t res;
	const int fd = open_file(file, O_RDWR);

	//- read a
	res = measure_read(fd, s_read, len_read);
	if (failed(res == len_read)
		|| failed(strcmp(s_read, s8B("a")) == 0)
	)
		fprintf(stdout, "Should read 8x'a'. Read: '%s'.\n", s_read);

		//- write B on b
		res = measure_write(fd, s_write, len_write);
		if (failed(res == len_write))
			fprintf(stdout, "Should write 8 Bytes.\n");

	//- read c
	const ssize_t res_write = measure_read(fd, s_read, len_read);
	if (failed(res == len_read)
		|| failed(strcmp(s_read, s8B("c")) == 0)
	)
		fprintf(stdout, "Should read 8x'c'. Read: '%s'.\n", s_read);

		close_file(fd);

		//check final state
		__compare_file_to(file, s8B("a") s8B("B") s8B("c"));
}

void write_middle_several_blocs(const char* file){
	fprintf(stdout, "--- Test write in the middle of a file, over several blocks, using seek. ---\n");

	//initialize file
	__initialize_file_to(file, s4KB("a") s4KB("a") s4KB("a")); //12KB

	//test writing over several blocks
	const char *s_write = s4KB("b") s4KB("b");
	const ssize_t len_write = strlen(s_write);

	const int fd = open_file(file, O_WRONLY);

	//seek position to 2KB
	lseek(fd, v1KB*2, SEEK_SET);

	//write 8KB (1/2 1st block, 1 2nd block, 1/2 3rd block)
	const ssize_t res = measure_write(fd, s_write, len_write);
	if (failed(res == len_write))
		fprintf(stdout, "Should write 8KB = 8388608 Bytes.\n");

	close_file(fd);

	//check final state
	//2KB x 'a' + 2KB x 'b' + 4KB x 'b' + 2KB x 'b' + 2KB x 'a'
	__compare_file_to(file, s1KB("a") s1KB("a") s4KB("b") s4KB("b") s1KB("a") s1KB("a"));
}

void write_at_end_append(const char* file){
	fprintf(stdout, "--- Test write at the end of a file using O_APPEND. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a"));

	//write with append flag
	const char *s_write = s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY | O_APPEND); //new open, append flag
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	__compare_file_to(file, s8B("a") s8B("b"));
}

void write_at_end_no_append(const char* file){
	fprintf(stdout, "--- Test write at the end of a file NOT using O_APPEND. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a"));

	//write with append flag
	const char *s_write = s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd, 0, SEEK_END); //set offset at the end
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	__compare_file_to(file, s8B("a") s8B("b"));
}

void write_over_end(const char* file){
	fprintf(stdout, "--- Test write a bit before the end and write over it. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a") s8B("a"));

	//write with append flag
	const char *s_write = s8B("b") s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd, 8, SEEK_SET);
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	__compare_file_to(file, s8B("a") s8B("b") s8B("b"));
}

void write_after_end(const char* file){
	fprintf(stdout, "--- Test creating a hole in a file. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a"));

	//write with append flag
	const char *s_write = s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd, 16, SEEK_SET);
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	const ssize_t len_read = 25;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	//expecting s8B("a") s8B("\0") s8B("b")
	const int fd_final = open_file(file, O_RDONLY);
	const ssize_t res = measure_read(fd_final, s_read, len_read);
	close_file(fd_final);

	//check read
	if (failed(res == 24))
		fprintf(stdout, "Should read '%s'. Read: '%s'.\n", s_final, s_read);

	//check content
	for (size_t i=0; i<8; i++){
		if (failed(s_read[i] == 'a'))
			fprintf(stdout, "Should read 'a'. Read: '%c'.\n", s_read[i]);
	}
	for (size_t i=0; i<8; i++){
		if (failed(s_read[i] == '\0'))
			fprintf(stdout, "Should read '\0'. Read: '%c'.\n", s_read[i]);
	}
	for (size_t i=0; i<8; i++){
		if (failed(s_read[i] == 'b'))
			fprintf(stdout, "Should read 'b'. Read: '%c'.\n", s_read[i]);
	}
}

int main(int argc, char* argv[]){

	const char *path = "/mnt/test_mnt/test_"

	//simple read-write
	run_test(read_write_simple, path + "read_write_simple");

	//read 4 blocks of 1KB
	run_test(read_write_several_blocs, path + "read_write_several_blocs");

	//write >4MB in a file
	run_test(write_too_big, path + "write_too_big");

	//read/write in the middle of a file, check offset
	run_test(write_middle_simple, path + "write_middle_simple");

	//describe //TODO
	run_test(write_middle_several_blocs, path + "write_middle_several_blocs");

	//describe //TODO
	run_test(write_at_end_append, path + "write_at_end_append");

	//describe //TODO
	run_test(write_at_end_no_append, path + "write_at_end_no_append");

	//describe //TODO
	run_test(write_over_end, path + "write_over_end");

	//describe //TODO
	run_test(write_after_end, path + "write_after_end");

	return EXIT_SUCCESS;
}
