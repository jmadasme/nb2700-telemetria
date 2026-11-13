# Manual de operador — nb2700-telemetria

**Equipo:** NetModule NB2700 (NRSW/OpenWrt, PowerPC)
**Utilidades:** `nbping` (ICMP) y `atcmd` (AT sobre serie)
**Audiencia:** técnicos de campo, integradores y operadores de red
**Versión documentada:** NRSW 4.6.0.105, toolchain `netbox-ppc` (GCC 4.4.5 + uClibc 0.9.31)

---

## 1. Propósito

Este manual explica cómo instalar y usar dos utilidades de diagnóstico en un
router NetModule NB2700:

- **`nbping`** comprueba conectividad IPv4 desde la propia pila de red del router,
  con pérdida de paquetes y RTT.
- **`atcmd`** envía un comando AT al módem celular u-blox y muestra la respuesta
  cruda, sin pasar por el resumen del CLI de NetModule.

Las herramientas no son servicios: se ejecutan cuando el operador las invoca y
terminan solas. No modifican la configuración del router ni del módem, salvo que
el comando AT enviado lo haga explícitamente.

---

## 2. Requisitos

### 2.1 En el router

| Requisito | Detalle |
|---|---|
| Modelo | NB2700 (probado). Familia PowerPC `netbox-ppc`: NB1600, NB2710, NB3700, NB3710 |
| Firmware | NRSW 4.6.0.105 probado; otras versiones no validadas |
| Acceso | `root` por SSH o Telnet |
| Espacio | ~40 KB por binario; `/tmp` o `/usr/bin` |
| Módem | u-blox MPCI-L280 presente para usar `atcmd` |

### 2.2 En el puesto de trabajo

| Requisito | Detalle |
|---|---|
| Sistema | Linux x86_64 |
| Herramientas | `ssh`, `scp`, `sha256sum`, opcionalmente `nc` |
| Toolchain | `netbox-ppc` para compilar (no incluida en el repositorio) |
| Red | Interfaz en la LAN del router o ruta hacia ella |

### 2.3 Conocimientos mínimos

- Login por SSH y manejo de archivos en Linux.
- Diferencia entre LAN y WAN.
- Lectura de una tabla de rutas (`ip route`).
- Nociones de AT y de estados de registro celular.

---

## 3. Antes de empezar

### 3.1 Identificar el equipo

```bash
uname -m                    # debe decir: ppc
cat /etc/release-info       # version y profile
cat /proc/cpuinfo           # e300c3 @ 400 MHz
cat /proc/mtd               # particiones
df -h                       # espacio libre
```

Si `uname -m` no es `ppc`, **detenerse**: el binario de este proyecto no es para
ese equipo.

### 3.2 Comprobar el acceso

```bash
# Telnet (de fábrica)
telnet <IP_ROUTER>

# SSH (si está habilitado)
ssh -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    root@<IP_ROUTER>
```

Si SSH falla con `no matching host key type`, el cliente es moderno y el dropbear
del router es antiguo. Usar las opciones anteriores. Si falla con `Connection
timed out`, revisar el firewall del puesto de trabajo además del router.

### 3.3 Verificar el binario antes de copiarlo

En el host:

```bash
file nbping
# ELF 32-bit MSB executable, PowerPC ..., statically linked, stripped
sha256sum nbping
```

Guardar el hash. Se usará para verificar la copia.

---

## 4. Instalación

### 4.1 Transferencia por `scp`

```bash
scp -O nbping root@<IP_ROUTER>:/tmp/nbping
scp -O atcmd  root@<IP_ROUTER>:/tmp/atcmd
```

`-O` fuerza el protocolo SCP clásico. El `dropbear` del NB2700 no trae
`sftp-server`; sin `-O`, el comando falla con `subsystem request failed`.

Si el cliente exige algoritmos legacy:

```bash
scp -O -o StrictHostKeyChecking=no \
  -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
  -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  nbping root@<IP_ROUTER>:/tmp/nbping
```

### 4.2 Alternativa por `netcat`

```bash
# En el host
nc -l -p 9999 < nbping

# En el router
nc <IP_HOST> 9999 > /tmp/nbping
```

Luego verificar el hash en ambos extremos.

### 4.3 Preparar permisos

```bash
chmod 755 /tmp/nbping /tmp/atcmd
```

### 4.4 Verificar la copia

```bash
# En el host
sha256sum nbping

# En el router
sha256sum /tmp/nbping
```

Los valores deben coincidir. Si no coinciden, repetir la transferencia: un binario
truncado puede fallar de formas confusas.

### 4.5 Instalación permanente (opcional)

