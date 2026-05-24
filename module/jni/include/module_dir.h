#pragma once

void dcfg_set_module_dir_fd(int fd);
int dcfg_get_module_dir_fd();
int dcfg_open_module_file(const char *name, int flags, int mode = 0644);
