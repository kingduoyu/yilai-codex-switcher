#ifndef YILAI_HISTORY_REPAIR_H
#define YILAI_HISTORY_REPAIR_H
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit manual action only. Caller holds the operation lock and closes Codex.
 * Result/error are malloc-owned and released with yilai_config_free. */
char *yilai_repair_history(const char *codex_home, char **error);
int yilai_history_repair_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
