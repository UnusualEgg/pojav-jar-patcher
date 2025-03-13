#include <errno.h>
#include <fcntl.h>
#include <json.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cwalk.h>
#include <zip.h>
#include <zipint.h>

#include "args.h"
#include <argp.h>

struct jvalue *load_json(const char *filename) {
    char *file_buf = NULL;
    size_t file_len = 0;
    struct jerr err = {0};
    struct jvalue *j = load_filename(filename, &file_buf, &file_len, &err);
    if (!j) {
        print_jerr_str(&err, file_buf);
        perror(filename);
        free(file_buf);
        return NULL;
    }
    free(file_buf);
    return j;
}

const mode_t mode_all = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
int cp(const char *from, const char *to) {
    int from_fd = open(from, O_RDONLY);
    if (from_fd < 0) {
        perror("open");
        return -1;
    }
    const mode_t mode = mode_all;
    int to_fd = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (to_fd < 0) {
        close(from_fd);
        perror("open");
        return -1;
    }
    ssize_t nread = 0;
    ssize_t nwritten = 0;
    const size_t buf_size = 1024;
    uint8_t buf[buf_size];
    while (nread = read(from_fd, buf, buf_size), nread > 0) {
        uint8_t *buf_ptr = buf;
        do {
            nwritten = write(to_fd, buf_ptr, nread);

            if (nwritten >= 0) {
                nread -= nwritten;
                buf_ptr += nwritten;
            } else if (errno != EINTR) {
                int old = errno;
                close(from_fd);
                if (to_fd > 0)
                    close(to_fd);
                errno = old;
                return -1;
            }
        } while (nread > 0);
    }
    if (nread == 0) {
        if (close(to_fd) < 0) {
            int old = errno;
            close(from_fd);
            errno = old;
            return -1;
        }
        close(from_fd);
        return 0;
    }
    int old = errno;
    close(from_fd);
    if (to_fd > 0)
        close(to_fd);
    errno = old;
    return -1;
}

#define retnul(v)                                                                                  \
    if (!v) {                                                                                      \
        fprintf(stderr, "retnull\n");                                                              \
        goto cleanup;                                                                              \
    }

