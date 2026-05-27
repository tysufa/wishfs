#include <errno.h> //errno
#include <stdio.h> //fprintf, stdout
#include <stdbool.h> //bool
#include <string.h> //strlen
#include <time.h> //clock
#include <fcntl.h> //open
#include <unistd.h> //read, llseek

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

#define v1KB (1 << 10)
#define v1MB (1 << 20)

#define PATH "/mnt/test_mnt/t_"

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
	const int fd = open(_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR); \
	manage_error(fd, __func__, "open"); \
	close_file(fd); \
} while(0)

#define _measure_op(_fd, _op, _sop, _s, _len) \
({ \
	ssize_t res = 1; \
	ssize_t offset = 0; \
	const clock_t tstart = clock(); \
	while (res > 0){ \
		res = (_op)((_fd), (_s) + offset, (_len) - offset); \
		manage_error(res, __func__, _sop); \
		offset += res; \
	} \
	const clock_t tend = clock(); \
	const clock_t tdiff = (float)(tend - tstart); \
	const size_t nb_op = res < 0 ? res : offset; \
	fprintf(stdout, "- " _sop ": %ld bytes, time: %d.\n", nb_op, tdiff); \
	nb_op; \
})

#define measure_write(_fd, _s, _len) \
({ \
	_measure_op(_fd, write, "write", _s, _len); \
})

#define measure_read(_fd, _s, _len) \
({ \
	_measure_op(_fd, read, "read", _s, _len); \
})

void __init_buffer_with_zeroes(char buf[], size_t len){
  bzero(buf, len);
}

void __initialize_file_to(const char *file, const char* s_init){
	const ssize_t len_init = strlen(s_init);

	const int fd_init = open_file(file, O_WRONLY);
	const ssize_t res = measure_write(fd_init, s_init, len_init);
	close_file(fd_init);
}

void __compare_file_to(const char *file, const char* s_final){
	const ssize_t len_final = strlen(s_final);
	const size_t len_read = len_final + 100;

	char s_read[len_read];
	bzero(s_read, len_read);

	const int fd_final = open_file(file, O_RDONLY);
	const ssize_t res = measure_read(fd_final, s_read, len_read);
	close_file(fd_final);
	if (failed(res == len_final)
		|| failed(memcmp(s_read, s_final, len_final) == 0)
	)
		fprintf(stdout, "Should read '%s'. Read: '%s'.\n", s_final, s_read);
}

void print_diff_hybrid(const char *label_a, const unsigned char *a,
                       const char *label_b, const unsigned char *b, size_t len) {
    printf("Index | Diff | %-20s | %-20s\n", label_a, label_b);
    printf("------------------------------------------------------------------\n");

    for (size_t i = 0; i < len; i++) {
        // Détection de la différence
        char diff_marker = (a[i] == b[i]) ? ' ' : 'X';

        // Formatage pour la colonne A
        char hex_a[10], char_a;
        sprintf(hex_a, "%02x", a[i]);
        char_a = isprint(a[i]) ? a[i] : '.';

        // Formatage pour la colonne B
        char hex_b[10], char_b;
        sprintf(hex_b, "%02x", b[i]);
        char_b = isprint(b[i]) ? b[i] : '.';

        // Affichage de la ligne
        printf("%05zu |  %c   | 0x%s [%c]          | 0x%s [%c]\n",
               i, diff_marker, hex_a, char_a, hex_b, char_b);
    }
}

void run_test(void (*func)(const char *), char *path){
	//stocker free blocks (1) //TODO
	create_file(path);
	func(path);
	//supprimer fichier //TODO
	//comparer nombre free blocks avec (1) //TODO
	//sanity check


}
