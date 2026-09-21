/* Plugins Mgr payload: web UI on port 4002 for GoldHEN plugins. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <ps4/klog.h>

#include "log.h"
#include "fs.h"
#include "server.h"

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

static void get_device_ip(char *out, size_t outsz) {
    struct ifaddrs *iflist;
    struct ifaddrs *ifa;

    if (!out || outsz == 0) {
        return;
    }

    strncpy(out, "0.0.0.0", outsz - 1);
    out[outsz - 1] = '\0';

    if (getifaddrs(&iflist) != 0) {
        return;
    }

    for (ifa = iflist; ifa != NULL; ifa = ifa->ifa_next) {
        struct sockaddr_in *addr;

        if (!ifa->ifa_addr ||
            ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        addr = (struct sockaddr_in *)ifa->ifa_addr;

        if (ntohl(addr->sin_addr.s_addr) == INADDR_LOOPBACK) {
            continue;
        }

        if (inet_ntop(AF_INET, &addr->sin_addr, out, outsz) != NULL) {
            break;
        }
    }

    freeifaddrs(iflist);
}

int main(void) {
    char device_ip[INET_ADDRSTRLEN];
    char notification[0x400];

    if (plg_ensure_dir(MGR_DIR) != 0) {
        klog_printf("[plugins-mgr] Warning: could not create %s\n",
                    MGR_DIR);
    }

    plg_ensure_dir(PLUGINS_DIR);

    plg_log("Payload started (Plugins Mgr, port %d)", SERVER_PORT);

    get_device_ip(device_ip, sizeof(device_ip));

    snprintf(notification, sizeof(notification),
             "plugins mgr v1.0 (c) Haider A.H Listening on %s:%d",
             device_ip, SERVER_PORT);

    show_notification(notification);

    if (server_run() != 0) {
        plg_log("Payload exiting due to server error");

        show_notification(
            "plugins mgr v1.0 (c) Haider A.H Server stopped with error");

        return 1;
    }

    plg_log("Payload exiting normally");

    show_notification(
        "plugins mgr v1.0 (c) Haider A.H Server stopped");

    return 0;
}
