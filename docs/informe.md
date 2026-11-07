# Informe: seguridad de archivos en capa kernel y mmap

**Asignatura:** Sistemas Operativos, EAFIT 2026-2  
**Solución propuesta:** política de control mediante BPF LSM en `mmap_file`  
**Alcance:** prototipo académico; las pruebas deben ejecutarse en VM Linux compatible.

## Resumen

El prototipo adjunto deniega mapeos de archivos `MAP_SHARED` con `PROT_WRITE` en el hook LSM `mmap_file`. La regla se aplica en el kernel cuando se crea el VMA, antes de que se instalen PTE que habiliten el acceso. Preserva los mapeos privados escribibles: sus escrituras siguen semántica copy-on-write y no actualizan el archivo. Esta solución controla una clase de exposición; no cifra páginas ni inspecciona cada byte accedido tras el mapeo.

## I. Preguntas diagnósticas

### 1. Mapeo virtual y primer page fault

El proceso ejecuta `mmap` (biblioteca libc normalmente emite `mmap`/`mmap2`), entra al kernel por la syscall y pasa validaciones de longitud, offset, flags, descriptor y protecciones. El kernel obtiene el objeto `file`, comprueba permisos y llama a las verificaciones de seguridad, incluido el hook LSM `mmap_file`. Si procede, reserva un intervalo virtual, construye un `vm_area_struct` con operaciones y flags apropiados y lo enlaza al `mm_struct`; el mapeo suele ser perezoso y aún no contiene páginas residentes. Al retornar, el primer acceso genera una excepción de página por ausencia de traducción (o por permiso). La MMU transfiere control al manejador de excepción; Linux localiza el VMA, valida dirección y tipo de acceso, y entra por `handle_mm_fault` hacia las rutas de fault de archivo/anónimo. Para un archivo, se obtiene/carga la página de la page cache (lectura de almacenamiento si no está presente), se valida EOF y permisos; para COW se asigna y copia una página privada al primer write. Linux instala/actualiza la PTE, actualiza contabilidad/TLB según corresponda y reanuda la instrucción. Errores como acceso fuera del VMA o permiso inválido terminan normalmente en `SIGSEGV`/`SIGBUS`.

### 2. `MAP_SHARED` frente a `MAP_PRIVATE`

Las flags de `vm_area_struct` expresan permisos y semántica de compartición (`VM_SHARED`, `VM_MAYWRITE`, etc.). En `MAP_SHARED`, procesos que mapean el mismo objeto ven modificaciones compartidas; una escritura permitida ensucia la page cache y puede persistirse al archivo. En `MAP_PRIVATE`, las escrituras usan COW: primero se puede compartir una página de solo lectura; al escribir, se crea una copia privada, invisible a otros mapeos y no persistida al archivo. `msync` no convierte escrituras privadas en cambios del archivo. La seguridad depende también de permisos del descriptor, `prot`, flags permitidas, ACL/LSM y de que el kernel aplique correctamente COW. Datos legibles siguen siendo potencialmente exfiltrables por cualquier proceso autorizado a leerlos.

### 3. Ciclo COW y protecciones

El kernel puede mapear inicialmente la página original como no escribible, aunque el VMA admita escritura privada. Un write dispara protección; el manejador verifica VMA y crea/copia una página anónima privada, actualiza la PTE escribible y reintenta la instrucción. Refcounts, locks, invalidación TLB y sincronización evitan que una escritura privada se propague indebidamente. `VM_MAYWRITE` refleja capacidad máxima del VMA; no significa que la PTE actual deba ser escribible. Confundir permisos solicitados (`PROT_*`), permisos admitidos y flags máximas, o hacer writable una página compartida sin la ruta COW, puede convertir una modificación privada en una escritura al page cache/archivo, corromper datos o permitir escalada si el archivo es privilegiado. El caso Dirty COW (CVE-2016-5195) fue una carrera histórica en la ruta COW; kernels corregidos serializan y validan esa ruta. No se debe inferir que el bug persiste en kernels actuales.

