# TINY ESP32 Neural Terminal — ESP32-S3-WROOM-1 N16R8

Port inicial del motor de inferencia TNY2 del proyecto TINY 2.1 a ESP32-S3, con interfaz web local y telemetría de hardware.

**Hardware objetivo:** ESP32-S3-WROOM-1 N16R8 (16 MB Flash QIO + 8 MB PSRAM OPI). No selecciones OPI Flash: en N16R8 la PSRAM es OPI y la Flash es QIO.

## Contenido

- `TINY_ESP32_Neural_Terminal.ino`: firmware, AP Wi-Fi, API HTTP y tarea FreeRTOS de inferencia.
- `src/TinyEngine.cpp/.h`: lector del formato binario TNY2 y motor Transformer carácter a carácter.
- `src/WebUI.h`: interfaz retro-neón responsive con chat y telemetría.
- `data/TINY.BIN`: coloca aquí el modelo original.
- `partitions.csv`: 3 MB para firmware y el resto de Flash para LittleFS.

## Obtener el modelo

Descarga `TINY.BIN` de la carpeta `dos` del repositorio oficial: https://github.com/WollyMonjac/Tiny-2.1/tree/main/dos

Cópialo exactamente como `data/TINY.BIN`. No se incluye en este ZIP. El firmware valida la cabecera `TNY2`, dimensiones y longitud del archivo.

## Arduino IDE 2.x — instalación

1. Instala Arduino IDE 2.2.1 o posterior.
2. En **Archivo > Preferencias**, añade esta URL en «URLs adicionales de gestor de placas»:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Abre **Herramientas > Placa > Gestor de placas**, busca `esp32` e instala **esp32 by Espressif Systems** (core 3.x).
4. Descomprime el proyecto. Abre `TINY_ESP32_Neural_Terminal.ino` desde su carpeta. No muevas el `.ino` fuera de esa carpeta.
5. Copia el modelo original en `data/TINY.BIN`.
6. Instala el complemento LittleFS para IDE 2.x (véase «Subir LittleFS» más abajo).

## Arduino IDE 2.x — configuración de placa

En **Herramientas** selecciona:

- Placa: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled` (si utilizas el USB nativo de la placa; si usas el puente USB-UART puedes dejarlo Disabled)
- CPU Frequency: `240MHz (WiFi)`
- Flash Size: `16MB (128Mb)`
- Flash Mode: `QIO 80MHz`
- PSRAM: `OPI PSRAM`
- Partition Scheme: el archivo `partitions.csv` del proyecto se aplica como tabla personalizada en el sketch. Si tu core no lo detecta automáticamente, usa una tabla personalizada equivalente con 3 MB APP y el resto LittleFS; no elijas una tabla de 4 MB.
- Upload Mode: `UART0 / Hardware CDC` (según el conector USB usado)
- Upload Speed: empieza con `460800`; si falla, baja a `115200`.

**Importante:** N16R8 significa Flash de 16 MB y PSRAM de 8 MB. Según Espressif, para WROOM-1 N16R8 corresponde Flash QIO y PSRAM OPI. Comprueba la serigrafía del módulo y la salida serie de arranque.

## Instalar el cargador LittleFS en Arduino IDE 2.x

El IDE no sube automáticamente la carpeta `data/`. Instala el complemento oficial `arduino-littlefs-upload` compatible con Arduino IDE 2.2.1 o superior:

1. Abre https://github.com/earlephilhower/arduino-littlefs-upload
2. Descarga el archivo `.vsix` desde la sección de releases (o desde el enlace VSIX del README del proyecto).
3. Cierra Arduino IDE.
4. En Linux crea la carpeta: `mkdir -p ~/.arduinoIDE/plugins`
5. Copia el `.vsix` descargado dentro de `~/.arduinoIDE/plugins/`.
6. Reinicia Arduino IDE.

Para subir el sistema de archivos: abre el sketch, selecciona la placa y el puerto, cierra el Monitor Serie y pulsa `Ctrl+Shift+P`. Ejecuta **Upload LittleFS to Pico/ESP8266/ESP32**. El complemento construye y carga la imagen a la partición LittleFS.

## Orden de carga

1. Con el proyecto abierto, selecciona `ESP32S3 Dev Module`, puerto y configuración N16R8.
2. Ejecuta **Sketch > Verify/Compile**.
3. Carga el firmware con el botón **Upload**.
4. Cierra el Monitor Serie si lo abriste.
5. Ejecuta `Ctrl+Shift+P` → **Upload LittleFS to Pico/ESP8266/ESP32** para cargar `data/TINY.BIN`.
6. Abre el Monitor Serie a 115200 baudios y pulsa RESET.
7. Conecta el móvil/PC a `TINY-NEURAL`, contraseña inicial `tinyterminal`.
8. Abre `http://192.168.4.1`.

Si cambias `partitions.csv`, vuelve a cargar primero el firmware (para escribir la nueva tabla) y después LittleFS. Cambiar la tabla puede borrar el contenido anterior de Flash.

## Telemetría disponible

La página consulta `GET /api/status` y presenta: heap libre/total/mínimo, PSRAM libre/total, Flash, uso de LittleFS, CPU MHz, temperatura interna del chip (si el core expone una lectura válida), tiempo de inferencia, caracteres/s, caracteres generados y actividad feed-forward por capa. La temperatura es del silicio, no temperatura ambiente.

## API

- `GET /` — interfaz web.
- `GET /api/status` — estado y telemetría JSON.
- `POST /api/chat` — texto plano, hasta 72 caracteres ASCII; responde HTTP 202 y la interfaz consulta el resultado.
- `POST /api/config` — parámetro `creativity=0..9`.

## Limitaciones conocidas

- El modelo original usa 97 tokens: padding, fin de línea y caracteres ASCII imprimibles (` ` a `~`). La primera adaptación sustituye caracteres fuera de ASCII por espacios; no es todavía un modelo español con tildes y ñ.
- TINY se entrenó con un conjunto limitado de hechos y puede inventar respuestas fuera de su dominio.
- El indicador de actividad por capa representa la media de activaciones ReLU de la subcapa feed-forward; no es una visualización completa de atención ni de todos los pesos.
- Esta adaptación debe validarse en la placa física y con el `TINY.BIN` oficial. No se declara compilación ni funcionamiento en hardware verificados.
- El punto de acceso usa una contraseña inicial de demostración. Cámbiala en el `.ino` antes de compartir el dispositivo.

## Licencias

Código original TINY 2.1: MIT. Este port se distribuye bajo MIT. Revisa `DATA_LICENSES.md` del repositorio original antes de redistribuir conjuntos de datos o modelos derivados.


## FIX2 — corrección de la interfaz

Se protege la actualización del contador de caracteres cuando el elemento `#chars` no está presente en el DOM. Evita que una excepción JavaScript interrumpa `apiStatus()` y deje el panel en `LINK LOST`/`INITIALIZING` aunque el servidor HTTP y el modelo estén operativos.
