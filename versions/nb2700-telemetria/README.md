# nb2700-telemetria

**Dos utilidades C mínimas para diagnosticar un router NetModule NB2700 desde adentro: ping ICMP con raw sockets y diálogo AT directo con el módem celular u-blox.**

![Router](https://img.shields.io/badge/router-NetModule%20NB2700-1f6feb?style=flat-square)
![Arquitectura](https://img.shields.io/badge/arquitectura-PowerPC%20e300c3-d29922?style=flat-square)
![Toolchain](https://img.shields.io/badge/toolchain-GCC%204.4.5%20%2B%20uClibc%200.9.31-8b949e?style=flat-square)
![Firmware](https://img.shields.io/badge/firmware-NRSW%204.6.0.105-3fb950?style=flat-square)
![Lenguaje](https://img.shields.io/badge/lenguaje-C%20(gnu89)-555?style=flat-square)
![Licencia](https://img.shields.io/badge/licencia-MIT-3fb950?style=flat-square)

> **Estado:** pipeline validado de punta a punta sobre hardware real. `nbping` fue
> cross-compilado con `netbox-ppc`, transferido por `scp` y ejecutado en un NB2700,
> respondiendo contra un host de la LAN. `atcmd` fue cross-compilado y transferido;
> el enlace celular no llegó a validarse con tráfico de Internet por falta de
> cobertura suficiente en el lugar de la prueba (ver [§10](#10-diagnóstico-celular)
> y [§15](#15-cómo-se-construyó)).

No es un framework de monitoreo ni un agente de telemetría autónomo. Es, a
propósito, **el eslabón más pequeño que demuestra que el pipeline de
cross-compilación para NRSW funciona**: fuente C, toolchain oficial, binario
estático PowerPC, transferencia al router y ejecución como root.

---

## Tabla de contenidos

- [1. Qué es este proyecto](#1-qué-es-este-proyecto)
- [2. Hardware objetivo](#2-hardware-objetivo)
- [3. Arquitectura](#3-arquitectura)
- [4. `nbping` — ping ICMP desde el router](#4-nbping--ping-icmp-desde-el-router)
- [5. `atcmd` — consola AT de un solo disparo](#5-atcmd--consola-at-de-un-solo-disparo)
- [6. Cross-compilación con `netbox-ppc`](#6-cross-compilación-con-netbox-ppc)
- [7. Transferencia al router](#7-transferencia-al-router)
- [8. Ejecución y persistencia](#8-ejecución-y-persistencia)
- [9. Descubrimiento del router](#9-descubrimiento-del-router)
- [10. Diagnóstico celular](#10-diagnóstico-celular)
- [11. Seguridad](#11-seguridad)
- [12. Rendimiento y recursos](#12-rendimiento-y-recursos)
- [13. Resolución de problemas](#13-resolución-de-problemas)
- [14. Limitaciones y alcance](#14-limitaciones-y-alcance)
- [15. Cómo se construyó](#15-cómo-se-construyó)
- [16. Estructura del repositorio](#16-estructura-del-repositorio)
- [17. Referencias](#17-referencias)
- [18. Licencia](#18-licencia)

---

## 1. Qué es este proyecto

Dos programas C para correr **dentro** de un NB2700 con firmware NRSW (OpenWrt).
Se compilan en un host Linux x86_64 con la toolchain oficial de NetModule y se
copian al router, donde se ejecutan como `root`.

| Utilidad | Qué resuelve | Cómo |
|---|---|---|
| `nbping` | ¿Hay conectividad IP desde el router hacia un destino? | Raw socket `AF_INET`/`SOCK_RAW`/`IPPROTO_ICMP`, echo request/reply, pérdida y RTT |
| `atcmd` | ¿Qué responde el módem celular a un comando AT concreto? | Abre el puerto serie en raw, escribe un comando, lee con timeout y lo imprime |

**Por qué existe.** El diagnóstico habitual mira el router desde afuera: Web
Manager, CLI, ping desde una notebook. Eso asume que el salto de gestión funciona.
La pregunta útil en campo es qué ve el router desde su propia pila y su propio
módem. `nbping` mide desde la interfaz del router; `atcmd` habla con el módem sin
la capa de resumen del CLI, lo que permite ver errores AT crudos, registro, señal
y estados que el Web Manager no muestra.

**Por qué no alcanza BusyBox.** NRSW ya trae un `ping`. `nbping` no lo reemplaza:
existe para validar que un binario GCC 4.4.5 + uClibc 0.9.31, estático y
big-endian, corre en el equipo. Es una prueba de humo del pipeline con salida
estable y fácil de parsear. `atcmd` cubre la consulta AT puntual sin `microcom`,
`picocom` ni `screen`, que no están instalados por defecto.

**Qué no es:** no es un daemon ni un servicio `init.d`; no recolecta métricas
periódicas ni las publica por MQTT/HTTP/InfluxDB; no administra configuración; no
incluye toolchain ni firmware; no soporta IPv6 ni DNS en `nbping`.

---

## 2. Hardware objetivo

Equipo de referencia: **NetModule NB2700**. La toolchain `netbox-ppc` cubre
también NB1600, NB2710, NB3700 y NB3710. El código no usa periféricos específicos,
así que debería funcionar en la familia; **solo el NB2700 fue probado**.

| Dato | Valor observado | Verificación |
|---|---|---|
| CPU | PowerPC **e300c3 @ 400 MHz** (MPC830x RDB) | `cat /proc/cpuinfo` |
| Arquitectura | `ppc`, 32 bits, **big-endian** (MSB) | `uname -m`, `readelf -h` |
| RAM | 56 MB totales, ~31 MB libres en reposo | `cat /proc/meminfo` |
| Flash | `boot` 4 MB; `root0`/`root1` 48 MB c/u; `data` 25 MB | `cat /proc/mtd` |
| Firmware | NRSW **4.6.0.105**, perfil `NETBOX` | `cat /etc/release-info` |
| Shell | BusyBox 1.30.1 (`ash`) | Banner |
| Módem | u-blox **MPCI-L280** (TOBY-L2, `tobyl2`), miniPCIe | `cli status wwan` |
| DIO | IN1, IN2, OUT1, OUT2 | `cli status summary` |
| Gestión | Telnet 23 y HTTP 80 abiertos de fábrica; SSH 22 deshabilitado | Escaneo y `cli get` |
| Estado | **EOL**: soporte oficial finalizado en marzo de 2025 | Aviso de NetModule |

**Puertos físicos.** Ethernet 1–4 son LAN conmutada (gestión, hosts locales);
Ethernet 5 es WAN (uplink). La IP de gestión de fábrica vive en la LAN
(por defecto en la subred de gestión) y hay DHCP activo. En este documento se usa
`<IP_ROUTER>` para no fijar direcciones de una instalación concreta.

**Sistema de archivos.** El firmware se monta en lectura y los cambios van a overlay:

| Punto | Tamaño | Persistente | Uso |
|---|---|---|---|
| `/rom` | ~48 MB | No (ro) | Imagen del firmware |
| `/tmp` | ~28 MB (`tmpfs`) | **No** | Transferencia y pruebas |
| `/overlay` | 25 MB | **Sí** | Cambios sobre `/`, incluido `/usr/bin` |

Un binario en `/tmp` desaparece al reiniciar; uno en `/usr/bin` sobrevive pero
consume flash ([§8](#8-ejecución-y-persistencia)).

**Familia y toolchains:**

| Familia | Modelos | Salida esperada |
|---|---|---|
| `netbox-ppc` | NB1600, NB2700, NB2710, NB3700, NB3710 | `ELF 32-bit MSB PowerPC` |
| `netbolt-arm` | NB1800, NB2800, NB3701, NB3711, NB3800 | `ELF 32-bit LSB ARM` |
| `netbird-arm` | NB1601, NB800 | `ELF 32-bit LSB ARM` |

Un binario PowerPC no corre en ARM ni en MIPS. Verificar con `file` antes de
transferir ahorra media hora de diagnóstico equivocado.

---

## 3. Arquitectura

Tres planos: host de desarrollo, router y módem. La toolchain vive en el host; los
binarios en el router; el módem cuelga del bus miniPCIe y expone puertos serie.

```text
┌──────────────────────── HOST x86_64 ────────────────────────┐
│  nbping.c   atcmd.c   Makefile                              │
│      └──────────┴─────────┘                                 │
│                 │ make                                      │
│                 ▼                                           │
│  sdk/toolchain-ppc-4.4.5-x86_64/                            │
│  └── powerpc-openwrt-linux-gcc (GCC 4.4.5 + uClibc 0.9.31)  │
│                 │                                           │
│                 ▼                                           │
│  nbping, atcmd → ELF 32-bit MSB PowerPC, static, stripped   │
└─────────────────┼───────────────────────────────────────────┘
                  │ scp -O / nc / base64 sobre ssh
                  ▼
┌────────────────────── ROUTER NB2700 ────────────────────────┐
│  NRSW 4.6.0.105 (OpenWrt + BusyBox 1.30.1)                  │
│  /tmp/nbping (volátil)   /usr/bin/nbping (overlay)          │
│  nbping ── raw ICMP ──► LAN ◄──► hosts / Internet           │
│  atcmd  ── /dev/ttyACM* ──► u-blox MPCI-L280 ──► celular    │
└─────────────────────────────────────────────────────────────┘
```

Diagramas editables: [`docs/assets/architecture.svg`](docs/assets/architecture.svg),
[`docs/assets/cross-compile.svg`](docs/assets/cross-compile.svg) y
[`docs/assets/telemetry-flow.svg`](docs/assets/telemetry-flow.svg).

**Sesión típica.** (1) conectar el host a una LAN; (2) entrar por Telnet o SSH
como root; (3) verificar `uname -m` y `/etc/release-info`; (4) copiar binarios;
(5) `nbping` contra gateway y contra un destino público; (6) `atcmd` para señal,
registro y SIM; (7) cruzar lecturas: si el ping a Internet falla pero el módem
está registrado con señal razonable, el problema es WAN/APN/firewall; si no hay
registro, es radio o SIM.

| Decisión | Motivo | Costo |
|---|---|---|
| Linkeado **estático** | No depender de la versión de uClibc del firmware | 34–40 KB en vez de ~10 KB |
| **`-Os`** | La flash y la RAM son el recurso escaso | Velocidad irrelevante acá |
| **C puro** | Ecosistema limitado en GCC 4.4.5 | Checksum y parseo a mano |
| **Raw sockets** | Control de ID, secuencia y timeout | Requiere `root` |
| **Un disparo** en `atcmd` | Menos estado y superficie de error | No sirve interactivo |
| Sin `setuid` | Un binario root-setuid en campo es un riesgo mayor | Invocar como root |

---

## 4. `nbping` — ping ICMP desde el router

### 4.1 Uso

```text
nbping <ip_destino> [num_paquetes] [timeout_ms_por_paquete]
```

| Argumento | Obligatorio | Default | Descripción |
|---|---|---|---|
| `ip_destino` | Sí | — | IPv4 decimal punteada; no resuelve DNS |
| `num_paquetes` | No | `4` | Cantidad de echo requests |
| `timeout_ms` | No | `1000` | Espera máxima por reply, en ms |

```bash
nbping 198.51.100.1              # gateway LAN, 4 pings, 1 s
nbping 203.0.113.10 10 1500      # destino público, 10 pings, 1,5 s
nbping 198.51.100.20 1 300       # chequeo rápido para scripts
```

### 4.2 Salida

```text
# nbping 198.51.100.20 4 1500
PING 198.51.100.20: 4 paquetes, timeout 1500 ms
paquete 1: ok, 0 ms
paquete 2: ok, 0 ms
paquete 3: ok, 0 ms
paquete 4: ok, 0 ms

--- estadisticas ---
enviados: 4, recibidos: 4, perdidos: 0 (0%)
rtt min/avg/max = 0/0/0 ms
```

En LAN el RTT real es de fracciones de milisegundo; el programa trunca a
milisegundos al imprimir, así que `0 ms` significa "menos de 1 ms", no una
medición inválida. Contra Internet se ven decenas o cientos de ms según ruta y
tecnología de acceso. Con pérdida:

```text
# nbping 203.0.113.10 5 800
paquete 1: ok, 84 ms
paquete 2: timeout
paquete 3: ok, 91 ms
paquete 4: timeout
paquete 5: ok, 88 ms
enviados: 5, recibidos: 3, perdidos: 2 (40%)
rtt min/avg/max = 84/87/91 ms
```

### 4.3 Códigos de salida

| Código | Significado |
|---|---|
| `0` | Al menos un reply válido |
| `1` | Cero replies, o error de uso/socket |

No distingue "sin reply" de "error de socket" en el código; el motivo está en
`stderr`, donde `perror` lo imprime.

### 4.4 Cómo funciona

1. Valida `argc`, convierte el destino con `inet_aton()`, ajusta `count` y
   `timeout_ms` con `atoi()` (`count <= 0` vuelve al default).
2. Crea `socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)`.
3. Por paquete: arma 64 bytes con cabecera ICMP tipo `8` (echo request), código
   `0`, ID = `getpid() & 0xffff`, secuencia incremental; calcula checksum;
   guarda el instante y hace `sendto()`; espera con `select()`; recibe con
   `recvfrom()`; salta la cabecera IP con `ihl * 4`; acepta solo `ICMP_ECHOREPLY`
   con el mismo ID y secuencia; calcula el RTT.
4. Duerme 200 ms entre paquetes e imprime el resumen.

El checksum es la suma de palabras de 16 bits con plegado de acarreos y
complemento a uno (RFC 1071), calculada sobre los 64 bytes con el campo en cero.

### 4.5 Root y límites

Los raw sockets requieren `CAP_NET_RAW`; en NRSW se corre como `root` o falla con
`socket (se necesita root): Operation not permitted`. **No** se recomienda
`setuid root`; si hace falta un usuario no privilegiado, un wrapper con `sudo`
restringido es menos malo.

Limitaciones conocidas: solo IPv4; sin DNS; sin `SO_BINDTODEVICE`; no valida la
IP de origen del reply (solo ID y secuencia); no valida que el paquete tenga
`ihl * 4 + 8` bytes antes de leer ICMP; `count` no tiene techo; el timeout de
`select()` se reinicia con cada paquete ajeno; el ID puede colisionar entre
instancias con PIDs congruentes módulo 65536. Nada de esto afecta el uso previsto,
pero está documentado para quien extienda el código.

---

## 5. `atcmd` — consola AT de un solo disparo

### 5.1 Uso

```text
atcmd <puerto> <comando> [espera_s] [crlf]
```

| Argumento | Obligatorio | Default | Descripción |
|---|---|---|---|
| `puerto` | Sí | — | `/dev/ttyACM0`, `/dev/ttyUSB2`, etc. |
| `comando` | Sí | — | Cadena AT sin `\r`; el programa lo agrega |
| `espera_s` | No | `5` | Segundos de espera por respuesta |
| `crlf` | No | `1` | `1` envía `\r`; `0` envía los bytes tal cual |

```bash
atcmd /dev/ttyACM0 "AT+CSQ" 3       # calidad de señal
atcmd /dev/ttyACM0 "AT+CGSN" 3      # IMEI
atcmd /dev/ttyACM0 "ATI" 3          # modelo y firmware
atcmd /dev/ttyACM0 "AT+CFUN?" 3     # estado funcional
atcmd /dev/ttyACM0 "+++" 2 0        # escape de modo datos (sin CR)
```

### 5.2 Salida

```text
# atcmd /dev/ttyACM0 "AT+CSQ" 3
AT+CSQ
+CSQ: 21,99

OK
```

La primera línea es el eco (si está habilitado). Sin respuesta:
`(sin respuesta)`. Un error del módem se muestra como `+CME ERROR: <n>` o
`+CMS ERROR: <n>`; `atcmd` no lo interpreta, solo lo imprime
([§10](#10-diagnóstico-celular)).

### 5.3 Puerto serie

| Parámetro | Valor |
|---|---|
| Velocidad | 115200 (`B115200` en `c_cflag`) |
| Datos / paridad / stop | 8 / ninguna / 1 |
| Control de flujo | Ninguno |
| Modo | Raw (`c_lflag = 0`, `c_oflag = 0`) |
| Bloqueo | `O_NONBLOCK` + `select()` |

Si el módem está a otra velocidad, `atcmd` devuelve basura o nada. Confirmar con
`cli status wwan` y la documentación del módem.

**Semántica de la espera.** El bucle usa `select()` con `wait_s` **en cada
iteración**: después del último byte espera hasta `wait_s` antes de imprimir, y
si el módem sigue emitiendo continúa mientras quepa en 4096 bytes. Para comandos
puntuales usar 2–3 s; para `AT+CFUN=1,1`, 10 s o más.

### 5.4 Detalles honestos

- El flag se llama `send_crlf` pero envía `"\r"`, no `"\r\n"`. Alcanza para AT,
  aunque el nombre induzca a error.
- No implementa el *guard time* de 1 s antes/después de `+++`; hay que respetarlo
  desde el invocador.
- No restaura `termios` al salir ni usa `cfsetispeed()`/`cfsetospeed()`; escribe
  `B115200` directo en `c_cflag`. Funciona con uClibc, no es portable.
- No filtra eco ni separa líneas; imprime el flujo completo.
- No valida que el comando empiece con `AT`; envía cualquier cadena.
- 4096 bytes pueden truncar respuestas largas (listado de operadores).

### 5.5 Dónde está el puerto

El MPCI-L280 puede exponerse como `cdc_acm` (`/dev/ttyACM*`) o como `option`
(`/dev/ttyUSB*`). No adivinar:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
cli status wwan
dmesg | grep -i -E 'acm|option|ublox|toby'
```

En el equipo probado respondió en `/dev/ttyACM0`. El puerto de datos (PPP/QMI) no
es el de AT.

---

## 6. Cross-compilación con `netbox-ppc`

### 6.1 La toolchain

| Dato | Valor |
|---|---|
| Familia | `netbox-ppc` |
| Archivo | `toolchain-ppc-4.4.5-x86_64.tar.bz2` (~17 MB) |
| Origen | `https://share.netmodule.com/router/public/toolchain/` |
| Compilador | `powerpc-openwrt-linux-gcc` |
| Versiones | GCC 4.4.5, uClibc 0.9.31 |
| Host | Linux x86_64 |
| Salida | `ELF 32-bit MSB PowerPC` |

La toolchain **no se versiona**: su licencia y distribución corresponden a
NetModule. `.gitignore` excluye `sdk/`, `firmware/` y los binarios compilados.

### 6.2 Obtención y build

```bash
mkdir -p sdk
cp toolchain-ppc-4.4.5-x86_64.tar.bz2 sdk/
tar -xjf sdk/toolchain-ppc-4.4.5-x86_64.tar.bz2 -C sdk/
sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc --version
```

Estructura esperada:

```text
sdk/
├── toolchain-ppc-4.4.5-x86_64.tar.bz2
└── toolchain-ppc-4.4.5-x86_64/
    ├── bin/powerpc-openwrt-linux-{gcc,strip,...}
    ├── include/
    ├── lib/
    └── powerpc-openwrt-linux/
```

El Makefile resuelve todo con:

```make
SDK_DIR ?= $(CURDIR)/sdk/toolchain-ppc-4.4.5-x86_64
CROSS   ?= $(SDK_DIR)/bin/powerpc-openwrt-linux
CC       = $(CROSS)-gcc
STRIP    = $(CROSS)-strip
CFLAGS   = -static -Os -Wall -Wextra
TARGET   = nbping
```

```bash
make                                      # compila y strippea nbping
make clean                                # borra el binario
make SDK_DIR=/opt/netmodule/toolchain-ppc-4.4.5-x86_64
```

Salida esperada:

```text
powerpc-openwrt-linux-gcc -static -Os -Wall -Wextra -o nbping nbping.c
powerpc-openwrt-linux-strip nbping
Compilado: nbping: ELF 32-bit MSB executable, PowerPC or cisco 4500,
version 1 (SYSV), statically linked, stripped
```

### 6.3 `atcmd` se compila a mano

**El Makefile actual solo construye `nbping`.** Es una deuda técnica deliberada:
se preservó el artefacto validado. `atcmd` usa la misma toolchain:

```bash
CC=sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc
STRIP=sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-strip
$CC -static -Os -Wall -Wextra -o atcmd atcmd.c
$STRIP atcmd
```

Para incorporarlo al build, ver
[`docs/developer.md`](docs/developer.md#63-cómo-agregar-un-nuevo-binario-al-makefile).

### 6.4 Verificación

```bash
file nbping
# ELF 32-bit MSB executable, PowerPC or cisco 4500, statically linked, stripped

readelf -h nbping | grep -E 'Class|Data|Machine|Type'
# Class: ELF32 | Data: big endian | Type: EXEC | Machine: PowerPC
```

| Campo | Correcto | Si dice otra cosa |
|---|---|---|
| `Class` | `ELF32` | Se compiló para 64 bits |
| `Data` | `big endian` | Toolchain equivocada (ARM/MIPS) |
| `Machine` | `PowerPC` | Inejecutable en el NB2700 |

**Por qué `-static` y `-Os`.** `-static` elimina la dependencia de la versión de
`libc.so` del firmware: el binario pesa más pero no falla con
`version 'GLIBC_2.7' not found` ni con un `ld-uClibc.so.0` ausente. `-Os` compensa
el tamaño. `-Wall -Wextra` se mantienen porque el código es chico.

**Reproducibilidad.** El build no es bit-a-bit reproducible: `strip` y el linker
de 2010 incrustan timestamps y rutas. Comparar `sha256sum` del binario recién
compilado contra el que corre en el router, no contra un hash histórico. Para
reproducibilidad estricta harían falta `SOURCE_DATE_EPOCH` y
`-ffile-prefix-map` (no disponible en GCC 4.4.5).

---

## 7. Transferencia al router

### 7.1 `scp` (recomendado)

El `dropbear` del NB2700 **no incluye `sftp-server`**. El `scp` moderno usa SFTP
por defecto y falla con `subsystem request failed`. Forzar el protocolo antiguo:

```bash
scp -O nbping root@<IP_ROUTER>:/tmp/nbping
scp -O atcmd  root@<IP_ROUTER>:/tmp/atcmd
```

Con OpenSSH moderno contra dropbear 2019, agregar algoritmos legacy:

```bash
scp -O -o StrictHostKeyChecking=no \
  -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
  -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  nbping root@<IP_ROUTER>:/tmp/nbping
```

`StrictHostKeyChecking=no` es aceptable en laboratorio, **no** en producción:
desactiva la verificación de identidad. Registrar la huella la primera vez.

### 7.2 `netcat`

```bash
# Host sirve una vez; router recibe
nc -l -p 9999 < nbping
# (en el router)
nc <IP_HOST> 9999 > /tmp/nbping

# Router envía; host guarda
nc -l -p 9999 > nbping.recibido
# (en el router)
nc <IP_HOST> 9999 < /tmp/nbping
```

`nc` no verifica integridad. Comparar siempre:

```bash
sha256sum nbping                 # host
sha256sum /tmp/nbping            # router (BusyBox incluye sha256sum)
```

### 7.3 `base64` sobre SSH

```bash
base64 -w0 nbping | ssh root@<IP_ROUTER> "base64 -d > /tmp/nbping"
```

Más lento y con más CPU en el router, pero funciona con cualquier shell y no
necesita `sftp-server`. Para 34 KB es viable.

### 7.4 Verificación

```bash
ssh root@<IP_ROUTER> '
  chmod 755 /tmp/nbping /tmp/atcmd
  file /tmp/nbping
  /tmp/nbping 198.51.100.1 1 500
'
```

Si no hay `file`, ejecutar: un binario de arquitectura equivocada devuelve
`Exec format error` de inmediato.

---

## 8. Ejecución y persistencia

```bash
chmod 755 /tmp/nbping
/tmp/nbping 198.51.100.20 4 1000     # volátil, no consume flash
```

Instalación persistente (sobrevive reinicios):

```bash
cp /tmp/nbping /usr/bin/nbping
chmod 755 /usr/bin/nbping
nbping 198.51.100.20 4 1000
```

`/usr/bin` está sobre el overlay (`/dev/mtdblock3`). Consume ~34 KB de 25 MB;
cada escritura desgasta flash, así que no reinstalar en cada boot. Una
actualización de firmware puede reemplazar o conservar el overlay según el
procedimiento; verificar después.

Desinstalar:

```bash
rm -f /usr/bin/nbping /usr/bin/atcmd
```

Ambas asumen `root`. En NRSW el login de gestión es `root` con el password de
administración; no hay usuario sin privilegios por defecto. Si se crea uno,
`nbping` seguirá necesitando `CAP_NET_RAW` y `atcmd` permisos sobre el nodo serie.

**No hay servicio `init.d`.** Convertirlo en chequeo periódico excede el alcance y
agrega riesgo (logs en flash). Para un despliegue serio, enviar métricas fuera del
router en vez de acumularlas localmente.

---

## 9. Descubrimiento del router

### 9.1 Conexión y puertos

Conectar el host a **Ethernet 1** (LAN), no a Ethernet 5 (WAN). Configurar la
misma subred o dejar DHCP.

| Puerto | Servicio | Estado de fábrica | Nota |
|---|---|---|---|
| 22/tcp | SSH (dropbear) | Cerrado/filtrado | Habilitar desde Web Manager o CLI |
| 23/tcp | Telnet | Abierto | Sin cifrar; solo puesta en marcha |
| 80/tcp | HTTP (Web Manager) | Abierto | Administración |
| 443/tcp | HTTPS | Cerrado | Opcional |
| 53/tcp,udp | DNS | Abierto | Servidor DNS del router |
| 161/udp | SNMP | Cerrado | Opcional |

### 9.2 Primer acceso

```bash
telnet <IP_ROUTER>
# login: root   password: [REDACTED]
cli set admin.password='[REDACTED]'   # cambia y cierra la sesión
```

El cambio aplica al `root` de SSH y al Web Manager. Es el momento de habilitar SSH
y deshabilitar Telnet:

```bash
cli set ssh.status=1
cli set ssh.port=22
cli set ssh.pwlogin=1
cli set ssh.rootlogin=1
cli commit
cli get -c ssh          # verificar nombres exactos en esta versión
```

```bash
ssh -o StrictHostKeyChecking=no \
    -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    root@<IP_ROUTER>
```

### 9.3 El firewall del host también bloquea

Durante el descubrimiento el puerto 22 aparecía cerrado aunque `dropbear`
escuchaba. La causa era una regla `DENY OUT 22/tcp` de UFW en el host; Telnet (23)
no estaba bloqueado y por eso funcionaba. Revisar ambos extremos:

```bash
sudo ufw status numbered
sudo iptables -S OUTPUT
sudo nft list ruleset 2>/dev/null
```

```bash
netstat -lntp
ps | grep dropbear
```

### 9.4 CLI y telemetría

| Comando | Muestra |
|---|---|
| `cli status summary` | Resumen, serial, DIO |
| `cli status system` | Sistema, firmware, uptime |
| `cli status wwan` | Módem, SIM, registro, señal |
| `cli status gnss` | GPS/GNSS, si existe |
| `cli status eth/lan/wan` | Interfaces y configuración |
| `cli get -c <sección>` | Volcado de configuración |
| `cli set <clave>=<valor>` | Cambio de configuración |
| `cli commit` | Aplicar cambios |

| Ruta | Contenido |
|---|---|
| `/proc/cpuinfo` | Modelo y velocidad de CPU |
| `/proc/meminfo` | Memoria total/libre |
| `/proc/loadavg` | Carga 1/5/15 min |
| `/proc/uptime` | Uptime en segundos |
| `/proc/mtd` | Particiones flash |
| `/proc/net/dev` | Contadores por interfaz |
| `/sys/class/thermal/thermal_zone*/temp` | Temperatura, si hay sensor |
| `/etc/release-info` | Versión NRSW y perfil |

---

## 10. Diagnóstico celular

| Pregunta | Dónde mirar |
|---|---|
| ¿Módem presente y encendido? | `cli status wwan`, `dmesg`, `/dev/ttyACM*` |
| ¿Detecta la SIM? | `AT+CPIN?`, `cli status wwan` |
| ¿Registrado? | `AT+CREG?`, `AT+CGREG?` |
| ¿Qué señal? | `AT+CSQ` |
| ¿Qué operador? | `AT+COPS?` |
| ¿Datos adjuntos? | `AT+CGATT?`, estado de WAN |
| ¿Firmware del módem? | `ATI`, `AT+CGMR` |

### 10.1 Secuencia recomendada

```bash
P=/dev/ttyACM0
atcmd $P "AT"          2   # sanity check: OK
atcmd $P "ATI"         3   # fabricante, modelo, revisión
atcmd $P "AT+CPIN?"    3   # SIM: READY / SIM PIN / ...
atcmd $P "AT+CSQ"      3   # señal
atcmd $P "AT+CREG?"    3   # registro voz/datos
atcmd $P "AT+CGREG?"   3   # registro datos
atcmd $P "AT+COPS?"    5   # operador
atcmd $P "AT+CGATT?"   3   # attach a paquetes
atcmd $P "AT+CGSN"     3   # IMEI
```

### 10.2 Interpretar `AT+CSQ`

Respuesta: `+CSQ: <rssi>,<ber>`.

| RSSI | dBm aprox. | Interpretación |
|---|---|---|
| 0–9 | −113 a −95 | Muy débil; enlace inestable |
| 10–14 | −93 a −85 | Débil; funciona con reintentos |
| 15–19 | −83 a −75 | Aceptable |
| 20–24 | −73 a −65 | Buena |
| 25–31 | −63 a −51 | Excelente |
| 99 | Desconocido | Sin medición / sin servicio |

`ber` es la tasa de error estimada; `99` = no disponible. En tecnologías modernas
rara vez es útil.

### 10.3 Interpretar registro

| `AT+CREG?` | Estado | Significado |
|---|---|---|
| `0,1` | Registrado, red local | OK |
| `0,2` | Buscando | Sin registro todavía |
| `0,3` | Denegado | SIM, APN, roaming o cobertura |
| `0,4` | Unknown | Transitorio |
| `0,5` | Registrado, roaming | OK con acuerdo |

`AT+CGREG?` es el equivalente de datos; en LTE también `AT+CEREG?`. Un módem puede
estar registrado en voz y no adjunto a datos: consultar ambos.

### 10.4 Hallazgo de la validación

Con SIM de operador chileno, el módem detectó la SIM como `ready`, leyó ICCID e
IMSI, arrancó en WCDMA banda 5 (3G), midió **−111 a −107 dBm** (nivel 0) y quedó
en `registrationNotUsed`. Conclusión: problema de **radio/antena**, no de
software. Se cambió la antena y se reubicó el equipo; el ping a Internet por LTE
quedó pendiente. La validación de `nbping` se hizo contra la LAN, suficiente para
probar compilación, transferencia y ejecución.

### 10.5 APN y firmware

Si hay registro pero no hay IP:

```bash
cli set wwan.apn='<APN_DEL_OPERADOR>'
cli commit
cli status wwan
```

El APN depende de operador y tipo de SIM (prepago, pospago, M2M, roaming); no se
hardcodea. Verificar modo de red, banda, roaming, autenticación PAP/CHAP, estado
de WAN y ruta por defecto.

El firmware del MPCI-L280 es independiente de NRSW. Actualizarlo es delicado y
puede dejar el módem inutilizable si se interrumpe. Este repositorio **no** incluye
imágenes ni automatiza la actualización; usar el manual de NetModule y las
herramientas de u-blox.

---

## 11. Seguridad

**Modelo de amenaza.** Ambas corren como `root` en un equipo conectado a Internet.
`nbping` puede inyectar ICMP a cualquier IPv4; `atcmd` puede cambiar el estado del
módem (`AT+CFUN` reinicia el módulo) y leer identificadores. No son herramientas
de escalada: no tienen `setuid`, no abren puertos, no escriben fuera de lo que se
les indica. El riesgo está en el acceso, no en el binario.

**Credenciales.** No hay credenciales en el repositorio; los ejemplos usan
placeholders. La documentación histórica de `doc/` puede contener datos de un
equipo real: **no se enlaza** y debe sanearse o excluirse antes de publicar. Si ya
se publicó, cambiar el password del router y rotar lo expuesto. Cambiar el password
de fábrica en el primer login y no reutilizarlo.

**Telnet y SSH.** Telnet transmite en claro. Orden correcto: entrar por Telnet solo
la primera vez desde un segmento de confianza, cambiar password, habilitar SSH,
verificar, deshabilitar Telnet y restringir la gestión por firewall. El dropbear
del NB2700 es antiguo (2019.78 en el equipo probado) y acepta `ssh-rsa`/`ssh-dss`;
habilitarlos en el cliente es un compromiso. Conviene usar claves cuando se pueda,
limitar el origen, no exponer 22 a Internet y actualizar firmware si existe una
versión que modernice dropbear (el modelo está EOL, puede no haberla).

**Uso seguro de `atcmd`.** No enviar comandos desde entrada no confiable; no usar
`AT+CFUN=1,1` en producción sin ventana; no hacer polling agresivo; cuidar las
salidas porque IMEI, IMSI e ICCID son identificatorios.

**Checklist:** password cambiado y único · Telnet deshabilitado · SSH restringido ·
gestión solo desde red de administración · Web Manager no expuesto · firmware en la
última versión disponible · configuración respaldada · binarios de diagnóstico
eliminados tras la ventana · logs sin credenciales ni identificadores completos.

**Divulgación.** Vulnerabilidades: issue privado o contacto directo antes de
publicar detalles. Sin bug bounty ni SLA.

---

## 12. Rendimiento y recursos

| Binario | Tamaño | Linkeado | Stripped |
|---|---|---|---|
| `nbping` | 34.640 bytes (~34 KB) | Estático | Sí |
| `atcmd` | 40.164 bytes (~40 KB) | Estático | Sí |

`atcmd` es mayor porque arrastra más `stdio`/`termios`. Ambos caben decenas de
veces en la partición de datos y cientos en `/tmp`.

| Aspecto | `nbping` | `atcmd` |
|---|---|---|
| RAM residente | Unos cientos de KB | Similar, buffer de 4 KB |
| CPU | Despreciable (red + `select`) | Despreciable (UART) |
| Duración | `count × (timeout + 200 ms)` peor caso | Al menos `wait_s` tras el último byte |
| Flash | Ninguna desde `/tmp` | Ninguna |
| Hilos | Ninguno | Ninguno |

El router tiene 56 MB de RAM y ~31 MB libres. El riesgo no es memoria sino uso
indebido: un `count` enorme con timeout alto puede correr horas y consumir cuota
si el destino es público. Cuatro pings son ~400 bytes en total, negligible
incluso para una SIM M2M. La cadencia de 200 ms evita saturar enlaces lentos sin
hacer eterna una prueba de 10 paquetes.

La partición de datos es NAND de 25 MB. Instalar una vez es trivial; reinstalar en
cada arranque, no. Si se automatiza, comparar hash antes de copiar:

```bash
[ "$(sha256sum /tmp/nbping | cut -d' ' -f1)" = "$(sha256sum /usr/bin/nbping | cut -d' ' -f1)" ] \
  || cp /tmp/nbping /usr/bin/nbping
```

---

## 13. Resolución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| `Exec format error` | Arquitectura/endianness equivocada | Verificar `file`; recompilar con `netbox-ppc` |
| `Permission denied` | Falta bit `x` | `chmod 755 /tmp/nbping` |
| `socket ... Operation not permitted` | Sin root | Login como `root` |
| `gcc: No such file` | Toolchain no extraída o `SDK_DIR` mal | Verificar `sdk/.../bin/`; pasar `SDK_DIR=` |
| `scp: subsystem request failed` | dropbear sin `sftp-server` | `scp -O` |
| `no matching host key type` | OpenSSH moderno vs. dropbear viejo | `-o HostKeyAlgorithms=+ssh-rsa,ssh-dss` y `PubkeyAcceptedAlgorithms=+ssh-rsa` |
| `Connection timed out` al 22 | SSH deshabilitado o firewall del host | `cli set ssh.status=1`; revisar UFW/iptables |
| `Connection refused` al 23 | Telnet deshabilitado | Habilitar temporalmente o usar SSH |
| `atcmd: open: No such file` | Puerto inexistente | `ls /dev/ttyACM* /dev/ttyUSB*`; `dmesg` |
| `(sin respuesta)` | Puerto/velocidad/módem | Probar `AT`; verificar `cli status wwan`; otro nodo |
| Caracteres raros | Baudios o modo de datos | Confirmar 115200; usar puerto AT, no datos |
| `+CME ERROR: 10` | SIM ausente/error | Revisar SIM y `AT+CPIN?` |
| `+CME ERROR: 3` | Operación no permitida | `AT+CFUN?`, `AT+CPIN?` |
| 100% pérdida en LAN | IP, host apagado, firewall | Gateway, `ip addr`, `iptables -S` en destino |
| 100% pérdida a Internet | Sin ruta/NAT/APN | `ip route`, `cli status wan`, `cli status wwan` |
| RTT `0 ms` en LAN | Resolución de ms | Correcto; comparar con destino público |
| `/overlay` lleno | Logs/binarios acumulados | `df -h`; limpiar `/tmp` y overlay |
| Cambios desaparecen | Se escribió en `/tmp` | Instalar en `/usr/bin` |
| No arranca tras un cambio | Configuración inválida | Recuperación del manual; consola serie |

**Diagnóstico por capas:** física (link, cable, antena) → enlace (`ip link`,
`cli status eth`) → IP (`ip addr`, `ip route`) → ICMP (gateway, luego público) →
DNS (si por IP funciona y por nombre no) → celular (`cli status wwan`, `AT+CSQ`,
`AT+CREG?`) → datos (`AT+CGATT?`, WAN, APN) → aplicación (puerto, firewall, NAT).

---

## 14. Limitaciones y alcance

**Toolchain y firmware fuera del repositorio.** `netbox-ppc` (~17 MB) se descarga
del portal de NetModule y tiene su licencia. Las imágenes NRSW y del módem
pertenecen al fabricante y no se redistribuyen. Los binarios no se versionan: se
generan con `make`. Clonar el repo no alcanza para ejecutar nada; hay que obtener
la toolchain, compilar y transferir. Es un repositorio de código y documentación,
no un release.

**Cobertura.** Solo NB2700 con NRSW 4.6.0.105. La toolchain cubre otros PowerPC de
la familia, pero no hay garantía entre revisiones de firmware. No se probó en ARM;
debería compilar, pero no está validado.

**EOL.** NetModule declaró el NB2700 fin de vida y el soporte terminó en marzo de
2025: no habrá NRSW nuevo, los repositorios y toolchains pueden desaparecer, las
vulnerabilidades no se parchean. Sirve para laboratorio, formación y sistemas
legacy, no para despliegues nuevos.

**Extensiones de cliente.** NetModule no soporta oficialmente extensiones en el
firmware. Los binarios corren como root vía SSH, fuera del marco de soporte. Todo
problema derivado es responsabilidad del operador.

**Funcionalidad.** `nbping`: IPv4, ICMP, sin DNS/IPv6 ni selección de interfaz.
`atcmd`: un comando por invocación, 115200 fijo, sin sesión interactiva. Sin tests
automatizados, CI ni integración con monitoreo. Sin daemon, recolección periódica,
exportación de métricas, manejo de señales ni limpieza de estado del módem.

**Documentación histórica.** `doc/` contiene una bitácora del equipo real con
posibles direcciones, credenciales, seriales e identificadores de SIM. No se
enlaza y no debe publicarse sin sanear. La documentación nueva vive en `docs/`.

---

## 15. Cómo se construyó

Sesión de trabajo sobre hardware real, 24 de septiembre de 2026:

1. **Identificación.** Telnet, `uname -m` → `ppc`, `/proc/cpuinfo` → e300c3,
   `/etc/release-info` → NRSW 4.6.0.105. Se documentaron puertos, RAM, flash y DIO.
2. **Red.** Ethernet 1–4 = LAN; Ethernet 5 = WAN; gestión en LAN con DHCP.
3. **Toolchain.** Se buscó un SDK ya descargado: el de otra marca era MIPS y además
   había desaparecido de `/tmp`. Había que bajar `netbox-ppc`.
4. **Primer binario.** `nbping.c` compilado, strippeado y transferido; 4/4 replies
   en LAN con RTT sub-milisegundo.
5. **Persistencia.** Copia en `/usr/bin`; verificado que sobrevive reinicios.
6. **Segundo binario.** `atcmd.c` compilado y transferido; usado para leer el módem.
7. **Celular.** SIM detectada, sin registro por señal muy débil; prueba de Internet
   pendiente de mejor antena.
8. **Documentación.** README, manual de operador, guía de desarrollador,
   referencias y diagramas.

| Paso | Resultado | Evidencia |
|---|---|---|
| Toolchain en el host | ✅ | `powerpc-openwrt-linux-gcc --version` |
| `nbping` compila | ✅ | ELF MSB PowerPC estático |
| `nbping` se transfiere | ✅ | `scp -O` sin errores |
| `nbping` ejecuta | ✅ | 4/4 replies contra host LAN |
| `nbping` persiste | ✅ | `/usr/bin` + reinicio |
| `atcmd` compila/transfiere | ✅ | Mismo ELF; `scp -O` |
| `atcmd` ejecuta | ✅ | Respuestas AT del módem |
| SIM detectada | ✅ | `AT+CPIN?` → `READY` |
| Registro celular | ❌ | −107 dBm, sin registro |
| Ping a Internet por LTE | ⏸️ | Pendiente de antena/cobertura |

La validación del pipeline no depende del último paso: compilar, transferir y
ejecutar ya quedó demostrado. Falta conectividad de datos móviles, que es una
condición del entorno, no del software.

| Decisión | Alternativa | Motivo |
|---|---|---|
| Estático | Dinámico contra uClibc | Evitar incompatibilidades de versión |
| Raw socket propio | `ping` de BusyBox | Validar el binario cross-compilado |
| `-Os` | `-O2` | Flash y RAM escasas |
| `atcmd` de un disparo | Consola interactiva | Menos estado, fácil de scriptear |
| Sin target en Makefile | Target adicional | Preservar el artefacto validado |
| Docs en `docs/` | Reescribir `doc/` | No alterar el registro histórico |

---

## 16. Estructura del repositorio

```text
nb2700-telemetria/
├── README.md                     # Este documento
├── LICENSE                       # MIT, Copyright (c) 2026 wanderc137
├── Makefile                      # Cross-compile de nbping
├── nbping.c                      # Ping ICMP, raw sockets (187 líneas)
├── atcmd.c                       # Utilidad AT de un disparo (85 líneas)
├── .gitignore                    # sdk/, firmware/, binarios, credenciales
├── doc/
│   └── 00-descubrimiento-router.md   # Registro histórico (no saneado)
├── docs/
│   ├── datasheet.md              # Manual de operador
│   ├── developer.md              # Código, build y protocolos
│   ├── REFERENCES.md             # Fuentes anotadas
│   ├── BITACORA.md               # Problemas, decisiones y lecciones
│   └── assets/
│       ├── architecture.svg
│       ├── cross-compile.svg
│       └── telemetry-flow.svg
├── firmware/                     # (excluido) imágenes NRSW y u-blox
└── sdk/                          # (excluido) toolchain netbox-ppc
```

| Ruta | ¿En git? | Motivo |
|---|---|---|
| `*.c`, `Makefile`, `README.md`, `docs/`, `LICENSE` | Sí | Código y documentación |
| `nbping`, `atcmd` | No | Artefactos de build |
| `sdk/` | No | Toolchain de terceros, ~17 MB |
| `firmware/` | No | Imágenes de fabricante, ~45 MB |
| `doc/` | Sí (heredado) | Registro histórico; sanear antes de publicar |

---

## 17. Referencias

- **NetModule Wiki** — toolchain, SDK, CLI y modelos: <https://wiki.netmodule.com>
- **Descargas NetModule** — toolchains y releases NRSW:
  <https://share.netmodule.com/router/public/toolchain/>
- **RFC 792** — ICMP: <https://www.rfc-editor.org/rfc/rfc792>
- **RFC 1071** — Internet Checksum: <https://www.rfc-editor.org/rfc/rfc1071>
- **Linux `raw(7)`** — raw sockets IPv4:
  <https://man7.org/linux/man-pages/man7/raw.7.html>
- **Linux `termios(3)`** — terminal:
  <https://man7.org/linux/man-pages/man3/termios.3.html>
- **u-blox TOBY-L2 / MPCI-L2** — manual de comandos AT del fabricante.
- **Dropbear SSH** — <https://matt.ucc.asn.au/dropbear/>
- **OpenWrt** — base de NRSW: <https://openwrt.org/docs/>
- **BusyBox** — <https://busybox.net/>
- **Pipeline NetModule** — <https://github.com/wanderc137/opencode-pipelines>

Bibliografía anotada, con qué se usó de cada fuente y qué se verificó en el equipo,
en [`docs/REFERENCES.md`](docs/REFERENCES.md).

---

## 18. Licencia

MIT. Ver [`LICENSE`](LICENSE).

```text
Copyright (c) 2026 wanderc137
```

El software se entrega "tal cual", sin garantía. El uso en producción, la
modificación del módem y la exposición de la gestión a Internet son
responsabilidad del operador.

---

<p align="center">
  <sub>Pipeline NetModule · <a href="https://github.com/wanderc137/opencode-pipelines">opencode-pipelines</a> · <a href="https://opencode.ai">opencode</a></sub>
</p>
