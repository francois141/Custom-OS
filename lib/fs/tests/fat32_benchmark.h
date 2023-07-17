//
// Created by francois on 09.05.23.
//

#ifndef AOS2023_TEAM09_BENCHMARKS_H
#define AOS2023_TEAM09_BENCHMARKS_H

#define DEBUG_ON_BENCHMARK_LOCAL

#if defined(DEBUG_ON_BENCHMARK_LOCAL)
#define DEBUG_FILESYSTEM_BENCHMARK(x...) debug_printf(x)
#else
#define DEBUG_FILESYSTEM_BENCHMARK(x...) ((void)0)
#endif

#include <fs/fat32.h>
#include <aos/systime.h>

void _benchmark_fread(struct fat32_filesystem *fs, const char *path);
void _benchmark_fread(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_BENCHMARK("Benchmark start\n");

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    const unsigned int size = 4096;

    // Warmup
    void *data = calloc(size,1);
    size_t nb_bytes_read;
    assert(!fat32_read(fs, handle, data, size, &nb_bytes_read));
    assert(nb_bytes_read == size);
    free(data);

    for(size_t i = 512; i <= size; i *= 2) {
        data = calloc(size,1);
        assert(!fat32_seek(fs, handle,FS_SEEK_SET,0));
        uint64_t start = systime_now();
        assert(!fat32_read(fs, handle, data, i, &nb_bytes_read));
        uint64_t end = systime_now();
        assert(nb_bytes_read == i);
        debug_printf("BENCHMARK READ FILE SIZE %lu: %lu\n",i, systime_to_us(end - start));
        free(data);
    }

    DEBUG_FILESYSTEM_BENCHMARK("Benchmark end\n");
}

void _benchmark_fwrite(struct fat32_filesystem *fs, const char *path);
void _benchmark_fwrite(struct fat32_filesystem *fs, const char *path) {
    DEBUG_FILESYSTEM_BENCHMARK("Benchmark start\n");

    struct fat32_handle *handle;
    errval_t err = fat32_open(fs, path, &handle);
    assert(!err_is_fail(err));

    const unsigned int size = 4096;

    // Warmup
    void *data = calloc(size,1);
    size_t nb_bytes_written;
    assert(!fat32_write(fs, handle, data, size, &nb_bytes_written));
    assert(nb_bytes_written == size);
    free(data);

    // Benchmark
    for(size_t i = 512; i <= size; i *= 2) {
        data = calloc(size,1);
        assert(!fat32_seek(fs, handle,FS_SEEK_SET,0));
        uint64_t start = systime_now();
        assert(!fat32_write(fs, handle, data, i, &nb_bytes_written));
        uint64_t end = systime_now();
        assert(nb_bytes_written == i);
        debug_printf("BENCHMARK WRITE FILE SIZE %lu: %lu\n",i, systime_to_us(end - start));
        free(data);
    }

    DEBUG_FILESYSTEM_BENCHMARK("Benchmark end\n");
}


void run_benchmarks(struct fat32_filesystem *fs);
void run_benchmarks(struct fat32_filesystem *fs) {
    // File for the benchmark
    const char *test_file_path = "/SDCARD/TEST/HELLO.TXT";
    // Benchmarks
    _benchmark_fread(fs,test_file_path);
    _benchmark_fwrite(fs,test_file_path);
    return;
}


#endif //AOS2023_TEAM09_BENCHMARKS_H
