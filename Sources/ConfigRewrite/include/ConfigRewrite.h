#ifndef YILAI_CONFIG_REWRITE_H
#define YILAI_CONFIG_REWRITE_H
#ifdef __cplusplus
extern "C" {
#endif
enum {
  YILAI_ENHANCE = 0,
  YILAI_CONFIGURE = 1,
  YILAI_CLEANUP = 2,
  YILAI_UNIFY_HISTORY = 3
};
char *yilai_apply_config(const char *existing, const char *key, int action,
                         char **error);
void yilai_config_free(char *value);
int yilai_config_mode(const char *text);
int yilai_config_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
