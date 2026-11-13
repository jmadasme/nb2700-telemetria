# Guía de desarrollador — nb2700-telemetria

Esta guía explica el código, el build y los protocolos que hay detrás de `nbping`
y `atcmd`. Está pensada para quien necesita modificar, extender o revalidar el
proyecto en un entorno NetModule NRSW.

---

## 1. Mapa del repositorio

```text
.
├── nbping.c          # 187 líneas · ping ICMP con raw sockets
├── atcmd.c           # 85 líneas · cliente AT de un disparo
├── Makefile          # build de nbping con la toolchain netbox-ppc
├── README.md
├── LICENSE
├── docs/
│   ├── datasheet.md
│   ├── developer.md  # este archivo
│   ├── REFERENCES.md
│   ├── BITACORA.md
│   └── assets/*.svg
├── doc/              # registro histórico, no saneado
├── sdk/              # toolchain (excluida de git)
└── firmware/         # imágenes (excluidas de git)
```

No hay librerías propias ni headers. Cada utilidad es un único archivo C
autocontenido, lo que simplifica el cross-compile y la auditoría.

---

## 2. Restricciones del entorno

Antes de tocar el código conviene tener presentes las restricciones que impone el
firmware:

| Restricción | Consecuencia práctica |
|---|---|
| GCC 4.4.5 | Sin `_Generic`, sin `static_assert`, sin atributos modernos; C89/gnu89 |
| uClibc 0.9.31 | Sin `getaddrinfo` completo, sin algunas funciones POSIX recientes |
| PowerPC big-endian | Cuidado con `htons`/`ntohs` y con accesos no alineados |
| 56 MB RAM | Evitar buffers grandes y recursión profunda |
| Flash NAND | Evitar escrituras periódicas |
| BusyBox | No asumir utilidades GNU en scripts |

El código actual evita deliberadamente: `pthread`, `mmap`, `epoll`, `getopt_long`,
`asprintf`, JSON y cualquier dependencia externa. Esa disciplina es lo que permite
compilar con la toolchain de 2010 sin sorpresas.

---

## 3. `nbping.c`

### 3.1 Estructura

```text
nbping.c
├── includes (stdio, stdlib, string, unistd, errno, signal, socket, time, netinet)
├── #define PKT_SIZE 64, DEF_COUNT 4, DEF_TIMEOUT 1000, MAX_WAIT 5
├── static int count, timeout_ms
├── icmp_checksum(void *data, int len)
├── do_ping(int sock, struct sockaddr_in *addr, int seq)
└── main(int argc, char *argv[])
```

`MAX_WAIT` está definido pero no se usa. Es un residuo de una versión anterior que
esperaba con `alarm()`; se puede eliminar sin impacto.

### 3.2 `icmp_checksum`

```c
static unsigned short icmp_checksum(void *data, int len)
{
    unsigned short *p = (unsigned short *)data;
    unsigned int sum = 0;

    while (len > 1) { sum += *p++; len -= 2; }
    if (len == 1) sum += *(unsigned char *)p;

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}
```

Implementa RFC 1071: suma de palabras de 16 bits en orden de red, plegado de
acarreados y complemento a uno. Se llama con el campo `checksum` en cero y el
resultado se escribe en ese campo.

**Detalle de portabilidad:** `pkt` es un `char[64]`, que puede no estar alineado a
2 bytes. En PowerPC un acceso `unsigned short *` desalineado puede generar una
excepción de alineación; en la práctica el compilador alineó el array y el equipo
funcionó, pero es una deuda técnica. La forma robusta es leer byte a byte y armar
la palabra, o declarar el buffer con `__attribute__((aligned(2)))`.

### 3.3 `do_ping`

Firma:

```c
static long do_ping(int sock, struct sockaddr_in *addr, int seq)
```

Devuelve el RTT en milisegundos o `-1` si falla. Pasos:

```text
memset(pkt, 0, 64)
icmp->type      = ICMP_ECHO (8)
icmp->code      = 0
icmp->id        = htons(getpid() & 0xffff)
icmp->sequence  = htons(seq)
icmp->checksum  = 0
icmp->checksum  = icmp_checksum(pkt, 64)

gettimeofday(&start)
sendto(sock, pkt, 64, ...)  ── error ──► perror, return -1

loop:
  select(sock+1, ..., timeout_ms)
    n <= 0  ──► return -1 (timeout)
  recvfrom(rbuf, 512)
    EINTR   ──► continue
    otro    ──► return -1
  iph   = rbuf
  iphlen = iph->ihl * 4
  ricmp = rbuf + iphlen
  si type == ECHOREPLY && id == pid && seq == seq:
      gettimeofday(&end)
      return rtt_ms
  si no: volver a select
```

