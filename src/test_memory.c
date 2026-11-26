#include "os_test.h"

#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(__SANITIZE_ADDRESS__)
#define OS_TEST_ASAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OS_TEST_ASAN_ENABLED 1
#else
#define OS_TEST_ASAN_ENABLED 0
#endif
#else
#define OS_TEST_ASAN_ENABLED 0
#endif

/* --------------------------------------------------------------------------
 * 1. mmap page alignment, read/write, and munmap test
 * -------------------------------------------------------------------------- */

int test_mmap_page_alignment(char *detail_buf, size_t detail_size) {
    long page_size = sysconf(_SC_PAGESIZE);
    ASSERT_TRUE(page_size > 0, "sysconf(_SC_PAGESIZE) returned invalid size");

    size_t len = (size_t)page_size;
    void *page = mmap(NULL, len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(page != MAP_FAILED, "mmap() failed");

    uintptr_t addr = (uintptr_t)page;
    int aligned = ((addr % (uintptr_t)len) == 0U);

    volatile uint8_t *bytes = (volatile uint8_t *)page;
    bytes[0] = 0xA5U;
    bytes[len - 1U] = 0x5AU;

    uint8_t first = bytes[0];
    uint8_t last = bytes[len - 1U];

    int unmap_rc = munmap(page, len);

    ASSERT_EQ(0, unmap_rc, "munmap() failed");
    ASSERT_TRUE(aligned, "mmap() address is not page-aligned");
    ASSERT_EQ(0xA5U, first, "first byte readback mismatch");
    ASSERT_EQ(0x5AU, last, "last byte readback mismatch");

    PASS_WITH_INFO("page_size=%zu, addr=0x%" PRIxPTR ", aligned=yes",
                   len, addr);
}

/* --------------------------------------------------------------------------
 * SIGSEGV fault-trapping helpers (disabled under AddressSanitizer)
 * -------------------------------------------------------------------------- */

#if !OS_TEST_ASAN_ENABLED

static sigjmp_buf g_segv_env;
static volatile sig_atomic_t g_segv_armed = 0;
static volatile sig_atomic_t g_segv_caught = 0;
static volatile sig_atomic_t g_segv_signo = 0;

static void segv_test_handler(int signo) {
    if (g_segv_armed) {
        g_segv_caught = 1;
        g_segv_signo = signo;
        g_segv_armed = 0;
        siglongjmp(g_segv_env, 1);
    }
}

static int probe_fault_write(volatile uint8_t *target, uint8_t value,
                             sig_atomic_t *caught_out,
                             sig_atomic_t *signo_out) {
    struct sigaction new_sa;
    struct sigaction old_sa;
    memset(&new_sa, 0, sizeof(new_sa));
    new_sa.sa_handler = segv_test_handler;
    sigemptyset(&new_sa.sa_mask);
    new_sa.sa_flags = 0;

    if (sigaction(SIGSEGV, &new_sa, &old_sa) != 0) {
        return -1;
    }

    g_segv_caught = 0;
    g_segv_signo = 0;

    if (sigsetjmp(g_segv_env, 1) == 0) {
        g_segv_armed = 1;
        *target = value;
        g_segv_armed = 0;
    }

    int restore_rc = sigaction(SIGSEGV, &old_sa, NULL);
    *caught_out = g_segv_caught;
    *signo_out = g_segv_signo;
    return restore_rc;
}

static int probe_fault_read(const volatile uint8_t *target,
                            sig_atomic_t *caught_out,
                            sig_atomic_t *signo_out) {
    struct sigaction new_sa;
    struct sigaction old_sa;
    memset(&new_sa, 0, sizeof(new_sa));
    new_sa.sa_handler = segv_test_handler;
    sigemptyset(&new_sa.sa_mask);
    new_sa.sa_flags = 0;

    if (sigaction(SIGSEGV, &new_sa, &old_sa) != 0) {
        return -1;
    }

    g_segv_caught = 0;
    g_segv_signo = 0;

    if (sigsetjmp(g_segv_env, 1) == 0) {
        g_segv_armed = 1;
        volatile uint8_t sink = *target;
        (void)sink;
        g_segv_armed = 0;
    }

    int restore_rc = sigaction(SIGSEGV, &old_sa, NULL);
    *caught_out = g_segv_caught;
    *signo_out = g_segv_signo;
    return restore_rc;
}

#endif /* !OS_TEST_ASAN_ENABLED */

/* --------------------------------------------------------------------------
 * 2. mprotect(PROT_READ) write-violation SIGSEGV test
 * -------------------------------------------------------------------------- */

int test_mprotect_readonly_fault(char *detail_buf, size_t detail_size) {
#if OS_TEST_ASAN_ENABLED
    SKIP_TEST("skipped under AddressSanitizer (intentional SIGSEGV fault test)");
#else
    long page_size = sysconf(_SC_PAGESIZE);
    ASSERT_TRUE(page_size > 0, "sysconf(_SC_PAGESIZE) returned invalid size");

    size_t len = (size_t)page_size;
    void *page = mmap(NULL, len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(page != MAP_FAILED, "mmap() failed");

    volatile uint8_t *bytes = (volatile uint8_t *)page;
    bytes[0] = 0x42U;

    int mp_rc = mprotect(page, len, PROT_READ);
    if (mp_rc != 0) {
        (void)munmap(page, len);
        ASSERT_EQ(0, mp_rc, "mprotect(PROT_READ) failed");
    }

    sig_atomic_t caught = 0;
    sig_atomic_t signo = 0;
    int sig_rc = probe_fault_write(bytes, 0x99U, &caught, &signo);
    uint8_t preserved = bytes[0];
    int unmap_rc = munmap(page, len);

    ASSERT_EQ(0, sig_rc, "sigaction(SIGSEGV) setup/restore failed");
    ASSERT_EQ(0, unmap_rc, "munmap() failed");
    ASSERT_EQ(1, caught, "write to PROT_READ page did not trigger SIGSEGV");
    ASSERT_EQ(SIGSEGV, signo, "unexpected signal caught on PROT_READ write");
    ASSERT_EQ(0x42U, preserved, "PROT_READ page contents were unexpectedly modified");

    PASS_WITH_INFO("mprotect=PROT_READ, caught_signal=SIGSEGV(%d), value_preserved=0x%02x",
                   (int)signo, (unsigned int)preserved);
#endif
}

/* --------------------------------------------------------------------------
 * 3. mprotect(PROT_NONE) guard page SIGSEGV test
 * -------------------------------------------------------------------------- */

int test_mprotect_guard_page(char *detail_buf, size_t detail_size) {
#if OS_TEST_ASAN_ENABLED
    SKIP_TEST("skipped under AddressSanitizer (intentional SIGSEGV fault test)");
#else
    long page_size = sysconf(_SC_PAGESIZE);
    ASSERT_TRUE(page_size > 0, "sysconf(_SC_PAGESIZE) returned invalid size");

    size_t pg = (size_t)page_size;
    size_t total_len = pg * 2U;
    void *region = mmap(NULL, total_len, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(region != MAP_FAILED, "mmap(2 pages) failed");

    uint8_t *base = (uint8_t *)region;
    uint8_t *guard = base + pg;

    int mp_rc = mprotect(guard, pg, PROT_NONE);
    if (mp_rc != 0) {
        (void)munmap(region, total_len);
        ASSERT_EQ(0, mp_rc, "mprotect(PROT_NONE) on guard page failed");
    }

    /* Access to the first (valid) page must succeed normally */
    volatile uint8_t *valid_page = (volatile uint8_t *)base;
    valid_page[0] = 0x77U;
    valid_page[pg - 1U] = 0x88U;
    uint8_t v_first = valid_page[0];
    uint8_t v_last = valid_page[pg - 1U];

    /* Read from the second (PROT_NONE) page must trigger SIGSEGV */
    sig_atomic_t caught = 0;
    sig_atomic_t signo = 0;
    int sig_rc = probe_fault_read((const volatile uint8_t *)guard, &caught, &signo);
    int unmap_rc = munmap(region, total_len);

    ASSERT_EQ(0, sig_rc, "sigaction(SIGSEGV) setup/restore failed");
    ASSERT_EQ(0, unmap_rc, "munmap(2 pages) failed");
    ASSERT_EQ(0x77U, v_first, "valid page first byte mismatch");
    ASSERT_EQ(0x88U, v_last, "valid page last byte mismatch");
    ASSERT_EQ(1, caught, "read from PROT_NONE guard page did not trigger SIGSEGV");
    ASSERT_EQ(SIGSEGV, signo, "unexpected signal caught on PROT_NONE read");

    PASS_WITH_INFO("valid_page=rw_ok, guard_page=PROT_NONE, caught_signal=SIGSEGV(%d)",
                   (int)signo);
#endif
}
