# Manual de Descubrimiento — Router NetModule NB2700

> Registro paso a paso del proceso de descubrimiento, conexión y preparación
> del entorno para el proyecto de telemetría.
> Fecha de inicio: 2026-09-24

---

## 1. Identificación del hardware

| Dato | Valor | Cómo se obtuvo |
|---|---|---|
| Modelo | **NB2700** (NetModule AG) | Banner de login Telnet |
| Serial | `<SERIAL>` | `cli status summary` |
| CPU | PowerPC **e300c3 @ 400MHz** (platform MPC830x RDB) | `cat /proc/cpuinfo` |
| Arquitectura | **ppc** (PowerPC 32-bit) | `uname -m` |
| RAM | 56 MB total (~31 MB disponibles) | `cat /proc/meminfo` |
| Flash | mtd0 boot (4MB), mtd1/mtd2 root0/root1 (48MB c/u), mtd3 data (25MB) | `cat /proc/mtd` |
| Firmware NRSW | **4.6.0.105** (profile NETBOX) | `cat /etc/release-info` |
| Shell | BusyBox v1.30.1 (ash) | Banner login |
| Modem celular | u-blox MPCI-L280 (tobyl2), **sin SIM** | `cli status wwan` |
| DIO | IN1=off, IN2=off, OUT1=off, OUT2=on | `cli status summary` |

## 2. Puertos físicos (conexión de red)

- **Ethernet 1-4** = puertos **LAN** (red local / gestión).
- **Ethernet 5** (separado) = puerto **WAN** (uplink a internet).
- Para configuración inicial: conectar el notebook al **Ethernet1**.
- IP por defecto de gestión: **192.0.2.1** (Ethernet1), DHCP servidor activo.

## 3. Acceso al router (descubrimiento de puertos)

Escaneo de puertos contra 192.0.2.1:

| Puerto | Servicio | Estado |
|---|---|---|
| 22 | SSH | **CERRADO/FILTRADO** (por defecto deshabilitado en NRSW) |
| 23 | Telnet | **ABIERTO** ✅ |
| 80 | HTTP (Web Manager) | **ABIERTO** ✅ |
| 443 | HTTPS | cerrado |
| 161 | SNMP | cerrado |
| 53 | DNS | abierto (servidor DNS del router) |

### Conexión por Telnet (funciona por defecto)

```bash
telnet 192.0.2.1
# login: root
# password: <vacío — solo Enter en estado de fábrica>
```

Verificación dentro del router:
```bash
uname -m          # → ppc
cat /etc/release-info   # → version: 4.6.0.105, profile: NETBOX
cli status summary
```

## 4. Comandos útiles del CLI NetModule (`cli`)

```bash
cli status -h            # lista secciones disponibles
cli status summary       # resumen: serial + DIO
cli status system        # información del sistema
cli status wwan          # módulo celular (u-blox, SIM, señal)
cli status gnss          # GPS/GNSS
cli status eth           # interfaces ethernet
cli status lan           # LAN
cli status wan           # WAN
```

## 5. Próximos pasos del proyecto

1. [ ] Asignar IP fija al router y al notebook
2. [ ] Habilitar SSH + crear usuario/password
3. [ ] Conectar por SSH
4. [ ] Descargar/instalar toolchain de cross-compilación netbox-ppc
5. [ ] Programa C: ping al notebook desde el router
6. [ ] Cross-compilar, transferir y ejecutar en el router
7. [ ] (Futuro) Poner SIM celular → ping a Google / telemetría
## 6. Prueba de reutilización de toolchain existente (2026-09-24)

**Petición del usuario**: reutilizar lo ya descargado (SDK Teltonika) antes de bajar algo nuevo.

**Resultado de la búsqueda en todo el sistema**:
- El SDK de Teltonika (openwrt-sdk-21.02.0-ramips-mt76x8 mipsel) estaba en `/tmp/opencode/`
  → **ya NO existe** (carpeta vacía, se perdió en reinicio).
- NO hay ninguna toolchain PowerPC en el sistema.
- NO hay staging_dir / toolchain-* / SDK OpenWrt en /home/jma.
- Solo existe gcc nativo 13.3.0 (x86_64) → compila para la máquina local, NO para el router.
- Docker: solo imágenes dvwa y zigbee2mqtt (sin utilidad para cross-compile).