El buffer de recepción es de 512 bytes, suficiente para un paquete ICMP normal.
No se valida `n` antes de leer `iph->ihl` ni antes de acceder a `ricmp`; un paquete
truncado o malformado podría provocar una lectura fuera de límites. En una red
controlada es improbable, pero es la primera corrección que debería hacerse si el
programa se expone a tráfico hostil.

Tampoco se compara `sin_addr` del remitente. Un tercero podría inyectar un reply
con el ID y la secuencia correctos; el programa lo aceptaría. Para uso de
diagnóstico no es un problema; para un monitor de disponibilidad, sí.

### 3.4 `main`

```text
argc < 2                 ──► usage, return 1
inet_aton(argv[1]) == 0  ──► "IP invalida", return 1
argc >= 3: count = atoi(argv[2])
argc >= 4: timeout_ms = atoi(argv[3])
count <= 0: count = 4

socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)
  < 0 ──► perror("socket (se necesita root)"), return 1

print "PING ..."
for i in 0..count-1:
    rtt = do_ping(sock, addr, i+1)
    rtt < 0 ? "timeout" : "ok, rtt ms"
    usleep(200000)
close(sock)
print estadísticas
return recv_ok > 0 ? 0 : 1
```

No hay `signal()` ni limpieza especial: el proceso termina y el kernel cierra el
socket. `atoi()` no valida errores; `nbping host abc` produce `count = 0`, que se
corrige al default. `nbping host 4 -5` deja un timeout negativo, que `select()`
rechaza con `EINVAL` y el programa interpreta como timeout. Es aceptable para una
herramienta de operador, no para una API.

### 3.5 Formato ICMP en el cable

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Type      |     Code      |          Checksum             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Identifier          |        Sequence Number        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             Data ...                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Campo | Valor en `nbping` |
|---|---|
| Type | `8` (echo request) / `0` (echo reply) |
| Code | `0` |
| Checksum | RFC 1071 |
| Identifier | `getpid() & 0xffff`, en orden de red |
| Sequence | 1..count, en orden de red |
| Data | 56 bytes en cero |

El kernel entrega al raw socket el paquete IP completo, incluida la cabecera. Por
eso el código suma `iph->ihl * 4` antes de mirar ICMP. `IPPROTO_ICMP` en el
`socket()` hace que el kernel filtre por protocolo, pero no por ID ni por tipo.

### 3.6 Cómo probar cambios sin router

En el host no se puede ejecutar el binario PowerPC, pero sí se puede compilar una
versión nativa para validar lógica:

```bash
gcc -Wall -Wextra -O0 -g -o nbping-host nbping.c
sudo ./nbping-host 127.0.0.1 2 500
```

Advertencias esperadas en un host moderno: ninguna relevante. Si aparece
`implicit declaration of function`, revisar que se esté compilando con el estándar
correcto (`-std=gnu89` o el default de GCC 4.4.5).

---

## 4. `atcmd.c`

### 4.1 Estructura

```text
atcmd.c
├── includes (stdio, stdlib, string, unistd, fcntl, errno, termios, sys/select)
├── #define BUF_SIZE 4096
└── main()
```

No hay funciones auxiliares: toda la lógica vive en `main`, en 85 líneas.

### 4.2 Flujo

```text
argc < 3 ──► usage, return 1
wait_s   = argc >= 4 ? atoi(argv[3]) : 5
send_crlf= argc >= 5 ? atoi(argv[4]) : 1

fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK)
  < 0 ──► perror("open"), return 1

tio = {0}
tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD
tio.c_iflag = 0
tio.c_oflag = 0
tio.c_lflag = 0
tio.c_cc[VMIN] = 0
tio.c_cc[VTIME] = 1
tcsetattr(fd, TCSANOW, &tio)

while read(fd, buf, 4096) > 0: descartar   # vaciar buffer residual

send_crlf ? dprintf(fd, "%s\r", argv[2]) : write(fd, argv[2], strlen)

total = 0
while total < 4095:
    select(fd+1, wait_s)
      <= 0 ──► break
    n = read(fd, buf+total, 4095-total)
      <= 0 ──► break
    total += n

buf[total] = 0
imprimir bytes imprimibles (32..126, \n, \r)
o "(sin respuesta)"
close(fd); return 0
```

