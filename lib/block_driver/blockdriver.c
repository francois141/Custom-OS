//
// Created by francois on 01.05.23.
//


/**
* \file
* \brief Hello world application
 */

/*
* Copyright (c) 2016 ETH Zurich.
* Copyright (c) 2022 The University of British Columbia.
* All rights reserved.
*
* This file is distributed under the terms in the attached LICENSE file.
* If you do not find this file, copies can be found by writing to:
* ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
* Attn: Systems Group.
 */

#include <stdlib.h>
#include <stdio.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/cache.h>
#include <grading/grading.h>
#include <grading/io.h>
#include <aos/systime.h>

#include <drivers/sdhc.h>
#include <maps/imx8x_map.h>

#include "../../include/block_driver/blockdriver.h"

// #define DEBUG_ON_BLOCKDRIVER

#if defined(DEBUG_ON_BLOCKDRIVER)
#define DEBUG_BLOCKDRIVER(x...) debug_printf(x)
#else
#define DEBUG_BLOCKDRIVER(x...) ((void)0)
#endif

errval_t _map_cap_to_driver(struct block_driver *b_driver);
errval_t _init_block_driver(struct block_driver *b_driver);

errval_t _read_block(struct block_driver *b_driver, int lba, void *block);
errval_t _write_block(struct block_driver *b_driver, int lba, void *block);

