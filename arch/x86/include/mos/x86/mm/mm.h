// SPDX-Licence-Identifier: GPL-3.0-or-later

#pragma once

#include "mos/mm/mm_types.h"
#include "mos/types.h"
#include "mos/x86/boot/multiboot.h"

#define MEM_MAX_BLOCKS 64
extern memblock_t x86_mem_regions[MEM_MAX_BLOCKS];
extern size_t x86_mem_regions_count;

void x86_mem_init(const multiboot_mmap_entry_t *map_entry, u32 count);
void x86_mem_add_region(u64 start, size_t size, bool available);