**Conclusión**: es IMPOSIBLE cross-compilar para el NB2700 (PowerPC) con lo presente.
Además, aunque existiera el SDK Teltonika sería inútil: `mipsel` ≠ `ppc`.
→ ES NECESARIO descargar la toolchain oficial de NetModule: **netbox-ppc**.

## 7. Toolchain oficial requerida

| Dato | Valor |
|---|---|
| Familia | netbox-ppc (NB1600, NB2700, NB2710, NB3700, NB3710) |
| Archivo | `toolchain-ppc-4.4.5-x86_64.tar.bz2` |
| URL base | https://share.netmodule.com/router/public/toolchain/ |
| Compilador | `powerpc-openwrt-linux-gcc` |
| GCC + libc | GCC 4.4.5 + uClibc 0.9.31 |
| Binario | `ELF 32-bit MSB PowerPC` |

## 8. Investigación: "¿dónde está el SDK Teltonika de hace 4 días?"

**Hallazgo (2026-09-24)**:
1. El SDK Teltonika se descargó en `/tmp/opencode/openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64`
   (documentado en README.md / Makefile / PLAN.md de lp104MqttTeltonikaV2).
2. Hoy `/tmp/opencode/` está **VACÍO** → el SDK se perdió (el /tmp de Linux se limpia al reiniciar).
3. El binario compilado entonces (`lp104mqttv2`) sigue en el proyecto y es:
   `ELF 32-bit LSB MIPS MIPS32 rel2 statically linked` → arquitectura **MIPS** (mipsel_24kc).

**Conclusión CRÍTICA**: aunque el SDK Teltonika existiera, **NO serviría para el NB2700**:
- Teltonika RUT200 = arquitectura MIPS (mipsel) → binarios ELF LSB MIPS.
- NetModule NB2700 = arquitectura PowerPC (ppc) → binarios ELF MSB PowerPC.
- Un binario MIPS no se ejecuta en un router PowerPC y viceversa.

→ Para el NB2700 es OBLIGATORIO descargar la toolchain oficial de NetModule:
  **netbox-ppc** (`toolchain-ppc-4.4.5-x86_64.tar.bz2`) de https://share.netmodule.com/router/public/toolchain/

## 9. SDK Teltonika re-descargado y reinstalado (persistente) — 2026-09-24

**Solicitud del usuario**: descargar la toolchain Teltonika y dejarla en el proyecto.

**Acciones realizadas**:
1. Descargado `openwrt-sdk-21.02.0-ramips-mt76x8_gcc-8.4.0_musl.Linux-x86_64.tar.xz`
   (100 MB) desde https://downloads.openwrt.org/releases/21.02.0/targets/ramips/mt76x8/
2. Extraído en el proyecto V1:
   `/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/lp104MqttTeltonika/sdk/`
3. Toolchain verificada: `mipsel-openwrt-linux-musl-gcc` (gcc 8.4.0) compila
   `ELF 32-bit LSB MIPS MIPS32 rel2 statically linked` ✅
4. **Rutas actualizadas** en V1 y V2 (Makefile, README.md, PLAN.md, docs/):
   - Antes: `SDK_DIR ?= /tmp/opencode/openwrt-sdk-...` (se perdía al reiniciar)
   - Ahora: `SDK_DIR ?= .../lp104MqttTeltonika/sdk/openwrt-sdk-...` (persistente)
5. Compilación limpia verificada en ambos proyectos:
   - V1 `lp104mqtt` → ELF MIPS ✅
   - V2 `lp104mqttv2` → ELF MIPS ✅
6. `.gitignore` de ambos proyectos ya excluye `sdk/` (no se sube a GitHub).

**Toolchains instaladas en la máquina (persistentes)**:
| Toolchain | Ruta | Para |
|---|---|---|
| netbox-ppc | `/home/jma/toolchains/toolchain-ppc-4.4.5-x86_64.tar.bz2` (17 MB, sin extraer aún) | NB2700 (PowerPC) |
| openwrt-sdk ramips/mt76x8 | `.../lp104MqttTeltonika/sdk/` | Teltonika RUT200 (MIPS) |

## 10. Toolchain netbox-ppc extraída y verificada — 2026-09-24

**Acciones**:
1. Extraída `toolchain-ppc-4.4.5-x86_64.tar.bz2` (17 MB) en `/home/jma/toolchains/netbox-ppc/`
2. Compilador: `powerpc-openwrt-linux-gcc` (GCC 4.4.5 + uClibc 0.9.31)
3. **Prueba de compilación EXITOSA** → `ELF 32-bit MSB executable PowerPC, statically linked` ✅

