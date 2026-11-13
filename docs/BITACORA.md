# Bitácora de desarrollo

Registro de los problemas encontrados, las decisiones tomadas y las lecciones que
deja el proyecto. Está escrito para que un futuro mantenedor no repita los mismos
callejones sin salida.

**Periodo:** 24 de septiembre de 2026
**Equipo:** NetModule NB2700, NRSW 4.6.0.105, PowerPC e300c3
**Objetivo:** validar el pipeline de cross-compilación con la toolchain oficial y
construir dos utilidades de diagnóstico.

---

## 1. Punto de partida

El equipo llegó con firmware de fábrica, Telnet y Web Manager accesibles, SSH
deshabilitado y sin documentación propia. El plan inicial era modesto: compilar un
programa C, copiarlo y ejecutarlo. La primera sorpresa fue que el entorno de
desarrollo no tenía nada preparado para PowerPC.

### Lo que se sabía al comenzar

- El router respondía en la LAN y permitía login por Telnet.
- `uname -m` devolvía `ppc`.
- La toolchain de NetModule se descarga del portal del fabricante.
- El proyecto hermano de otra marca usaba una toolchain MIPS.

### Lo que se descubrió en la primera hora

| Hallazgo | Consecuencia |
|---|---|
| La toolchain MIPS de otro proyecto no servía | Arquitectura y endianness distintos |
| Esa toolchain además había desaparecido de `/tmp` | El directorio temporal se limpia al reiniciar |
| No había ningún SDK PowerPC en el sistema | Había que descargar `netbox-ppc` |
| El firmware trae BusyBox con `ping` | `nbping` debía justificarse como validación, no como reemplazo |
| El módem es un u-blox MPCI-L280 | El acceso AT sería por puerto serie, no por red |

---

## 2. Problemas y soluciones

### 2.1 Toolchain inexistente y SDK perdido

**Síntoma.** No había compilador capaz de generar un binario para el NB2700.

**Investigación.** Se buscó en todo el sistema:

- `find / -name '*toolchain*'`, `find / -name '*openwrt*'`;
- `/tmp/opencode/` estaba vacío;
- solo había `gcc` nativo x86_64;
- Docker tenía imágenes sin relación.

**Causa.** El SDK de otro proyecto se había descargado en `/tmp`, que se limpia al
reiniciar. Además, aunque hubiera sobrevivido, era MIPS.

**Solución.** Descargar `toolchain-ppc-4.4.5-x86_64.tar.bz2` desde
`https://share.netmodule.com/router/public/toolchain/`, extraerlo y verificar que
`powerpc-openwrt-linux-gcc` compila un ELF PowerPC.

**Lección.** Nunca dejar una toolchain en `/tmp`. Guardarla en el proyecto
(`sdk/`) o en una ruta persistente del usuario, y excluirla de git por tamaño y
licencia.

### 2.2 El puerto 22 “cerrado” que no lo estaba

**Síntoma.** `ssh root@<IP_ROUTER>` daba `Connection timed out`, pero
`telnet <IP_ROUTER>` funcionaba y `dropbear` corría en el router.

**Diagnóstico.** En el router:

```bash
netstat -lntp        # dropbear escuchando en 22
ps | grep dropbear
```

En el host:

```bash
sudo ufw status numbered
# [1] 22/tcp DENY OUT Anywhere
```

**Causa raíz doble:**

1. UFW del host tenía una regla `DENY OUT 22/tcp` **antes** de la regla `ALLOW`.
   El SYN nunca salía. Telnet (23) no estaba bloqueado, por eso funcionaba.
2. OpenSSH 9.6 del host rechazaba las claves de dropbear 2019.78
   (`ssh-rsa`/`ssh-dss`): `no matching host key type found`.

**Solución.**

```bash
sudo ufw allow out to <IP_ROUTER> port 22 proto tcp
sudo ufw delete deny out 22/tcp
```

Y en el cliente:

```bash
ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    root@<IP_ROUTER>
```

**Lección.** Cuando un puerto parece filtrado, revisar **los dos extremos** y el
orden de las reglas. Un `DENY` global de salida es fácil de olvidar y produce un
síntoma idéntico a un servicio apagado.

### 2.3 `scp` falla contra dropbear

