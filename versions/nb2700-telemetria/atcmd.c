/*
 * atcmd.c - Utilidad AT serial para diagnóstico del módem u-blox
 * Cross-compile: powerpc-openwrt-linux-gcc -static -Os -o atcmd atcmd.c
 *
 * Uso: atcmd <puerto> <comando_AT> [tiempo_espera_s]
 * Ej:  atcmd /dev/ttyACM0 "AT+CFUN=1" 10
 *      atcmd /dev/ttyACM0 "+++" 3      (escape de modo datos)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>

#define BUF_SIZE 4096

int main(int argc, char *argv[])
{
    int fd, n, i, total = 0;
    int wait_s = 5;
    char buf[BUF_SIZE];
    struct termios tio;
    fd_set rfds;
    struct timeval tv;
    int send_crlf = 1;

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <puerto> <comando> [espera_s] [crlf=0|1]\n", argv[0]);
        return 1;
    }
    if (argc >= 4) wait_s = atoi(argv[3]);
    if (argc >= 5) send_crlf = atoi(argv[4]);

    fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &tio);

    /* Limpiar buffer de entrada residual */
    while ((n = read(fd, buf, sizeof(buf))) > 0) { /* descartar */ }

    if (send_crlf) {
        dprintf(fd, "%s\r", argv[2]);
    } else {
        write(fd, argv[2], strlen(argv[2]));
    }

    /* Leer respuesta con timeout */
    while (total < BUF_SIZE - 1) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = wait_s;
        tv.tv_usec = 0;
        n = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (n <= 0) break;              /* timeout */
        n = read(fd, buf + total, BUF_SIZE - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = 0;
    if (total > 0) {
        /* Imprimir solo la parte legible (strip nul/ctrl) */
        for (i = 0; i < total; i++) {
            char c = buf[i];
            if (c >= 32 && c < 127) putchar(c);
            else if (c == '\n' || c == '\r') putchar(c);
        }
        printf("\n");
    } else {
        printf("(sin respuesta)\n");
    }

    close(fd);
    return 0;
}