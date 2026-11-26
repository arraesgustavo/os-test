#include "os_test.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

/* --------------------------------------------------------------------------
 * Safe /proc and /sys reading helpers
 * -------------------------------------------------------------------------- */

typedef enum {
    AFILE_OK = 0,
    AFILE_NOENT = 1,
    AFILE_ACCES = 2,
    AFILE_INVAL = 3,
    AFILE_ERR = 4
} afile_status_t;

static afile_status_t read_attr_file(const char *path,
                                     char *out_buf,
                                     size_t out_size) {
    if (out_buf == NULL || out_size == 0U) {
        return AFILE_ERR;
    }
    out_buf[0] = '\0';

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return AFILE_NOENT;
        }
        if (errno == EACCES || errno == EPERM) {
            return AFILE_ACCES;
        }
        if (errno == EINVAL) {
            return AFILE_INVAL;
        }
        return AFILE_ERR;
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
                return AFILE_ACCES;
            }
            if (saved == EINVAL) {
                return AFILE_INVAL;
            }
            return AFILE_ERR;
        }
    }

    (void)close(fd);
    out_buf[total] = '\0';

    /* /proc/self/attr/current may include trailing NUL or whitespace */
    while (total > 0U &&
           (out_buf[total - 1U] == '\0' || out_buf[total - 1U] == '\n' ||
            out_buf[total - 1U] == '\r' || out_buf[total - 1U] == ' '  ||
            out_buf[total - 1U] == '\t')) {
        out_buf[--total] = '\0';
    }

    return AFILE_OK;
}

/* --------------------------------------------------------------------------
 * 1. SELinux process domain and enforcing mode
 * -------------------------------------------------------------------------- */

int test_selinux_domain_and_enforcing(char *detail_buf, size_t detail_size) {
    char domain[128];
    afile_status_t dom_st = read_attr_file("/proc/self/attr/current",
                                           domain, sizeof(domain));

    char enforce_buf[32];
    afile_status_t enf_st = read_attr_file("/sys/fs/selinux/enforce",
                                           enforce_buf, sizeof(enforce_buf));

    if (enf_st == AFILE_NOENT) {
        SKIP_TEST("SELinux not active (/sys/fs/selinux/enforce absent)");
    }

    if (dom_st == AFILE_NOENT || dom_st == AFILE_INVAL) {
        SKIP_TEST("SELinux domain unavailable (/proc/self/attr/current unsupported)");
    }

    if (dom_st == AFILE_ACCES) {
        snprintf(domain, sizeof(domain), "blocked(EACCES)");
    } else {
        ASSERT_EQ(AFILE_OK, dom_st, "failed to read /proc/self/attr/current");
        ASSERT_TRUE(domain[0] != '\0', "SELinux domain string is empty");
    }

    if (enf_st == AFILE_ACCES) {
        PASS_WITH_INFO("domain=%s, enforce=blocked(EACCES/SELinux)", domain);
    }

    ASSERT_EQ(AFILE_OK, enf_st, "failed to read /sys/fs/selinux/enforce");

    char *endptr = NULL;
    errno = 0;
    long enforce_val = strtol(enforce_buf, &endptr, 10);
    ASSERT_TRUE(errno == 0 && endptr != enforce_buf && *endptr == '\0',
                "invalid integer in /sys/fs/selinux/enforce");
    ASSERT_EQ(1L, enforce_val, "SELinux is not in enforcing mode (enforce != 1)");

    PASS_WITH_INFO("domain=%s, enforce=%ld (enforcing)", domain, enforce_val);
}

/* --------------------------------------------------------------------------
 * 2. Android Verified Boot (AVB) and build security properties
 * -------------------------------------------------------------------------- */

#ifdef __ANDROID__
static void read_android_prop(const char *name, char *out_buf, size_t out_size) {
    char raw[PROP_VALUE_MAX];
    memset(raw, 0, sizeof(raw));
    int len = __system_property_get(name, raw);
    if (len > 0) {
        snprintf(out_buf, out_size, "%s", raw);
    } else {
        snprintf(out_buf, out_size, "<unset>");
    }
}
#endif

int test_android_verified_boot_and_build(char *detail_buf, size_t detail_size) {
#ifdef __ANDROID__
    char vb_state[PROP_VALUE_MAX];
    char dev_state[PROP_VALUE_MAX];
    char build_type[PROP_VALUE_MAX];
    char debuggable[PROP_VALUE_MAX];
    char secure[PROP_VALUE_MAX];
    char sdk_ver[PROP_VALUE_MAX];
    char sec_patch[PROP_VALUE_MAX];

    read_android_prop("ro.boot.verifiedbootstate", vb_state, sizeof(vb_state));
    read_android_prop("ro.boot.vbmeta.device_state", dev_state, sizeof(dev_state));
    read_android_prop("ro.build.type", build_type, sizeof(build_type));
    read_android_prop("ro.debuggable", debuggable, sizeof(debuggable));
    read_android_prop("ro.secure", secure, sizeof(secure));
    read_android_prop("ro.build.version.sdk", sdk_ver, sizeof(sdk_ver));
    read_android_prop("ro.build.version.security_patch", sec_patch, sizeof(sec_patch));

    ASSERT_TRUE(strcmp(sdk_ver, "<unset>") != 0,
                "essential property ro.build.version.sdk is inaccessible");
    ASSERT_TRUE(strcmp(build_type, "<unset>") != 0,
                "essential property ro.build.type is inaccessible");

    PASS_WITH_INFO("avb=%s, device_state=%s, build=%s, debuggable=%s, secure=%s, sdk=%s, patch=%s",
                   vb_state, dev_state, build_type, debuggable,
                   secure, sdk_ver, sec_patch);
#else
    SKIP_TEST("requires Android runtime (__system_property_get / AVB properties)");
#endif
}