### 4.3 Decisiones y problemas

**`dprintf`.** Es POSIX.1-2008. uClibc 0.9.31 la incluye, pero con `-std=c99` y
`-Wall` puede emitir `implicit declaration`. El binario compilado existe y
funciona, así que la declaración estuvo disponible. Si se recompila con un
`-std` más estricto, usar `snprintf` + `write` para evitar dudas.

**Velocidad fija.** No hay opción de línea de comandos para baudios. El módem del
NB2700 usa 115200; si otro equipo usa 9600 o 460800, hay que cambiar el `#define`
o agregar un argumento. La forma correcta es `cfsetispeed()`/`cfsetospeed()` con
`B9600`, etc.

**Sin `tcflush`.** El vaciado se hace leyendo hasta que no hay datos. Funciona,
pero `tcflush(fd, TCIFLUSH)` es más directo y no depende de que el puerto esté
temporalmente en modo no bloqueante.

**Sin restauración de `termios`.** El descriptor se cierra al salir y el TTY
vuelve a su estado anterior cuando no queda ningún proceso con él abierto, pero
no es una garantía portable. Guardar `struct termios old` y restaurar con
`tcsetattr(fd, TCSANOW, &old)` es la práctica correcta.

**Espera por iteración.** `wait_s` se aplica en cada `select()`. El tiempo total
puede ser mayor que `wait_s` si llegan datos en varios bloques. Para una consola
interactiva sería un bug; para un one-shot es aceptable y está documentado.

**Filtro de salida.** Se imprimen solo `32..126`, `\n` y `\r`. Se descartan NUL,
`\t`, `\b` y bytes altos. Si el módem devuelve UTF-8 o caracteres extendidos, se
pierden. Para AT estándar (ASCII) es suficiente.

### 4.4 Protocolo AT

```text
Host                         Módem
  │                            │
  │  "AT+CSQ\r"                │
  │ ─────────────────────────► │
  │                            │  ejecuta
  │  "AT+CSQ\r\r\n"            │  eco (si ATE1)
  │ ◄───────────────────────── │
  │  "+CSQ: 21,99\r\n"         │
  │ ◄───────────────────────── │
  │  "\r\nOK\r\n"              │
  │ ◄───────────────────────── │
```

Reglas relevantes:

- Toda línea termina en `\r`; el módem responde con `\r\n`.
- `OK` indica éxito; `ERROR`, `+CME ERROR` o `+CMS ERROR` indican fallo.
- El eco se controla con `ATE0`/`ATE1`.
- Algunos comandos son de solo lectura (`?`), otros de escritura (`=`).
- `+++` escapa de modo datos y requiere guard time.
- El módem puede emitir notificaciones asíncronas (`+CREG:`, `RING`, `+CEREG:`)
  en cualquier momento; `atcmd` las imprimiría mezcladas con la respuesta.

Para una implementación más robusta habría que leer hasta encontrar `OK`, `ERROR`
o un timeout, y separar líneas. `atcmd` deliberadamente no lo hace: imprime el
flujo crudo para que el operador vea todo.

### 4.5 Probar en el host

Con un adaptador USB-serie y un módem:

```bash
gcc -Wall -Wextra -O0 -g -o atcmd-host atcmd.c
./atcmd-host /dev/ttyUSB0 "ATI" 3
```

Si no hay módem, se puede usar un pseudo-terminal (`socat -d -d pty,raw,echo=0
pty,raw,echo=0`) y escribir del otro lado para validar la mecánica.

---

## 5. Makefile

### 5.1 Contenido y resolución

```make
SDK_DIR ?= $(CURDIR)/sdk/toolchain-ppc-4.4.5-x86_64
CROSS   ?= $(SDK_DIR)/bin/powerpc-openwrt-linux
CC       = $(CROSS)-gcc
STRIP    = $(CROSS)-strip

CFLAGS   = -static -Os -Wall -Wextra
TARGET   = nbping

all: $(TARGET)

$(TARGET): nbping.c
	$(CC) $(CFLAGS) -o $@ nbping.c
	$(STRIP) $@
	@echo "Compilado: $$(file $@)"

clean:
	rm -f $(TARGET)

.PHONY: all clean
```

