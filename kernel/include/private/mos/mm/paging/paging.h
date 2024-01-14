// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mos/mm/mm.h"

#include <mos/lib/structures/bitmap.h>
#include <mos/lib/sync/spinlock.h>
#include <mos/platform/platform.h>

/**
 * @defgroup paging Paging
 * @ingroup mm
 * @brief A platform-independent interface to map/unmap virtual memory to physical memory.
 *
 * @{
 */

typedef enum
{
    VALLOC_DEFAULT = 0,        ///< Default allocation flags.
    VALLOC_EXACT = MMAP_EXACT, ///< Allocate pages at the exact address.
} valloc_flags;

/**
 * @brief Gets npages unmapped free pages from a page table.
 *
 * @param table The page table to allocate from.
 * @param npages The number of pages to allocate.
 * @param base_vaddr The base virtual address to allocate at.
 * @param flags Flags to set on the pages, see @ref valloc_flags.
 * @return ptr_t The virtual address of the block of virtual memory.
 *
 * @note This function neither allocates nor maps the pages, it only
 * determines the block of virtual memory that can be used to allocate
 * and map the pages.
 *
 * @note The returned @ref vmblock_t only contains the virtual address
 * and the number of pages, it does not contain any physical addresses,
 * nor the flags of the pages.
 *
 * @warning Should call with mmctx->mm_lock held.
 */

vmap_t *mm_get_free_vaddr_locked(mm_context_t *mmctx, size_t n_pages, ptr_t base_vaddr, valloc_flags flags);

/**
 * @brief Map a block of virtual memory to a block of physical memory.
 *
 * @param table The page table to map to.
 * @param vaddr The virtual address to map to.
 * @param pfn The physical frame to map from.
 * @param npages The number of pages to map.
 * @param flags Flags to set on the pages, see @ref vm_flags.
 *
 * @details This function maps the pages in the block, their reference count will NOT be incremented.
 *
 * @note This function is rarely used directly, you don't always know the physical address of the
 * pages you want to map.
 */
void mm_map_kernel_pages(mm_context_t *mmctx, ptr_t vaddr, pfn_t pfn, size_t npages, vm_flags flags);
vmap_t *mm_map_user_pages(mm_context_t *mmctx, ptr_t vaddr, pfn_t pfn, size_t npages, vm_flags flags, valloc_flags vaflags, vmap_type_t type, vmap_content_t content);

/**
 * @brief Replace the mappings of a page with a new physical frame.
 *
 * @param table The page table to replace in.
 * @param vaddr The virtual address to replace.
 * @param pfn The physical frame to replace to.
 * @param flags The new flags to set on the pages.
 * @return vmblock_t The replaced block of virtual memory, with the number of pages.
 *
 * @note The reference count of the physical frame will be incremented, and the reference count of the
 * old physical frame will be decremented.
 */
void mm_replace_page_locked(mm_context_t *mmctx, ptr_t vaddr, pfn_t pfn, vm_flags flags);

/**
 * @brief Remap a block of virtual memory from one page table to another, i.e. copy the mappings.
 *
 * @param from The page table to copy from.
 * @param fvaddr The virtual address to copy from.
 * @param to The page table to copy to.
 * @param tvaddr The virtual address to copy to.
 * @param npages The number of pages to copy.
 * @param clear_dest Whether to clear the destination page table before copying.
 *
 * @details This function firstly unmaps the pages in the destination page table, then copies the pages
 * mappings, including the flags and physical addresses, the physical page reference count is updated
 * accordingly.
 *
 * @note If clear_dest is set to true, then the destination page table is cleared before copying, otherwise
 * the function assumes that there are no existing mappings in the destination page table.
 */
vmap_t *mm_clone_vmap_locked(vmap_t *src_vmap, mm_context_t *dst_ctx);

/**
 * @brief Get if a virtual address is mapped in a page table.
 *
 * @param table The page table to get the physical address from.
 * @param vaddr The virtual address to get the physical address of.
 * @return bool True if the virtual address is mapped, false otherwise.
 */
bool mm_get_is_mapped_locked(mm_context_t *mmctx, ptr_t vaddr);

/**
 * @brief Update the flags of a block of virtual memory.
 *
 * @param table The page table to update the flags in.
 * @param vaddr The virtual address to update the flags of.
 * @param npages The number of pages to update the flags of.
 * @param flags The flags to set on the pages, see @ref vm_flags.
 *
 * @note This function is just a wrapper around @ref platform_mm_flag_pages, with correct locking.
 */
void mm_flag_pages_locked(mm_context_t *mmctx, ptr_t vaddr, size_t npages, vm_flags flags);

ptr_t mm_get_phys_addr(mm_context_t *ctx, ptr_t vaddr);

/** @} */
