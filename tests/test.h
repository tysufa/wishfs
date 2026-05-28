#include <ctype.h>
#include <errno.h>   //errno
#include <fcntl.h>   //open
#include <stdbool.h> //bool
#include <stddef.h>
#include <stdio.h>  //fprintf, stdout
#include <string.h> //strlen
#include <time.h>   //clock
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

void print_diff_hybrid(
  const char          *label_a,
  const unsigned char *a,
  const char          *label_b,
  const unsigned char *b,
  size_t               len
);

void manage_error(int no, const char *fname, const char *fdescr) {
  if (no < 0) {
    const char *err_descr = strerror(errno);
    fprintf(stdout, "Error in %s:%s: %s\n", fname, fdescr, err_descr);
  }
}

void check_test(bool expr, char *err_msg) {
  if (!expr)
    fprintf(stdout, "Test incorrect: %s.\n", err_msg);
}

bool failed(bool expr) {
  if (expr)
    fprintf(stdout, "Test succeeded\n");
  else
    fprintf(stdout, "Test failed\n");
  return !expr;
}

#define open_file(_path, _flags)        \
  ({                                    \
    const int fd = open(_path, _flags); \
    manage_error(fd, __func__, "open"); \
    fd;                                 \
  })

#define close_file(_fd)                   \
  do {                                    \
    const int ret = close(_fd);           \
    manage_error(ret, __func__, "close"); \
  } while (0)

#define create_file(_path)                                                     \
  do {                                                                         \
    const int fd = open(_path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR); \
    manage_error(fd, __func__, "open");                                        \
    close_file(fd);                                                            \
  } while (0)

#define _measure_op(_fd, _op, _sop, _s, _len)                            \
  ({                                                                     \
    ssize_t       res    = 1;                                            \
    ssize_t       offset = 0;                                            \
    const clock_t tstart = clock();                                      \
    while (res > 0) {                                                    \
      res = (_op) ((_fd), (_s) + offset, (_len) - offset);               \
      manage_error(res, __func__, _sop);                                 \
      offset += res;                                                     \
    }                                                                    \
    const clock_t tend  = clock();                                       \
    const clock_t tdiff = (float) (tend - tstart);                       \
    const size_t  nb_op = res < 0 ? res : offset;                        \
    fprintf(stdout, "- " _sop ": %ld bytes, time: %d.\n", nb_op, tdiff); \
    nb_op;                                                               \
  })

#define measure_write(_fd, _s, _len) ({ _measure_op(_fd, write, "write", _s, _len); })

#define measure_read(_fd, _s, _len) ({ _measure_op(_fd, read, "read", _s, _len); })

void __init_buffer_with_zeroes(char buf[], size_t len) {
  bzero(buf, len);
}

void __initialize_file_to(const char *file, const char *s_init) {
  const ssize_t len_init = strlen(s_init);

  const int     fd_init = open_file(file, O_WRONLY);
  const ssize_t res     = measure_write(fd_init, s_init, len_init);
  close_file(fd_init);
}

void __compare_file_to(const char *file, const char *s_final, ssize_t len_final) {
  const size_t len_read = len_final + 100;

  char s_read[len_read];
  bzero(s_read, len_read);

  const int     fd_final = open_file(file, O_RDONLY);
  const ssize_t res      = measure_read(fd_final, s_read, len_read);
  close_file(fd_final);
  if (failed(res == len_final))
    printf("Read %d while expecting %d\n", res, len_final);
  if (failed(memcmp(s_read, s_final, len_final) == 0))
    // fprintf(stdout, "Should read '%s'. Read: '%s'.\n", s_final, s_read);
    print_diff_hybrid("Should read", s_final, "Did read", s_read, len_final);
}

void print_diff_hybrid(
  const char          *label_a,
  const unsigned char *a,
  const char          *label_b,
  const unsigned char *b,
  size_t               len
) {
  printf("Offset | d | %-27s | %-26s\n", label_a, label_b);
  printf("-------|---|-----------------------------|----------------------------\n");

  for (size_t i = 0; i < len; i += 8) {
    size_t chunk     = ((len - i) < 8) ? (len - i) : 8;
    int    line_diff = 0;

    // 1. Détection d'une différence sur la ligne entière
    for (size_t j = 0; j < chunk; j++) {
      if (a[i + j] != b[i + j]) {
        line_diff = 1;
        break;
      }
    }

    // 2. Affichage de l'offset et du marqueur
    printf("%06zx | %s | ", i, line_diff ? "X" : " ");

    // 3. Affichage Hexa + Char pour Buffer A
    for (size_t j = 0; j < 8; j++)
      if (j < chunk)
        printf("%02x", a[i + j]);
      else
        printf("  "); // Padding
    printf(" [");
    for (size_t j = 0; j < chunk; j++)
      printf("%c", isprint(a[i + j]) ? a[i + j] : '.');
    printf("] | ");

    // 4. Affichage Hexa + Char pour Buffer B
    for (size_t j = 0; j < 8; j++)
      if (j < chunk)
        printf("%02x", b[i + j]);
      else
        printf("  "); // Padding
    printf(" [");
    for (size_t j = 0; j < chunk; j++)
      printf("%c", isprint(b[i + j]) ? b[i + j] : '.');
    printf("]\n");
  }
}

void run_test(void (*func)(const char *), char *path){
	const size_t len = 7;
	int fd;


	//get number of free blocks
	char s1[len];
	__init_buffer_with_zeroes(s1, len);
	fd = open_file("/sys/fs/ouichefs/partition_sdc/free_blocks", O_RDONLY);
	measure_read(fd, s1, len);


	//execute function on file
	create_file(path);
	func(path);
	const int no = unlink(path);
	manage_error(no, "run_test", "unlink");


	//get number of free blocks
	char s2[len];
	__init_buffer_with_zeroes(s2, len);
	fd = open_file("/sys/fs/ouichefs/sdc/free_blocks", O_RDONLY);
	measure_read(fd, s2, len);

	//compare results
	fprintf(stdout, "Comparing number of free blocks: ");
	__compare_file_to(s1, s2, len);


	//TODO: sanity check
}
