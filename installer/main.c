#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <ps4/klog.h>

#include "payload_data.h"

#define INSTALL_DIR      "/data/payloads"
#define INSTALL_PATH     INSTALL_DIR "/ps4-plugins-mgr.elf"
#define INSTALL_TMP_PATH INSTALL_DIR "/ps4-plugins-mgr.elf.tmp"

int sceKernelSendNotificationRequest(int api, char *buffer, size_t size,
                                     int blocking);

#pragma pack(push, 1)
typedef struct SceNotificationRequest {
    uint32_t type;
    uint32_t req_id;
    uint32_t priority;
    uint32_t msg_id;
    uint32_t target_id;
    uint32_t user_id;
    uint32_t device_id;
    uint32_t addressing_user_id;
    uint32_t app_id;
    uint32_t error_number;
    uint32_t attribute;
    uint8_t has_icon;
    union {
        struct {
            char message[0x400];
            char icon_uri[0x800];
        };
        char buffer[0xC03];
    };
} SceNotificationRequest;
#pragma pack(pop)

static void show_notification(const char *message) {
    SceNotificationRequest req;

    memset(&req, 0, sizeof(req));

    req.type = 0;
    req.msg_id = -1;
    req.target_id = -1;
    req.user_id = -1;
    req.device_id = -1;
    req.addressing_user_id = -1;
    req.has_icon = 1;

    strncpy(req.message, message, sizeof(req.message) - 1);
    strncpy(req.icon_uri,
            "cxml://psnotification/tex_default_icon_notification",
            sizeof(req.icon_uri) - 1);

    sceKernelSendNotificationRequest(0, (char *)&req, sizeof(req), 0);
}

static int ensure_install_dir(void) {
    struct stat st;

    if (stat(INSTALL_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }

    if (mkdir(INSTALL_DIR, 0777) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int install_payload(void) {
    FILE *fp;
    size_t written;

    if (ensure_install_dir() != 0) {
        return -1;
    }

    fp = fopen(INSTALL_TMP_PATH, "wb");
    if (!fp) {
        return -1;
    }

    written = fwrite(EMBEDDED_PAYLOAD, 1, EMBEDDED_PAYLOAD_SIZE, fp);

    if (written != EMBEDDED_PAYLOAD_SIZE) {
        fclose(fp);
        unlink(INSTALL_TMP_PATH);
        return -1;
    }

    if (fclose(fp) != 0) {
        unlink(INSTALL_TMP_PATH);
        return -1;
    }

    if (rename(INSTALL_TMP_PATH, INSTALL_PATH) != 0) {
        unlink(INSTALL_TMP_PATH);
        return -1;
    }

    return 0;
}

int main(void) {
    char notification[0x400];

    klog_printf("[plugins-mgr-installer] Starting installer\n");

    if (install_payload() != 0) {
        klog_printf("[plugins-mgr-installer] Installation failed\n");

        snprintf(notification, sizeof(notification),
                 "Plugins Mgr Installer v1.1 (c) Haider A.H installation failed");

        show_notification(notification);
        return 1;
    }

    klog_printf("[plugins-mgr-installer] Installed payload to %s\n",
                INSTALL_PATH);

    snprintf(notification, sizeof(notification),
             "Plugins Mgr Installer v1.1 (c) Haider A.H installed to /data/payloads");

    show_notification(notification);

    return 0;
}
