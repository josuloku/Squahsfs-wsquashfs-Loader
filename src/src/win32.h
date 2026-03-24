#ifndef SQFS_WIN32_H
#define SQFS_WIN32_H
#include <windows.h>
#include <sys/stat.h>

/* Basic definitions for Windows compatibility */
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#endif

/* Type definitions to match Linux usually found in sys/types.h but missing or different in Windows */
typedef int uid_t;
typedef int gid_t;


#endif