errval_t _map_cap_to_driver(struct block_driver *b_driver) {
    errval_t err = SYS_ERR_OK;

    err = slot_alloc(&b_driver->sdhc_cap);
    if(err_is_fail(err)) {
        return err;
    }

    struct capability cap;
    err = cap_direct_identify(cap_devices, &cap);
    if(err_is_fail(err)) {
        return err;
    }

    struct capref new_cap;
    err = slot_alloc(&new_cap);

    err = cap_retype(b_driver->sdhc_cap, cap_devices,IMX8X_SDHC2_BASE - IMX8X_START_DEV_RANGE,ObjType_DevFrame,ROUND_PAGE_UP(IMX8X_SDHC_SIZE));
    if(err_is_fail(err)) {
        return err;
    }

    err = paging_map_frame_attr_offset(get_current_paging_state(),(void**)&b_driver->sdhc_vaddr, ROUND_PAGE_UP(IMX8X_SDHC_SIZE), b_driver->sdhc_cap, 0, VREGION_FLAGS_READ_WRITE_NOCACHE);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

errval_t _init_block_driver(struct block_driver *b_driver) {
    errval_t err = SYS_ERR_OK;

    err = _map_cap_to_driver(b_driver);
    if(err_is_fail(err)) {
        return err;
    }

    err = sdhc_init(&b_driver->driver_structure, (void *) b_driver->sdhc_vaddr);
    if(err_is_fail(err)) {
        return err;
    }

    DEBUG_BLOCKDRIVER("Initialized driver\n");

    size_t size;
    err = frame_alloc(&b_driver->rw_frame, BASE_PAGE_SIZE, &size);
    if(err_is_fail(err)) {
        return err;
    }

    assert(size >= BASE_PAGE_SIZE);

    err = paging_map_frame_attr_offset(get_current_paging_state(), (void**)&b_driver->read_vaddr, size, b_driver->rw_frame, 0, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        return err;
    }

    struct capability rw_frame_identity;
    err = cap_direct_identify( b_driver->rw_frame, &rw_frame_identity);
    if (err_is_fail(err)) {
        return err;
    }

    b_driver->read_paddr = get_address(&rw_frame_identity);
    b_driver->write_paddr = get_address(&rw_frame_identity) + SDHC_BLOCK_SIZE;
    b_driver->write_vaddr = b_driver->read_vaddr + SDHC_BLOCK_SIZE;

    DEBUG_BLOCKDRIVER("RW frame for the driver is up\n");

    thread_mutex_init(&mm_mutex);

    DEBUG_BLOCKDRIVER("Mutex is ready \n");

    return SYS_ERR_OK;
}

errval_t _read_block(struct block_driver *b_driver, int lba, void *block) {
    errval_t err = SYS_ERR_OK;
    // Step 1) Flush the cache in the read range
    arm64_dcache_wbinv_range(b_driver->read_vaddr, SDHC_BLOCK_SIZE);
    // Step 2) Read from the block driver
    err = sdhc_read_block(b_driver->driver_structure, lba, b_driver->read_paddr, b_driver->read_vaddr);
    if(err_is_fail(err)) {
        return err;
    }
    // Step 3) Copy the data into block
    memcpy(block, (void*)b_driver->read_vaddr, SDHC_BLOCK_SIZE);
    return SYS_ERR_OK;
}

errval_t read_block(struct block_driver *b_driver, int lba, void *block) {
    thread_mutex_lock(&b_driver->mm_mutex);
    errval_t err = _read_block(b_driver, lba, block);
    thread_mutex_unlock(&b_driver->mm_mutex);
    return err;
}

errval_t _write_block(struct block_driver *b_driver, int lba, void *block) {
    errval_t err = SYS_ERR_OK;
    memcpy((void*)b_driver->write_vaddr, block, SDHC_BLOCK_SIZE);
    // Step 1) Flush the cache in the write range
    arm64_dcache_wb_range(b_driver->write_vaddr, SDHC_BLOCK_SIZE);
    // Step 2) Write to the block driver
    err = sdhc_write_block(b_driver->driver_structure, lba, b_driver->write_paddr);
    if(err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

errval_t write_block(struct block_driver *b_driver, int lba, void *block) {
    thread_mutex_lock(&b_driver->mm_mutex);
    errval_t err = _write_block(b_driver, lba, block);
    thread_mutex_unlock(&b_driver->mm_mutex);
    return err;
}

errval_t benchmark_read(struct block_driver *b_driver, size_t number_runs) {
    void *bloc = malloc(512);
    assert(!read_block(b_driver, 50, bloc));
    for(size_t lba = 50; lba < number_runs + 50;lba++) {
        uint64_t start = systime_now();
        assert(!read_block(b_driver, 50, bloc));
        uint64_t end = systime_now();
        debug_printf("BENCHMARK OUTPUT READ: %lu\n", systime_to_us(end - start));
    }
    return SYS_ERR_OK;
}

errval_t benchmark_write(struct block_driver *b_driver, size_t number_runs) {
    void *bloc = malloc(512);
    for(size_t lba = 50; lba < number_runs + 50;lba++) {
        uint64_t start = systime_now();
        assert(!write_block(b_driver, lba, bloc));
        uint64_t end = systime_now();
        debug_printf("BENCHMARK OUTPUT WRITE: %lu\n", systime_to_us(end - start));
    }
    return SYS_ERR_OK;
}

void test_driver(struct block_driver *b_driver) {

    char *block = (char*)malloc(512);
    char *block2 = (char*)malloc(512);

    for(int k = 0; k < 1;k++) {
        for(int i = 2; i <= 30;i++) {
            debug_printf("Write read block : %d\n", i);
            for(int j = 0; j < 512;j++) {
                block[j] = (j + i) % 199;
            }
            assert(err_is_ok(write_block(b_driver, i, block)));
            assert(err_is_ok(read_block(b_driver, i, block2)));
            for(int j = 0; j < 512;j++) {
                assert(block[j] == block2[j]);
            }
        }
    }
}

errval_t launch_driver(struct block_driver *b_driver)
{
    errval_t err = SYS_ERR_OK;

    DEBUG_BLOCKDRIVER("Hello from blockserver! \n");

    err = _init_block_driver(b_driver);
    if(err_is_fail(err)) {
        return err;
    }

    DEBUG_BLOCKDRIVER("Initialization of the driver done\n");

    DEBUG_BLOCKDRIVER("Reading for block of the disk\n");

    uint8_t *block = malloc(512);
    err = read_block(b_driver,0, (void*)block);
    if(err_is_fail(err)) {
        return err;
    }

    assert(block[510] == 0x55);
    assert(block[511] == 0xAA);

    DEBUG_BLOCKDRIVER("First block read with success\n");

    return SYS_ERR_OK;
}