| Variable | Tipo | Efecto |
|---|---|---|
| `SDK_DIR` | `?=` | Se puede sobrescribir por entorno o línea de comandos |
| `CROSS` | `?=` | Prefijo de las herramientas |
| `CC` | `=` | Ruta final del compilador |
| `STRIP` | `=` | Ruta final del stripper |
| `CFLAGS` | `=` | Flags fijos; no usa `?=` |

`CURDIR` es el directorio desde donde se invoca `make`, no necesariamente el del
Makefile. Si se ejecuta `make -C /ruta` funciona; si se invoca desde otro lado con
`-f`, `SDK_DIR` puede resolverse mal. Pasar `SDK_DIR` absoluto evita el problema.

### 5.2 Por qué no hay target de `atcmd`

El Makefile se escribió para validar `nbping` y se preservó tal cual. Agregar
`atcmd` es trivial, pero cambia el artefacto que se validó. La decisión está
documentada en `docs/BITACORA.md`.

### 5.3 Cómo agregar un nuevo binario al Makefile

Patrón recomendado, sin romper el target actual:

```make
TARGETS  = nbping atcmd

all: $(TARGETS)

nbping: nbping.c
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) $@
	@echo "Compilado: $$(file $@)"

atcmd: atcmd.c
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) $@
	@echo "Compilado: $$(file $@)"

clean:
	rm -f $(TARGETS)
```

Para un binario nuevo, agregarlo a `TARGETS`, una regla con sus dependencias y
nada más. Mantener `-static -Os -Wall -Wextra` salvo que haya una razón medida
para cambiarlos.

---

## 6. Cross-compilación paso a paso

### 6.1 Preparar la toolchain

```bash
mkdir -p sdk
cp toolchain-ppc-4.4.5-x86_64.tar.bz2 sdk/
tar -xjf sdk/toolchain-ppc-4.4.5-x86_64.tar.bz2 -C sdk/
```

Verificar:

```bash
sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc --version
# powerpc-openwrt-linux-gcc (GCC) 4.4.5
```

Si el binario del compilador no arranca en el host (error de librerías de 32 bits
o de versiones viejas de `libmpfr`/`libgmp`), usar la imagen VirtualBox oficial de
NetModule o un contenedor con una distribución de la época. No mezclar headers
del host con los de la toolchain.

### 6.2 Variables de entorno

La toolchain de NetModule puede requerir `STAGING_DIR` para encontrar headers y
librerías:

```bash
export STAGING_DIR="$PWD/sdk/toolchain-ppc-4.4.5-x86_64"
```

El Makefile actual no lo exporta; si el compilador se queja de headers faltantes,
exportarlo antes de `make`. No editar el Makefile para hardcodear rutas
personales.

### 6.3 Compilar

```bash
make clean && make
```

Salida esperada:

```text
powerpc-openwrt-linux-gcc -static -Os -Wall -Wextra -o nbping nbping.c
powerpc-openwrt-linux-strip nbping
Compilado: nbping: ELF 32-bit MSB executable, PowerPC or cisco 4500,
version 1 (SYSV), statically linked, stripped
```

### 6.4 Verificar

```bash
file nbping
readelf -h nbping | grep -E 'Class|Data|Machine'
```

| Campo | Esperado |
|---|---|
| `Class` | `ELF32` |
| `Data` | `2's complement, big endian` |
| `Machine` | `PowerPC` |
| `Type` | `EXEC` |

Si `Machine` es `ARM` o `MIPS`, se usó la toolchain equivocada. Si `Class` es
`ELF64`, se compiló para el host o para otra familia.

### 6.5 Errores típicos de build

| Error | Causa | Solución |
|---|---|---|
| `powerpc-openwrt-linux-gcc: not found` | `SDK_DIR` mal o toolchain no extraída | Verificar ruta; pasar `SDK_DIR` |
| `stdio.h: No such file` | Falta `STAGING_DIR` o sysroot | Exportar `STAGING_DIR` |
| `cannot find -lc` | Toolchain incompleta | Reextraer el tar.gz |
| `unrecognized command line option '-Os'` | Se invocó el gcc del host | Revisar `CC` |
| `error: expected ...` en código válido | Estándar más estricto que gnu89 | Agregar `-std=gnu89` |
| `warning: implicit declaration of 'dprintf'` | `_GNU_SOURCE`/POSIX no declarado | Usar `snprintf`+`write` o definir `_POSIX_C_SOURCE 200809L` |