**Comando de cross-compilación** (referencia rápida):
```bash
export STAGING_DIR=/home/jma/toolchains/netbox-ppc/toolchain-ppc-4.4.5-x86_64
CC=/home/jma/toolchains/netbox-ppc/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc
$CC -static -Os -Wall -o programa programa.c
/home/jma/toolchains/netbox-ppc/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-strip programa
```

## 11. Configuración de red (siguiente paso)

- Router NB2700: IP **192.0.2.1** (LAN Ethernet1) — ya fija por defecto.
- Notebook: interfaz `enp0s31f6` = **192.0.2.50/24** (ya en la subred correcta).
- Pendiente: habilitar SSH en el router y asignar usuario/password.

## 12. Toolchain netbox-ppc movida al proyecto (decisión del usuario) — 2026-09-24

El usuario pidió que la toolchain de NetModule viva en el proyecto, igual que la Teltonika.

**Ubicación final**:
`/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/nb2700-telemetria/sdk/`

```
sdk/
├── toolchain-ppc-4.4.5-x86_64.tar.bz2   (17 MB, fuente original)
└── toolchain-ppc-4.4.5-x86_64/          (extraída)
    └── bin/powerpc-openwrt-linux-gcc
```

**Referencia rápida de cross-compilación para NB2700**:
```bash
export STAGING_DIR=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/nb2700-telemetria/sdk/toolchain-ppc-4.4.5-x86_64
CC=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/nb2700-telemetria/sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-gcc
STRIP=/home/jma/Documents/Proyects/OpenCode/AG.CreadorContenido/proyectos/nb2700-telemetria/sdk/toolchain-ppc-4.4.5-x86_64/bin/powerpc-openwrt-linux-strip
$CC -static -Os -Wall -o programa programa.c
$STRIP programa
# resultado: ELF 32-bit MSB PowerPC
```

## 13. SSH habilitado — causa raíz encontrada (2026-09-24)

**Síntoma**: puerto 22 "cerrado" desde el notebook, aunque dropbear corría en el router
(`dropbear -p 22 -B`, banner `SSH-2.0-dropbear_2019.78` OK desde el propio router).

**CAUSA RAÍZ (doble)**:
1. **UFW del notebook** tenía regla `22/tcp DENY OUT Anywhere` (salida SSH BLOQUEADA).
   El SYN nunca salía por ninguna interfaz. Telnet 23 no estaba bloqueado → por eso
   el telnet funcionaba.
   - Fix: `sudo ufw allow out to 192.0.2.1 port 22 proto tcp`
   - Luego `sudo ufw delete deny out 22/tcp` (eliminar el DENY global de salida 22).
   - Verificado: regla DENY estaba en pos.1 ANTES del ALLOW pos.7 → se eliminó.
2. **OpenSSH 9.6 del notebook** rechaza las claves viejas de dropbear 2019.78
   (ssh-rsa/ssh-dss). Error: `no matching host key type found`.

**Comando SSH que funciona**:
```bash
ssh -o StrictHostKeyChecking=no -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa root@192.0.2.1
```
✅ Verificado: `uname -m` → `ppc`

**Nota**: la config SSH del router ya estaba correcta (ssh.status=1, ssh.port=22,
ssh.pwlogin=1, ssh.rootlogin=1). El bloqueo era 100% del lado del notebook.

## 14. Usuario y password SSH configurados (2026-09-24)

**Parámetros NetModule para usuarios**:
- `cli get -c user` → `user.0`..`user.19`
- `cli get -c admin` → `admin.password`, `admin.hash`, `admin.shell`, `admin.bootpass`, etc.

**Configuración aplicada**:
```bash
cli set admin.password=[REDACTED]
```
- Al cambiarlo, la sesión telnet actual se cierra y pide login de nuevo (normal).
- El password aplica al usuario root en SSH y al Web Manager.

**Acceso SSH final**:
```bash
sshpass -p '<ROUTER_PASSWORD>' ssh -o StrictHostKeyChecking=no \
  -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
  -o PubkeyAcceptedAlgorithms=+ssh-rsa root@192.0.2.1
```
✅ Verificado: `SSH_PASSWORD_OK` + `ppc`

