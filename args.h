#pragma once

#include <argp.h>
#include <stdbool.h>
extern struct argp args_struct;
struct arg_storage {
    char *config_file;
    bool verbose;
};