```bash
cp /tmp/nbping /usr/bin/nbping
cp /tmp/atcmd  /usr/bin/atcmd
chmod 755 /usr/bin/nbping /usr/bin/atcmd
```

`/usr/bin` está sobre el overlay y sobrevive reinicios. `/tmp` es volátil.

---

## 5. Operación de `nbping`

### 5.1 Sintaxis

```text
nbping <ip_destino> [num_paquetes] [timeout_ms]
```

| Parámetro | Default | Rango recomendado |
|---|---|---|
| `ip_destino` | — | IPv4 numérica |
| `num_paquetes` | 4 | 1–20 para campo; más para estadística |
| `timeout_ms` | 1000 | 300–3000 en LAN; 1000–5000 en LTE |

### 5.2 Procedimiento de campo

1. **Gateway.** Confirmar que el router ve su propio gateway.

   ```bash
   ip route | grep default
   nbping <IP_GATEWAY> 4 1000
   ```

2. **Host local.** Probar un equipo de la LAN.

   ```bash
   nbping <IP_HOST_LAN> 4 1000
   ```

3. **Destino público por IP.** Aísla DNS y ruta WAN.

   ```bash
   nbping 203.0.113.10 4 2000
   ```

4. **Destino público por nombre.** `nbping` no resuelve DNS; para probar nombres
   usar `ping` de BusyBox o `nslookup`:

   ```bash
   ping -c 4 example.com
   nslookup example.com
   ```

### 5.3 Leer la salida

| Línea | Interpretación |
|---|---|
| `paquete N: ok, X ms` | Reply válido con RTT de X ms |
| `paquete N: timeout` | Sin reply dentro del timeout |
| `enviados/recibidos/perdidos` | Resumen de la prueba |
| `rtt min/avg/max` | Solo se imprime si hubo al menos un reply |

`0 ms` en LAN significa menos de 1 ms (el programa trunca a milisegundos). No es
un error.

### 5.4 Criterios de decisión

| Resultado | Interpretación | Acción |
|---|---|---|
| Gateway OK, host LAN OK, público OK | Conectividad completa | Ninguna |
| Gateway OK, host LAN OK, público timeout | Sin salida a Internet | Revisar WAN, APN, NAT, firewall |
| Gateway OK, host LAN timeout | LAN o host destino | Revisar switch, IP, firewall del host |
| Gateway timeout | Enlace local o configuración IP | Revisar cable, interfaz, ruta |
| Pérdida intermitente | Radio, congestión o interferencia | Revisar señal, antena, carga |
| RTT alto y variable | Congestión o enlace móvil | Medir varias veces, comparar horarios |

### 5.5 Uso en scripts

```bash
if nbping 203.0.113.10 3 1500 >/tmp/nbping.out 2>&1; then
    echo "enlace OK"
else
    echo "enlace caído"
    cat /tmp/nbping.out
fi
```

Código de salida: `0` si hubo al menos un reply; `1` en caso contrario.

---

## 6. Operación de `atcmd`

### 6.1 Sintaxis

```text
atcmd <puerto> <comando> [espera_s] [crlf]
```

| Parámetro | Default | Nota |
|---|---|---|
| `puerto` | — | `/dev/ttyACM0`, `/dev/ttyUSB2`, etc. |
| `comando` | — | Sin `\r`; el programa lo agrega |
| `espera_s` | 5 | Segundos de silencio antes de imprimir |
| `crlf` | 1 | `1` agrega CR; `0` envía literal |

### 6.2 Localizar el puerto del módem

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
cli status wwan
dmesg | grep -i -E 'acm|option|ublox|toby'
```

En el NB2700 probado, el puerto AT fue `/dev/ttyACM0`. En otras revisiones puede
ser `/dev/ttyUSB2`. El puerto de datos (PPP/QMI) no responde a AT de la misma
forma; probar con `AT` y confirmar `OK`.

### 6.3 Secuencia de diagnóstico

```bash
P=/dev/ttyACM0

