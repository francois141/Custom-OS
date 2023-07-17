#include "tests.h"

#include <stdbool.h>

#include <mm/mm.h>
#include <aos/paging.h>
#include <proc_mgmt.h>

#include "errors/errno.h"
#include "mem_alloc.h"

#define TEST_PAGES       10
#define TEST_ALLOC_COUNT ((TEST_PAGES * BASE_PAGE_SIZE) / sizeof(struct capref))
#define ITERATIONS       10
#define ALLOC_SIZE       16384
#define ALLOC_ALIGN      8192

#define CONCURRENT_PAGING_TEST_THREADS 5
#define CONCURRENT_PAGING_TEST_SIZE    (1 << 10)

#define FAIL_ON_ERR(x)                                                                             \
    err = (x);                                                                                     \
    if (err_is_fail(err)) {                                                                        \
        return err;                                                                                \
    }

#define ASSERT_ERR(x)                                                                              \
    if (!(x)) {                                                                                    \
        return err_push(err, SYS_ERR_GUARD_MISMATCH);                                              \
    }

#define EXPECT_ERR(x)                                                                              \
    err = (x);                                                                                     \
    if (err_is_fail(err)) {                                                                        \
        err = SYS_ERR_OK;                                                                          \
    } else {                                                                                       \
        return SYS_ERR_GUARD_MISMATCH;                                                             \
    }


