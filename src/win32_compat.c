#include "config.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>

/* Implementación de pread usando descriptores de archivo POSIX de Windows (int) */
ssize_t pread(int fd, void *buf, size_t count, sqfs_off_t offset) {
    /* Guardar posición actual */
    __int64 pos = _lseeki64(fd, 0, SEEK_CUR);
    if (pos == -1) return -1;

    /* Mover a la posición de lectura */
    if (_lseeki64(fd, (__int64)offset, SEEK_SET) == -1) return -1;

    /* Leer */
    int res = _read(fd, buf, (unsigned int)count);

    /* Restaurar posición */
    _lseeki64(fd, pos, SEEK_SET);
    
    return res;
}

int getuid() { return 0; }
int getgid() { return 0; }