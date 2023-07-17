/**
 * \file
 * \brief PMAP Implementaiton for AOS
 */

/*
 * Copyright (c) 2019 ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef PAGING_TYPES_H_
#define PAGING_TYPES_H_ 1

#include <aos/solution.h>
#include <aos/rb_tree.h>

#include <pthread.h>

#define VADDR_OFFSET ((lvaddr_t)512UL*1024*1024*1024) // 1GB
#define VREGION_FLAGS_READ       0x01 // Reading allowed
#define VREGION_FLAGS_WRITE      0x02 // Writing allowed
#define VREGION_FLAGS_EXECUTE    0x04 // Execute allowed
#define VREGION_FLAGS_NOCACHE    0x08 // Caching disabled
#define VREGION_FLAGS_MPB        0x10 // Message passing buffer
#define VREGION_FLAGS_GUARD      0x20 // Guard page
#define VREGION_FLAGS_LARGE_PAGE 0x40 // Large page mapping
#define VREGION_FLAGS_MASK       0x7f // Mask of all individual VREGION_FLAGS

#define VREGION_FLAGS_READ_WRITE \
   (VREGION_FLAGS_READ | VREGION_FLAGS_WRITE)
#define VREGION_FLAGS_READ_EXECUTE \
   (VREGION_FLAGS_READ | VREGION_FLAGS_EXECUTE)
#define VREGION_FLAGS_READ_WRITE_NOCACHE \
   (VREGION_FLAGS_READ | VREGION_FLAGS_WRITE | VREGION_FLAGS_NOCACHE)
#define VREGION_FLAGS_READ_WRITE_MPB \
   (VREGION_FLAGS_READ | VREGION_FLAGS_WRITE | VREGION_FLAGS_MPB)

typedef int paging_flags_t;

extern bool mm_mutex_init;
extern struct thread_mutex mm_mutex;

struct page_table {
   enum objtype type;                            ///< type of the page table
   uint16_t index;                               ///< index in the parent page table (redundant?)
   struct capref page_table;                     ///< page table capability TODO rename? (cap?)
   struct capref mapping;                        ///< mapping capability (from parent to this) [NULL_CAP for L0]
   union page_table_entry {
       struct page_table *pt;                   ///< if type != L3
       struct capref frame_cap;                 ///< if type == L3 (mapping to the frame)
   } entries[VMSAv8_64_PTABLE_NUM_ENTRIES];     ///< entries within the page table
   // TODO we can potentially use this bit for type != L3
   /// "bit-array" indicating whether the corresponding index was allocated lazily (if type == L3)
   /// we need this as lazily allocated frames are treated slightly different (see try_map)
   int32_t lazy[(VMSAv8_64_PTABLE_NUM_ENTRIES + 32 - 1) / 32];
   uint16_t num_children;                       ///< counts the number of non-NULL children.
};

struct rb_tree;

/// struct to store the paging state of a process' virtual address space.
struct paging_state {
   /// slot allocator to be used for this paging state
   struct slot_allocator *slot_alloc;

   /// binary balanced tree containing the free and allocated virtual memory ranges managed by this structure
   struct rb_tree virtual_memory;

   /// slab allocator used for page_table items
   struct slab_allocator page_table_allocator;
   struct slab_allocator rb_node_allocator;

   /// "root-level" page table.
   struct page_table l0;

   /// initial buffer for the slab allocator
   /// we need to be able to allocate a page table for the first L1, L2, and L3 page tables.
   /// to be able to slab_grow we must extend the buffer by sizeof(struct slab_head), see slab.c:56
   char _page_table_buf[sizeof(struct slab_head) + 12 * sizeof(struct page_table)];
   /// TODO: set the initial minimum amount needed for this buffer
   char _rb_node_buf[sizeof(struct slab_head) + 12 * sizeof(struct rb_node)];

   bool _refill_slab_pt;  ///< True iff we are in the process of refilling page_table_allocator
   bool _refill_slab_rb;  ///< True iff we are in the process of refilling rb_node_allocator
};


#endif  /// PAGING_TYPES_H_