---

## 7. Despliegue

```bash
# Copiar
scp -O nbping root@<IP_ROUTER>:/tmp/nbping

# Verificar
ssh root@<IP_ROUTER> 'chmod 755 /tmp/nbping; sha256sum /tmp/nbping; file /tmp/nbping'

# Ejecutar
ssh root@<IP_ROUTER> '/tmp/nbping 198.51.100.1 4 1000'

# Persistir
ssh root@<IP_ROUTER> 'cp /tmp/nbping /usr/bin/nbping && chmod 755 /usr/bin/nbping'
```

Para `atcmd`, el mismo procedimiento. Recordar que `scp` sin `-O` falla por falta
de `sftp-server`.

### 7.1 Prueba de humo

```bash
/tmp/nbping 127.0.0.1 1 500
```

El loopback siempre responde si la pila está sana. Si falla, el problema es del
binario o del kernel, no de la red.

---

## 8. Cómo extender el código

### 8.1 Agregar resolución DNS a `nbping`

No usar `getaddrinfo` si la uClibc del firmware no la trae completa. Alternativa
mínima: aceptar solo IPv4 y usar `gethostbyname` (presente en uClibc), con
`#include <netdb.h>`:

```c
struct hostent *he = gethostbyname(argv[1]);
if (!he) { fprintf(stderr, "no resuelve: %s\n", argv[1]); return 1; }
memcpy(&in, he->h_addr, sizeof(in));
```

Mantener la opción numérica como camino principal para no depender de DNS en un
equipo que puede estar sin red.

### 8.2 Agregar `SO_BINDTODEVICE`

Para forzar la interfaz de salida (útil cuando hay LAN y LTE):

```c
const char *iface = "eth0";
setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface) + 1);
```

Requiere `root`, que ya se tiene. Documentar que el nombre de interfaz varía entre
firmware y modelo.

### 8.3 Agregar salida JSON

Sin librerías, imprimir una línea por paquete y un objeto final:

```c
printf("{\"target\":\"%s\",\"sent\":%d,\"recv\":%d,\"loss\":%.0f,"
       "\"rtt_min\":%ld,\"rtt_avg\":%ld,\"rtt_max\":%ld}\n",
       target, sent, recv_ok, loss, rtt_min, rtt_avg, rtt_max);
```

Cuidado con escapar el destino si se aceptan nombres. Para IPv4 numérica no hace
falta.

### 8.4 Agregar baudios a `atcmd`

```c
int baud = argc >= 6 ? atoi(argv[5]) : 115200;
speed_t sp = B115200;
switch (baud) {
    case 9600:   sp = B9600;   break;
    case 19200:  sp = B19200;  break;
    case 38400:  sp = B38400;  break;
    case 57600:  sp = B57600;  break;
    case 115200: sp = B115200; break;
    default: fprintf(stderr, "baudios no soportados\n"); return 1;
}
cfsetispeed(&tio, sp);
cfsetospeed(&tio, sp);
```

No usar `BOTHER` ni velocidades no estándar: uClibc y el driver del módem pueden
no soportarlas.

### 8.5 Convertir `atcmd` en interactivo

Sería otro programa: leer de `stdin`, escribir al puerto, multiplexar con
`select()` sobre ambos descriptores, manejar `SIGINT` y restaurar `termios` con
`atexit()`. No conviene meterlo en el mismo binario: el one-shot es más fácil de
auditar y de usar en scripts.

### 8.6 Reutilizar el checksum

`icmp_checksum` sirve para cualquier cabecera IP/ICMP/TCP/UDP. Si se extrae a un
header, mantenerla `static` en cada archivo o crear un `netutils.c` con su header;
no hace falta una librería.

---

## 9. Deuda técnica conocida