**Síntoma.** `scp nbping root@<IP_ROUTER>:/tmp/` terminaba con
`subsystem request failed`.

**Causa.** El `scp` moderno usa el subsistema SFTP; el dropbear del NB2700 no
incluye `sftp-server`.

**Solución.** Forzar el protocolo clásico:

```bash
scp -O nbping root@<IP_ROUTER>:/tmp/nbping
```

**Lección.** `-O` es obligatorio con dropbear antiguos. Si además hay problemas de
algoritmos, agregar las opciones de host key. Tener siempre un plan B (netcat o
base64 sobre SSH).

### 2.4 Primer cross-compile

**Objetivo.** Producir un binario que el router acepte.

**Comando.**

```bash
CC=sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc
$CC -static -Os -Wall -Wextra -o nbping nbping.c
sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-strip nbping
```

**Verificación.**

```text
ELF 32-bit MSB executable, PowerPC or cisco 4500, version 1 (SYSV),
statically linked, stripped
```

**Resultado.** El binario se copió y ejecutó. Cuatro pings a un host de la LAN
devolvieron 4/4 replies con RTT sub-milisegundo.

**Lección.** Verificar `file` y `readelf` antes de transferir. Un binario con la
arquitectura equivocada falla con `Exec format error` y hace perder tiempo.

### 2.5 `nbping`: checksum, alineación y validación

**Contexto.** El checksum ICMP se calcula sobre un `char[64]` casteado a
`unsigned short *`.

**Riesgo.** En PowerPC un acceso de 16 bits desalineado puede generar una
excepción. En la práctica el array quedó alineado y funcionó, pero no es una
garantía del lenguaje.

**Decisiones.**

- Se mantuvo el cálculo simple porque el objetivo era la validación del pipeline.
- Se documentó como deuda técnica.
- Si se reescribe, usar lectura byte a byte o `__attribute__((aligned(2)))`.

**Otras debilidades detectadas.** No se valida el tamaño del paquete recibido
antes de leer la cabecera ICMP; no se compara la IP de origen; el timeout de
`select()` se reinicia con paquetes ajenos. Ninguna afecta el uso en laboratorio,
pero deberían corregirse antes de exponer la herramienta a tráfico hostil.

**Lección.** Un programa que funciona no es lo mismo que un programa correcto.
Documentar el filo es parte del trabajo.

### 2.6 `atcmd`: el comando que no estaba en el Makefile

**Situación.** El Makefile se escribió para `nbping`. `atcmd` se compiló a mano
con la misma toolchain y quedó fuera del build automatizado.

**Por qué no se corrigió.** El Makefile era el artefacto validado; agregar un
target cambiaba el binario de la validación. Se prefirió documentar el comando
manual antes que introducir una modificación no probada.

**Consecuencia.** Un clon nuevo del repositorio ejecuta `make` y obtiene solo
`nbping`. `atcmd` requiere el comando explícito de `docs/developer.md`.

**Deuda.** Agregar el target es trivial y debería hacerse en la próxima iteración,
con una prueba de humo en el router.

### 2.7 `dprintf` y la uClibc de 2010

**Síntoma potencial.** `dprintf` es POSIX.1-2008; con `-std=c99` y `-Wall` puede
aparecer como declaración implícita.

**Realidad.** El binario compiló y funcionó, así que uClibc la declaró. Aun así,
es una dependencia innecesaria.

**Alternativa robusta.**

```c
char line[256];
int n = snprintf(line, sizeof(line), "%s\r", argv[2]);
if (n > 0) write(fd, line, (size_t)n);
```

**Lección.** En toolchains viejas, preferir funciones de C89 antes que POSIX
reciente. `snprintf` + `write` es más portable que `dprintf`.

### 2.8 `termios`: configuración directa y sin restauración

**Observaciones.**

- Se asigna `B115200` directamente en `c_cflag` en vez de usar
  `cfsetispeed()`/`cfsetospeed()`.
- No se guarda ni restaura la configuración original.
- No se usa `tcflush()`; se vacía leyendo.

**Impacto.** Funciona con uClibc y el driver del módem, pero no es portable ni
teóricamente correcto. Para una versión interactiva habría que corregirlo.

**Lección.** El código de diagnóstico tiende a acumular atajos. Conviene marcar
cada atajo con un comentario `/* TODO: ... */` para que la deuda sea visible.

