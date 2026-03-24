/* src/lz4.c CORREGIDO */
#include "config.h"
#include "squashfuse.h" 
#include <sqfs/compressor.h>

#ifdef ENABLE_LZ4
#include <lz4.h>

/* Wrapper para adaptar la firma de LZ4 a sqfs_compressor_t */
static sqfs_s32 lz4_do_block(sqfs_compressor_t *cmp, const sqfs_u8 *in,
			     sqfs_u32 size, sqfs_u8 *out, sqfs_u32 outsize) {
    /* El cast a int es necesario para lz4 */
    int res = LZ4_decompress_safe((const char*)in, (char*)out, (int)size, (int)outsize);
    if (res < 0) return SQFS_ERR;
    return res; /* Devuelve el número de bytes escritos */
}

/* No necesitamos configuración compleja para LZ4 en lectura */
static void lz4_get_configuration(const sqfs_compressor_t *cmp, sqfs_compressor_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->id = SQFS_COMP_LZ4;
}

static sqfs_compressor_t lz4_comp = {
    .base = { 0 }, /* sqfs_object_t base */
    .get_configuration = lz4_get_configuration,
    .write_options = NULL,
    .read_options = NULL,
    .do_block = lz4_do_block
};

int sqfs_compressor_lz4_init(sqfs_compressor_config_t *cfg, sqfs_compressor_t **cmp) {
    *cmp = &lz4_comp; 
    return SQFS_OK;
}
#else
int sqfs_compressor_lz4_init(sqfs_compressor_config_t *cfg, sqfs_compressor_t **cmp) {
    return SQFS_ERR;
}
#endif