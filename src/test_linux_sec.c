#include "os_test.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef PR_GET_NO_NEW_PRIVS
#define PR_GET_NO_NEW_PRIVS 39
#endif

/* --------------------------------------------------------------------------
 * Safe /proc and /sys file reading helpers
 * -------------------------------------------------------------------------- */

typedef enum {
    SYSFILE_OK = 0,
    SYSFILE_NOENT = 1,
    SYSFILE_ACCES = 2,
    SYSFILE_ERR = 3
} sysfile_status_t;

static sysfile_status_t read_sysfile_text(const char *path,
                                          char *out_buf,
                                          size_t out_size) {
    if (out_buf == NULL || out_size == 0U) {
        return SYSFILE_ERR;
    }
    out_buf[0] = '\0';

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return SYSFILE_NOENT;
        }
        if (errno == EACCES || errno == EPERM) {
            return SYSFILE_ACCES;
        }
        return SYSFILE_ERR;
    }

    size_t total = 0U;
    while (total + 1U < out_size) {
        ssize_t n = read(fd, out_buf + total, out_size - 1U - total);
        if (n > 0) {
            total += (size_t)n;
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            int saved = errno;
            (void)close(fd);
            if (saved == EACCES || saved == EPERM) {
                return SYSFILE_ACCES;
            }
            return SYSFILE_ERR;
        }
    }

    (void)close(fd);
    out_buf[total] = '\0';

    while (total > 0U &&
           (out_buf[total - 1U] == '\n' || out_buf[total - 1U] == '\r' ||
            out_buf[total - 1U] == ' '  || out_buf[total - 1U] == '\t')) {
        out_buf[--total] = '\0';
    }

    return SYSFILE_OK;
}

static sysfile_status_t read_sysfile_int(const char *path, int *out_val) {
    char buf[64];
    sysfile_status_t st = read_sysfile_text(path, buf, sizeof(buf));
    if (st != SYSFILE_OK) {
        return st;
    }

    char *endptr = NULL;
    errno = 0;
    long v = strtol(buf, &endptr, 10);
    if (errno != 0 || endptr == buf || *endptr != '\0') {
        return SYSFILE_ERR;
    }

    *out_val = (int)v;
    return SYSFILE_OK;
}

/* --------------------------------------------------------------------------
 * 1. ASLR status (/proc/sys/kernel/randomize_va_space + mmap delta)
 * -------------------------------------------------------------------------- */

