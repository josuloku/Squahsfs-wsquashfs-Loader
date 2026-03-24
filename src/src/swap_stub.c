#include "config.h"
#include "squashfuse.h"
#include "swap.h"

/* Implementación manual simple de swap para evitar macros complejas */
static uint16_t swap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}
static uint32_t swap32(uint32_t x) {
    return ((x << 24) | ((x << 8) & 0xFF0000) | ((x >> 8) & 0xFF00) | (x >> 24));
}
static uint64_t swap64(uint64_t x) {
    return ((uint64_t)swap32((uint32_t)x) << 32) | swap32((uint32_t)(x >> 32));
}

void sqfs_swapin_super_block(sqfs_super *s) {
    /* Si el sistema es Little Endian (como Windows/Intel), no hacemos nada */
    /* SquashFS suele ser Little Endian por defecto */
    /* Si necesitas soporte Big Endian, aquí irían las llamadas a swapXX */
}

void sqfs_swapin_inode(sqfs_inode *i) { /* Stub */ }
void sqfs_swapin_dir_header(sqfs_dir_header *h) { /* Stub */ }
void sqfs_swapin_dir_entry(sqfs_dir_entry *e) { /* Stub */ }
void sqfs_swapin_dir_index(sqfs_dir_index *i) { /* Stub */ }
void sqfs_swapin_id(sqfs_id *id) { /* Stub */ }

/* Stubs para xattr que fallaban */
void sqfs_swapin_xattr_id_table(sqfs_xattr_id_table *t) { }
void sqfs_swapin_xattr_id(sqfs_xattr_id *i) { }
void sqfs_swapin_xattr_entry(sqfs_xattr_entry *e) { }
void sqfs_swapin_xattr_val(sqfs_xattr_val *v) { }

/* Stubs para fragmentos */
void sqfs_swapin_fragment_entry(sqfs_fragment_entry *e) { }