### 2.9 El módem detecta la SIM pero no se registra

**Síntoma.** Con SIM insertada:

- `AT+CPIN?` → `READY`;
- ICCID e IMSI legibles;
- banda WCDMA 5 (3G);
- señal entre −111 y −107 dBm;
- `registrationNotUsed`;
- sin conectividad IP.

**Diagnóstico.** La señal estaba por debajo del umbral práctico. No era un
problema de APN ni de configuración: el módem no completaba el registro.

**Acción.** Cambiar la antena y reubicar el equipo. La prueba de Internet por LTE
quedó pendiente.

**Lección.** Antes de tocar APN, PDP o rutas, verificar señal y registro. Un
`AT+CSQ` de 0–9 explica más que veinte cambios de configuración.

### 2.10 Persistencia: `/tmp` versus overlay

**Síntoma.** Un binario copiado a `/tmp` desaparecía tras reiniciar.

**Causa.** `/tmp` es `tmpfs`; el overlay persistente está en `/dev/mtdblock3`,
montado sobre `/`.

**Solución.** Copiar a `/usr/bin` y `chmod 755`. Verificado tras reinicio.

**Lección.** Distinguir siempre `tmpfs` de overlay en sistemas embebidos. La
persistencia no es gratuita: consume flash y puede perderse en una actualización
de firmware.

### 2.11 Documentación histórica con datos sensibles

**Situación.** El directorio `doc/` contiene una bitácora de descubrimiento del
equipo real. Incluye direcciones de la instalación, un password de laboratorio,
serial del router e identificadores de SIM.

**Restricción.** La consigna de este trabajo prohíbe modificar `doc/`. Por lo
tanto:

- la documentación nueva vive en `docs/`;
- el README **no enlaza** `doc/`;
- este documento advierte que ese material debe sanearse o excluirse antes de
  publicar el repositorio.

**Acción pendiente antes de hacer público el repo:**

1. Revisar `doc/00-descubrimiento-router.md` y reemplazar credenciales, seriales,
   ICCID/IMSI, IPs y rutas personales por placeholders.
2. O mover el archivo fuera del repositorio y conservarlo como registro privado.
3. Cambiar el password del router si el repositorio ya estuvo público.
4. Verificar el historial de git: un archivo borrado sigue en commits anteriores.

**Lección.** La documentación de descubrimiento es útil, pero se escribe con datos
reales. Sanearla es parte del trabajo, no un paso opcional. Si no se puede
modificar, hay que aislarla y avisar.

---

## 3. Vías descartadas

| Vía | Por qué se descartó |
|---|---|
| Reutilizar el SDK MIPS de otro proyecto | Arquitectura incompatible; además se había perdido |
| Usar `gcc` nativo con `-m32` | Genera x86, no PowerPC |
| Usar una toolchain genérica de OpenWrt | No garantiza compatibilidad con uClibc 0.9.31 ni con NRSW |
| Linkear dinámico | Dependencia de la versión exacta de libc del firmware |
| Reemplazar `ping` de BusyBox | No valida el pipeline, que es el objetivo |
| Instalar `setuid root` | Riesgo mayor en un equipo de campo |
| `microcom`/`picocom`/`screen` para AT | No están en el firmware por defecto |
| Consola AT interactiva | Más estado y más superficie de error; el one-shot cubre el caso |
| Daemon de telemetría con MQTT | Fuera de alcance; agrega dependencias y escritura en flash |
| Guardar logs en el router | Desgaste de NAND y riesgo de llenar el overlay |
| `scp` sin `-O` | Falla por falta de `sftp-server` |
| Exponer SSH a Internet | Superficie de ataque innecesaria en un equipo EOL |

---

## 4. Decisiones técnicas y su justificación

| Decisión | Alternativa | Motivo |
|---|---|---|
| `-static` | Dinámico | Evitar incompatibilidades de uClibc |
| `-Os` | `-O2` | Flash y RAM escasas |
| Raw socket propio | `ping` del sistema | Validar el binario cross-compilado |
| `-Wall -Wextra` | Sin warnings | Código chico; las advertencias son baratas |
| `atcmd` one-shot | Interactivo | Menos estado, fácil de scriptear |
| Sin Makefile para `atcmd` | Target adicional | Preservar el artefacto validado |
| Documentar en `docs/` | Reescribir `doc/` | No alterar el registro histórico |
| Placeholders en ejemplos | IPs reales | Evitar filtrar instalaciones |
| Sin imágenes de firmware | Incluirlas | Licencia, tamaño y riesgo de redistribución |
| Sin binarios en git | Incluirlos | Se regeneran; el repo queda liviano |

