#ifndef YILAI_CONFIG_REWRITE_H
#define YILAI_CONFIG_REWRITE_H
#ifdef __cplusplus
extern "C" {
#endif
char *yilai_rewrite_config(const char *existing, const char *key,
                           const char *catalog_path, int official, char **error);
void yilai_config_free(char *value);
int yilai_config_mode(const char *text);
int yilai_verify_config(const char *text, const char *key, const char *catalog_path);
int yilai_config_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
