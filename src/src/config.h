#ifndef SQFS_CONFIG_H
#define SQFS_CONFIG_H

#ifdef _MSC_VER
    #define _CRT_NONSTDC_NO_DEPRECATE
    #pragma warning(disable: 4996) 
    #pragma warning(disable: 4244)
    #pragma warning(disable: 4267)
#endif

#ifndef _WIN32
#define _WIN32
#endif

/* Basic C headers first */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <windows.h>

/* Arreglar off_t para que sea 64 bits en MSVC */
#ifdef _MSC_VER
    #ifndef _OFF_T_DEFINED
        typedef __int64 off_t;
        #define _OFF_T_DEFINED
    #endif
    
    #ifndef SSIZE_T_DEFINED
        #ifdef _WIN64
            typedef __int64 ssize_t;
        #else
            typedef int ssize_t;
        #endif
        #define SSIZE_T_DEFINED
    #endif
#endif

/* Squashfuse configuration */
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 0

#define ENABLE_LZ4 1

/* Compatibility macros */
#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#endif

#endif /* SQFS_CONFIG_H */