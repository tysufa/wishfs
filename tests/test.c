#include <fcntl.h> //open
#include <unistd.h> //read
#include <time.h> //clock
#include <string.h> //strlen
#include <stdio.h> //stdout
#include <errno.h> //errno
#include <stdlib.h> //exit
#include <stdbool.h> //bool

#define s8B(c) c c c c c c c c
#define s32B(c) s8B(c) s8B(c) s8B(c) s8B(c)
#define s128B(c) s32B(c) s32B(c) s32B(c) s32B(c)

#define s1KB(c) s128B(c) s128B(c) s128B(c) s128B(c) s128B(c) s128B(c) s128B(c) s128B(c)
#define s4KB(c) s1KB(c) s1KB(c) s1KB(c) s1KB(c)
#define s8KB(c) s4KB(c) s4KB(c)
#define s32KB(c) s8KB(c) s8KB(c) s8KB(c) s8KB(c)
#define s128KB(c) s32KB(c) s32KB(c) s32KB(c) s32KB(c)

#define s1MB(c) s128KB(c) s128KB(c) s128KB(c) s128KB(c) s128KB(c) s128KB(c) s128KB(c) s128KB(c)
#define s4MB(c) s1MB(c) s1MB(c) s1MB(c) s1MB(c)

#define v1KB 1 << 10
#define v1MB 1 << 20

void manage_error(int no, const char *fname, const char *fdescr){
	if (no < 0){
		const char *err_descr = strerror(errno);
		fprintf(stdout, "Error in %s:%s: %s\n", fname, fdescr, err_descr);
	}
}

void check_test(bool expr, char *err_msg){
	if (!expr){
		fprintf(stdout, "Test incorrect: %s.\n", err_msg);
	}
}

bool failed(bool expr){
	if (expr)
		fprintf(stdout, "Test succeeded\n");
	else
		fprintf(stdout, "Test failed\n");
	return !expr;
}

#define open_file(_path, _flags) \
({ \
	const int fd = open(_path, _flags); \
	manage_error(fd, __func__, "open"); \
	fd; \
})

#define close_file(_fd) \
do { \
	const int ret = close(_fd); \
	manage_error(ret, __func__, "close"); \
} while(0)

#define create_file(_path) \
do { \
	const int fd = open(_path, O_CREAT | O_TRUNC | O_RDWR, 600); \
	manage_error(fd, __func__, "open"); \
	close_file(fd); \
} while(0)

#define _measure_op(_fd, _op, _sop, _s, _len) \
({ \
	const clock_t tstart = clock(); \
	const ssize_t count = (_op)(_fd, _s, _len); \
	const clock_t tend = clock(); \
	const clock_t tdiff = (float)(tend - tstart); \
	manage_error(count, __func__, _sop); \
	fprintf(stdout, "- " _sop ": %ld bytes, time: %d.\n", count, tdiff); \
	count; \
})

#define measure_write(_fd, _s, _len) \
({ \
	_measure_op(_fd, write, "write", _s, _len); \
})

#define measure_read(_fd, _s, _len) \
({ \
	_measure_op(_fd, read, "read", _s, _len); \
})

void read_write_simple(const char *file){

	fprintf(stdout, "--- Test write on a simple case, and read too much bytes. ---\n");

	const char *s_write = "Hi, this is a test.";
	const ssize_t len_write = strlen(s_write);

	char s_read[v1KB] = {0};
	const size_t len_read = v1KB;

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

	char s_read[(v1KB << 4) + 100] = {0};
	const size_t len_read = (v1KB << 4) + 100;

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
		|| failed(s_read[v1KB << 4] == 'b')
		|| failed(s_read[(v1KB << 4)*2] == 'c')
		|| failed(s_read[(v1KB << 4)*3] == 'd')
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

void write_middle_small(const char* file){
	fprintf(stdout, "--- Test write in the middle of a small file. ---\n");

	const char *s_init = s8B("a") s8B("b") s8B("c");
	const ssize_t len_init = strlen(s_init);

	const char *s_write = s8B("B");
	const ssize_t len_write = strlen(s_write);

	const char *s_final = s8B("a") s8B("B") s8B("c");
	const ssize_t len_final = strlen(s_final);

	char s_read[100] = {0};
	const size_t len_read = 8;

	ssize_t res;

	check_test(len_init == len_read*3, "len_write must be 8Bx3");
	check_test(len_final == len_read*3, "len_write must be 8Bx3");
	check_test(len_write == len_read, "len_write must be 8B");

	//initialize file
	const int fd_init = open_file(file, O_WRONLY);
	res = measure_write(fd_init, s_init, len_init);
	close_file(fd_init);

	//tests on read & write
	const int fd = open_file(file, O_RDWR);

	//read a
	res = measure_read(fd, s_read, len_read);
	if (failed(res == len_read)
		|| failed(strcmp(s_read, s8B("a")) == 0)
	)
		fprintf(stdout, "Should read 8x'a'. Read: '%s'.\n", s_read);

	//write on b
	res = measure_write(fd, s_write, len_write);
	if (failed(res == len_write))
		fprintf(stdout, "Should write 8 Bytes.\n");

	//read c
	const ssize_t res_write = measure_read(fd, s_read, len_read);
	if (failed(res == len_read)
		|| failed(strcmp(s_read, s8B("c")) == 0)
	)
		fprintf(stdout, "Should read 8x'c'. Read: '%s'.\n", s_read);

	close_file(fd);

	//check final state
	const int fd_final = open_file(file, O_RDONLY);
	res = measure_read(fd_final, s_read, len_final);
	close_file(fd_final);
	if (failed(res == len_final)
		|| failed(strcmp(s_read, s_final) == 0)
	)
		fprintf(stdout, "Should read '%s'. Read: '%s'.\n", s_final, s_read);
}

int main(int argc, char* argv[]){

	char* fname;
	//simple read-write
	fname = "/mnt/test_mnt/test11_simple_file";
	create_file(fname);
	read_write_simple(fname);

	//read 4 blocks of 1KB
	fname = "/mnt/test_mnt/test11_4KB_file";
	create_file(fname);
	read_write_several_blocs(fname);

	//write >4MB in a file
	fname = "/mnt/test_mnt/test11_4M32B_file";
	create_file(fname);
	write_too_big(fname);

	//read/write in the middle of a file, check offset
	fname = "/mnt/test_mnt/test11_middle_small_file";
	create_file(fname);
	write_middle_small(fname);

	return EXIT_SUCCESS;
}
