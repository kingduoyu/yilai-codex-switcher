#ifndef YILAI_CONFIG_SOURCES_H
#define YILAI_CONFIG_SOURCES_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct YilaiConfigSources YilaiConfigSources;
/* Caller holds the whole-home operation lock and has closed Codex/CCS.
 * runtime may be empty to locate an installed runtime. All errors/results are
 * malloc-owned. No full runtime response or credentials are returned as logs. */
YilaiConfigSources *yilai_sources_prepare(const char *home, const char *runtime, char **error);
char *yilai_sources_summary(const YilaiConfigSources *context);
int yilai_sources_apply(YilaiConfigSources *context, char **error);
int yilai_sources_verify(YilaiConfigSources *context, int official, const char *key, char **error);
int yilai_sources_rollback(YilaiConfigSources *context, char **error);
/* Finishes successful work or releases an already rolled-back context. */
void yilai_sources_finish(YilaiConfigSources *context);
#ifdef __cplusplus
}
#endif
#endif