#define assert_type(v, expected_type)                                                              \
    if (v->type != expected_type) {                                                                \
        fprintf(stderr, "assert_type %s but got %s(%x) from %s\n", #expected_type,                 \
                type_to_str(v->type), v->type, #v);                                                \
        goto cleanup;                                                                              \
    }

int open_zip(zip_t **mod_zip, const char *filename, int mode) {
    int errorp = 0;
    *mod_zip = zip_open(filename, mode, &errorp);
    if (!mod_zip) {
        zip_error_t zip_error;
        zip_error_init_with_code(&zip_error, errorp);
        fprintf(stderr, "Failed to open `%s`: %s\n", filename, zip_error_strerror(&zip_error));
        zip_error_fini(&zip_error);
        return -1;
    }
    return 0;
}
int patch_mod(zip_t *out_jar, const char *mod, bool put_in_jar, zip_t **mod_zip,
              struct arg_storage *s) {
    if (s->verbose)
        printf("patching %s\n", mod);
    *mod_zip = NULL;
    if (open_zip(mod_zip, mod, ZIP_RDONLY) == -1) {
        return -1;
    }

    // check if we have a mod_
    int64_t num_of_entries = zip_get_num_entries(*mod_zip, 0);
    if (num_of_entries == -1) {
        fprintf(stderr, "%s is NULL\n", mod);
        return -1;
    }
    if (!put_in_jar) {
        for (int64_t i = 0; i < num_of_entries; i++) {
            const char *name = zip_get_name(*mod_zip, i, 0);
            if (!name) {
                fprintf(stderr, "zip_get_name returned NULL\n");
                zip_close(*mod_zip);
                return -1;
            }
            const char *prefix = "mod_";
            size_t len = strlen(name);
            if (len > 4) {
                if (memcmp(name, prefix, 4) == 0) {
                    put_in_jar = true;
                    break;
                }
            }
        }
    }
    // put it in if needed
    if (put_in_jar) {
        if (s->verbose)
            printf("adding `%s`:\n", mod);
        for (int64_t i = 0; i < num_of_entries; i++) {
            zip_error_t zip_error;
            zip_stat_t *source_stat = malloc(sizeof(zip_stat_t));
            zip_stat_init(source_stat);
            if (zip_stat_index(*mod_zip, i, 0, source_stat) == -1) {
                // zip_error_t *stat_error = zip_get_error(mod_zip);
                if (source_stat->name) {
                    printf("name:%s\n", source_stat->name);
                }
                fprintf(stderr, "Failed to stat idx %li of `%s`: %s also %lx\n", i, mod,
                        zip_error_strerror(&(*mod_zip)->error), source_stat->valid);
                // zip_error_fini(stat_error);
                zip_close(*mod_zip);
                free(source_stat);
                return -1;
            }
            if (s->verbose)
                printf("\t%s\n", source_stat->name);
            zip_source_t *source_file =
                zip_source_zip_file_create(*mod_zip, i, ZIP_FL_COMPRESSED, 0, -1, NULL, &zip_error);
            if (!source_file) {
                fprintf(stderr, "Failed to open `%s`: %s/%s\n", mod, zip_error_strerror(&zip_error),
                        zip_error_strerror(&(*mod_zip)->error));
                zip_source_free(source_file);
                zip_error_fini(&zip_error);
                zip_close(*mod_zip);
                free(source_stat);
                return -1;
            }
            // zip_file_replace
            int64_t new_index =
                zip_file_add(out_jar, source_stat->name, source_file, ZIP_FL_OVERWRITE);
            if (new_index == -1) {
                fprintf(stderr, "Failed to add %s idx %li of `%s`: out_jar:%s\n", source_stat->name,
                        i, mod, zip_error_strerror(&out_jar->error));
                zip_source_free(source_file);
                zip_close(*mod_zip);
                free(source_stat);
                return -1;
            }
            free(source_stat);
        }
    } else {
        // put in mods folder
        if (s->verbose)
            printf("in mods folder\n");
        if (zip_close(*mod_zip) == -1) {
            fprintf(stderr, "zip_close failed %d\n", __LINE__);
            zip_discard(*mod_zip);
            return -1;
        }
        if (mkdir("mods", mode_all) == -1) {
            if (errno != EEXIST) {
                perror("mkdir mods");
                return -1;
            }
        }
        if (cp(mod, "mods") == -1) {
            perror("copy to mods");
            return -1;
        }
    }
    return 0;
}
char *path_join(const char *path1, const char *path2) {
    size_t path_len = strlen(path1);
    size_t buf_len = path_len + 1 + strlen(path2) + 1;
    char *new_path_buf = malloc(buf_len);
    if (!new_path_buf)
        return NULL;
    cwk_path_join(path1, path2, new_path_buf, buf_len);
    return new_path_buf;
}
char *change_extension(const char *path, const char *extension) {
    size_t path_len = strlen(path);
    size_t buf_len = path_len + 1 + strlen(extension) + 1;
    char *new_path_buf = malloc(buf_len);
    if (!new_path_buf)
        return NULL;
    cwk_path_change_extension(path, extension, new_path_buf, buf_len);
    return new_path_buf;
}
int patch_mods(struct jvalue *mods_jarray, zip_t *output_jar, bool check_modloader_mod,
               zip_t **mod_zips, struct arg_storage *s) {
    const size_t *len_ptr = jarray_len(mods_jarray);
    size_t num_mods = *len_ptr;
    // iter thru mods
    for (size_t i = 0; i < num_mods; i++) {
        struct jvalue *mod_jstr = jarray_get(mods_jarray, i);
        if (!mod_jstr || mod_jstr->type != JSTR) {
            fprintf(stderr, "invalid mod\n");
            return EXIT_FAILURE;
        }
        char *mod_filename = path_join("input", jstr_get(mod_jstr));
        if (s->verbose)
            printf("adding %s\n", mod_filename);
        if (patch_mod(output_jar, mod_filename, !check_modloader_mod, &mod_zips[i], s) == -1) {
            fprintf(stderr, "couldn't patch\n");
            free(mod_filename);
            return EXIT_FAILURE;
        }
        free(mod_filename);
    }
    return 0;
}
void remove_meta_inf(zip_t *output_jar) {
    int64_t len = zip_get_num_entries(output_jar, 0);
    for (int64_t i = 0; i < len; i++) {
        const char *name = zip_get_name(output_jar, i, 0);
        if (name && strncmp(name, "META-INF/", 9) == 0) {
            if (zip_delete(output_jar, i) == 0) {
                printf("deleted %s\n", name);
            } else {
                fprintf(stderr, "failed to delete %s\n", name);
            }
        }
    }
}
int patch_version(const char *filename) {

    int status = EXIT_SUCCESS;

    order = load_json(filename);
    if (!order)
        goto cleanup;
    if (order->type != JOBJECT) {
        fprintf(stderr, "Expected config file to be a %s\n", type_to_str(JOBJECT));
        goto cleanup;
    }
cleanup:
    return status;
}
#define assert_key(v, key, t)                                                                      \
    if (!v) {                                                                                      \
        fprintf(stderr, "expected key \"%s\"\n", key);                                             \
        goto cleanup;                                                                              \
    }                                                                                              \
    if (mc_obj->type != JOBJECT) {                                                                 \
        fprintf(stderr, "expected key \"%s\" to be a %s\n", key, type_to_str(t));                  \
        goto cleanup;                                                                              \
    }
int main(int argc, char *argv[]) {
    // args
    struct arg_storage s = {0};
    error_t argp_result = argp_parse(&args_struct, argc, argv, 0, NULL, &s);
    if (argp_result != 0) {
        fprintf(stderr, "argp_parse error:%s\n", strerror(argp_result));
        return EXIT_FAILURE;
    }

    // main
    zip_t *output_jar = NULL;
    size_t mod_zips_len = 0;
    zip_t **mod_zips = NULL;
    size_t any_mod_zips_len = 0;
    zip_t **any_mod_zips = NULL;
    struct jvalue *order = NULL;
    // done with defaults

    int status = EXIT_SUCCESS;

    char *config_file_name = s.config_file ? s.config_file : "order.json";
    order = load_json(config_file_name);
    if (!order)
        goto cleanup;
    if (order->type != JOBJECT) {
        fprintf(stderr, "Expected config file to be a %s\n", type_to_str(JOBJECT));
        goto cleanup;
    }

    // input output jars
    struct jvalue *mc_obj = jobj_get(order, "mc");
    assert_key(mc_obj, "mc", JOBJECT);

    struct jvalue *in_jstr = jobj_get(mc_obj, "in");
    assert_key(in_jstr, "in", JSTR);
    char *in_jar_filename = jstr_get(in_jstr);

    struct jvalue *out_jstr = jobj_get(mc_obj, "out");
    assert_key(out_jstr, "out", JSTR);
    char *out_filename = jstr_get(out_jstr);
    char *out_jar_filename = change_extension(out_filename, "jar");
    retnul(out_jar_filename);

    if (cp(in_jar_filename, out_jar_filename) == -1) {
        perror("copy in to out");
        free(out_jar_filename);
        status = EXIT_FAILURE;
        goto cleanup;
    }

    printf("opening %s\n", out_jar_filename);
    if (open_zip(&output_jar, out_jar_filename, 0) == -1 || !output_jar) {
        free(out_jar_filename);
        status = EXIT_FAILURE;
        goto cleanup;
    }
    free(out_jar_filename);
    // on_exit(exit_zip_close, output_jar);

    // patch using 2 lists
    struct jvalue *mods_in_jar_jarray = jobj_get(order, "in_jar");
    assert_key(mods_in_jar_jarray, "in_jar", JARRAY);

    //------- in_jar
    mod_zips_len = mods_in_jar_jarray->val.array.len;
    mod_zips = malloc(sizeof(zip_t *) * mod_zips_len);
    if (!mod_zips) {
        perror("malloc");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    if (patch_mods(mods_in_jar_jarray, output_jar, false, mod_zips, &s) != 0) {
        fprintf(stderr, "patch mods failed\n");
        status = EXIT_FAILURE;
        goto cleanup;
    }

    //------- any_mods
    struct jvalue *any_mods_jarray = jobj_get(order, "mods");
    assert_key(any_mods_jarray, "mods", JARRAY);

    // mods
    any_mod_zips_len = any_mods_jarray->val.array.len;
    any_mod_zips = malloc(sizeof(zip_t *) * any_mod_zips_len);
    if (!any_mod_zips) {
        perror("malloc");
        status = EXIT_FAILURE;
        goto cleanup;
    }
    if (patch_mods(any_mods_jarray, output_jar, true, mod_zips, &s) != 0) {
        fprintf(stderr, "patch mods failed\n");
        status = EXIT_FAILURE;
        goto cleanup;
    }

    // remove META-INF
    remove_meta_inf(output_jar);

    // now patch version file
    struct jvalue *version_jstr = jobj_get(mc_obj, "version");

    if (version_jstr) {
        if (version_jstr->type != JSTR && version_jstr->type != JNULL) {
            fprintf(stderr, "expected key \"version\" to be a string or null");
            status = EXIT_FAILURE;
        } else if (version_jstr->type == JSTR) {
            // TODO
            patch_version(jstr_get(version_jstr));
        }
    }
cleanup:
    // close output_jar
    if (output_jar) {
        if (zip_close(output_jar) == -1) {
            if (output_jar->error.zip_err == ZIP_ER_INVAL) {
                printf("ZIP_ER_INVAL\n");
            }
            fprintf(stderr, "zip_close err(%p):%s\n", (void *)output_jar, zip_strerror(output_jar));
            zip_discard(output_jar);
            status = EXIT_FAILURE;
        }
    }
    // close mod zips
    for (size_t i = 0; i < mod_zips_len; i++) {
        if (mod_zips[i])
            zip_close(mod_zips[i]);
    }
    free(mod_zips);
    for (size_t i = 0; i < any_mod_zips_len; i++) {
        if (any_mod_zips[i])
            zip_close(any_mod_zips[i]);
    }
    free(any_mod_zips);
    // free order.json
    if (order)
        free_object(order);
    free(s.config_file);

    printf("finished successfuly\n");
    return status;
}