### 4. Hook syscall table LKM frente a LSM

Reemplazar entradas de syscall table desde un LKM es frágil y dependiente de arquitectura/configuración: la tabla puede no exportarse, hay protecciones de kernel, concurrencia y cambios de ABI, además de riesgo de corrupción/panic. Es fácil omitir rutas alternativas y es una técnica de alto riesgo. LSM ofrece puntos de control diseñados para autorización, con integración de credenciales/política y composición con otros módulos. `mmap_file` es apropiado para autorizar mapeos de archivos; es viable con LSM BPF en kernels configurados para ello o con un LSM integrado al kernel. BPF evita parchear el kernel, aunque requiere BTF, configuración, permisos y verificación; no brinda inspección de cada load/store posterior. Para producción, preferir LSM y política auditable; no hookear syscall table.

### 5. Cifrado transparente

El cifrado debe integrarse en una capa de filesystem/almacenamiento que transforme datos al leerlos y escribirlos, con gestión de claves y autenticación adecuada. La page cache debe contener páginas en el dominio de datos esperado por el mapeo autorizado y la ruta mmap debe preservar coherencia con read/write, truncado y writeback. Cifrado de bloque (p. ej., dispositivo) protege datos en reposo, pero descifra bloques en el kernel y no da aislamiento por proceso una vez que el contenido aparece en page cache; los procesos autorizados pueden compartir esas páginas. Cifrado por archivo (como fscrypt en su alcance soportado) requiere verificar explícitamente soporte y restricciones de mmap/consistencia. Si se requiere secreto entre procesos concurrentes, hay que impedir permisos/mapeos no autorizados o usar dominios de memoria aislados y claves por principal; un simple mmap de página descifrada compartida no lo consigue. Nunca se deben dejar claves en logs ni asumir que el cifrado en disco protege contra root/kernel comprometido.

### 6. `msync`, `fsync` e integridad

En mapeos compartidos, stores marcan páginas sucias mediante mecanismos de dirty tracking; writeback sincroniza page cache con el filesystem. `msync(MS_SYNC)` solicita sincronización del rango mapeado y espera sus errores de writeback; `fsync(fd)` sincroniza datos y metadatos asociados al archivo según la semántica del filesystem. No son una transacción general ni garantizan durabilidad del directorio sin sincronizarlo cuando aplica. Un filtro que solo inspecciona `write(2)` se evade escribiendo por stores en páginas compartidas; observar `msync` es tardío y puede permitir datos transitorios/efectos antes de persistencia. La política debe controlar `mmap` y permisos al crear el VMA, y validar contenido en el punto de persistencia si esa es la meta. El prototipo deniega mapeo compartido escribible, una mitigación preventiva simple.

### 7. Auditoría con eBPF

Un programa BPF puede adjuntarse a hooks LSM (autorización en `mmap_file`) y a trazas/kprobes/fentry/fexit para contar o registrar eventos de fault, dirtying y sincronización cuando el kernel expone puntos estables y verificables. Debe filtrar por inode/dispositivo, credenciales, proceso y flags; agregar contadores en mapas y enviar eventos muestreados por ring buffer reduce overhead. Los probes internos pueden cambiar entre versiones, perder eventos o no observar cada acceso a memoria; page faults no ocurren en cada load/store una vez instalada la PTE. eBPF de observabilidad no equivale a mediación completa. El prototipo implementa solo decisión de autorización en `mmap_file`, no un monitor de cada acceso.

### 8. Descriptor `O_RDONLY` y `MAP_PRIVATE` escribible

Sí, Linux suele permitir `mmap(fd, ..., PROT_READ|PROT_WRITE, MAP_PRIVATE, ...)` sobre descriptor de solo lectura, si el archivo es legible y las demás restricciones/LSM lo permiten. `MAP_PRIVATE` asegura que el write se resuelva con COW y no se propague al archivo, por eso no requiere permiso de escritura sobre el backing file. No es una modificación persistente del archivo. Puede haber exposición del contenido legible a ese proceso, presión de memoria, comportamiento dependiente de EOF/truncado y falsas expectativas de confidencialidad; no debería alterar temporalmente el archivo por la semántica esperada. Un defecto de COW en el kernel sería una vulnerabilidad del kernel, no el comportamiento normal.

