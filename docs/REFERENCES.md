# Referencias y fuentes

Bibliografía anotada del proyecto. Cada entrada indica qué se consultó, para qué
sirvió y qué se verificó en el equipo. Las URLs se consultaron durante septiembre
de 2026; los portales de fabricante pueden cambiar sin aviso.

---

## 1. Documentación de NetModule

### 1.1 Wiki oficial

- **NetModule Wiki** — <https://wiki.netmodule.com>
- Uso: procedimientos de toolchain, referencia del CLI, configuración de SSH,
  compilación de software propio y particularidades de NRSW.
- Verificado: estructura de `cli status`, nombres de secciones (`wwan`, `eth`,
  `lan`, `wan`, `gnss`), comportamiento de `cli set`/`cli commit`.
- Nota: la wiki cubre varios modelos y versiones. Conviene contrastar siempre con
  la versión de firmware del equipo; un comando puede existir en una rama y no en
  otra.

### 1.2 Portal de descargas

- **NetModule Share** — <https://share.netmodule.com/router/public/toolchain/>
- Uso: descarga de `toolchain-ppc-4.4.5-x86_64.tar.bz2` y de imágenes NRSW.
- Verificado: el archivo comprimido pesa ~17 MB y, al extraerlo, contiene
  `bin/powerpc-openwrt-linux-gcc` con GCC 4.4.5.
- Nota: las toolchains y los firmware no se redistribuyen en este repositorio.
  Descargar desde el origen y verificar el checksum si el portal lo publica.

### 1.3 Manual del modelo

- **NetModule NB2700** — manual de usuario y guía de configuración del fabricante.
- Uso: significados de LEDs, puertos, DIO, procedimiento de recuperación,
  configuración de WAN celular y actualización de firmware.
- Verificado en el equipo: correspondencia Ethernet 1–4 = LAN, Ethernet 5 = WAN;
  IP de gestión en la LAN; particiones `boot`, `root0`, `root1`, `data`.
- Nota: el NB2700 está EOL. Los manuales pueden seguir publicados en formato PDF
  aunque el producto ya no se venda.

### 1.4 Soporte del fabricante

- **NetModule Support** — `support@netmodule.com` (canal oficial).
- Uso: consultas sobre toolchain y compatibilidad.
- Limitación: NetModule no soporta oficialmente extensiones de cliente. Un
  binario propio que corre como root queda fuera del alcance del soporte.

---

## 2. Estándares de red

### 2.1 RFC 792 — ICMP

- **Internet Control Message Protocol** — <https://www.rfc-editor.org/rfc/rfc792>
- Uso: formato de la cabecera ICMP, tipos `0` (echo reply) y `8` (echo request),
  campos identifier y sequence.
- Verificado: `nbping` construye exactamente esos campos y valida el tipo de
  respuesta.

### 2.2 RFC 1071 — Internet Checksum

- **Computing the Internet Checksum** —
  <https://www.rfc-editor.org/rfc/rfc1071>
- Uso: algoritmo de suma de 16 bits con plegado de acarreos y complemento a uno.
- Verificado: los paquetes enviados por `nbping` obtienen reply, lo que implica
  que el kernel del router aceptó el checksum.

### 2.3 RFC 791 — IPv4

- **Internet Protocol** — <https://www.rfc-editor.org/rfc/rfc791>
- Uso: interpretación de `iph->ihl` para saltar la cabecera IP en el raw socket.
- Verificado: el offset se calcula como `ihl * 4` y el parseo funciona con
  cabeceras de 20 bytes (sin opciones).

---

## 3. Sistemas operativos y utilidades

### 3.1 Linux `raw(7)`

- <https://man7.org/linux/man-pages/man7/raw.7.html>
- Uso: semántica de `SOCK_RAW`, requisito de `CAP_NET_RAW`, entrega del paquete
  IP completo al espacio de usuario.
- Verificado: sin root, `socket()` falla con `EPERM`; con root, `recvfrom()`
  entrega la cabecera IP incluida.

### 3.2 Linux `termios(3)`

- <https://man7.org/linux/man-pages/man3/termios.3.html>
- Uso: configuración de 115200 8N1, modo raw, `VMIN`/`VTIME`, diferencia entre
  `cfsetispeed`/`cfsetospeed` y asignación directa de `B115200`.
- Verificado: el módem respondió a AT con la configuración aplicada.

### 3.3 Linux `select(2)`

- <https://man7.org/linux/man-pages/man2/select.2.html>
- Uso: espera con timeout en `nbping` y `atcmd`, comportamiento ante `EINTR`.
- Verificado: los timeouts se respetan en la práctica; el detalle de reinicio
  del plazo ante paquetes ajenos está documentado como limitación.

### 3.4 OpenWrt

- **Documentación** — <https://openwrt.org/docs/>
- Uso: concepto de overlay, `procd`/`init.d`, estructura de `/etc/config`,
  differences entre imagen squashfs+overlay y jffs2.
- Verificado: `/rom` de solo lectura, `/overlay` sobre `mtdblock3`, raíz `overlay`.

### 3.5 BusyBox

- <https://busybox.net/>
- Uso: comandos disponibles en el shell (`nc`, `sha256sum`, `ping`, `nslookup`),
  sintaxis de `ash`.
