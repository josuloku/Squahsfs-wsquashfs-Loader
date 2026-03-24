# SquashWinFS

## English

**SquashWinFS** lets you mount SquashFS images directly on Windows as virtual drives using **libsquashfs + WinFsp/FUSE**.

Tired of unpacking gigabytes every time you want to try a game? Have collections prepared on Linux/Batocera and want to use them on Windows without headaches?

The idea in 10 seconds: **Double click → mount the image → run the game. Done.** No folders. No extracting. No waiting.

---

### Features

- **On-the-fly mounting**: No extracting, no waiting
- **Virtual read-only drive**: Appears in Explorer as a real disk
- **Auto autorun**: Reads and executes `autorun.cmd` from inside the image
- **Optional COW layer**: Temporary writes without touching the original image
- **Smart fallback**: If mounting fails, offers extraction with 7-Zip and saves game profile
- **Launcher integration**: Compatible with LaunchBox, CLI frontends, drag and drop...

Temporary writes are stored cleanly at: `%LOCALAPPDATA%\SquashWinFS\Overlay\<Game>`

---

### How to use

#### GUI
Drag the `.wsquashfs` onto the `.exe` and you're done.

#### Extension association
Right click → Open with → Select `SquashWinFS.exe` → Check "Always use" → From now on, double click and play.

#### CLI
```
SquashWinFS.exe -i "path\to\game.wsquashfs" [-m X:] [-o VolumeName] [--mount-auto] [--debug|--trace]
```

---

### Requirements

- **WinFsp driver**: Install once from https://github.com/winfsp/winfsp/releases (free)
- **7-Zip** (optional): For extraction fallback mode. Include `7z.exe` / `7zG.exe` next to the executable or in `tools\7zip\`

---

### Building from source

1. Install [WinFsp](https://github.com/winfsp/winfsp)
2. Open `SquashWinFS.sln` in Visual Studio 2022
3. Build in Release x64
4. Required DLLs are automatically copied to the output directory

---

### Included dependencies

The project includes required libraries in `external/sqfs/`:
- `libsquashfs.dll` - Main SquashFS library
- `libzstd.dll`, `liblz4-1.dll`, `liblzma-5.dll`, `liblzo2-2.dll`, `zlib1.dll` - Compression codecs

---

### Who is this for?

- You have a portable collection and want order and efficiency
- You prepare games from Linux / Batocera and want them on Windows without re-packaging
- You use LaunchBox or another frontend and want a clean, automatable loader
- You simply hate having huge folders scattered across your disk

---

### Image format (.wsquashfs)

The `.wsquashfs` format is simply standard SquashFS. Optionally includes an `autorun.cmd` that runs automatically when mounted.

### Compression tool
**[WsquashTools](https://github.com/Josuloku/wsquash-tools)** - Select the game directory, specify the executable, and automatically generates the `autorun.cmd` for the loader to work.

---

## Español

**SquashWinFS** te permite montar imágenes SquashFS directamente en Windows como unidades virtuales usando **libsquashfs + WinFsp/FUSE**.

¿Cansado de descomprimir gigas y gigas cada vez que quieres probar un juego? ¿Tienes colecciones preparadas en Linux/Batocera y quieres usarlas en Windows sin dramas?

La idea en 10 segundos: **Doble clic → monta la imagen → ejecuta el juego. Fin.** Sin carpetas. Sin extracciones. Sin esperar.

---

### Características

- **Montaje al vuelo**: Sin extraer, sin esperar
- **Unidad virtual de solo lectura**: Aparece en el Explorador como un disco real
- **Autorun automático**: Lee y ejecuta el `autorun.cmd` del interior de la imagen
- **Capa COW opcional**: Escrituras temporales sin tocar la imagen original
- **Fallback inteligente**: Si falla el montaje, ofrece extraer con 7-Zip y guarda perfil por juego
- **Integración con launchers**: Compatible con LaunchBox, frontend CLI, arrastrar y soltar...

Las escrituras temporales se guardan limpiamente en: `%LOCALAPPDATA%\SquashWinFS\Overlay\<Juego>`

---

### Cómo usarlo

#### GUI
Arrastra el `.wsquashfs` sobre el `.exe` y listo.

#### Asociación de extensión
Clic derecho → Abrir con → Seleccionar `SquashWinFS.exe` → Marcar "Usar siempre" → A partir de ahí, doble clic y a jugar.

#### CLI
```
SquashWinFS.exe -i "ruta\al\juego.wsquashfs" [-m X:] [-o Volumen] [--mount-auto] [--debug|--trace]
```

---

### Requisitos

- **Driver WinFsp**: Instalar una vez desde https://github.com/winfsp/winfsp/releases (gratuito)
- **7-Zip** (opcional): Para el modo de extracción fallback. Incluye `7z.exe` / `7zG.exe` junto al ejecutable o en `tools\7zip\`

---

### Compilación desde código fuente

1. Instalar [WinFsp](https://github.com/winfsp/winfsp)
2. Abrir `SquashWinFS.sln` en Visual Studio 2022
3. Compilar en Release x64
4. Las DLLs necesarias se copian automáticamente al directorio de salida

---

### Dependencias incluidas

El proyecto incluye las librerías necesarias en `external/sqfs/`:
- `libsquashfs.dll` - Librería principal SquashFS
- `libzstd.dll`, `liblz4-1.dll`, `liblzma-5.dll`, `liblzo2-2.dll`, `zlib1.dll` - Códecs de compresión

---

### ¿Para quién es esto?

- Tienes una colección portable y quieres orden y eficiencia
- Preparas juegos desde Linux / Batocera y los quieres en Windows sin reempaquetar
- Usas LaunchBox u otro frontend y quieres un loader limpio y automatizable
- Simplemente odias tener carpetas enormes desperdigadas por el disco

---

### Formato de imagen (.wsquashfs)

El formato `.wsquashfs` es simplemente SquashFS estándar. Incluye opcionalmente un `autorun.cmd` que se ejecuta automáticamente al montar.

### Herramienta de compresión
**[WsquashTools](https://github.com/Josuloku/wsquash-tools)** - Selecciona el directorio del juego, indica el ejecutable, y genera automáticamente el `autorun.cmd` para que todo funcione con el loader.

---

## Support / Apoyo

☕ **Did this save you time?** This was developed in my spare time with care, and I'll keep releasing related tools. If it saves you time, space, or headaches, consider buying me a coffee:

👉 **https://ko-fi.com/josuloku** ☕

Every coffee makes my day! 😊

---

## License / Licencia

This project is free software. See included library terms (libsquashfs, WinFsp).
