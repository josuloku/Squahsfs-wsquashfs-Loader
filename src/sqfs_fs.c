#include "config.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

extern void mlogf(const char* fmt, ...);


#include <windows.h>


#include <sqfs/io.h>
#include <sqfs/super.h>
#include <sqfs/compressor.h>
#include <sqfs/dir.h>
#include <sqfs/dir_reader.h>
#include <sqfs/data_reader.h>
#include <sqfs/id_table.h>
#include <sqfs/inode.h>
#include <sqfs/predef.h>

#include "sqfs_fs.h"

CRITICAL_SECTION g_dir_cs;  int g_dir_cs_init = 0;
static CRITICAL_SECTION g_data_cs; int g_data_cs_init = 0;

void dir_lock(void) { if (g_dir_cs_init)   EnterCriticalSection(&g_dir_cs); }
void dir_unlock(void) { if (g_dir_cs_init)   LeaveCriticalSection(&g_dir_cs); }
static void data_lock(void) { if (g_data_cs_init) EnterCriticalSection(&g_data_cs); }
static void data_unlock(void) { if (g_data_cs_init) LeaveCriticalSection(&g_data_cs); }


static void normalize_path(const char* in, char* out, size_t outsz)
{
    size_t j = 0;
    if (!in || outsz == 0) { if (outsz) out[0] = '\0'; return; }
    size_t i = 0; while (in[i] == '/' || in[i] == '\\') i++;

    int last_slash = 0;
    for (; in[i] && j + 1 < outsz; ++i) {
        char c = in[i];
        if (c == '\\') c = '/';
        if (c == '/') { if (last_slash) continue; last_slash = 1; }
        else last_slash = 0;
        out[j++] = c;
    }
    while (j > 0 && out[j - 1] == '/') j--;
    out[j] = '\0';
}

int sqfs_ctx_open(SqfsCtx* ctx, const char* image_path, const char* volname)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->image_path = image_path; ctx->volname = volname;

    if (!g_dir_cs_init) { InitializeCriticalSection(&g_dir_cs);  g_dir_cs_init = 1; }
    if (!g_data_cs_init) { InitializeCriticalSection(&g_data_cs); g_data_cs_init = 1; }

    ctx->file = sqfs_open_file(image_path, SQFS_FILE_OPEN_READ_ONLY);
    if (!ctx->file) { mlogf("[error] sqfs_open_file FAIL\n"); goto fail; }

    if (sqfs_super_read(&ctx->super, ctx->file)) {
        mlogf("[error] sqfs_super_read FAIL\n"); goto fail;
    }

    sqfs_compressor_config_t cfg;
    if (sqfs_compressor_config_init(&cfg,
        (SQFS_COMPRESSOR)ctx->super.compression_id,
        ctx->super.block_size,
        SQFS_COMP_FLAG_UNCOMPRESS)) {
        mlogf("[error] compressor_config_init FAIL\n"); goto fail;
    }
    if (sqfs_compressor_create(&cfg, &ctx->cmp)) {
        mlogf("[error] compressor_create FAIL\n"); goto fail;
    }

    ctx->dir = sqfs_dir_reader_create(&ctx->super, ctx->cmp, ctx->file, 0);
    if (!ctx->dir) { mlogf("[error] dir_reader_create FAIL\n"); goto fail; }

    ctx->data = sqfs_data_reader_create(ctx->file, ctx->super.block_size, ctx->cmp, 0);
    if (!ctx->data) { mlogf("[error] data_reader_create FAIL\n"); goto fail; }

    if (sqfs_data_reader_load_fragment_table(ctx->data, &ctx->super)) {
        mlogf("[error] load_fragment_table FAIL\n"); goto fail;
    }

    ctx->idtbl = sqfs_id_table_create(0);
    if (!ctx->idtbl) { mlogf("[error] id_table_create FAIL\n"); goto fail; }
    if (sqfs_id_table_read(ctx->idtbl, ctx->file, &ctx->super, ctx->cmp)) {
        mlogf("[error] id_table_read FAIL\n"); goto fail;
    }

    return 0;

fail:
    if (ctx->idtbl) { sqfs_destroy(ctx->idtbl); ctx->idtbl = NULL; }
    if (ctx->data) { sqfs_destroy(ctx->data); ctx->data = NULL; }
    if (ctx->dir) { sqfs_destroy(ctx->dir); ctx->dir = NULL; }
    if (ctx->cmp) { sqfs_destroy(ctx->cmp); ctx->cmp = NULL; }
    if (ctx->file) { sqfs_destroy(ctx->file); ctx->file = NULL; }
    return -EIO;
}

void sqfs_ctx_close(SqfsCtx* ctx)
{
    if (!ctx) return;
    if (ctx->idtbl)  sqfs_destroy(ctx->idtbl);
    if (ctx->data)   sqfs_destroy(ctx->data);
    if (ctx->dir) { dir_lock(); sqfs_destroy(ctx->dir); dir_unlock(); }
    if (ctx->cmp)    sqfs_destroy(ctx->cmp);
    if (ctx->file)   sqfs_destroy(ctx->file);
    memset(ctx, 0, sizeof(*ctx));

    if (g_dir_cs_init) { DeleteCriticalSection(&g_dir_cs);  g_dir_cs_init = 0; }
    if (g_data_cs_init) { DeleteCriticalSection(&g_data_cs); g_data_cs_init = 0; }
}