atcmd $P "AT"          2
atcmd $P "ATI"         3
atcmd $P "AT+CPIN?"    3
atcmd $P "AT+CSQ"      3
atcmd $P "AT+CREG?"    3
atcmd $P "AT+CGREG?"   3
atcmd $P "AT+COPS?"    5
atcmd $P "AT+CGATT?"   3
atcmd $P "AT+CGSN"     3
```

No ejecutar todos en rápida sucesión si el módem está inestable. Esperar la
respuesta de cada uno antes del siguiente.

### 6.4 Comandos frecuentes

| Comando | Respuesta típica | Para qué |
|---|---|---|
| `AT` | `OK` | Verificar que el puerto responde |
| `ATI` | Fabricante, modelo, revisión | Identificar módem |
| `AT+CPIN?` | `+CPIN: READY` | SIM lista |
| `AT+CSQ` | `+CSQ: 21,99` | Señal |
| `AT+CREG?` | `+CREG: 0,1` | Registro de voz |
| `AT+CGREG?` | `+CGREG: 0,1` | Registro de datos |
| `AT+COPS?` | `+COPS: 0,0,"OPERADOR",7` | Operador y tecnología |
| `AT+CGATT?` | `+CGATT: 1` | Adjunto a paquetes |
| `AT+CGSN` | IMEI | Identidad |
| `AT+CFUN?` | `+CFUN: 1` | Estado funcional |
| `AT+CGMR` | Revisión de firmware | Versión del módem |

### 6.5 Interpretar errores

| Respuesta | Significado | Acción |
|---|---|---|
| `+CME ERROR: 3` | Operación no permitida | Revisar estado con `AT+CFUN?` y `AT+CPIN?` |
| `+CME ERROR: 10` | SIM no insertada | Revisar SIM y contacto |
| `+CME ERROR: 11` | PIN requerido | Introducir PIN o desbloquear |
| `+CME ERROR: 13` | Fallo de SIM | Revisar SIM, limpiar contactos |
| `+CME ERROR: 30` | Sin red | Señal, antena, cobertura |
| `+CMS ERROR: 500` | Error desconocido | Reiniciar módem con ventana de mantenimiento |
| `(sin respuesta)` | Puerto/velocidad/puerto equivocado | Probar otro nodo; confirmar 115200 |

La numeración de errores depende del firmware del módem. La tabla es orientativa;
consultar el manual AT de u-blox para el significado exacto.

### 6.6 Señal: tabla rápida

| `AT+CSQ` | dBm | Nivel |
|---|---|---|
| 0–9 | −113 a −95 | Muy débil |
| 10–14 | −93 a −85 | Débil |
| 15–19 | −83 a −75 | Aceptable |
| 20–24 | −73 a −65 | Buena |
| 25–31 | −63 a −51 | Excelente |
| 99 | — | Desconocido |

Para LTE, `AT+CESQ` da RSRP/RSRQ/SINR, más útil que RSSI. Si el firmware lo
soporta, preferirlo para diagnosticar calidad.

### 6.7 Escape de modo datos

```bash
# Guard time: 1 segundo de silencio antes
sleep 1
atcmd /dev/ttyACM0 "+++" 2 0
# Guard time: 1 segundo después
sleep 1
atcmd /dev/ttyACM0 "ATH" 2
```

`atcmd` no implementa el guard time; hay que darlo desde el shell. El `0` final
evita agregar CR, requisito del escape `+++`.

### 6.8 Precauciones

- `AT+CFUN=1,1` reinicia el módem y corta el servicio.
- `AT+COPS=...` puede dejar el módem sin registro si el operador no es válido.
- `AT+CGDCONT` modifica el contexto PDP; documentar antes de cambiar.
- No ejecutar comandos AT en un bucle cerrado; el módem no es un servicio web.
- Guardar las salidas con cuidado: IMEI, IMSI e ICCID son datos personales o
  identificatorios.

---

## 7. Mantenimiento

### 7.1 Verificar que el binario sigue siendo el correcto

```bash
sha256sum /usr/bin/nbping /usr/bin/atcmd
file /usr/bin/nbping
```

### 7.2 Actualizar una utilidad

1. Compilar en el host.
2. Transferir a `/tmp` con otro nombre (`nbping.new`).
3. Verificar hash y ejecución.
4. Reemplazar el binario en `/usr/bin` solo si la prueba fue exitosa.
5. Conservar una copia del anterior hasta confirmar.

### 7.3 Desinstalar

```bash
rm -f /usr/bin/nbping /usr/bin/atcmd
```

No hay archivos de configuración ni servicios que limpiar.

### 7.4 Espacio y flash

```bash
df -h
du -sh /usr/bin/nbping /usr/bin/atcmd
```

No reinstalar en cada arranque. El overlay es NAND y tiene ciclos de escritura
limitados.

### 7.5 Después de actualizar firmware

Una actualización de NRSW puede reemplazar el overlay. Verificar si los binarios
siguen presentes y si la arquitectura coincide. Volver a copiarlos si hace falta.

---

## 8. Preguntas frecuentes

**¿`nbping` reemplaza al `ping` de BusyBox?**
No. BusyBox ya trae `ping`. `nbping` existe para validar el pipeline de
cross-compilación y ofrece una salida estable para scripts.

**¿Por qué pide root?**
Usa raw sockets ICMP, que requieren `CAP_NET_RAW`. En NRSW se corre como `root`.

**¿Puedo usarlo en un NB2800?**
No con este binario. El NB2800 es ARM; hace falta `netbolt-arm` y recompilar. No
está validado.

**¿Puedo usarlo en un router de otra marca?**
No. El binario es PowerPC big-endian. Otro router requiere su propia toolchain.

**¿Por qué el RTT da 0 ms?**
Porque el programa trunca a milisegundos y la LAN responde en menos de 1 ms. No es
un fallo.

**¿Por qué `scp` falla con `subsystem request failed`?**
Porque el dropbear del router no tiene `sftp-server`. Usar `scp -O`.

**¿Por qué SSH dice `no matching host key type`?**
El cliente OpenSSH es moderno y el dropbear del router es antiguo. Habilitar
`ssh-rsa`/`ssh-dss` en el cliente.

**¿`atcmd` puede dejar el módem sin servicio?**
Sí, si se envían comandos de reinicio o de selección de operador. Es una
herramienta de diagnóstico, no de operación desatendida.

**¿Por qué `atcmd` tarda varios segundos?**
Porque espera `wait_s` de silencio después del último byte. Es el comportamiento
diseñado.

**¿Funciona con IPv6?**
No. `nbping` es solo IPv4.

**¿Puedo compilar sin la toolchain oficial?**
No se recomienda. Un GCC moderno para PowerPC puede producir binarios
incompatibles con uClibc o con el kernel del router. La toolchain oficial es la
única validada.

**¿Dónde consigo la toolchain?**
En `https://share.netmodule.com/router/public/toolchain/`. No se distribuye en
este repositorio.

