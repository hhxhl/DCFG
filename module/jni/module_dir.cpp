// Module directory access helpers.

#include "module_dir.h"

#include <fcntl.h>
#include <unistd.h>

static int g_module_dir_fd = -1;

void dcfg_set_module_dir_fd(int fd) {
    if (g_module_dir_fd >= 0 && g_module_dir_fd != fd) {
        close(g_module_dir_fd);
    }
    g_module_dir_fd = fd;
}

int dcfg_get_module_dir_fd() {
    return g_module_dir_fd;
}

int dcfg_open_module_file(const char *name, int flags, int mode) {
    if (g_module_dir_fd < 0 || !name || !name[0]) return -1;
    return openat(g_module_dir_fd, name, flags | O_CLOEXEC, mode);
}
