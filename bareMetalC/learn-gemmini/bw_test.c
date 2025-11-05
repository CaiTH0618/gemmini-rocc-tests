#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif

// #define ADDR_SIZE 0x00100000U
#define ADDR_SIZE 0x00010000U

#define SBUS_SPAD_ADDR_BASE 0xC0000000U
#define SBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define SBUS_SPAD_ADDR_CEIL (SBUS_SPAD_ADDR_BASE + SBUS_SPAD_ADDR_SIZE)

#define MBUS_SPAD_ADDR_BASE 0x08000000U
#define MBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define MBUS_SPAD_ADDR_CEIL (MBUS_SPAD_ADDR_BASE + MBUS_SPAD_ADDR_SIZE)

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))
#define MEM_ADDR_CEIL (MEM_ADDR_BASE + MEM_ADDR_SIZE)

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

static inline uint64_t read_cycles() {
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;
}

static inline void fence_rw_rw(void) {
    /* RISC-V full fence for ordering (if running on RISC-V); adjust per arch */
    __asm__ volatile("fence rw, rw" ::: "memory");
}


uint64_t bw_test(uint64_t addr_base, uint64_t addr_ceil) {
    printf("bw_test: starts\n");
    printf("bw_test: addr_base=%lx, addr_ceil=%lx\n", addr_base, addr_ceil);

    const uint64_t alignment = 64;
    if (addr_base >= addr_ceil) {
        printf("bw_test: error: addr_base >= addr_ceil\n");
        return 0;
    }

    uint64_t start_addr = (addr_base + (alignment - 1)) & ~(alignment - 1ULL);
    uint64_t end_addr = addr_ceil & ~(alignment - 1ULL);
    if (start_addr >= end_addr) {
        printf("bw_test: error: start_addr >= end_addr\n");
        return 0;
    }

    uint64_t available_bytes = end_addr - start_addr;
    if (available_bytes < alignment * 2) {
        printf("bw_test: error: not enough space for two buffers\n");
        return 0;
    }

    available_bytes &= ~(alignment - 1ULL);
    uint64_t buffer_bytes = (available_bytes / 2) & ~(alignment - 1ULL);
    if (buffer_bytes == 0) {
        printf("bw_test: error: buffer_bytes == 0\n");
        return 0;
    }

    printf("bw_test: buffer_bytes=%lu\n", buffer_bytes);

    uint8_t *base_ptr = (uint8_t *)start_addr;
    uint8_t *buf1 = base_ptr;
    uint8_t *buf2 = base_ptr + buffer_bytes;

    uint64_t word_count = buffer_bytes / sizeof(uint64_t);
    uint64_t *wsrc = (uint64_t *)buf1;
    uint64_t *wdst = (uint64_t *)buf2;
    for (uint64_t i = 0; i < word_count; ++i) {
        wsrc[i] = 0x0123456789ABCDEFULL ^ i;
        wdst[i] = 0;
    }

    const uint32_t iterations = 10;
    uint64_t total_cycles = 0;

    for (uint32_t iter = 0; iter < iterations; ++iter) {
        printf("bw_test: iteration %lu/%lu\n", iter + 1, iterations);
        fence_rw_rw();
        uint64_t copy_start = read_cycles();

        uint64_t *src = (uint64_t *)buf1;
        uint64_t *dst = (uint64_t *)buf2;
        uint64_t *src_end = src + word_count;

        while (src + 8 <= src_end) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            dst[4] = src[4];
            dst[5] = src[5];
            dst[6] = src[6];
            dst[7] = src[7];
            src += 8;
            dst += 8;
        }
        for (; src < src_end; ++src, ++dst) {
            *dst = *src;
        }

        fence_rw_rw();
        uint64_t copy_end = read_cycles();
        uint64_t cycles = copy_end - copy_start;
        total_cycles += cycles;
        uint64_t scaled_bw = (1000 * (buffer_bytes * 2)) / cycles;
        printf("bw_test: %lu * 0.001 bytes/cycle\n", scaled_bw);
    }

    uint64_t checksum = ((volatile uint64_t *)buf2)[0];
    asm volatile("" :: "r"(checksum));

    if (total_cycles == 0) {
        printf("bw_test: error: zero cycles measured\n");
        return 0;
    }

    uint64_t scaled_bytes_per_cycle = (1000 * (buffer_bytes * 2 * iterations)) / total_cycles;

    printf("bw_test: ends\n");
    return scaled_bytes_per_cycle;
}


int main() {
    printf("bw_test main: starts\n");

    uint64_t mem_bw = bw_test(MEM_ADDR_BASE, MEM_ADDR_CEIL);
    printf("memory bandwdith : %lu * 0.001 (bytes/cyc)\n\n", mem_bw);

    uint64_t mbus_spad_bw = bw_test(MBUS_SPAD_ADDR_BASE, MBUS_SPAD_ADDR_CEIL);
    printf("mbus spad bandwdith : %lu * 0.001 (bytes/cyc)\n\n", mbus_spad_bw);

    uint64_t sbus_spad_bw = bw_test(SBUS_SPAD_ADDR_BASE, SBUS_SPAD_ADDR_CEIL);
    printf("sbus spad bandwdith : %lu * 0.001 (bytes/cyc)\n\n", sbus_spad_bw);

    printf("bw_test main: ends\n");
    return 0;
}