| Ítem | Impacto | Prioridad |
|---|---|---|
| `atcmd` fuera del Makefile | Build manual, fácil de olvidar | Alta |
| Sin validación de longitud en `recvfrom` | Lectura fuera de límites con paquetes truncados | Alta |
| Sin verificación de IP de origen | Acepta replies inyectados | Media |
| Checksum con cast a `unsigned short *` | Acceso potencialmente desalineado en PPC | Media |
| `atoi` sin validación | Valores inválidos producen defaults o timeouts raros | Media |
| `MAX_WAIT` sin usar | Ruido | Baja |
| `send_crlf` envía solo CR | Nombre engañoso | Baja |
| Sin restauración de `termios` | Efecto colateral teórico | Baja |
| `count` sin techo | Uso accidental prolongado | Baja |
| Sin tests automatizados | Regresiones silenciosas | Media |

Ninguna de estas deudas impide el uso previsto. Se documentan para que un futuro
mantenedor no las descubra a golpes.

---

## 10. Buenas prácticas para este entorno

1. **Compilar siempre con la toolchain oficial.** Un GCC moderno puede generar
   instrucciones o relocations que el kernel del router no entiende.
2. **Linkear estático.** Evita el infierno de versiones de uClibc.
3. **Medir el tamaño.** `ls -l` después de `strip`; si crece mucho, revisar flags.
4. **Probar en loopback primero.** `/tmp/nbping 127.0.0.1` antes de culpar a la red.
5. **No escribir en flash sin necesidad.** `/tmp` para pruebas, `/usr/bin` para
   persistencia consciente.
6. **No hardcodear IPs, credenciales ni seriales.** Usar placeholders y variables.
7. **Documentar los comandos AT que cambian estado.** `AT+CFUN`, `AT+COPS`,
   `AT+CGDCONT` no son de solo lectura.
8. **Respetar el guard time de `+++`.** Un escape mal temporizado no funciona.
9. **Verificar el hash después de transferir.** `scp` puede truncar si se corta la
   sesión.
10. **No exponer la gestión a Internet.** El dropbear del NB2700 es antiguo.
11. **Mantener el código en gnu89.** Declaraciones al inicio de bloque, sin
    `//` si se quiere máxima compatibilidad (aunque GCC 4.4.5 acepta `//`).
12. **Evitar `printf` de 64 bits.** `%lld` puede no estar soportado por uClibc;
    usar `long` y `%ld`.

---

## 11. Plan de pruebas manual

No hay suite automatizada. El siguiente plan cubre lo mínimo antes de declarar un
build válido:

| # | Prueba | Comando | Criterio |
|---|---|---|---|
| 1 | Tipo de archivo | `file nbping` | ELF32 MSB PowerPC, static |
| 2 | Cabecera | `readelf -h nbping` | Machine PowerPC, big endian |
| 3 | Transferencia | `scp -O ...` | Sin errores |
| 4 | Hash | `sha256sum` en ambos lados | Iguales |
| 5 | Ejecución loopback | `nbping 127.0.0.1 1 500` | 1/1 reply |
| 6 | Gateway | `nbping <GW> 4 1000` | 4/4 o pérdida justificada |
| 7 | Host LAN | `nbping <HOST> 4 1000` | 4/4 |
| 8 | Público | `nbping 203.0.113.10 4 2000` | Según enlace |
| 9 | Sin root | ejecutar como usuario | Falla con mensaje claro |
| 10 | `atcmd` sanity | `atcmd <P> "AT" 2` | `OK` |
| 11 | `atcmd` señal | `atcmd <P> "AT+CSQ" 3` | `+CSQ: n,99` |
| 12 | Persistencia | copiar a `/usr/bin`, reiniciar | Sigue presente |

Para regresión, conservar la salida de cada prueba y comparar.

---

## 12. Referencias técnicas

- RFC 792 (ICMP): <https://www.rfc-editor.org/rfc/rfc792>
- RFC 1071 (checksum): <https://www.rfc-editor.org/rfc/rfc1071>
- `raw(7)`: <https://man7.org/linux/man-pages/man7/raw.7.html>
- `termios(3)`: <https://man7.org/linux/man-pages/man3/termios.3.html>
- `select(2)`: <https://man7.org/linux/man-pages/man2/select.2.html>
- NetModule Wiki: <https://wiki.netmodule.com>
- Toolchains: <https://share.netmodule.com/router/public/toolchain/>
- u-blox AT commands: manual del fabricante para TOBY-L2/MPCI-L2.

La bibliografía anotada está en [`REFERENCES.md`](REFERENCES.md). Los problemas
encontrados durante el desarrollo y las decisiones que se tomaron están en
[`BITACORA.md`](BITACORA.md).
