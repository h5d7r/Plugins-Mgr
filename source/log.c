#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ps4/klog.h>
#include "log.h"
#include "fs.h"

void plg_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    FILE *fp;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    klog_printf("[plugins-mgr] %s\n", buf);

    fp = fopen(LOG_PATH, "a");
    if (fp) {
        fprintf(fp, "%s\n", buf);
        fclose(fp);
    }
}
