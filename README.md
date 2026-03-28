ko-fi.com/josuloku ***

# SquashWinFS (v1.1)

[Español](#español) | [English](#english)

---

<a name="español"></a>
# 🇪🇸 Español

**SquashWinFS** es una herramienta ligera para Windows diseñada para montar imágenes SquashFS de forma transparente. Permite ejecutar juegos o aplicaciones directamente desde contenedores comprimidos sin descompresión previa, utilizando un sistema de **Overlay** para persistencia de datos.

Desarrollado por **Josuloku**.

## ✨ Características
- **Montaje FUSE:** Monta archivos `.squashfs` y `.wsquashfs` como unidades virtuales.
- **Sistema de Overlay:** Las escrituras (savegames, configs) se guardan en `%LOCALAPPDATA%\SquashWinFS\Overlay`, manteniendo la imagen original intacta.
- **Detección de Ejecución:** Ejecuta automáticamente el archivo `autorun.cmd` si existe en la raíz de la imagen.
- **Modo Extracción (Anti-Cheat):** Fallback automático a extracción física si el juego tiene sistemas Anti-Cheat incompatibles con unidades virtuales.

## 📋 Requisitos y Dependencias

Para que SquashWinFS funcione, es fundamental distinguir entre lo que ya viene incluido y lo que debes instalar por tu cuenta:

### ✅ Ya incluido (No requiere instalación)
- **Librerías SquashFS:** Las DLLs necesarias (`libsquashfs.dll`, `zlib1.dll`, etc.) **ya vienen incluidas** junto al ejecutable. No necesitas buscarlas ni instalarlas aparte, pero **deben permanecer en la misma carpeta** que `SquashWinFS.exe`.

### ⚠️ Requiere Instalación Manual (OBLIGATORIO)
- **WinFSP (Windows File System Proxy):** Es el motor que permite crear unidades virtuales. **Debes instalarlo manualmente** en tu sistema o el programa no podrá funcionar.
    - **[Descargar WinFSP aquí](https://winfsp.dev/rel/)** (Descarga e instala la versión `.msi` más reciente).

## 🚀 Cómo Ejecutarlo

### 1. Arrastrar y Soltar (Drag & Drop)
Arrastra tu archivo `.squashfs` o `.wsquashfs` sobre `SquashWinFS.exe`. El programa buscará una letra libre, montará la imagen y ejecutará el contenido.

### 2. Registrar como Programa Predeterminado (Recomendado)
Para abrir tus juegos con un doble clic:
1. Clic derecho sobre un archivo `.squashfs` -> **Abrir con...**
2. Selecciona **Elegir otra aplicación**.
3. Busca `SquashWinFS.exe` en tu PC.
4. Marca **"Siempre usar esta aplicación para abrir archivos .squashfs"**.
5. Repite para la extensión `.wsquashfs`.

### 3. Línea de Comandos (Avanzado)
```powershell
# Montaje básico
SquashWinFS.exe -i "juego.squashfs" -m Z:

# Forzar modo extracción
SquashWinFS.exe -i "juego.squashfs" --mode extract
```

---

<a name="english"></a>
# 🇺🇸 English

**SquashWinFS** is a lightweight Windows tool designed to mount SquashFS images transparently. It allows you to run games or applications directly from compressed containers without prior decompression, using an **Overlay** system for data persistence.

Developed by **Josuloku**.

## ✨ Features
- **FUSE Mounting:** Mounts `.squashfs` and `.wsquashfs` files as virtual drives.
- **Overlay System:** All writes (savegames, configs) are stored in `%LOCALAPPDATA%\SquashWinFS\Overlay`, keeping the original image intact.
- **Auto-Run Detection:** Automatically executes `autorun.cmd` if found in the image root.
- **Extraction Mode (Anti-Cheat):** Automatic fallback to physical extraction if the game's Anti-Cheat system is incompatible with virtual drives.

## 📋 Requirements and Dependencies

For SquashWinFS to work, you must distinguish between what is already included and what you need to install yourself:

### ✅ Already Included (No installation required)
- **SquashFS Libraries:** The necessary DLLs (`libsquashfs.dll`, `zlib1.dll`, etc.) **are already included** in the package. You don't need to install them separately, but **they must remain in the same directory** as `SquashWinFS.exe`.

### ⚠️ Manual Installation Required (MANDATORY)
- **WinFSP (Windows File System Proxy):** This is the core engine for virtual drives. **You must install it manually** or the program will not work.
    - **[Download WinFSP here](https://winfsp.dev/rel/)** (Download and install the latest `.msi` version).

## 🚀 How to Run

### 1. Drag & Drop
Simply drag your `.squashfs` or `.wsquashfs` file onto `SquashWinFS.exe`. The program will find a free drive letter, mount the image, and run the content.

### 2. Set as Default Program (Recommended)
To open your games with a double-click:
1. Right-click a `.squashfs` file -> **Open with...**
2. Select **Choose another app**.
3. Browse and select `SquashWinFS.exe` on your PC.
4. Check **"Always use this app to open .squashfs files"**.
5. Repeat for the `.wsquashfs` extension.

### 3. Command Line (Advanced)
```powershell
# Basic mount
SquashWinFS.exe -i "game.squashfs" -m Z :

# Force extraction mode
SquashWinFS.exe -i "game.squashfs" --mode extract
```

ko-fi.com/josuloku ***

---
**Credits:** Created by **Josuloku**.
