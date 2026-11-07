# Sistemas Operativos Parcial 3

## Seguridad de archivos mapeados con mmap

Proyecto académico para el parcial de Sistemas Operativos (EAFIT, 2026-2). Implementa una política de control en el hook `mmap_file` de BPF LSM para bloquear mapeos compartidos escribibles, junto con pruebas reproducibles en espacio de usuario. Esta política protege la decisión de crear el mapeo; no inspecciona cada carga o escritura que ocurre después de instalar las PTE.

## Arquitectura

```text
proceso -> mmap(2) -> VFS/LSM: mmap_file -> política BPF LSM -> permitir o -EACCES
```

El hook BPF LSM usa `mmap_file(struct file *, unsigned long, unsigned long, unsigned long)`. Por defecto niega `MAP_SHARED` con `PROT_WRITE`; los mapeos privados escribibles permanecen permitidos por COW, sin modificar el archivo subyacente. La regla es deliberadamente sencilla y configurable en código. Requiere kernel con BPF LSM habilitado, privilegios para cargar el programa y `bpftool`/libbpf.

## Archivos

- `src/mmap_guard.bpf.c`: política BPF LSM (C).
- `src/mmap_guard.c`: cargador/libbpf, adjunta el programa y mantiene la política activa.
- `tests/mmap_probe.c`: demuestra que un `MAP_SHARED` escribible es rechazado y un `MAP_PRIVATE` escribible puede modificarse en privado.
- `tests/mmap_race.c`: estresa escrituras privadas concurrentes con `MADV_DONTNEED` y comprueba que el archivo de respaldo no cambie.
- `tests/mmap_bench.c`: mide el tiempo combinado de mapear, tocar cada página y desmontar una región de lectura.
- `tests/run_tests.sh`: compila y ejecuta la prueba; puede usar `sudo` para cargar la política.
- `docs/informe.md`: análisis, respuestas diagnósticas, estrategia IA y plan de medición.
- `output/pdf/informe_sistemas_operativos_parcial3.pdf`: informe final en PDF.
- `scripts/build_report_pdf.py`: genera el PDF a partir del informe Markdown.

## Requisitos y compilación

Linux 5.x/6.x con `CONFIG_BPF`, `CONFIG_BPF_SYSCALL`, `CONFIG_BPF_LSM`, BTF disponible y `bpf` en `CONFIG_LSM`. Se necesitan clang/LLVM, libbpf de desarrollo, bpftool, pkg-config y make. En Ubuntu/WSL:

```sh
sudo apt update
sudo apt install -y build-essential clang llvm libbpf-dev bpftool pkg-config
make clean
make
sudo ./build/mmap_guard
```

El cargador permanece en primer plano: detenerlo descarga el programa y retira la política. En otra terminal:

```sh
make test
```

Para una medición básica, crea un archivo de prueba de 64 MiB y ejecuta el benchmark antes y después de activar el guard. La utilidad solo mapea en lectura para aislar el costo del hook en mappings permitidos:

```sh
dd if=/dev/zero of=/tmp/mmap-bench.dat bs=1M count=64 status=progress
./build/mmap_bench /tmp/mmap-bench.dat 100 64
```

Guarda varias ejecuciones en ambos estados y compara medianas; `perf stat -r 30 -e page-faults,minor-faults,major-faults,cycles,instructions ./build/mmap_bench /tmp/mmap-bench.dat 100 64` añade contadores de hardware cuando estén disponibles. Anota kernel, CPU, tamaño y si la page cache estaba caliente. Los contadores globales del proceso no separan por sí solos fallos fríos y calientes.

La prueba crea archivos temporales en un directorio temporal y los elimina al terminar. Sin privilegios o sin BPF LSM habilitado, la prueba de denegación no puede demostrar la defensa activa; el script reporta la condición.

## Límites y seguridad

La política no es cifrado, no inspecciona contenido, no previene escrituras por `write(2)` ni protege contra root/kernel comprometido. `MAP_PRIVATE` permite modificaciones privadas y puede revelar datos que el proceso ya podía leer. El hook valida la creación del VMA, no accesos posteriores. Use una VM de laboratorio: nunca experimente con módulos que parcheen la syscall table ni con cambios de CR0. No ejecute PoC de Dirty COW en sistemas de producción.

Consulte `docs/informe.md` para supuestos, diseño, respuestas, resultados reportados de compilación/pruebas en WSL2 y mediciones exploratorias del benchmark. Las cifras incluyen el kernel y las limitaciones del entorno; no deben generalizarse a otras máquinas.