**Estado del plan del usuario**:
- [x] 1. IP fija (router 192.0.2.1 / notebook 192.0.2.50, ya estáticas)
- [x] 2. Usuario y password SSH (root / <ROUTER_PASSWORD>)
- [x] 3. Conexión SSH funcionando
- [x] 4. Toolchain cross-compiler (netbox-ppc en sdk/)
- [ ] 5. Programa C que hace ping al notebook (192.0.2.50)
- [ ] 6. SIM celular → ping a Google

## 15. Programa C nbping compilado, transferido y ejecutado (2026-09-24)

**Archivos creados en el proyecto**:
- `nbping.c` — ping ICMP con raw sockets, C puro (sin dependencias)
- `Makefile` — cross-compile con netbox-ppc (SDK_DIR apunta a sdk/ del proyecto)

**Cross-compilación**:
```bash
make   # → nbping: ELF 32-bit MSB PowerPC, statically linked, stripped (34 KB)
```

**Transferencia al router** (importante: dropbear NO tiene sftp-server, usar `-O`):
```bash
sshpass -p '<ROUTER_PASSWORD>' scp -O -o StrictHostKeyChecking=no \
  -o HostKeyAlgorithms=+ssh-rsa,ssh-dss \
  -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  nbping root@192.0.2.1:/tmp/nbping
```

**Ejecución en el router**:
```
# /tmp/nbping 192.0.2.50 4 1500
PING 192.0.2.50: 4 paquetes, timeout 1500 ms
paquete 1: ok, 0 ms
paquete 2: ok, 0 ms
paquete 3: ok, 0 ms
paquete 4: ok, 0 ms
enviados: 4, recibidos: 4, perdidos: 0 (0%)
rtt min/avg/max = 0/0/0 ms
```

**VALIDADO**: compilación cross (netbox-ppc) ✅ | transferencia scp ✅ | ejecución en
el NB2700 ✅ | conectividad router↔notebook ✅

**Estado del plan del usuario**:
- [x] 1-4. IP fija, SSH, toolchain (hechos)
- [x] 5. Programa C ping al notebook (192.0.2.50) — EXITOSO
- [ ] 6. SIM celular → ping a Google (pendiente: requiere SIM física)

## 16. Instalación persistente de nbping en el router (2026-09-24)

- El router usa **overlayfs**: `/` es overlay sobre `/dev/mtdblock3` (25 MB, ~21 MB libres).
- Cualquier archivo escrito en `/usr/bin` se persiste en la partición de datos.
- Instalado: `cp /tmp/nbping /usr/bin/nbping && chmod 755 /usr/bin/nbping`
- Ahora ejecutable por nombre desde cualquier directorio: `nbping <ip> [n] [timeout_ms]`
- SOBREVIVE a reinicios. (El binario en /tmp se borraba, pero /usr/bin no.)

**Filesystem del router**:
```
/dev/root      48.0M  47.5M   548K  99%  /rom      (firmware, solo lectura)
tmpfs          28.1M  176K    28.1M   1%  /tmp      (volatil)
/dev/mtdblock3 25.0M   3.7M   21.3M  15%  /overlay  (datos persistente)
overlay        25.0M   3.7M   21.3M  15%  /         (raiz = overlay)
```

## 17. Próximo: prueba de internet con SIM Entel (paso 6 del plan)

Al encender el router con la SIM Entel:
1. `cli status wwan` → verificar SIM detectada + señal (RSSI/dBm) + operador
2. `cli status wan` → IP celular (WAN)
3. `nbping 8.8.8.8 4 1500` → ping a Google (IP)
4. `nbping google.com 4 1500` → ping con DNS
5. Si no conecta: APN Entel = `bam.entelpcs.cl` → `cli set wwan.apn=bam.entelpcs.cl`

## 18. SIM Entel detectada automáticamente (2026-09-24)

Con la SIM Entel puesta y el router encendido:
- `SIM status: ready` ✅ (detección automática, como en Teltonika)
- ICCID: <ICCID> | IMSI: <IMSI> (730013 = Entel Chile)
- Banda inicial: WCDMA-B5 (3G)
- **Señal débil**: -111 → -107 dBm (nivel 0) — el módem buscaba red pero no registraba
  (`registration state: registrationNotUsed`)
- Acción del usuario: apagar router y conectar **antena mejor** para mejorar señal.