---

## 5. Cronología resumida

| Hora aproximada | Hito |
|---|---|
| Inicio | Telnet, identificación de hardware y red |
| +30 min | Búsqueda fallida de toolchain existente |
| +45 min | Descarga de `netbox-ppc` |
| +60 min | Toolchain extraída y verificada |
| +75 min | `nbping.c` compilado |
| +90 min | SSH resuelto (UFW + host keys) |
| +100 min | `scp -O` y primera ejecución en el router |
| +120 min | Persistencia en `/usr/bin` |
| +140 min | `atcmd.c` compilado y transferido |
| +160 min | Diagnóstico celular: SIM OK, señal muy débil |
| +180 min | Documentación y diagramas |

Los tiempos son aproximados y sirven para dimensionar un trabajo similar, no como
métrica de productividad.

---

## 6. Lecciones aprendidas

1. **La arquitectura manda.** Un binario MIPS no corre en PowerPC, por más que
   ambos sean "Linux embebido". Verificar `file` antes de transferir.
2. **`/tmp` no es persistente.** Ni en el host ni en el router. Guardar toolchains
   y binarios en rutas estables.
3. **El firewall del host existe.** Un puerto "cerrado" puede ser una regla de
   salida en la notebook, no un servicio apagado.
4. **Dropbear no es OpenSSH.** `scp -O` y algoritmos legacy son la norma en
   firmware de 2019.
5. **Estático simplifica.** En un equipo con uClibc de 2010, linkear estático
   evita una clase entera de errores.
6. **La señal se mide antes de configurar.** APN, PDP y rutas no arreglan una
   antena desconectada.
7. **Persistencia = flash.** Cada copia a `/usr/bin` desgasta NAND; hacerlo con
   conciencia.
8. **Documentar el filo.** Las limitaciones del código son parte de la entrega,
   no un secreto.
9. **Sanear antes de publicar.** Los datos de laboratorio (IPs, seriales, SIM,
   passwords) no van a un repo público, ni siquiera en el historial.
10. **Un artefacto validado vale más que un build perfecto.** Si el Makefile
    funciona para `nbping`, no se lo cambia sin revalidar en el router.

---

## 7. Trabajo pendiente

| Ítem | Tipo | Prioridad |
|---|---|---|
| Agregar `atcmd` al Makefile | Build | Alta |
| Sanear o excluir `doc/` antes de publicar | Seguridad | Alta |
| Validar ping a Internet con LTE y antena adecuada | Prueba | Media |
| Validar en otro modelo PowerPC de la familia | Compatibilidad | Media |
| Corregir validación de longitud en `recvfrom` | Robustez | Media |
| Verificar IP de origen del reply | Robustez | Media |
| Alinear el buffer o calcular checksum byte a byte | Portabilidad | Media |
| Agregar `SO_BINDTODEVICE` | Funcionalidad | Baja |
| Agregar salida JSON | Funcionalidad | Baja |
| Restaurar `termios` y usar `cfsetispeed` | Corrección | Baja |
| Tests automatizados en host | Calidad | Baja |
| Explorar compilación ARM con `netbolt-arm` | Compatibilidad | Baja |

---

## 8. Conclusión

El proyecto cumplió su objetivo: demostrar que un binario C compilado con la
toolchain oficial de NetModule corre en un NB2700, se transfiere por la red y
responde a estímulos reales. Las dos utilidades son pequeñas, pero el aprendizaje
del camino —toolchain, arquitectura, SSH, transferencia, persistencia, módem— es
el activo principal.

El siguiente paso lógico no es agregar funciones, sino cerrar la deuda de
seguridad y build: sanear la documentación histórica, incorporar `atcmd` al
Makefile y validar el enlace celular con una antena adecuada. Con eso, el proyecto
queda en condiciones de servir como plantilla para otros equipos de la familia
`netbox-ppc`.
