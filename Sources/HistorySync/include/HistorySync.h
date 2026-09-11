#ifndef YILAI_HISTORY_SYNC_H
#define YILAI_HISTORY_SYNC_H
#ifdef __cplusplus
extern "C" {
#endif
/* Caller must close Codex and CCS. Only an explicitly requested operation calls
 * this API. */
char *yilai_sync_history(const char *codex_home, int restore, char **error);
/* Recover a pending history transaction before capturing or writing config/auth.
 * Preserves the current configuration and messages added since the interrupted
 * sync. Returns a JSON report; result/error use yilai_config_free, like sync. */
char *yilai_recover_history(const char *codex_home, char **error);
int yilai_history_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