### 9. Carrera `MADV_DONTNEED` y escrituras privadas

En kernels corregidos, la invalidación de páginas privadas y la ruta de COW sincronizan el estado del VMA/PTE, revalidan entradas y usan bloqueos/refcounts para que una escritura concurrente no modifique accidentalmente la página compartida. `MADV_DONTNEED` descarta páginas residentes del rango según su semántica; el acceso posterior las resuelve de nuevo desde el backing object o estado anónimo. La garantía exacta depende de versión y tipo de mapeo, por lo que una prueba debe ejecutarse en kernel actualizado y la revisión debe seguir la implementación de esa versión. Para validar, hacer stress concurrente en VM con archivos desechables y comparar hash del archivo contra el valor esperado; no usar datos privilegiados ni explotar kernel antiguo fuera de laboratorio.

### 10. Páginas, tablas y bits MMU

`struct page` representa metadatos de páginas físicas, incluyendo referencias, estado y vínculo al page cache/anon. Las tablas PGD/P4D/PUD/PMD/PTE forman la traducción jerárquica de direcciones virtuales; PTE contiene PFN y atributos. En x86, bits R/W, U/S y NX (junto con controles superiores como SMEP/SMAP y políticas W^X) restringen quién accede, si puede escribir y si puede ejecutar. El kernel puede mantener páginas de archivo no ejecutables y mappings de usuario non-executable; permisos correctos reducen inyección/ejecución. Sin embargo, NX no evita que datos maliciosos sean leídos o que el proceso abuse de una página ejecutable permitida. ASLR, W^X, CFI/seccomp/LSM y validación de contenido son capas complementarias; los bits MMU aíslan dominios pero no sustituyen autorización del VMA.

## II. Diseño y uso de IA

### Flujo de trabajo asistido

1. Explorar la ruta syscall → VFS → LSM → VMA/page fault y contrastar nombres/firmas con headers/BTF de la versión objetivo.
2. Comparar alternativas (LSM BPF, LSM integrado, tracing eBPF) y acotar requisito medible: denegar mapeo compartido escribible.
3. Generar un prototipo mínimo en C, revisar manualmente constantes UAPI/semántica, y compilar con warnings estrictos contra headers del kernel objetivo.
4. Generar pruebas seguras con archivos temporales; ejecutar solo en VM; contrastar la política cargada con el comportamiento sin política y documentar límites.
5. Usar IA para revisar escenarios, no como autoridad: comprobar APIs, hooks y configuración en documentación oficial y headers instalados.

### Prompts estructurados para documentar el flujo asistido

La siguiente tabla registra prompts representativos del proceso de análisis y validación asistido. Si el equipo conserva una bitácora literal de sus interacciones, debe reemplazar estos textos por las formulaciones exactas utilizadas.

| Contexto | Rol | Restricciones | Prompt y resultado esperado |
|---|---|---|---|
| Linux 6.x, autorización de `mmap` de archivos | Ingeniero de seguridad de kernel | No parchear syscall table; usar hook LSM y explicar firma contra BTF | “Diseña una política BPF LSM en `mmap_file` que deniegue `MAP_SHARED` con `PROT_WRITE`. Indica supuestos, flags UAPI y límites de mediación. Devuelve pseudocódigo y puntos que debo verificar en headers.” Resultado: condición de autorización y checklist de compatibilidad. |
| Prototipo C eBPF/libbpf | Revisor de código C de bajo nivel | No usar API no exportada ni cambiar CR0; warnings como errores | “Revisa este programa BPF LSM para Linux 6.x: valida contexto, retorno chained-LSM, manejo de errores/libbpf y limpieza del link. Señala cada símbolo que no pueda confirmarse localmente.” Resultado: hallazgos concretos, sin asumir compilación. |
| Prueba de política mmap | QA de seguridad | Solo archivos temporales; no dirty COW real; no tocar archivos del sistema | “Escribe una prueba en C que demuestre rechazo de shared-writable y éxito de private-writable bajo política activa; valida errno, limpia recursos y evita confundir política inactiva con éxito.” Resultado: prueba reproducible con criterio de pase/fallo. |
| Protocolo de desempeño | Ingeniero de rendimiento Linux | No inventar cifras; separar cold/warm cache y syscall/fault | “Propón mediciones antes/después para mmap y page faults usando perf/strace, reporta variables, repeticiones, comandos y formato de tabla, sin fabricar resultados.” Resultado: procedimiento y plantilla para datos reales. |

