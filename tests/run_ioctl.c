#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OUICHEFS_MAGIC 0x48434957
#define OUICHEFS_IOC_GET_EXTENTS _IO(OUICHEFS_MAGIC, 0)

int main(int argc, char *argv[]) {
  int fd;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <chemin_du_peripherique>\n", argv[0]);
    return EXIT_FAILURE;
  }

  // 1. Ouverture du fichier (ou device node)
  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "Erreur lors de l'ouverture de %s: %s\n", argv[1], strerror(errno));
    return EXIT_FAILURE;
  }

  printf("Exécution de l'ioctl APPEL_IOC (0x%lx) sur %s...\n", (unsigned long) OUICHEFS_IOC_GET_EXTENTS, argv[1]);

  // 2. Appel ioctl
  if (ioctl(fd, OUICHEFS_IOC_GET_EXTENTS) < 0) {
    fprintf(stderr, "Erreur ioctl: %s\n", strerror(errno));
    close(fd);
    return EXIT_FAILURE;
  }

  printf("Succès !\n");

  // 3. Fermeture
  close(fd);
  return EXIT_SUCCESS;
}