int test_aslr_status(char *detail_buf, size_t detail_size) {
    long page_size = sysconf(_SC_PAGESIZE);
    ASSERT_TRUE(page_size > 0, "sysconf(_SC_PAGESIZE) returned invalid size");
    size_t len = (size_t)page_size;

    void *p1 = mmap(NULL, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(p1 != MAP_FAILED, "first mmap() failed");

    void *p2 = mmap(NULL, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p2 == MAP_FAILED) {
        (void)munmap(p1, len);
        ASSERT_TRUE(0, "second mmap() failed");
    }

    uintptr_t a1 = (uintptr_t)p1;
    uintptr_t a2 = (uintptr_t)p2;
    uintptr_t delta = (a1 > a2) ? (a1 - a2) : (a2 - a1);

    int u1 = munmap(p1, len);
    int u2 = munmap(p2, len);
    ASSERT_EQ(0, u1, "munmap(p1) failed");
    ASSERT_EQ(0, u2, "munmap(p2) failed");
    ASSERT_TRUE(a1 != a2 && delta > 0U, "independent mmap() allocations collided");

    int aslr_level = -1;
    sysfile_status_t st = read_sysfile_int("/proc/sys/kernel/randomize_va_space",
                                           &aslr_level);

    if (st == SYSFILE_OK) {
        ASSERT_EQ(2, aslr_level,
                  "ASLR randomize_va_space is not set to full randomization (2)");
        PASS_WITH_INFO("randomize_va_space=%d (full), mmap_delta=0x%" PRIxPTR,
                       aslr_level, delta);
    }

    if (st == SYSFILE_ACCES) {
        PASS_WITH_INFO("randomize_va_space=blocked(EACCES/SELinux), mmap_delta=0x%" PRIxPTR,
                       delta);
    }

    if (st == SYSFILE_NOENT) {
        PASS_WITH_INFO("randomize_va_space=unavailable(ENOENT), mmap_delta=0x%" PRIxPTR,
                       delta);
    }

    ASSERT_TRUE(0, "failed to parse /proc/sys/kernel/randomize_va_space");
}

/* --------------------------------------------------------------------------
 * 2. Kernel info leak protections (kptr_restrict and dmesg_restrict)
 * -------------------------------------------------------------------------- */

int test_kernel_info_leak_protections(char *detail_buf, size_t detail_size) {
    int kptr_val = -1;
    int dmesg_val = -1;
    sysfile_status_t kptr_st = read_sysfile_int("/proc/sys/kernel/kptr_restrict",
                                                &kptr_val);
    sysfile_status_t dmesg_st = read_sysfile_int("/proc/sys/kernel/dmesg_restrict",
                                                 &dmesg_val);

    if (kptr_st == SYSFILE_NOENT && dmesg_st == SYSFILE_NOENT) {
        SKIP_TEST("kptr_restrict and dmesg_restrict sysctl nodes are not present");
    }

    char kptr_str[32];
    char dmesg_str[32];
    int kptr_protected = 0;
    int dmesg_protected = 0;

    if (kptr_st == SYSFILE_OK) {
        snprintf(kptr_str, sizeof(kptr_str), "%d", kptr_val);
        if (kptr_val >= 1) {
            kptr_protected = 1;
        }
    } else if (kptr_st == SYSFILE_ACCES) {
        snprintf(kptr_str, sizeof(kptr_str), "EACCES(protected)");
        kptr_protected = 1;
    } else if (kptr_st == SYSFILE_NOENT) {
        snprintf(kptr_str, sizeof(kptr_str), "ENOENT");
    } else {
        ASSERT_TRUE(0, "failed to read /proc/sys/kernel/kptr_restrict");
    }

    if (dmesg_st == SYSFILE_OK) {
        snprintf(dmesg_str, sizeof(dmesg_str), "%d", dmesg_val);
        if (dmesg_val >= 1) {
            dmesg_protected = 1;
        }
    } else if (dmesg_st == SYSFILE_ACCES) {
        snprintf(dmesg_str, sizeof(dmesg_str), "EACCES(protected)");
        dmesg_protected = 1;
    } else if (dmesg_st == SYSFILE_NOENT) {
        snprintf(dmesg_str, sizeof(dmesg_str), "ENOENT");
    } else {
        ASSERT_TRUE(0, "failed to read /proc/sys/kernel/dmesg_restrict");
    }

    ASSERT_TRUE(kptr_protected || dmesg_protected,
                "kernel info-leak protections disabled (kptr_restrict=0 and dmesg_restrict=0)");

    PASS_WITH_INFO("kptr_restrict=%s, dmesg_restrict=%s", kptr_str, dmesg_str);
}

/* --------------------------------------------------------------------------
 * 3. Yama ptrace_scope (/proc/sys/kernel/yama/ptrace_scope)
 * -------------------------------------------------------------------------- */

int test_yama_ptrace_scope(char *detail_buf, size_t detail_size) {
    int scope = -1;
    sysfile_status_t st = read_sysfile_int("/proc/sys/kernel/yama/ptrace_scope",
                                           &scope);

    if (st == SYSFILE_NOENT) {
        SKIP_TEST("Yama LSM is not active (/proc/sys/kernel/yama/ptrace_scope absent)");
    }
    if (st == SYSFILE_ACCES) {
        PASS_WITH_INFO("yama_ptrace_scope=restricted(EACCES)");
    }
    ASSERT_EQ(SYSFILE_OK, st, "failed to read /proc/sys/kernel/yama/ptrace_scope");

    ASSERT_TRUE(scope >= 1,
                "yama ptrace_scope=0 allows unrestricted same-user ptrace");

    const char *mode_desc = "unknown";
    if (scope == 1) {
        mode_desc = "restricted_parent_only";
    } else if (scope == 2) {
        mode_desc = "admin_only";
    } else if (scope >= 3) {
        mode_desc = "no_attach";
    }

    PASS_WITH_INFO("yama_ptrace_scope=%d (%s)", scope, mode_desc);
}

/* --------------------------------------------------------------------------
 * 4. PR_SET_NO_NEW_PRIVS and Seccomp status in isolated child process
 * -------------------------------------------------------------------------- */

typedef struct {
    int set_rc;
    int get_rc;
    int seccomp_mode;
    int seccomp_found;
} child_priv_report_t;

static int read_self_seccomp_mode(int *out_mode) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        return -1;
    }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "Seccomp:", 8U) == 0) {
            char *p = line + 8;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            char *endptr = NULL;
            errno = 0;
            long val = strtol(p, &endptr, 10);
            if (errno == 0 && endptr != p) {
                *out_mode = (int)val;
                found = 1;
            }
            break;
        }
    }

    (void)fclose(fp);
    return found ? 0 : -1;
}

