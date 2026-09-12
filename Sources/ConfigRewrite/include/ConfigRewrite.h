#ifndef YILAI_CONFIG_REWRITE_H
#define YILAI_CONFIG_REWRITE_H
#ifdef __cplusplus
extern "C" {
#endif
char *yilai_configure_api(const char *existing, const char *key, char **error);
char *yilai_configure_official(const char *existing, char **error);
/* Backfill the desktop's saved full-access mode only when the configuration
 * has no explicit approval or sandbox policy. */
char *yilai_apply_desktop_mode(const char *existing, const char *mode,
                               char **error);
/* Pure rewrite for a caller-confirmed effective override layer; malloc-owned. */
char *yilai_clear_connection_overrides(const char *text, char **error);
/* Static catalog bytes; do not free. */
const char *yilai_model_catalog(void);
char *yilai_configure_catalog(const char *configured, const char *path, char **error);
void yilai_config_free(char *value);
int yilai_config_mode(const char *text);
int yilai_config_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