TEST_SUITE_DEFINE_FN(ram_alloc)
{
    (void)verbose;
    errval_t err = SYS_ERR_OK;
    if (quick) {
        return SYS_ERR_OK;
    }

    struct capref framecap;
    err = frame_alloc(&framecap, 3 * TEST_PAGES * BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "test frame alloc");
        return err;
    }

    void *buf;
    err = paging_map_frame(get_current_paging_state(), &buf, 3 * TEST_PAGES * BASE_PAGE_SIZE,
                           framecap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "test frame map");
        return err;
    }
    struct capref *caps        = (struct capref *)buf;
    struct capref *split_caps  = caps + TEST_ALLOC_COUNT;
    struct capref *split_caps2 = split_caps + TEST_ALLOC_COUNT;

    struct slot_allocator *ca = get_default_slot_allocator();
    for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
        ca->alloc(ca, &split_caps[i]);
        ca->alloc(ca, &split_caps2[i]);
    }

    debug_printf("testing allocator with 10 cycles of %lu fragmented allocations\n",
                 TEST_ALLOC_COUNT);

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
            err = aos_ram_alloc_aligned(&caps[i], ALLOC_SIZE, ALLOC_ALIGN);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "alloc testing");
                return err;
            }
        }

        for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
            struct capability c;
            err = cap_direct_identify(caps[i], &c);
            if (c.u.ram.bytes < ALLOC_SIZE || c.u.ram.base % ALLOC_ALIGN != 0) {
                debug_printf("%ld, 0x%lx\n", c.u.ram.bytes, c.u.ram.base);
                err = MM_ERR_CAP_INVALID;
            }
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "alloc testing");
                return err;
            }
        }

        for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
            err = cap_retype(split_caps[i], caps[i], 0, ObjType_RAM, ALLOC_SIZE / 2);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "alloc testing retype");
                return err;
            }
            err = cap_retype(split_caps2[i], caps[i], ALLOC_SIZE / 2, ObjType_RAM, ALLOC_SIZE / 2);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "alloc testing retype");
                return err;
            }
            err = cap_delete(caps[i]);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "alloc testing delete");
                return err;
            }
        }

        for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
            err = aos_ram_free(split_caps[i]);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "free testing split");
                return err;
            }
        }
        for (size_t i = 0; i < TEST_ALLOC_COUNT; ++i) {
            err = aos_ram_free(split_caps2[i]);
            if (err_is_fail(err)) {
                debug_printf("error index: %lu\n", i);
                DEBUG_ERR(err, "free testing split");
                return err;
            }
        }
    }

    debug_printf("Completed test_ram_alloc.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(frame_alloc)
{
    (void)verbose;
    errval_t err        = SYS_ERR_OK;
    size_t   alloc_size = 4096 * 4096;
    if (quick) {
        alloc_size = 1024 * 4096;
    }

    struct capref frame;
    FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));

    struct paging_state *st = get_current_paging_state();
    void                *buf;
    FAIL_ON_ERR(
        paging_map_frame_attr_offset(st, &buf, alloc_size, frame, 0, VREGION_FLAGS_READ_WRITE));

    char *data = (char *)buf;
    printf("buf: %llx\n", buf);
    for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
        *(data + i) = 'a' + ((i / 200) % 26);
    }
    printf("Reading: ");
    for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
        printf("%c", *(data + i));
    }
    printf("\n");

    FAIL_ON_ERR(paging_unmap(st, buf));
    aos_ram_free(frame);
    cap_destroy(frame);

    printf("Completed test_frame_alloc.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(malloc)
{
    (void)quick;
    (void)verbose;

    int *base_memory = (int *)malloc(0xffffffff);

    int *addr1  = base_memory + 0x0;
    int *addr2  = base_memory + 0xf;
    int *addr3  = base_memory + 0xff;
    int *addr4  = base_memory + 0xfff;
    int *addr5  = base_memory + 0xffff;
    int *addr6  = base_memory + 0xfffff;
    int *addr7  = base_memory + 0xffffff;
    int *addr8  = base_memory + 0xfffffff;

    *(addr1)  = 1;
    *(addr2)  = 2;
    *(addr3)  = 3;
    *(addr4)  = 4;
    *(addr5)  = 5;
    *(addr6)  = 6;
    *(addr7)  = 7;
    *(addr8)  = 8;

    if (*addr1 != 1)
        goto fail;
    if (*addr2 != 2)
        goto fail;
    if (*addr3 != 3)
        goto fail;
    if (*addr4 != 4)
        goto fail;
    if (*addr5 != 5)
        goto fail;
    if (*addr6 != 6)
        goto fail;
    if (*addr7 != 7)
        goto fail;
    if (*addr8 != 8)
        goto fail;

    // Test also write | It should not crash
    for (int i = 0; i < 10000; i++) {
        *(base_memory + i) = 0xbeea;
    }

    printf("Completed test_malloc.\n");
    free(base_memory);
    return SYS_ERR_OK;
fail:
    free(base_memory);
    return LIB_ERR_VSPACE_VREGION_NOT_FOUND;
}

TEST_SUITE_DEFINE_FN(stress_malloc)
{
    (void)verbose;
    int nb_rounds = 100;

    if (quick) {
        nb_rounds = 2;
    }

    for (int i = 0; i < nb_rounds; i++) {
        debug_printf("round %d\n", i);
        int *base_memory = (int *)malloc(0xffffffff);

        int *addr1 = base_memory + 0x0;
        int *addr2 = base_memory + 0xf;
        int *addr3 = base_memory + 0xff;
        int *addr4 = base_memory + 0xfff;

        debug_printf("%p %p %p %p\n", addr1, addr2, addr3, addr4);

        *(addr1) = 1;
        *(addr2) = 2;
        *(addr3) = 3;
        *(addr4) = 4;

        if (*addr1 != 1 || *addr2 != 2 || *addr3 != 3 || *addr4 != 4) {
            free(base_memory);
            return LIB_ERR_VSPACE_VREGION_NOT_FOUND;
        }
        free(base_memory);
    }

    printf("Completed test_malloc_stress.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(frame_page_fault_handler)
{
    (void)verbose;
    errval_t err = SYS_ERR_OK;

    size_t size = 1 << 20;
    if (quick) {
        size = 1 << 10;
    }

    struct paging_state *st = get_current_paging_state();
    void                *buf;
    FAIL_ON_ERR(paging_alloc(st, &buf, size, BASE_PAGE_SIZE));

    char *data   = (char *)buf;
    int   stride = 2000;
    printf("buf: %llx\n", buf);
    for (size_t i = 0; i < size; i += stride) {
        *(data + i) = 'a' + ((i / stride) % 26);
    }

    printf("Reading: ");
    for (size_t i = 0; i < size; i += stride) {
        printf("%c", *(data + i));
    }
    printf("\n");
    FAIL_ON_ERR(paging_unmap(st, buf));
    printf("Completed test_frame_page_fault_handler\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(frame_page_fault_handler_no_write)
{
    (void)verbose;
    errval_t err = SYS_ERR_OK;

    size_t size = 1 << 20;
    if (quick) {
        size = 1 << 10;
    }

    struct paging_state *st = get_current_paging_state();
    void                *buf;
    FAIL_ON_ERR(paging_alloc(st, &buf, size, BASE_PAGE_SIZE));
    FAIL_ON_ERR(paging_unmap(st, buf));

    printf("Completed test_frame_page_fault_handler_no_write\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(frame_map_huge_frame)
{
    (void)verbose;
    errval_t err = SYS_ERR_OK;

    size_t huge_alloc_size = 8 * 4096 * 4096;
    if (quick) {
        huge_alloc_size = 2 * 4096 * 4096;
    }

    struct capref frame;
    FAIL_ON_ERR(frame_alloc(&frame, huge_alloc_size, NULL));

    struct paging_state *st = get_current_paging_state();
    void                *buf;
    FAIL_ON_ERR(paging_map_frame_attr_offset(st, &buf, huge_alloc_size, frame, 0,
                                             VREGION_FLAGS_READ_WRITE));

    char *data = (char *)buf;
    printf("buf: %llx\n", buf);
    int stride = 200000;

    printf("Writing to the huge page. \n");
    for (size_t i = 0; i < huge_alloc_size; i += (huge_alloc_size / stride)) {
        *(data + i) = 'a' + ((i / 200) % 26);
    }
    printf("Wrote with success on the huge page table.\n");

    FAIL_ON_ERR(paging_unmap(st, buf));
    aos_ram_free(frame);
    cap_destroy(frame);

    printf("Completed test_frame_map_huge_frame.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_frame_alloc)
{
    (void)verbose;
    errval_t err              = SYS_ERR_OK;
    int      nb_stress_rounds = 500;
    int      nb_pages_to_map  = 1024;
    int      rounds_verbose   = 100;
    if (quick) {
        nb_stress_rounds = 100;
        nb_pages_to_map  = 16;
        rounds_verbose   = 10;
    }

    for (int round = 0; round < nb_stress_rounds; round++) {
        if (round % rounds_verbose == 0) {
            debug_printf("Stress test round : %d / %d\n", round, nb_stress_rounds);
        }
        const size_t  alloc_size = nb_pages_to_map * 4096;
        struct capref frame;
        FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));  // Extend to more than a single L3

        struct paging_state *st = get_current_paging_state();
        void                *buf;
        FAIL_ON_ERR(
            paging_map_frame_attr_offset(st, &buf, alloc_size, frame, 0, VREGION_FLAGS_READ_WRITE));

        char *data = (char *)buf;
        for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
            *(data + i) = 'a' + ((i / 200) % 26);
        }

        FAIL_ON_ERR(paging_unmap(st, buf));
        aos_ram_free(frame);
        cap_destroy(frame);
    }

    printf("Completed test_stress_frame_alloc.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_frame_alloc_arbitrary_sizes)
{
    (void)verbose;
    errval_t err              = SYS_ERR_OK;
    int      nb_stress_rounds = 4096;
    int      rounds_verbose   = 100;
    if (quick) {
        nb_stress_rounds = 100;
        rounds_verbose   = 10;
    }

    for (int round = 0; round < nb_stress_rounds; round++) {
        if (round % rounds_verbose == 0) {
            debug_printf("Stress test round arbitrary_sizes: %d / %d\n", round, nb_stress_rounds);
        }
        const size_t  alloc_size = (round + 1) * 4096;
        struct capref frame;
        FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));  // Extend to more than a single L3

        struct paging_state *st = get_current_paging_state();
        void                *buf;
        FAIL_ON_ERR(
            paging_map_frame_attr_offset(st, &buf, alloc_size, frame, 0, VREGION_FLAGS_READ_WRITE));

        char *data = (char *)buf;
        for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
            *(data + i) = 'a' + ((i / 200) % 26);
        }

        FAIL_ON_ERR(paging_unmap(st, buf));
        aos_ram_free(frame);
        cap_destroy(frame);
    }

    printf("Completed test_stress_frame_alloc_arbitrary_sizes.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_frame_alloc_arbitrary_sizes_cyclic)
{
    (void)verbose;
    errval_t err              = SYS_ERR_OK;
    int      nb_stress_rounds = 20000;
    int      rounds_verbose   = 100;
    if (quick) {
        nb_stress_rounds = 100;
        rounds_verbose   = 10;
    }

    for (int round = 0; round < nb_stress_rounds; round++) {
        if (round % rounds_verbose == 0) {
            debug_printf("Stress test round arbitrary_sizes cyclic: %d / %d\n", round,
                         nb_stress_rounds);
        }
        const size_t  alloc_size = ((round % 89) + 1) * 4096;
        struct capref frame;
        FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));  // Extend to more than a single L3

        struct paging_state *st = get_current_paging_state();
        void                *buf;
        FAIL_ON_ERR(
            paging_map_frame_attr_offset(st, &buf, alloc_size, frame, 0, VREGION_FLAGS_READ_WRITE));

        char *data = (char *)buf;
        for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
            *(data + i) = 'a' + ((i / 200) % 26);
        }

        FAIL_ON_ERR(paging_unmap(st, buf));
        aos_ram_free(frame);
        cap_destroy(frame);
    }

    printf("Completed test_stress_frame_alloc_arbitrary_sizes_cyclic.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_frame_alloc_small_alloc_sizes)
{
    (void)verbose;
    errval_t err              = SYS_ERR_OK;
    int      nb_stress_rounds = 50000;
    int      nb_pages_to_map  = 48;
    int      rounds_verbose   = 500;
    if (quick) {
        nb_stress_rounds = 100;
        nb_pages_to_map  = 32;
        rounds_verbose   = 10;
    }

    for (int round = 0; round < nb_stress_rounds; round++) {
        if (round % rounds_verbose == 0) {
            debug_printf("Stress test round : %d / %d\n", round, nb_stress_rounds);
        }
        const size_t  alloc_size = nb_pages_to_map * 4096;
        struct capref frame;
        FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));  // Extend to more than a single L3

        struct paging_state *st = get_current_paging_state();
        void                *buf;
        FAIL_ON_ERR(
            paging_map_frame_attr_offset(st, &buf, alloc_size, frame, 0, VREGION_FLAGS_READ_WRITE));

        char *data   = (char *)buf;
        int   stride = 2048;
        for (size_t i = 0; i < alloc_size; i += (alloc_size / stride)) {
            *(data + i) = 'a' + ((i / 200) % 26);
        }

        FAIL_ON_ERR(paging_unmap(st, buf));
        aos_ram_free(frame);
        cap_destroy(frame);
    }

    printf("Completed test_stress_frame_alloc_small_alloc_sizes.\n");
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_frame_alloc_with_pagefault_handler)
{
    (void)verbose;
    errval_t err              = SYS_ERR_OK;
    int      nb_stress_rounds = 500;
    int      nb_pages_to_map  = 1024;
    int      rounds_verbose   = 100;
    if (quick) {
        nb_stress_rounds = 100;
        nb_pages_to_map  = 32;
        rounds_verbose   = 10;
    }


    for (int round = 0; round < nb_stress_rounds; round++) {
        if (round % rounds_verbose == 0) {
            debug_printf("Stress test round : %d / %d\n", round, nb_stress_rounds);
        }
        const size_t  alloc_size = nb_pages_to_map * 4096;
        struct capref frame;
        FAIL_ON_ERR(frame_alloc(&frame, alloc_size, NULL));  // Extend to more than a single L3

        struct paging_state *st = get_current_paging_state();
        void                *buf;
        FAIL_ON_ERR(paging_alloc(st, &buf, alloc_size, BASE_PAGE_SIZE));

        char *data = (char *)buf;
        for (size_t i = 0; i < alloc_size; i += (alloc_size / 40)) {
            *(data + i) = 'a' + ((i / 200) % 26);
        }

        FAIL_ON_ERR(paging_unmap(st, buf));
        aos_ram_free(frame);
        cap_destroy(frame);
    }

    printf("Completed test_stress_frame_alloc_with_pagefault_handler.\n");
    return SYS_ERR_OK;
}

static int _test_concurrent_paging_thread(void *data)
{
    for (size_t i = 0; i < CONCURRENT_PAGING_TEST_SIZE; i += 1000) {
        volatile char *ptr = (char *)data + i;
        *ptr               = 'a';
        *ptr;
    }
    return 0;
}

TEST_SUITE_DEFINE_FN(concurrent_paging)
{
    (void)quick;
    (void)verbose;
    errval_t       err = SYS_ERR_OK;
    struct thread *threads[CONCURRENT_PAGING_TEST_THREADS];

    void *data = NULL;
    FAIL_ON_ERR(paging_alloc(get_current_paging_state(), &data, CONCURRENT_PAGING_TEST_SIZE,
                             BASE_PAGE_SIZE));
    debug_printf("test_concurrent_paging: allocated %d bytes at %p", CONCURRENT_PAGING_TEST_SIZE,
                 data);

    for (int i = 0; i < CONCURRENT_PAGING_TEST_THREADS; i++) {
        threads[i] = thread_create(_test_concurrent_paging_thread, data);
        assert(threads[i] != NULL);
    }

    for (int i = 0; i < CONCURRENT_PAGING_TEST_THREADS; i++) {
        int ret = 0;
        FAIL_ON_ERR(thread_join(threads[i], &ret));
        if (ret != 0) {
            return LIB_ERR_THREAD_JOIN;
        }
    }

    printf("Completed test_concurrent_paging.\n");
    return err;
}

static inline errval_t _test_assert_ps_len(size_t num_expected, struct proc_status **ps, size_t *num)
{
    errval_t err = SYS_ERR_OK;

    FAIL_ON_ERR(proc_mgmt_ps(ps, num));

    if (*num < num_expected) {
        free(*ps);
        return err_push(err, SYS_ERR_INVALID_SIZE);
    }

    return SYS_ERR_OK;
}

static inline errval_t _test_get_ps_index(struct proc_status *ps, size_t num, domainid_t pid,
                                          size_t *index)
{
    for (size_t i = 0; i < num; ++i) {
        if (ps[i].pid == pid) {
            *index = i;
            return SYS_ERR_OK;
        }
    }
    return SYS_ERR_GUARD_MISMATCH;
}

TEST_SUITE_DEFINE_FN(proc_spawn)
{
    (void)quick;
    (void)verbose;
#define PROC_NAME_LEN 16
    errval_t err = SYS_ERR_OK;

    domainid_t pid;
    FAIL_ON_ERR(proc_mgmt_spawn_program("hello", /*core*/ 0, &pid));

    char proc_name[PROC_NAME_LEN + 1];
    FAIL_ON_ERR(proc_mgmt_get_name(pid, proc_name, PROC_NAME_LEN));
    printf("name: %s\n", proc_name);

    struct proc_status *ps;
    size_t              num;
    FAIL_ON_ERR(_test_assert_ps_len(/*num_expected*/ 1, &ps, &num));
    size_t index;
    FAIL_ON_ERR(_test_get_ps_index(ps, num, pid, &index))
    ASSERT_ERR(ps[index].pid == pid);
    ASSERT_ERR(ps[index].state == PROC_STATE_RUNNING);
    free(ps);

    thread_yield();  // let the other process run for a bit
    debug_printf("suspending process with pid=%d\n", pid);
    FAIL_ON_ERR(proc_mgmt_suspend(pid));

    FAIL_ON_ERR(_test_assert_ps_len(/*num_expected*/ 1, &ps, &num));
    FAIL_ON_ERR(_test_get_ps_index(ps, num, pid, &index))
    ASSERT_ERR(ps[index].pid == pid);
    ASSERT_ERR(ps[index].state == PROC_STATE_PAUSED);
    free(ps);

    debug_printf("attempting to resume process with pid=%d\n", pid);
    FAIL_ON_ERR(proc_mgmt_resume(pid));
    thread_yield();

    FAIL_ON_ERR(_test_assert_ps_len(/*num_expected*/ 1, &ps, &num));
    FAIL_ON_ERR(_test_get_ps_index(ps, num, pid, &index))
    ASSERT_ERR(ps[index].pid == pid);
    ASSERT_ERR(ps[index].state == PROC_STATE_RUNNING);
    free(ps);

    thread_yield();
    debug_printf("killing process with pid=%d\n", pid);
    FAIL_ON_ERR(proc_mgmt_kill(pid));

    FAIL_ON_ERR(_test_assert_ps_len(/*num_expected*/ 0, &ps, &num));
    EXPECT_ERR(_test_get_ps_index(ps, num, pid, &index));
    return SYS_ERR_OK;
}

TEST_SUITE_DEFINE_FN(stress_proc_mgmt)
{
    (void)quick;
    (void)verbose;
    // TODO try to find a way to check correctness of the output programmatically...
    errval_t err = SYS_ERR_OK;

    const size_t num_procs_to_spawn = 25;
    const size_t num_yields         = 5;

    size_t nb_cycles = 3;
    for(size_t cycle = 0; cycle < nb_cycles;cycle++) {
        domainid_t pids[num_procs_to_spawn];

        // spawn a bunch of procs
        for (size_t i = 0; i < num_procs_to_spawn; ++i) {
            FAIL_ON_ERR(proc_mgmt_spawn_program("hello", /*core*/ 0, &pids[i]));
            debug_printf("Pid : %p\n", pids[i]);
        }

        struct proc_status *ps;
        size_t              num;
        FAIL_ON_ERR(_test_assert_ps_len(num_procs_to_spawn, &ps, &num));
        for (size_t i = 0; i < num_procs_to_spawn; ++i) {
            size_t index;
            FAIL_ON_ERR(_test_get_ps_index(ps, num, pids[i], &index))
            ASSERT_ERR(ps[index].state == PROC_STATE_RUNNING);
        }
        free(ps);

        // let the spawned procs run for a bit
        for (size_t i = 0; i < num_yields; ++i) {
            thread_yield();
        }

        // kill them all again
        FAIL_ON_ERR(proc_mgmt_killall("hello"));
        FAIL_ON_ERR(_test_assert_ps_len(/*num_expected*/ 0, &ps, &num));
        for (size_t i = 0; i < num_procs_to_spawn; ++i) {
            size_t index;
            EXPECT_ERR(_test_get_ps_index(ps, num, pids[i], &index))
        }
    }

    return SYS_ERR_OK;
}

#define TEST_SUITE_CALL(TEST)                                                                      \
    if (TEST_SUITE_CONFIG_IS_TEST_ENABLED(config, TEST)) {                                         \
        err = test_##TEST(config.quick, config.verbose);                                           \
        DEBUG_ERR(err, #TEST);                                                                     \
        if (err_is_fail(err) && !config.continue_on_err) {                                         \
            return err;                                                                            \
        }                                                                                          \
    }

errval_t test_suite_run(struct test_suite_config config)
{
    errval_t err = SYS_ERR_OK;
    TEST_SUITE_FOREACH(TEST_SUITE_CALL);
    return SYS_ERR_OK;
}
#undef TEST_SUITE_CALL