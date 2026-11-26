#include "os_test.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* --------------------------------------------------------------------------
 * 1. AF_UNIX SOCK_STREAM socketpair byte-by-byte integrity test
 * -------------------------------------------------------------------------- */

#define SOCKETPAIR_PAYLOAD_SIZE 256U

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = write(fd, buf + offset, len - offset);
        if (n > 0) {
            offset += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int read_all(int fd, uint8_t *buf, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = read(fd, buf + offset, len - offset);
        if (n > 0) {
            offset += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

int test_unix_socketpair(char *detail_buf, size_t detail_size) {
    int sv[2] = { -1, -1 };
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv),
              "socketpair(AF_UNIX, SOCK_STREAM) failed");

    uint8_t tx[SOCKETPAIR_PAYLOAD_SIZE];
    uint8_t rx[SOCKETPAIR_PAYLOAD_SIZE];
    memset(rx, 0, sizeof(rx));

    for (size_t i = 0; i < sizeof(tx); i++) {
        tx[i] = (uint8_t)(i & 0xFFU);
    }

    int wr_rc = write_all(sv[0], tx, sizeof(tx));
    int rd_rc = (wr_rc == 0) ? read_all(sv[1], rx, sizeof(rx)) : -1;

    int close0_rc = close(sv[0]);
    int close1_rc = close(sv[1]);

    ASSERT_EQ(0, wr_rc, "failed to write payload to socketpair");
    ASSERT_EQ(0, rd_rc, "failed to read payload from socketpair");
    ASSERT_EQ(0, close0_rc, "close(sv[0]) failed");
    ASSERT_EQ(0, close1_rc, "close(sv[1]) failed");

    for (size_t i = 0; i < sizeof(tx); i++) {
        ASSERT_EQ(tx[i], rx[i], "socketpair payload byte mismatch");
    }

    PASS_WITH_INFO("af_unix_sock_stream, payload_bytes=%zu, integrity=ok",
                   sizeof(tx));
}

/* --------------------------------------------------------------------------
 * 2. Anonymous shared memory test (memfd_create syscall + mmap fallback)
 * -------------------------------------------------------------------------- */

#define SHM_MAGIC_VALUE 0x5A5A1234U
#define SHM_EXPECTED_MSG "os-test-ipc-child-ok"

typedef struct {
    uint32_t magic;
    pid_t    child_pid;
    char     message[64];
} ipc_shm_region_t;

int test_anonymous_shared_memory(char *detail_buf, size_t detail_size) {
    const size_t shm_size = sizeof(ipc_shm_region_t);
    const char *backend = "mmap(MAP_SHARED|MAP_ANONYMOUS)";
    ipc_shm_region_t *region = MAP_FAILED;

#ifdef SYS_memfd_create
    long fd_raw = syscall(SYS_memfd_create, "os_test_shm", 0U);
    if (fd_raw >= 0) {
        int mfd = (int)fd_raw;
        if (ftruncate(mfd, (off_t)shm_size) == 0) {
            region = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, mfd, 0);
            if (region != MAP_FAILED) {
                backend = "memfd_create";
            }
        }
        (void)close(mfd);
    }
#endif

    if (region == MAP_FAILED) {
        region = mmap(NULL, shm_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    }

    ASSERT_TRUE(region != MAP_FAILED, "failed to map anonymous shared memory");

    memset(region, 0, shm_size);

    /* Flush stdio buffers before fork to avoid duplicate output */
    (void)fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) {
        (void)munmap(region, shm_size);
        ASSERT_TRUE(0, "fork() failed");
    }

    if (pid == 0) {
        region->magic = SHM_MAGIC_VALUE;
        region->child_pid = getpid();
        snprintf(region->message, sizeof(region->message), "%s", SHM_EXPECTED_MSG);
        _exit(0);
    }

    int wstatus = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(pid, &wstatus, 0);
    } while (waited < 0 && errno == EINTR);

    uint32_t observed_magic = region->magic;
    pid_t observed_pid = region->child_pid;
    char observed_msg[64];
    memcpy(observed_msg, region->message, sizeof(observed_msg));
    observed_msg[sizeof(observed_msg) - 1U] = '\0';

    int unmap_rc = munmap(region, shm_size);

    ASSERT_EQ(pid, waited, "waitpid() failed");
    ASSERT_TRUE(WIFEXITED(wstatus), "child process did not exit normally");
    ASSERT_EQ(0, WEXITSTATUS(wstatus), "child process exited with non-zero status");
    ASSERT_EQ(0, unmap_rc, "munmap() failed");
    ASSERT_EQ(SHM_MAGIC_VALUE, observed_magic, "shared memory magic mismatch");
    ASSERT_EQ(pid, observed_pid, "shared memory child PID mismatch");
    ASSERT_EQ(0, strcmp(SHM_EXPECTED_MSG, observed_msg),
              "shared memory message mismatch");

    PASS_WITH_INFO("backend=%s, child_pid=%d, magic=0x%08x",
                   backend, (int)observed_pid, (unsigned int)observed_magic);
}
