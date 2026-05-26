#include <unistd.h> //read, llseek
#include <string.h> //strlen
#include <stdio.h> //fprintf, stdout
#include <stdlib.h> //exit

#include "test.h"

void read_write_simple(const char *file){

	fprintf(stdout, "--- Test writing in a simple case, and reading too much bytes. ---\n");

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

void read_write_several_blocks(const char *file){
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
	fprintf(stdout, "--- Test writing in the middle of a simple file. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a") s8B("b") s8B("c"));

	//tests on reads & writes
	const size_t len_read = 9;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	ssize_t res;
	const int fd = open_file(file, O_RDWR);

	//- read a
	res = measure_read(fd, s_read, len_read-1);
	if (failed(res == len_read-1)
		|| failed(strcmp(s_read, s8B("a")) == 0)
	)
		fprintf(stdout, "Should read 8x'a'. Read: '%s'.\n", s_read);

		//- write B on b
		const char *s_write = s8B("B");
		const ssize_t len_write = strlen(s_write);

		res = measure_write(fd, s_write, len_write);
		if (failed(res == len_write))
			fprintf(stdout, "Should write 8 Bytes.\n");

	//- read c
	const ssize_t res_write = measure_read(fd, s_read, len_read-1);
	if (failed(res == len_read-1)
		|| failed(strcmp(s_read, s8B("c")) == 0)
	)
		fprintf(stdout, "Should read 8x'c'. Read: '%s'.\n", s_read);

		close_file(fd);

		//check final state
		__compare_file_to(file, s8B("a") s8B("B") s8B("c"));
}

void write_middle_several_blocks(const char* file){
	fprintf(stdout, "--- Test writing in the middle of a file, over several blocks. ---\n");

	ssize_t res;

	//initialize file
	__initialize_file_to(file, s4KB("a") s4KB("a") s4KB("a")); //12KB

	//test writing over several blocks
	const int fd = open_file(file, O_RDWR);

	//seek position to 2KB
	const size_t len_read = v1KB*2+1;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	res = measure_read(fd, s_read, len_read-1);
	if (failed(res == len_read-1))
		fprintf(stdout, "Should read 2KB = 2048 Bytes.\n");

	//write 8KB (1/2 1st block, 1 2nd block, 1/2 3rd block)
	const char *s_write = s4KB("b") s4KB("b");
	const ssize_t len_write = strlen(s_write);

	res = measure_write(fd, s_write, len_write);
	if (failed(res == len_write))
		fprintf(stdout, "Should write 8KB = 8192 Bytes.\n");

	close_file(fd);

	//check final state
	//2KB x 'a' + 2KB x 'b' + 4KB x 'b' + 2KB x 'b' + 2KB x 'a'
	__compare_file_to(file, s1KB("a") s1KB("a") s4KB("b") s4KB("b") s1KB("a") s1KB("a"));
}

void write_at_end_append(const char* file){
	fprintf(stdout, "--- Test writing at the end of a file using O_APPEND. ---\n");

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
	fprintf(stdout, "--- Test writing at the end of a file NOT using O_APPEND. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a"));

	//write with append flag
	const char *s_write = s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd_write, 0, SEEK_END); //set offset at the end
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	__compare_file_to(file, s8B("a") s8B("b"));
}

void write_over_end(const char *file){
	fprintf(stdout, "--- Test writing before and over the end. ---\n");

	//initialize file
	__initialize_file_to(file, s8B("a") s8B("a"));

	//write with append flag
	const char *s_write = s8B("b") s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd_write, 8, SEEK_SET);
	const ssize_t res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	__compare_file_to(file, s8B("a") s8B("b") s8B("b"));
}

