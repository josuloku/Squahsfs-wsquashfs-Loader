# WsquashTools — Compresor de juegos para SquashWinFS

## English

**WsquashTools** is a companion tool for [SquashWinFS](https://github.com/Josuloku/SquashWinFS) that helps you create `.wsquashfs` images from your game folders. Simply select the game directory, specify the executable, and it automatically generates the `autorun.cmd` needed for the loader to work.

---

### Features

- **Easy-to-use GUI**: Select folder, pick executable, done
- **Automatic autorun.cmd**: Generates the launch script automatically
- **Compression presets**: Choose compression level (fast, balanced, maximum)
- **Multiple codecs**: Supports zstd, lz4, lzma, lzo, gzip
- **Extraction tool**: Also includes unsquashfs to extract existing images

---

### How it works

1. Run `ToolsApp.exe`
2. Click "Select Folder" and choose your game directory
3. Click "Select Executable" and pick the main `.exe`
4. Choose compression settings (codec and level)
5. Click "Create Image"
6. Done! The `.wsquashfs` is ready to use with SquashWinFS

---

### Requirements

- **Windows 10/11** (x64)
- No additional dependencies required - all tools are bundled

---

### Included tools

- `mksquashfs.exe` - Creates SquashFS images
- `unsquashfs.exe` - Extracts SquashFS images
- Various compression libraries (zstd, lz4, lzma, lzo)

---

## Español

**WsquashTools** es una herramienta complementaria para [SquashWinFS](https://github.com/Josuloku/SquashWinFS) que te ayuda a crear imágenes `.wsquashfs` desde tus carpetas de juegos. Simplemente selecciona el directorio del juego, indica el ejecutable, y genera automáticamente el `autorun.cmd` necesario para que el loader funcione.

---

### Características

- **GUI fácil de usar**: Selecciona carpeta, elige ejecutable, listo
- **autorun.cmd automático**: Genera el script de lanzamiento automáticamente
- **Ajustes de compresión**: Elige nivel (rápido, equilibrado, máximo)
- **Múltiples códecs**: Soporta zstd, lz4, lzma, lzo, gzip
- **Herramienta de extracción**: También incluye unsquashfs para extraer imágenes existentes

---

### Cómo funciona

1. Ejecuta `ToolsApp.exe`
2. Clic en "Seleccionar Carpeta" y elige el directorio del juego
3. Clic en "Seleccionar Ejecutable" y elige el `.exe` principal
4. Elige los ajustes de compresión (códec y nivel)
5. Clic en "Crear Imagen"
6. ¡Hecho! El `.wsquashfs` está listo para usar con SquashWinFS

---

### Requisitos

- **Windows 10/11** (x64)
- No requiere dependencias adicionales - todas las herramientas están incluidas

---

### Herramientas incluidas

- `mksquashfs.exe` - Crea imágenes SquashFS
- `unsquashfs.exe` - Extrae imágenes SquashFS
- Varias librerías de compresión (zstd, lz4, lzma, lzo)

---

## Workflow completo / Complete Workflow

```
1. WsquashTools → Comprime tu juego en .wsquashfs
2. Transferir al PC Windows
3. SquashWinFS → Monta la imagen al instante
4. ¡Juega!
```

---

## Support / Apoyo

☕ **Did this save you time?** This was developed in my spare time with care, and I'll keep releasing related tools. If it saves you time, space, or headaches, consider buying me a coffee:

👉 **https://ko-fi.com/josuloku** ☕

Every coffee makes my day! 😊

---

## License / Licencia

This project is free software. See included tool terms (squashfs-tools).