### Revisión crítica de errores posibles de IA

Durante el desarrollo se detectaron y corrigieron errores concretos asistidos por IA: (1) se nombró inicialmente el hook como `file_mmap`, pero el hook LSM real es `mmap_file`; se verificó contra las definiciones del kernel y BTF. (2) Se incluyeron headers glibc (`errno.h`) en el objeto BPF, lo que hizo fallar clang por ausencia de headers multiarquitectura; se retiraron y se usaron constantes UAPI explícitas. (3) El Makefile no regeneraba `vmlinux.h` tras `make clean`; se cambió para generarlo antes del objeto BPF. La validación consistió en inspección del BTF/configuración del kernel WSL, compilación sin warnings y pruebas funcionales. Como práctica general, cada API debe cotejarse con los headers/BTF del kernel objetivo; nunca se debe asumir que un kprobe ve cada acceso ni desactivar WP de CR0 para resolver este problema.

### Arquitectura y trade-offs

| Opción | Cobertura | Rendimiento | Riesgo/operación |
|---|---|---|---|
| Wrapper `LD_PRELOAD` | Solo procesos que lo cargan | Bajo costo normal | Fácil de evadir; no frontera de seguridad |
| Hook syscall table LKM | Intercepta una ruta si funciona | Bajo costo por syscall | Frágil, inseguro, dependiente de versión |
| BPF LSM `mmap_file` (seleccionado) | Autoriza creación de VMA en el kernel | Costo al mapear, no por acceso ordinario | Requiere soporte/config/privilegios; no inspecciona bytes posteriores |
| Instrumentación fault/tracing | Observa eventos seleccionados | Puede añadir costo en fallos/eventos | No ve cada load/store y probes internos son frágiles |

La elección prioriza un hook de autorización soportado y un objetivo de seguridad acotado, con costo en el camino de creación del mapping. Para inspección de datos cada vez que se escribe haría falta cambiar el modelo (por ejemplo, restringir permisos/mapeos y mediar dirtying/writeback), con un impacto de rendimiento notable; un hook de `mmap` por sí solo no satisface ese objetivo.

## III. Implementación, pruebas y resultados

### Comportamiento y validación

El programa BPF conserva un error previo de otros LSM; de lo contrario comprueba que el tipo bajo de flags sea `MAP_SHARED` y que `prot` incluya `PROT_WRITE`, devolviendo `-EACCES`. El cargador adjunta el programa y mantiene vivo el link; al salir, lo destruye y la política deja de aplicarse. `mmap_probe` comprueba rechazo y que `MAP_PRIVATE` escribible funciona localmente. `mmap_race` estresa escrituras privadas concurrentes con `MADV_DONTNEED` y comprueba que el contenido del archivo de respaldo siga intacto. `tests/run_tests.sh` usa archivos temporales.

### Compilación en Linux objetivo

Ejecutar en una VM Linux compatible, con headers de la versión en ejecución, clang/LLVM, libbpf-dev, bpftool, pkg-config y BTF:

```sh
grep -E 'CONFIG_(BPF|BPF_SYSCALL|BPF_LSM|DEBUG_INFO_BTF)=' /boot/config-$(uname -r)
grep -o 'bpf' /sys/kernel/security/lsm
make clean && make
sudo ./build/mmap_guard
```

