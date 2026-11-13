/*
 * nbping.c - Ping ICMP desde un router NetModule (NRSW/OpenWrt)
 *
 * Programa de validacion del pipeline: compilacion cross (netbox-ppc),
 * transferencia vía scp y ejecucion en el router NB2700 (PowerPC).
 *
 * Hace ping a una IP destino usando raw sockets ICMP (requiere root).
 * C puro, sin dependencias externas (compatible GCC 4.4.5 + uClibc).
 *
 * Uso: nbping <ip_destino> [num_paquetes] [timeout_ms_por_paquete]
 * Ej:  nbping 192.0.2.10 4 1000
 *
 * Compilar (cross):
 *   powerpc-openwrt-linux-gcc -static -Os -Wall -o nbping nbping.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <time.h>

#define PKT_SIZE   64          /* tamano del paquete ICMP */
#define DEF_COUNT  4           /* pings por defecto */
#define DEF_TIMEOUT 1000       /* timeout por paquete (ms) */
#define MAX_WAIT   5           /* segundos esperando respuesta */

static int count = DEF_COUNT;
static int timeout_ms = DEF_TIMEOUT;

/* Calcula el checksum ICMP (complemento a 1 de la suma de 16 bits) */
static unsigned short icmp_checksum(void *data, int len)
{
    unsigned short *p = (unsigned short *)data;
    unsigned int sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1)
        sum += *(unsigned char *)p;

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

/* Lanza un ping ICMP a 'addr' y devuelve el RTT en ms, o -1 si falla */
static long do_ping(int sock, struct sockaddr_in *addr, int seq)
{
    char pkt[PKT_SIZE];
    struct icmphdr *icmp = (struct icmphdr *)pkt;
    struct timeval start, end;
    fd_set rfds;
    struct timeval tv;
    char rbuf[512];
    int n, rtt;

    memset(pkt, 0, sizeof(pkt));

    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = htons(getpid() & 0xffff);
    icmp->un.echo.sequence = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = icmp_checksum(pkt, sizeof(pkt));

    gettimeofday(&start, NULL);
    if (sendto(sock, pkt, sizeof(pkt), 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        perror("sendto");
        return -1;
    }

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        n = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (n <= 0)
            return -1;          /* timeout */

        n = recvfrom(sock, rbuf, sizeof(rbuf), 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        /* Buscar el echo reply correspondiente (deteccion por id+seq) */
        {
            struct iphdr *iph = (struct iphdr *)rbuf;
            int iphlen = iph->ihl * 4;
            struct icmphdr *ricmp =
                (struct icmphdr *)(rbuf + iphlen);

            if (ricmp->type == ICMP_ECHOREPLY &&
                ntohs(ricmp->un.echo.id) == (getpid() & 0xffff) &&
                ntohs(ricmp->un.echo.sequence) == (unsigned short)seq) {
                gettimeofday(&end, NULL);
                rtt = (int)((end.tv_sec - start.tv_sec) * 1000 +
                            (end.tv_usec - start.tv_usec) / 1000);
                return rtt;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    struct in_addr in;
    int sock, i;
    int sent = 0, recv_ok = 0;
    long rtt, rtt_min = 99999, rtt_max = 0, rtt_sum = 0;

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <ip_destino> [num_paquetes] [timeout_ms]\n",
                argv[0]);
        fprintf(stderr, "Ej:  %s 192.0.2.10 4 1000\n", argv[0]);
        return 1;
    }

    if (inet_aton(argv[1], &in) == 0) {
        fprintf(stderr, "IP invalida: %s\n", argv[1]);
        return 1;
    }

    if (argc >= 3)
        count = atoi(argv[2]);
    if (argc >= 4)
        timeout_ms = atoi(argv[3]);
    if (count <= 0)
        count = DEF_COUNT;

    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket (se necesita root)");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = in;

    printf("PING %s: %d paquetes, timeout %d ms\n",
           argv[1], count, timeout_ms);

    for (i = 0; i < count; i++) {
        rtt = do_ping(sock, &addr, i + 1);
        if (rtt < 0) {
            printf("paquete %d: timeout\n", i + 1);
        } else {
            printf("paquete %d: ok, %ld ms\n", i + 1, rtt);
            recv_ok++;
            rtt_sum += rtt;
            if (rtt < rtt_min) rtt_min = rtt;
            if (rtt > rtt_max) rtt_max = rtt;
        }
        sent++;
        usleep(200000);         /* 200 ms entre pings */
    }

    close(sock);

    printf("\n--- estadisticas ---\n");
    printf("enviados: %d, recibidos: %d, perdidos: %d (%.0f%%)\n",
           sent, recv_ok, sent - recv_ok,
           (sent - recv_ok) * 100.0 / (sent ? sent : 1));
    if (recv_ok > 0)
        printf("rtt min/avg/max = %ld/%ld/%ld ms\n",
               rtt_min, rtt_sum / recv_ok, rtt_max);

    return (recv_ok > 0) ? 0 : 1;
}