- Verificado: `sha256sum` y `nc` presentes; `sftp-server` no está en dropbear.

### 3.6 Dropbear SSH

- <https://matt.ucc.asn.au/dropbear/>
- Uso: compatibilidad de algoritmos, ausencia de `sftp-server`, versiones.
- Verificado: banner `SSH-2.0-dropbear_2019.78`; `scp` requiere `-O`; OpenSSH
  moderno requiere habilitar `ssh-rsa`/`ssh-dss`.

---

## 4. Módem u-blox

### 4.1 Familia TOBY-L2 / MPCI-L2

- **u-blox** — documentación de producto y manuales AT de la familia TOBY-L2.
- Uso: comandos `AT`, `ATI`, `AT+CPIN?`, `AT+CSQ`, `AT+CREG?`, `AT+CGREG?`,
  `AT+COPS?`, `AT+CGATT?`, `AT+CGSN`, `AT+CFUN`, `AT+CGDCONT`, `AT+CESQ`.
- Verificado: el módulo respondió a los comandos básicos y devolvió estado de SIM,
  señal y registro.
- Nota: el manual exacto depende de la revisión de firmware del módem. Los
  códigos `+CME ERROR` pueden variar entre versiones.

### 4.2 Notas de aplicación

- **u-blox** — notas sobre diseño de antena, RSSI/RSRP/RSRQ/SINR y selección de
  banda.
- Uso: interpretar −107 dBm como señal muy débil y descartar el software como
  causa del no-registro.
- Verificado: con esa señal el módem quedó en `registrationNotUsed`.

---

## 5. Herramientas de desarrollo

### 5.1 GCC 4.4.5

- **Manual de GCC 4.4.5** — <https://gcc.gnu.org/onlinedocs/gcc-4.4.5/>
- Uso: opciones `-static`, `-Os`, `-Wall`, `-Wextra`; estándar por defecto
  (gnu89); ausencia de características modernas.
- Verificado: el código compila sin advertencias con `-Wall -Wextra` en la
  toolchain del proyecto.

### 5.2 GNU Make

- **Manual de GNU Make** — <https://www.gnu.org/software/make/manual/>
- Uso: variables `?=` y `=`, `CURDIR`, reglas, `.PHONY`.
- Verificado: `make` y `make SDK_DIR=...` funcionan; `make clean` borra el target.

### 5.3 binutils

- **`readelf`** y **`file`** — inspección de cabeceras ELF.
- Uso: verificar clase, endianness, máquina y tipo de linkeo.
- Verificado: `ELF32`, big endian, PowerPC, `EXEC`, estático y stripped.

---

## 6. Fuentes del pipeline

### 6.1 opencode-pipelines

- <https://github.com/wanderc137/opencode-pipelines>
- Uso: contexto de automatización del pipeline NetModule, convenciones de
  documentación y despliegue.
- Nota: este repositorio es el artefacto de un proyecto puntual, no el pipeline
  completo.

### 6.2 opencode

- <https://opencode.ai>
- Uso: entorno de desarrollo asistido usado para escribir, compilar y documentar.

---

## 7. Fuentes descartadas y por qué

| Fuente | Motivo del descarte |
|---|---|
| SDK de otra marca (MIPS) | Arquitectura incompatible; además no persistía en `/tmp` |
| Toolchain genérica de OpenWrt para PowerPC | No garantiza compatibilidad con uClibc 0.9.31 ni con el kernel de NRSW |
| `ping` de BusyBox como reemplazo | No valida el binario cross-compilado, que es el objetivo del proyecto |
| `microcom`/`picocom`/`screen` para AT | No están instalados por defecto en NRSW |
| Documentación histórica de `doc/` | Contiene datos de un equipo real; no se enlaza ni se cita |
| Foros no oficiales sobre NB2700 | Información sin verificar y, en varios casos, desactualizada |

---

## 8. Cómo citar este proyecto

```text
wanderc137. nb2700-telemetria: utilidades C de diagnóstico para NetModule NB2700.
GitHub, 2026. https://github.com/wanderc137/nb2700-telemetria
```

Para las fuentes de terceros, citar la entrada original (RFC, manual de NetModule,
manual de u-blox) y no este resumen.

---

## 9. Verificación y vigencia

| Dato | Fecha | Cómo se verificó |
|---|---|---|
| NRSW 4.6.0.105 | 2026-09-24 | `cat /etc/release-info` |
| CPU e300c3 | 2026-09-24 | `cat /proc/cpuinfo` |
| Arquitectura ppc | 2026-09-24 | `uname -m` |
| Toolchain GCC 4.4.5 | 2026-09-24 | `--version` y `file` del binario |
| Módem MPCI-L280 | 2026-09-24 | `cli status wwan`, `ATI` |
| SIM detectada | 2026-09-24 | `AT+CPIN?` |
| Señal −107 dBm | 2026-09-24 | `AT+CSQ` y `cli status wwan` |

Los enlaces se comprobaron en la fecha indicada. Si una URL de fabricante deja de
funcionar, buscar el documento por nombre en el sitio oficial antes de recurrir a
copias de terceros.