En otra terminal, `make test`. Para retirar la política, Ctrl-C al cargador. Resultados esperados con política activa: `MAP_SHARED` escribible retorna `EACCES` y el privado pasa. Repetir sin política debe producir el control negativo: el shared writable es aceptado en un filesystem/archivo ordinario con permiso de escritura. Si el hook no carga, la prueba no demuestra eficacia; guardar el log, versión de kernel, config y versión de libbpf.

**Validación realizada por el estudiante en WSL2:** kernel `6.6.87.1-microsoft-standard-WSL2`, con `CONFIG_BPF_SYSCALL=y`, `CONFIG_BPF_LSM=y`, `CONFIG_DEBUG_INFO_BTF=y` y LSM `bpf` activo. `make clean && make` compiló los cinco ejecutables/objetos sin errores ni advertencias. `make test` pasó: se denegó `MAP_SHARED` escribible, se permitió `MAP_PRIVATE` escribible y la prueba concurrente `MADV_DONTNEED` dejó intacto el archivo de respaldo.

### Evaluación de desempeño

Se realizaron 10 ejecuciones por escenario en WSL2, con un archivo de 64 MiB en `/tmp/mmap-bench.dat`; cada ejecución hizo 100 ciclos de mapeo compartido de lectura, acceso a una ubicación por página y `munmap`. Se midió con `CLOCK_MONOTONIC` y `getrusage`; la segunda tanda se ejecutó con `mmap_guard` activo. Los resultados fueron:

| Escenario | Mediana de map-touch-unmap (ms/ciclo) | Rango (ms) | Minor faults por proceso | Major faults |
|---|---:|---:|---:|---:|
| Sin política, 10 ejecuciones | 7.149 | 4.822–16.762 | 102,402 | 0 |
| BPF LSM activo, 10 ejecuciones | 7.174 | 4.981–10.429 | 102,402 | 0 |

La diferencia de medianas observada es +0.025 ms por ciclo (aprox. +0.35%). La variación entre ejecuciones es grande y solo se hicieron 10 repeticiones; por tanto, esta medición exploratoria no permite atribuir con confianza esa diferencia al hook. El workload combina creación del mapping, recorrido de 64 MiB y `munmap`, no aísla la latencia de la syscall `mmap`. No se midieron ciclos/instrucciones con `perf` ni se separaron experimentos de cache fría/caliente. Para un resultado más sólido se recomienda al menos 30 repeticiones y `perf stat` si el kernel WSL expone esos contadores.

### Limitaciones y trabajo futuro

- El hook protege creación de mappings y no intercepta escrituras por puntero luego de instalarse la PTE.
- No inspecciona ni cifra contenido y no evita `write(2)`, truncado u otros canales de acceso.
- La regla global puede romper aplicaciones que legítimamente necesiten shared writable; una versión de producción necesita política por inode/credencial/namespace y gestión segura de configuración.
- Requiere que BPF LSM esté activo en el orden LSM de arranque, BTF disponible y capacidades adecuadas; el comportamiento puede variar por versión.
- Validar firma y retorno contra `vmlinux.h` generado en la distribución destino y compilar ahí; el prototipo aún necesita esa verificación práctica.
- Trabajo futuro: política por objeto en mapa BPF, auditoría de denegaciones con ring buffer, integración con despliegue/pinning apropiado, pruebas de concurrencia COW seguras y benchmark reproducible. Para amenaza de kernel comprometido, considerar Secure Boot, lockdown, aislamiento por hipervisor y tecnologías de memoria cifrada, sin asumir que sustituyen control de acceso.

## Referencias técnicas para consulta

Consultar la documentación del kernel objetivo y sus headers instalados, en particular `Documentation/bpf/`, `Documentation/security/LSM.rst`, `include/linux/mm.h`, UAPI `include/uapi/asm-generic/mman-common.h`, documentación de `mmap(2)`, `msync(2)`, `fsync(2)` y CVE-2016-5195. La versión del kernel y distribución deben anotarse en la defensa porque las interfaces internas cambian.

