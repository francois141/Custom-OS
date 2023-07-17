//
// Created by francois on 01.05.23.
//

#ifndef AOS2023_TEAM09_HAND_IN_BLOCKDRIVER_H
#define AOS2023_TEAM09_HAND_IN_BLOCKDRIVER_H

struct block_driver {
    struct capref sdhc_cap;
    lvaddr_t sdhc_vaddr;

    struct capref rw_frame;
    lvaddr_t write_vaddr;
    lvaddr_t read_vaddr;
    lpaddr_t write_paddr;
    lpaddr_t read_paddr;

    struct thread_mutex mm_mutex;

    struct sdhc_s *driver_structure;
};

errval_t read_block(struct block_driver *b_driver, int lba, void *block);
errval_t write_block(struct block_driver *b_driver, int lba, void *block);

errval_t benchmark_read(struct block_driver *b_driver, size_t number_runs);
errval_t benchmark_write(struct block_driver *b_driver, size_t number_runs);

void test_driver(struct block_driver *b_driver);
errval_t launch_driver(struct block_driver *b_driver);

#endif //AOS2023_TEAM09_HAND_IN_BLOCKDRIVER_H
