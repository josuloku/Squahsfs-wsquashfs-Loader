#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct SqfsStat {
    uint32_t mode;
    uint32_t nlink;
    uint64_t size;
    uint64_t sq_mtime;
    uint64_t ino;
} SqfsStat;


#include <sqfs/io.h>
#include <sqfs/super.h>
#include <sqfs/compressor.h>
#include <sqfs/dir.h>
#include <sqfs/dir_reader.h>
#include <sqfs/data_reader.h>
#include <sqfs/id_table.h>
#include <sqfs/inode.h>
#include <sqfs/predef.h>

#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct SqfsCtx {
        sqfs_file_t* file;
        sqfs_super_t         super;
        sqfs_compressor_t* cmp;
        sqfs_dir_reader_t* dir;
        sqfs_data_reader_t* data;
        sqfs_id_table_t* idtbl;
        const char* image_path;
        const char* volname;
    } SqfsCtx;

    int  sqfs_ctx_open(SqfsCtx* ctx, const char* image_path, const char* volname);
    void sqfs_ctx_close(SqfsCtx* ctx);
    int  sqfs_lookup_path(SqfsCtx* ctx, const char* path, sqfs_inode_generic_t** out_inode);
    int  sqfs_stat_from_inode(SqfsCtx* ctx, const sqfs_inode_generic_t* inode, SqfsStat* st);
    int  sqfs_read_file_range(SqfsCtx* ctx, const sqfs_inode_generic_t* inode,
        uint64_t off, void* buf, size_t size, size_t* out_read);
    int  sqfs_read_symlink(SqfsCtx* ctx, const sqfs_inode_generic_t* inode, char* target_buf, size_t buf_sz);
    typedef enum {
        SQFS_LOG_ERROR = 0,
        SQFS_LOG_WARN = 1,
        SQFS_LOG_INFO = 2,
        SQFS_LOG_DEBUG = 3,
        SQFS_LOG_TRACE = 4
    } log_level_t;

    void mlogf_level(log_level_t level, const char* fmt, ...);
    void mlogf(const char* fmt, ...);

    #define MLOG_ERROR(...) mlogf_level(SQFS_LOG_ERROR, __VA_ARGS__)
    #define MLOG_WARN(...) mlogf_level(SQFS_LOG_WARN, __VA_ARGS__)
    #define MLOG_INFO(...) mlogf_level(SQFS_LOG_INFO, __VA_ARGS__)
    #define MLOG_DEBUG(...) mlogf_level(SQFS_LOG_DEBUG, __VA_ARGS__)
    #define MLOG_TRACE(...) mlogf_level(SQFS_LOG_TRACE, __VA_ARGS__)


    void dir_lock(void);
    void dir_unlock(void);

#ifdef __cplusplus
}
#endif