/* --------------------------------------------------------------------------
 * 3. UID sandbox classification and POSIX capabilities (CapEff)
 * -------------------------------------------------------------------------- */

static int read_self_capeff(char *hex_buf, size_t hex_size,
                            unsigned long long *out_val) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        return -1;
    }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "CapEff:", 7U) == 0) {
            char *p = line + 7;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            size_t len = strlen(p);
            while (len > 0U &&
                   (p[len - 1U] == '\n' || p[len - 1U] == '\r' ||
                    p[len - 1U] == ' '  || p[len - 1U] == '\t')) {
                p[--len] = '\0';
            }

            char *endptr = NULL;
            errno = 0;
            unsigned long long val = strtoull(p, &endptr, 16);
            if (errno == 0 && endptr != p && *endptr == '\0') {
                snprintf(hex_buf, hex_size, "%s", p);
                *out_val = val;
                found = 1;
            }
            break;
        }
    }

    (void)fclose(fp);
    return found ? 0 : -1;
}

static const char *classify_uid(uid_t uid) {
    if (uid == 0U) {
        return "root";
    }
#ifdef __ANDROID__
    if (uid == 2000U) {
        return "AID_SHELL";
    }
    if (uid >= 10000U) {
        return "app_sandbox";
    }
    return "android_system_uid";
#else
    return "linux_user";
#endif
}

int test_uid_sandbox_and_capabilities(char *detail_buf, size_t detail_size) {
    uid_t uid = getuid();
    uid_t euid = geteuid();
    const char *role = classify_uid(uid);

    char cap_hex[64];
    unsigned long long cap_val = 0ULL;
    ASSERT_EQ(0, read_self_capeff(cap_hex, sizeof(cap_hex), &cap_val),
              "failed to parse CapEff from /proc/self/status");

    if (uid != 0U && euid != 0U) {
        ASSERT_TRUE(cap_val == 0ULL,
                    "non-root process holds privileged effective capabilities (CapEff != 0)");
    }

    PASS_WITH_INFO("uid=%u, euid=%u, role=%s, CapEff=%s",
                   (unsigned int)uid, (unsigned int)euid, role, cap_hex);
}

/* --------------------------------------------------------------------------
 * 4. Filesystem sandbox isolation and /dev/binder inspection
 * -------------------------------------------------------------------------- */

static int check_dir_blocked(const char *path, int *out_errno) {
    errno = 0;
    DIR *d = opendir(path);
    if (d != NULL) {
        (void)closedir(d);
        *out_errno = 0;
        return 0; /* Unexpectedly accessible */
    }
    *out_errno = errno;
    return (errno == EACCES || errno == EPERM) ? 1 : -1;
}

int test_filesystem_and_binder_isolation(char *detail_buf, size_t detail_size) {
    uid_t euid = geteuid();

#ifdef __ANDROID__
    const char *data_data_state = "root_bypass";
    const char *data_sys_state = "root_bypass";

    if (euid != 0U) {
        int err1 = 0;
        int rc1 = check_dir_blocked("/data/data", &err1);
        ASSERT_EQ(1, rc1, "/data/data was not blocked with EACCES/EPERM");
        data_data_state = (err1 == EACCES) ? "EACCES" : "EPERM";

        int err2 = 0;
        int rc2 = check_dir_blocked("/data/system", &err2);
        ASSERT_EQ(1, rc2, "/data/system was not blocked with EACCES/EPERM");
        data_sys_state = (err2 == EACCES) ? "EACCES" : "EPERM";
    }

    struct stat st;
    char binder_info[64];
    if (stat("/dev/binder", &st) == 0) {
        unsigned int mode = (unsigned int)(st.st_mode & 0777U);
        snprintf(binder_info, sizeof(binder_info), "present(chr=%d,mode=0%03o)",
                 S_ISCHR(st.st_mode) ? 1 : 0, mode);
    } else if (errno == EACCES || errno == EPERM) {
        snprintf(binder_info, sizeof(binder_info), "restricted(EACCES)");
    } else if (errno == ENOENT) {
        snprintf(binder_info, sizeof(binder_info), "absent(ENOENT)");
    } else {
        snprintf(binder_info, sizeof(binder_info), "stat_err(%d)", errno);
    }

    PASS_WITH_INFO("/data/data=%s, /data/system=%s, /dev/binder=%s",
                   data_data_state, data_sys_state, binder_info);
#else
    if (euid == 0U) {
        PASS_WITH_INFO("/root=skipped(euid=0), /dev/binder=n/a(linux_desktop)");
    }

    int err = 0;
    int rc = check_dir_blocked("/root", &err);
    if (rc == -1 && err == ENOENT) {
        PASS_WITH_INFO("/root=absent(ENOENT), /dev/binder=n/a(linux_desktop)");
    }

    ASSERT_EQ(1, rc, "/root was not blocked with EACCES/EPERM for non-root UID");

    PASS_WITH_INFO("/root=blocked(%s), /dev/binder=n/a(linux_desktop)",
                   (err == EACCES) ? "EACCES" : "EPERM");
#endif
}
