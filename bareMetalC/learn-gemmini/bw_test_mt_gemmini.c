#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_params.h"
#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "util.h"


#ifndef NUM_CORES
#warning `NUM_CORES` is not set explicitly. Default to 1.
#define NUM_CORES 1
#endif 


#define ADDR_SIZE 0x00100000U

#define SBUS_SPAD_ADDR_BASE 0xC0000000U
#define SBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define SBUS_SPAD_ADDR_CEIL (SBUS_SPAD_ADDR_BASE + SBUS_SPAD_ADDR_SIZE)

#define MBUS_SPAD_ADDR_BASE 0x08000000U
#define MBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define MBUS_SPAD_ADDR_CEIL (MBUS_SPAD_ADDR_BASE + MBUS_SPAD_ADDR_SIZE)

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static alignas(64) uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))
#define MEM_ADDR_CEIL (MEM_ADDR_BASE + MEM_ADDR_SIZE)

#define GEMMINI_WORD_BYTES sizeof(elem_t)
#define GEMMINI_MIN_TILE_BYTES (DIM * DIM * GEMMINI_WORD_BYTES)


// >>>> Configuration Region >>>>

// Whether to run a single test or multiple tests
// #define DO_MULTIPLE_TEST

// Whether to interleave `mvin` and `mvout`.
#define DO_INTERLEAVED_MVIN_MVOUT

// Whether to init buffers and check results
// #define DO_CHECK

// Select address region: memory/mbus/sbus
// #define ADDR_BASE MEM_ADDR_BASE
// #define ADDR_BASE MBUS_SPAD_ADDR_BASE
#define ADDR_BASE SBUS_SPAD_ADDR_BASE
static uint64_t buf_base = 0;

// Data bytes to move (Default spad capacity of gemmini is 256KB)
// static uint64_t bytes = 1024;
// static uint64_t bytes = 4 * 1024;
// static uint64_t bytes = 16 * 1024;
// static uint64_t bytes = 64 * 1024;
static uint64_t bytes = 256 * 1024;

// Iterations
static const uint64_t warmup_iterations = 1;
// static const uint64_t test_iterations = 1;
static const uint64_t test_iterations = 3;

// Round per iteration
static const uint64_t rounds_per_iter = 1;
// static const uint64_t rounds_per_iter = 5;

// <<<< Configuration Region <<<<


static inline void mvin(elem_t* mem_addr, uint64_t spad_addr, uint64_t bytes) {
    // A row of the matrix that are moved by one `mvin`/`mvout` should not
    // exceed the maximum bytes of one DMA burst (64 bytes by default). 
    // See `GemminiISA.scala` and `GemminiConfigs.scala`.

    uint64_t mem_addr_base = (uint64_t) mem_addr;
    uint64_t mem_addr_ceil = mem_addr_base + bytes;

    if (DIM * GEMMINI_WORD_BYTES <= MAX_BYTES) {
        uint64_t cols = MAX_BYTES / GEMMINI_WORD_BYTES;
        uint64_t stride = MAX_BYTES;

        // printf("config_ld(stride=%lu)\n", stride);
        gemmini_config_ld(stride);

        uint64_t maddr = mem_addr_base;
        uint64_t saddr = spad_addr;
        while (maddr < mem_addr_ceil) {
            uint64_t rows = 0;
            if ((maddr + MAX_BYTES * DIM) <= mem_addr_ceil) {
                rows = DIM;
            } else {
                assert((mem_addr_ceil - maddr) % MAX_BYTES == 0);
                rows = (mem_addr_ceil - maddr) / MAX_BYTES;
            }

            // printf("mvin(maddr=%lu, saddr=%lu, cols=%lu, rows=%lu)\n", 
            //         maddr, saddr, cols, rows);
            gemmini_extended_mvin(maddr, saddr, cols, rows);

            maddr += MAX_BYTES * rows;
            saddr += MAX_BYTES * rows / (DIM * GEMMINI_WORD_BYTES);
        }
        assert(maddr == mem_addr_ceil);
    } else {
        // TODO: Currently not support for bigger array.
        assert(false);
    }
}

