// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mos/platform/platform_defs.h"

#include <mos/mos_global.h>

#define MOS_MAX_PAGE_LEVEL 5

#if MOS_PLATFORM_PAGING_LEVELS > 4
#error "more levels are not supported"
#endif

// ! platform-independent pml types

#define define_pmlx(pmln)                                                                                                                                                \
    typedef struct                                                                                                                                                       \
    {                                                                                                                                                                    \
        pte_content_t content;                                                                                                                                           \
    } __packed pmln##e_t;                                                                                                                                                \
    typedef struct                                                                                                                                                       \
    {                                                                                                                                                                    \
        pmln##e_t *table;                                                                                                                                                \
    } pmln##_t

// nah, your platform must have at least 1 level of paging
define_pmlx(pml1);

#define pml1_index(vaddr) ((vaddr >> PML1_SHIFT) & PML1_MASK)
#define PML1E_NPAGES      1ULL

#if MOS_PLATFORM_PAGING_LEVELS >= 2
define_pmlx(pml2);
#define pml2_index(vaddr) ((vaddr >> PML2_SHIFT) & PML2_MASK)
#define PML2E_NPAGES      (PML1_ENTRIES * PML1E_NPAGES)
#if MOS_CONFIG(PML2_HUGE_CAPABLE)
#define PML2_HUGE_MASK (PML1_MASK << PML1_SHIFT)
#endif
#else
new_named_opaque_type(pml1_t, next, pml2_t);
#endif

#if MOS_PLATFORM_PAGING_LEVELS >= 3
define_pmlx(pml3);
#define pml3_index(vaddr) ((vaddr >> PML3_SHIFT) & PML3_MASK)
#define PML3E_NPAGES      (PML2_ENTRIES * PML2E_NPAGES)
#if MOS_CONFIG(PML3_HUGE_CAPABLE)
#define PML3_HUGE_MASK (PML2_HUGE_MASK | (PML2_MASK << PML2_SHIFT))
#endif
#else
new_named_opaque_type(pml2_t, next, pml3_t);
typedef pml2e_t pml3e_t;
#endif

#if MOS_PLATFORM_PAGING_LEVELS >= 4
define_pmlx(pml4);
#define pml4_index(vaddr) ((vaddr >> PML4_SHIFT) & PML4_MASK)
#define PML4E_NPAGES      (PML3_ENTRIES * PML3E_NPAGES)
#if MOS_CONFIG(PML4_HUGE_CAPABLE)
#define PML4_HUGE_MASK (PML3_HUGE_MASK | (PML3_MASK << PML3_SHIFT))
#endif
#else
new_named_opaque_type(pml3_t, next, pml4_t);
typedef pml3e_t pml4e_t;
#endif

#if MOS_PLATFORM_PAGING_LEVELS >= 5
#error "TODO: more than 4 levels"
#else
new_named_opaque_type(pml4_t, next, pml5_t);
typedef pml4e_t pml5e_t;
#endif

typedef struct
{
    MOS_CONCAT(MOS_CONCAT(pml, MOS_MAX_PAGE_LEVEL), _t) max;
} pgd_t;

#define pgd_create(top) ((pgd_t){ .max = { .next = top } })

typedef struct
{
    bool readonly;
    void (*pml4e_pre_traverse)(pml4_t pml4, pml4e_t *e, ptr_t vaddr, void *data);
    void (*pml3e_pre_traverse)(pml3_t pml3, pml3e_t *e, ptr_t vaddr, void *data);
    void (*pml2e_pre_traverse)(pml2_t pml2, pml2e_t *e, ptr_t vaddr, void *data);
    void (*pml1e_callback)(pml1_t pml1, pml1e_t *e, ptr_t vaddr, void *data);
    void (*pml2e_post_traverse)(pml2_t pml2, pml2e_t *e, ptr_t vaddr, void *data);
    void (*pml3e_post_traverse)(pml3_t pml3, pml3e_t *e, ptr_t vaddr, void *data);
    void (*pml4e_post_traverse)(pml4_t pml4, pml4e_t *e, ptr_t vaddr, void *data);
} pagetable_walk_options_t;

#define pml_create_table(x) ((x##_t){ .table = (x##e_t *) phyframe_va(mm_get_free_page(MEM_PAGETABLE)) })
