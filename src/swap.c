
/*
 * Copyright (c) 2012 Dave Vasilevsky <dave@vasilevsky.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "swap.h"

#ifndef HAVE_ASM_BYTEORDER_H
void sqfs_swapin16(uint16_t *v) {
	uint16_t x = *v;
	*v = (x >> 8) | (x << 8);
}

void sqfs_swapin32(uint32_t *v) {
	uint32_t x = *v;
	*v = ((x & 0xFF000000) >> 24) |
	     ((x & 0x00FF0000) >>  8) |
	     ((x & 0x0000FF00) <<  8) |
	     ((x & 0x000000FF) << 24);
}

void sqfs_swapin64(uint64_t *v) {
	uint64_t x = *v;
	*v = ((x & 0xFF00000000000000ULL) >> 56) |
	     ((x & 0x00FF000000000000ULL) >> 40) |
	     ((x & 0x0000FF0000000000ULL) >> 24) |
	     ((x & 0x000000FF00000000ULL) >>  8) |
	     ((x & 0x00000000FF000000ULL) <<  8) |
	     ((x & 0x0000000000FF0000ULL) << 24) |
	     ((x & 0x000000000000FF00ULL) << 40) |
	     ((x & 0x00000000000000FFULL) << 56);
}
#endif

void sqfs_swap16(uint16_t *n) {
	*n = (*n >> 8) + (*n << 8);
}

#include "squashfs_fs.h"

/* Implementation macros for swap.h.inc */
#define SWAP_DATA(name, size) sqfs_swapin##size##_internal(&s->name);
#define SWAP_START(name) void sqfs_swapin_##name(struct squashfs_##name *s) {
#define SWAP_END }

#include "swap.h.inc"