static inline void mvout(elem_t* mem_addr, uint64_t spad_addr, uint64_t bytes) {
    // Should be exactly the same as `mvin` execpt for gemmini calls.

    uint64_t mem_addr_base = (uint64_t) mem_addr;
    uint64_t mem_addr_ceil = mem_addr_base + bytes;

    if (DIM * GEMMINI_WORD_BYTES <= MAX_BYTES) {
        uint64_t cols = MAX_BYTES / GEMMINI_WORD_BYTES;
        uint64_t stride = MAX_BYTES;
        // printf("config_st(stride=%lu)\n", stride);
        gemmini_config_st(stride);

        uint64_t maddr = mem_addr_base;
        uint64_t saddr = spad_addr;
        while (maddr < mem_addr_ceil) {
            uint64_t rows = 0;
            if ((maddr + MAX_BYTES * DIM) <= mem_addr_ceil) {
                rows = DIM;
            } else {
                assert((mem_addr_ceil - maddr) % MAX_BYTES == 0);
                rows = (mem_addr_ceil - maddr) / MAX_BYTES;
            }

            // printf("mvout(maddr=%lu, saddr=%lu, cols=%lu, rows=%lu)\n", 
            //         maddr, saddr, cols, rows);
            gemmini_extended_mvout(maddr, saddr, cols, rows);

            maddr += MAX_BYTES * rows;
            saddr += MAX_BYTES * rows / (DIM * GEMMINI_WORD_BYTES);
        }
        assert(maddr == mem_addr_ceil);
    } else {
        // TODO: Currently not support for bigger array.
        assert(false);
    }
}

static void interleaved_mvin_mvout(
    elem_t* mem_addr_mvin, 
    elem_t* mem_addr_mvout, 
    uint64_t spad_addr, 
    uint64_t bytes
) {
    uint64_t mem_addr_mvin_base = (uint64_t) mem_addr_mvin;
    uint64_t mem_addr_mvout_base = (uint64_t) mem_addr_mvout;
    uint64_t mem_addr_mvin_ceil = mem_addr_mvin_base + bytes;

    if (DIM * GEMMINI_WORD_BYTES <= MAX_BYTES) {
        uint64_t cols = MAX_BYTES / GEMMINI_WORD_BYTES;
        uint64_t stride = MAX_BYTES;

        // printf("config_ld(stride=%lu)\n", stride);
        gemmini_config_ld(stride);
        gemmini_config_st(stride);

        uint64_t maddr_mvin = mem_addr_mvin_base;
        uint64_t maddr_mvout = mem_addr_mvout_base;
        uint64_t saddr = spad_addr;
        while (maddr_mvin < mem_addr_mvin_ceil) {
            uint64_t rows = 0;
            if ((maddr_mvin + MAX_BYTES * DIM) <= mem_addr_mvin_ceil) {
                rows = DIM;
            } else {
                assert((mem_addr_mvin_ceil - maddr_mvin) % MAX_BYTES == 0);
                rows = (mem_addr_mvin_ceil - maddr_mvin) / MAX_BYTES;
            }

            gemmini_extended_mvin(maddr_mvin, saddr, cols, rows);
            gemmini_extended_mvout(maddr_mvout, saddr, cols, rows);

            maddr_mvin += MAX_BYTES * rows;
            maddr_mvout += MAX_BYTES * rows;
            saddr += MAX_BYTES * rows / (DIM * GEMMINI_WORD_BYTES);
        }
        assert(maddr_mvin == mem_addr_mvin_ceil);
    } else {
        // TODO: Currently not support for bigger array.
        assert(false);
    }
}


void mem_reset(elem_t* addr, uint64_t bytes) {
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) 0;
    }
}

void mem_init(elem_t* addr, uint64_t bytes, int cid) {
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) (((uint32_t) 0x5A5A5A5A) ^ (uint32_t) i ^ (uint32_t) cid);
    }
}

bool mem_cmp(elem_t* addr, elem_t* addr2, uint64_t bytes) {
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        if (addr[i] != addr2[i]) {
            printf("mem_cmp: %d!=%d at %u/%u\n", addr[i], addr2[i], i, size);
            return false;
        }
    }
    return true;
}


