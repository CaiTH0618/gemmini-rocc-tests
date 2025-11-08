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


#define ADDR_SIZE 0x00100000U
// #define ADDR_SIZE 0x00010000U

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


#define AOT_GEMMINI_INSTRUCTION_GENERATION
// #define DO_CHECK

#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
#define GEMMINI_MOVE_ARG_LIST_MAX_SIZE 1024
static uint64_t gemmini_config_ld_stride = 0;
static uint64_t gemmini_mvin_arg_num = 0;
static uint64_t gemmini_mvin_arg_list[GEMMINI_MOVE_ARG_LIST_MAX_SIZE][4];
static uint64_t gemmini_config_st_stride = 0;
static uint64_t gemmini_mvout_arg_num = 0;
static uint64_t gemmini_mvout_arg_list[GEMMINI_MOVE_ARG_LIST_MAX_SIZE][4];
#endif

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
#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
        gemmini_config_ld_stride = stride;
#else
        gemmini_config_ld(stride);
#endif

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
#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
            uint64_t idx = gemmini_mvin_arg_num;
            assert(idx < GEMMINI_MOVE_ARG_LIST_MAX_SIZE);
            gemmini_mvin_arg_list[idx][0] = maddr;
            gemmini_mvin_arg_list[idx][1] = saddr;
            gemmini_mvin_arg_list[idx][2] = cols;
            gemmini_mvin_arg_list[idx][3] = rows;
            gemmini_mvin_arg_num++;
#else
            gemmini_extended_mvin(maddr, saddr, cols, rows);
#endif

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
#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
        gemmini_config_st_stride = stride;
#else
        gemmini_config_st(stride);
#endif

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
#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
            uint64_t idx = gemmini_mvout_arg_num;
            assert(idx < GEMMINI_MOVE_ARG_LIST_MAX_SIZE);
            gemmini_mvout_arg_list[idx][0] = maddr;
            gemmini_mvout_arg_list[idx][1] = saddr;
            gemmini_mvout_arg_list[idx][2] = cols;
            gemmini_mvout_arg_list[idx][3] = rows;
            gemmini_mvout_arg_num++;
#else
            gemmini_extended_mvout(maddr, saddr, cols, rows);
#endif

            maddr += MAX_BYTES * rows;
            saddr += MAX_BYTES * rows / (DIM * GEMMINI_WORD_BYTES);
        }
        assert(maddr == mem_addr_ceil);
    } else {
        // TODO: Currently not support for bigger array.
        assert(false);
    }
}


#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION

static inline void aot_mvin() {
    gemmini_config_ld(gemmini_config_ld_stride);
    for (int i = 0; i < gemmini_mvin_arg_num; i++) {
        gemmini_extended_mvin(gemmini_mvin_arg_list[i][0], 
                                gemmini_mvin_arg_list[i][1], 
                                gemmini_mvin_arg_list[i][2], 
                                gemmini_mvin_arg_list[i][3]);
    }
}

static inline void aot_mvout() {
    gemmini_config_st(gemmini_config_st_stride);
    for (int i = 0; i < gemmini_mvout_arg_num; i++) {
        gemmini_extended_mvout(gemmini_mvout_arg_list[i][0], 
                                gemmini_mvout_arg_list[i][1], 
                                gemmini_mvout_arg_list[i][2], 
                                gemmini_mvout_arg_list[i][3]);
    }
}

#endif


void mem_reset(elem_t* addr, uint64_t bytes) {
    printf("mem_reset ...\n");
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) 0;
    }
}

void mem_init(elem_t* addr, uint64_t bytes) {
    printf("mem_init ...\n");
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        addr[i] = (elem_t) (((uint32_t) 0x5A5A5A5A) ^ (uint32_t) i);
    }
}

bool mem_cmp(elem_t* addr, elem_t* addr2, uint64_t bytes) {
    printf("mem_cmp ...\n");
    size_t size = bytes / GEMMINI_WORD_BYTES;
    for (size_t i = 0; i < size; i++) {
        if (addr[i] != addr2[i]) {
            printf("mem_cmp: %d!=%d at %u/%u\n", addr[i], addr2[i], i, size);
            return false;
        }
    }
    return true;
}


int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif

    // const uint64_t bytes = 256;
    // const uint64_t bytes = 512;
    // const uint64_t bytes = 768;
    // const uint64_t bytes = 1024;
    // const uint64_t bytes = 2 * 1024;
    // const uint64_t bytes = 4 * 1024;
    // const uint64_t bytes = 16 * 1024;
    // const uint64_t bytes = 64 * 1024;
    const uint64_t bytes = 256 * 1024;

    assert((bytes % GEMMINI_MIN_TILE_BYTES) == 0);
    printf("total: %lu bytes\n", bytes);

    // elem_t* buf_base = (elem_t*) MEM_ADDR_BASE;
    // elem_t* buf_base = (elem_t*) MBUS_SPAD_ADDR_BASE;
    elem_t* buf_base = (elem_t*) SBUS_SPAD_ADDR_BASE;

    elem_t* buf_in = buf_base;
    elem_t* buf_out = (elem_t*) (((uint64_t) buf_base) + bytes);

#ifdef DO_CHECK
    mem_init(buf_in, bytes);
    // mem_reset(buf_out, bytes);
#endif

    // Call `mvin`/`mvout` without actual gemmini instruction calls to 
    // collect all the instruction arguments. This is for AOT mvin/mvout.
#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
    mvin(buf_in, 0, bytes);
    mvout(buf_out, 0, bytes);
    printf("AOT\n");
#else
    printf("JIT\n");
#endif

    printf("moving data: mem/spad -> gemmini -> mem/spad ...\n");
    gemmini_flush(0);
    uint64_t t_start = read_cycles();

#ifdef AOT_GEMMINI_INSTRUCTION_GENERATION
    // AOT mvin/mvout: Calculate the gemmini instruction before runtime.
    aot_mvin();
    aot_mvout();
#else
    // JIT mvin/mvout: Calculate the gemmini instruction arguments at runtime.
    mvin(buf_in, 0, bytes);
    mvout(buf_out, 0, bytes);
#endif

    gemmini_fence();
    uint64_t t_end = read_cycles();

    uint64_t cyc = t_end - t_start;
    uint64_t bw_scaled = 1000 * (bytes * 2) / cyc;

#ifdef DO_CHECK
    int eq = mem_cmp(buf_in, buf_out, bytes);
    printf("mem_cmp result: %d\n", eq);
#endif

    printf("%lu cycles\n", cyc);
    printf("%lu*0.001 bytes/cyc\n", bw_scaled);
    return 0;
}