**¿Dónde está el código?**
En `nbping.c` y `atcmd.c`, en la raíz del repositorio. La guía de desarrollador
está en `docs/developer.md`.

---

## 9. Solución de problemas rápidos

| Síntoma | Primer chequeo | Segundo chequeo |
|---|---|---|
| `Exec format error` | `file nbping` | Recompilar con `netbox-ppc` |
| `Permission denied` | `ls -l` | `chmod 755` |
| `Operation not permitted` | `id` | Login como root |
| Sin respuesta de red | `ip route` | `nbping` al gateway |
| Sin respuesta del módem | `ls /dev/tty*` | `cli status wwan` |
| Respuesta AT ilegible | Velocidad del puerto | Probar otro `/dev/tty*` |
| El router no responde | Link físico | Consola serie / recuperación |
| Cambios no persisten | ¿Está en `/tmp`? | Copiar a `/usr/bin` |
| Poco espacio | `df -h` | Limpiar `/tmp` y overlay |

---

## 10. Soporte y contacto

- **Issues del proyecto:** <https://github.com/wanderc137/nb2700-telemetria/issues>
- **Documentación de NetModule:** <https://wiki.netmodule.com>
- **Descargas de toolchain y firmware:** <https://share.netmodule.com>
- **Soporte del fabricante:** el NB2700 está EOL; el soporte oficial terminó en
  marzo de 2025. Para hardware fuera de garantía, la comunidad y la documentación
  archivada son la vía práctica.

Al reportar un problema, incluir:

1. Modelo y versión de NRSW (`cat /etc/release-info`).
2. Arquitectura (`uname -m`).
3. Comando ejecutado y salida completa.
4. `file` del binario y `sha256sum`.
5. Qué se esperaba y qué se obtuvo.

No incluir credenciales, seriales completos, IMEI/IMSI/ICCID ni direcciones
públicas de la instalación.

---

## 11. Glosario

| Término | Significado |
|---|---|
| **NRSW** | NetModule Router Software, basado en OpenWrt |
| **Toolchain** | Conjunto compilador, binutils y libc para cross-compilar |
| **Cross-compile** | Compilar en una arquitectura para ejecutar en otra |
| **Raw socket** | Socket que permite construir cabeceras de protocolo |
| **ICMP** | Protocolo de control de Internet (RFC 792) |
| **RTT** | Round-trip time, ida y vuelta de un paquete |
| **Overlay** | Capa de escritura persistente sobre un sistema de solo lectura |
| **Dropbear** | Servidor SSH liviano usado por NRSW |
| **APN** | Access Point Name, configuración de datos del operador |
| **CSQ** | Comando AT de calidad de señal |
| **EOL** | End of Life, fin de vida del producto |

---

*Documento derivado del código y de la operación real del equipo. Las marcas
NetModule, u-blox y OpenWrt pertenecen a sus respectivos titulares. Licencia del
proyecto: MIT.*