int test(int cid, int nc) {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

    assert(bytes <= ADDR_SIZE);
    assert((bytes % GEMMINI_MIN_TILE_BYTES) == 0);
    assert(bytes % nc == 0);
    uint64_t hart_bytes = bytes / nc;

    elem_t* buf_in = (elem_t*) (buf_base + hart_bytes * cid);
    elem_t* buf_out = (elem_t*) (buf_base + bytes + hart_bytes * cid);
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("hart %d: move %lu bytes from %lx to %lx\n", 
                    cid, hart_bytes, (uint64_t) buf_in, (uint64_t) buf_out);
        }
        barrier(nc);
    }

    gemmini_flush(0);

    uint64_t sum_bw_scaled = 0;
    uint64_t num_iters = warmup_iterations + test_iterations;
    for (uint64_t i = 0; i < num_iters; i++) {
        char warmup_sign[] = "(warmup)";
        if (i >= warmup_iterations) {
            warmup_sign[0] = '\0';
        }
        if (cid == 0) {
            printf("Iteration %d/%d %s\n", i + 1, num_iters, warmup_sign);
        }

#ifdef DO_CHECK
        if (cid == 0) {
            printf("\tmem_init ...\n");
        }
        mem_init(buf_in, hart_bytes, cid);
        if (cid == 0) {
            printf("\tmem_reset ...\n");
        }
        mem_reset(buf_out, hart_bytes);
#endif

        // if (cid == 0) {
        //     printf("\tmoving data ...\n");
        // }
        barrier(nc);
        uint64_t t_start = read_cycles();
        for (int round = 0; round < rounds_per_iter; round++) {
#ifdef DO_INTERLEAVED_MVIN_MVOUT
            interleaved_mvin_mvout(buf_in, buf_out, 0, hart_bytes);
#else
            mvin(buf_in, 0, hart_bytes);
            mvout(buf_out, 0, hart_bytes);
#endif
        }
        gemmini_fence();
        barrier(nc);
        uint64_t t_end = read_cycles();

        uint64_t cyc = t_end - t_start;
        uint64_t bw_scaled = 1000 * (bytes * 2 * rounds_per_iter) / cyc;

#ifdef DO_CHECK
        if (cid == 0) {
            printf("\tmem_cmp ...\n");
        }
        int eq = mem_cmp(buf_in, buf_out, hart_bytes);
        for (int i = 0; i < nc; i++) {
            if (i == cid) {
                printf("\thart %d: mem_cmp result: %d\n", cid, eq);
            }
            barrier(nc);
        }
#endif

        // if (cid == 0) {
        //     printf("\t%lu cycles\n", cyc);
        //     printf("\t%lu*0.001 bytes/cyc\n", bw_scaled);
        // }
        if (i >= warmup_iterations) {
            sum_bw_scaled += bw_scaled;
        }
    }

    if (test_iterations > 0) {
        uint64_t avg_bw_scaled = sum_bw_scaled / test_iterations;
        if (cid == 0) {
            printf("\n");
            printf("avg bandwidth: %lu*0.001 bytes/cyc\n", avg_bw_scaled);
        }
    }

    return 0;
}

int hart_main(int cid, int nc) {
#ifndef DO_MULTIPLE_TEST
    // Call single test.
    buf_base = ADDR_BASE;
    test(cid, nc);
#else
    // Call multiple tests with different params.
    uint64_t bytes_list[] = {1024, 4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024};
    // uint64_t addr_list[] = {MEM_ADDR_BASE, MBUS_SPAD_ADDR_BASE, SBUS_SPAD_ADDR_BASE};
    uint64_t addr_list[] = {MBUS_SPAD_ADDR_BASE, SBUS_SPAD_ADDR_BASE};
    for (int i = 0; i < (sizeof(bytes_list) / sizeof(uint64_t)); i++) {
        bytes = bytes_list[i];
        for (int j = 0; j < (sizeof(addr_list) / sizeof(uint64_t)); j++) {
            buf_base = addr_list[j];
            if (cid == 0) {
                printf("\n(%d, %d), bytes=%lu, addr=%lx\n", 
                    i, j, bytes, buf_base);
            }
            test(cid, nc);
        }
    }
#endif
}

void thread_entry(int cid, int nc) {
    // For multi-threaded program, start from this function instead of `main`.

    // Call custom function.
    hart_main(cid, nc);
    // Decide on returncode.
    int ret = 0;
    
    // Let one thread call `exit`.
    if (cid != 0) { while (1) {} }
    exit(ret);
}

int main() {
    return 1;
}