static int sqfs_find_case_insensitive(SqfsCtx* ctx, sqfs_inode_generic_t* parent, const char* name, sqfs_inode_generic_t** out_inode) {
    if (sqfs_dir_reader_open_dir(ctx->dir, parent, 0) != 0) return -ENOENT;
    sqfs_dir_entry_t* dent = NULL;
    size_t name_len = strlen(name);

    while (sqfs_dir_reader_read(ctx->dir, &dent) == 0) {
        size_t dent_name_len = (size_t)dent->size + 1;
        if (dent_name_len == name_len && _strnicmp((const char*)dent->name, name, name_len) == 0) {
            int ir = sqfs_dir_reader_get_inode(ctx->dir, out_inode);
            sqfs_free(dent);
            return ir;
        }
        sqfs_free(dent);
    }
    return -ENOENT;
}


int sqfs_lookup_path(SqfsCtx* ctx, const char* path, sqfs_inode_generic_t** out_inode)
{
    char rel[4096];
    normalize_path(path, rel, sizeof rel);

    if (rel[0] == '\0') {
        dir_lock();
        int r = sqfs_dir_reader_get_root_inode(ctx->dir, out_inode);
        dir_unlock();
        return (r == 0) ? 0 : -ENOENT;
    }

    dir_lock();
    // Intento 1: Búsqueda exacta
    int r = sqfs_dir_reader_find_by_path(ctx->dir, NULL, rel, out_inode);
    if (r == 0) { dir_unlock(); return 0; }

    // Intento 2: Búsqueda por componentes e insensible a mayúsculas
    sqfs_inode_generic_t* current = NULL;
    sqfs_dir_reader_get_root_inode(ctx->dir, &current);

    char* copy = _strdup(rel);
    char* next_tok = NULL;
    char* seg = strtok_s(copy, "/", &next_tok);

    while (seg) {
        sqfs_inode_generic_t* next_inode = NULL;
        if (sqfs_dir_reader_find_by_path(ctx->dir, current, seg, &next_inode) != 0) {
            if (sqfs_find_case_insensitive(ctx, current, seg, &next_inode) != 0) {
                sqfs_free(current);
                current = NULL;
                break;
            }
        }
        sqfs_free(current);
        current = next_inode;
        seg = strtok_s(NULL, "/", &next_tok);
    }
    free(copy);

    if (current) {
        *out_inode = current;
        dir_unlock();
        return 0;
    }
    dir_unlock();
    return -ENOENT;
}


int sqfs_stat_from_inode(SqfsCtx* ctx, const sqfs_inode_generic_t* inode, SqfsStat* st)
{
    (void)ctx;
    memset(st, 0, sizeof(*st));
    sqfs_u16 mode = inode->base.mode;
    unsigned int typ = (unsigned int)(mode & S_IFMT);
    st->nlink = 1;
    if (typ == S_IFDIR) {
        st->mode = S_IFDIR | 0777;
        st->size = 0;
    }
    else if (typ == S_IFREG) {
        st->mode = S_IFREG | 0777;
        sqfs_u64 sz = 0;
        if (sqfs_inode_get_file_size(inode, &sz) == 0) st->size = sz;
    }
    else if (typ == S_IFLNK) {
        st->mode = S_IFLNK | 0777;
        sqfs_u64 target_len = 0;
        if (inode->base.type == SQFS_INODE_SLINK) {
            target_len = inode->data.slink.target_size;
        }
        else if (inode->base.type == SQFS_INODE_EXT_SLINK) {
            target_len = inode->data.slink_ext.target_size;
        }
        st->size = target_len;
    }
    st->sq_mtime = (uint64_t)inode->base.mod_time;
    st->ino = (uint64_t)inode->base.inode_number;
    return 0;
}


int sqfs_read_file_range(SqfsCtx* ctx, const sqfs_inode_generic_t* inode,
    uint64_t off, void* buf, size_t size, size_t* out_read)
{
    if ((inode->base.mode & S_IFMT) != S_IFREG) return -EISDIR;
    sqfs_u64 fsz = 0;
    if (sqfs_inode_get_file_size(inode, &fsz) != 0) return -EIO;
    
    if (off >= fsz) { *out_read = 0; return 0; }
    size_t remain = (size_t)((off + size > fsz) ? (fsz - off) : size);
    unsigned char* p = (unsigned char*)buf;

    while (remain > 0) {
        size_t chunk = (remain > 1024 * 1024 ? 1024 * 1024 : remain);
        data_lock();
        int r = sqfs_data_reader_read(ctx->data, (sqfs_inode_generic_t*)inode, off, p, chunk);
        data_unlock();
        if (r < 0) return -EIO;
        if (r == 0) break;
        
        p += r;
        off += r;
        remain -= r;
    }
    *out_read = (size_t)(p - (unsigned char*)buf);
    return 0;
}

int sqfs_read_symlink(SqfsCtx* ctx, const sqfs_inode_generic_t* inode, char* target_buf, size_t buf_sz)
{
    (void)ctx;
    const char* target_path = NULL;
    size_t target_len = 0;

    if (inode->base.type == SQFS_INODE_SLINK) {
        target_len = inode->data.slink.target_size;
        target_path = (const char*)inode->extra;
    }
    else if (inode->base.type == SQFS_INODE_EXT_SLINK) {
        target_len = inode->data.slink_ext.target_size;
        target_path = (const char*)inode->extra;
    }
    else {
        return -EINVAL;
    }

    if (!target_path || target_len == 0) return -EIO;
    if (target_len + 1 > buf_sz) return -ENAMETOOLONG;

    memcpy(target_buf, target_path, target_len);
    target_buf[target_len] = '\0';
    return 0;
}
