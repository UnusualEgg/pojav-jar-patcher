#include "args.h"
#include <argp.h>
#include <string.h>
error_t parser(int key, char *arg, struct argp_state *state) {
    struct arg_storage *s = state->input;
    switch (key) {
        case 'c': {
            s->config_file = strdup(arg);
            return 0;
        }
        case 'v': {
            s->verbose = true;
            return 0;
        }
        case ARGP_KEY_INIT:
        case ARGP_KEY_END:
        case ARGP_KEY_FINI:
        case ARGP_KEY_SUCCESS:
        case ARGP_KEY_ERROR: {
            return 0;
        }
    }
    return ARGP_ERR_UNKNOWN;
}
static struct argp_option options[] = {
    {
        .name = "verbose",
        .key = 'v',
        .flags = 0,
        .doc = "shows files being moved",
    },
    {
        .name = "config",
        .key = 'c',
        .flags = 0,
        .arg = "config_file",
        .doc = "default is order.json",
    },
    {0},
};
struct argp args_struct = {
    .options = options,
    .doc = "puts mods in mods folder and in the minecraft jar",
    .parser = parser,
};