void create_sparse(const char *file){
	fprintf(stdout, "--- Test creating a hole in a file. ---\n");

	ssize_t res;

	//initialize file
	__initialize_file_to(file, s8B("a"));

	//write with append flag
	const char *s_write = s8B("b");
	const ssize_t len_write = strlen(s_write);

	const int fd_write = open_file(file, O_WRONLY);
	lseek(fd_write, 16, SEEK_SET);
	res = measure_write(fd_write, s_write, len_write);
	close_file(fd_write);

	//check final state
	const ssize_t len_read = 25;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	//expecting s8B("a") s8B("\0") s8B("b")
	const int fd_final = open_file(file, O_RDONLY);
	res = measure_read(fd_final, s_read, len_read);
	close_file(fd_final);

	//check read
	if (failed(res == len_read-1))
		fprintf(stdout, "Should read 24B. Read: '%s'.\n", s_read);

	//check content
	size_t i = 0;
	for (; i<8; i++){
		if (failed(s_read[i] == 'a'))
			fprintf(stdout, "Should read 'a'. Read: '%c'.\n", s_read[i]);
	}
	for (; i<16; i++){
		if (failed(s_read[i] == '\0'))
			fprintf(stdout, "Should read '\\0'. Read: '%d' (char interpreted as int).\n", s_read[i]);
	}
	for (; i<24; i++){
		if (failed(s_read[i] == 'b'))
			fprintf(stdout, "Should read 'b'. Read: '%c'.\n", s_read[i]);
	}
}

void write_sparse(const char *file){
	fprintf(stdout, "--- Writing in a hole in a file. ---\n");

	ssize_t res;
	char *s_write;
	ssize_t len_write;

	//write begin
	__initialize_file_to(file, s1KB("a") s1KB("a"));

	const int fd_write = open_file(file, O_APPEND); //position 2KB

	//create hole
	lseek(fd_write, v1KB * 4, SEEK_CUR); //6KB
	lseek(fd_write, v1KB * 4, SEEK_CUR); //10KB
	lseek(fd_write, v1KB * 4, SEEK_CUR); //14KB

	//write (make sparse)
	s_write = s1KB("d") s1KB("d");
	len_write = strlen(s_write);
	res = measure_write(fd_write, s_write, len_write); //16KB

	//write in the middle of the hole (over 1 whole block)
	lseek(fd_write, v1KB * 6, SEEK_SET); //6KB
	s_write = s4KB("c") s4KB("c");
	len_write = strlen(s_write);
	res = measure_write(fd_write, s_write, len_write); //14KB

	//write at the beginning of the hole
	lseek(fd_write, v1KB * 2, SEEK_SET); //2KB
	s_write = s4KB("b");
	len_write = strlen(s_write);
	res = measure_write(fd_write, s_write, len_write); //6KB

	close_file(fd_write);

	__compare_file_to(file, s1KB("a") s1KB("a") s4KB("b") s4KB("c") s4KB("c") s1KB("d") s1KB("d"));
}

void read_trunc_several_blocks(const char *file){
	fprintf(stdout, "--- Test with O_TRUNC option on several blocks. ---\n");

	//initialize file
	__initialize_file_to(file, s4KB("a") s4KB("a"));

	//read with trunc flag
	const size_t len_read = v1KB*2 + 1;
	char s_read[len_read];
	__init_buffer_with_zeroes(s_read, len_read);

	const int fd_read = open_file(file, O_RDONLY | O_TRUNC);
	const ssize_t res = measure_read(fd_read, s_read, len_read);
	close_file(fd_read);

	if (failed(res == 0))
		fprintf(stdout, "Should read nothing (empty file).\n");
}

int main(int argc, char* argv[]){

	fprintf(stdout, "=== TEST SUITE monothread ===\n");

	/** Writes then reads some Bytes. Tries to read larger than file size. */
	run_test(read_write_simple, PATH "simple");

	/** Writes then reads 16KB. */
	run_test(read_write_several_blocks, PATH "several_blocks");

	/** Writes >4MB, more than max size. */
	run_test(write_too_big, PATH "too_big.txt");

	/** Write in the middle of a 24B file. Then read whole file. */
	run_test(write_middle_simple, PATH "mid_simple");

	/** Write in the middle of a 12KB file. Then read whole file. */
	run_test(write_middle_several_blocks, PATH "mid_blks");

	/** Write at the end of a file by using O_APPEND. */
	run_test(write_at_end_append, PATH "end_append");

	/** Write at the end of a file without using O_APPEND. */
	run_test(write_at_end_no_append, PATH "end_no_append");

	/** Write before the end, but long enough to write over the end of the file. */
	run_test(write_over_end, PATH "over_end");

	/** Write after the end of a file. Read in the hole. */
	run_test(create_sparse, PATH "create_sparse");

	/** Write in the middle of a hole, over a block, then at the beginning of the hole to fill the file again. */
	run_test(write_sparse, PATH "write_sparse");

	/** Write in a file then open it with O_TRUNC. */
	run_test(read_trunc_several_blocks, PATH "trunc");

	return EXIT_SUCCESS;
}
