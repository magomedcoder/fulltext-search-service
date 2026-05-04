#ifndef FULLTEXTSEARCH_C_API_H
#define FULLTEXTSEARCH_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fulltextsearch_engine fulltextsearch_engine;

enum fulltextsearch_result_code {
    FULLTEXTSEARCH_OK = 0,
    FULLTEXTSEARCH_ERR_INVALID = 1,
    FULLTEXTSEARCH_ERR_INDEX_NOT_FOUND = 2,
    FULLTEXTSEARCH_ERR_SEARCH = 3,
    FULLTEXTSEARCH_ERR_OOM = 4,
};

typedef struct fulltextsearch_engine_options {
    const char *storage_path;
    int max_word_length;
    int stemming_enabled;
    const char *stemming_language;
    int dev_mode;
    const char *stop_words_file;
    const char *config_base_path;
} fulltextsearch_engine_options;

typedef struct fulltextsearch_search_params {
    const char *collection_name;
    const char *query;
    int phrase;
    int partial;
    int fuzzy;
    int fuzzy_max_edits;
    int limit;
    int offset;
} fulltextsearch_search_params;

const char *fulltextsearch_version_string(void);

fulltextsearch_engine *fulltextsearch_engine_create(const fulltextsearch_engine_options *opts);

void fulltextsearch_engine_destroy(fulltextsearch_engine *engine);

int fulltextsearch_search_json(
    fulltextsearch_engine *engine,
    const fulltextsearch_search_params *params,
    int max_limit,
    int max_offset,
    char **out_json,
    char *err_buf,
    size_t err_buf_len
);

void fulltextsearch_free_string(char *p);

#ifdef __cplusplus
}
#endif

#endif