static int write_exact(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0U;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int read_exact(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t off = 0U;
    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);
        if (n > 0) {
            off += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

int test_no_new_privs_and_seccomp(char *detail_buf, size_t detail_size) {
    int pfd[2] = { -1, -1 };
    ASSERT_EQ(0, pipe(pfd), "pipe() failed");

    (void)fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        (void)close(pfd[0]);
        (void)close(pfd[1]);
        ASSERT_TRUE(0, "fork() failed");
    }

    if (pid == 0) {
        (void)close(pfd[0]);

        child_priv_report_t rep;
        memset(&rep, 0, sizeof(rep));
        rep.set_rc = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        rep.get_rc = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
        rep.seccomp_mode = -1;
        if (read_self_seccomp_mode(&rep.seccomp_mode) == 0) {
            rep.seccomp_found = 1;
        }

        int wr = write_exact(pfd[1], &rep, sizeof(rep));
        (void)close(pfd[1]);
        _exit((wr == 0) ? 0 : 2);
    }

    (void)close(pfd[1]);

    child_priv_report_t rep;
    memset(&rep, 0, sizeof(rep));
    int rd_rc = read_exact(pfd[0], &rep, sizeof(rep));
    (void)close(pfd[0]);

    int wstatus = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(pid, &wstatus, 0);
    } while (waited < 0 && errno == EINTR);

    ASSERT_EQ(pid, waited, "waitpid() failed");
    ASSERT_TRUE(WIFEXITED(wstatus), "child did not exit normally");
    ASSERT_EQ(0, WEXITSTATUS(wstatus), "child exited with error status");
    ASSERT_EQ(0, rd_rc, "failed to read child privilege report from pipe");
    ASSERT_EQ(0, rep.set_rc, "prctl(PR_SET_NO_NEW_PRIVS, 1) failed");
    ASSERT_EQ(1, rep.get_rc, "prctl(PR_GET_NO_NEW_PRIVS) did not return 1");
    ASSERT_EQ(1, rep.seccomp_found, "Seccomp field not found in /proc/self/status");
    ASSERT_TRUE(rep.seccomp_mode >= 0 && rep.seccomp_mode <= 2,
                "unexpected Seccomp mode value in /proc/self/status");

    const char *seccomp_desc = "disabled";
    if (rep.seccomp_mode == 1) {
        seccomp_desc = "strict";
    } else if (rep.seccomp_mode == 2) {
        seccomp_desc = "filter";
    }

    PASS_WITH_INFO("no_new_privs=%d, seccomp=%d (%s)",
                   rep.get_rc, rep.seccomp_mode, seccomp_desc);
}

/* --------------------------------------------------------------------------
 * 5. Active Linux Security Modules stack (/sys/kernel/security/lsm)
 * -------------------------------------------------------------------------- */

int test_lsm_stack(char *detail_buf, size_t detail_size) {
    char lsm_buf[256];
    sysfile_status_t st = read_sysfile_text("/sys/kernel/security/lsm",
                                            lsm_buf, sizeof(lsm_buf));

    if (st == SYSFILE_NOENT) {
        SKIP_TEST("/sys/kernel/security/lsm not present (securityfs not mounted)");
    }
    if (st == SYSFILE_ACCES) {
        SKIP_TEST("/sys/kernel/security/lsm access denied (EACCES)");
    }
    ASSERT_EQ(SYSFILE_OK, st, "failed to read /sys/kernel/security/lsm");
    ASSERT_TRUE(lsm_buf[0] != '\0', "/sys/kernel/security/lsm is empty");

    PASS_WITH_INFO("lsm=%s", lsm_buf);
}
