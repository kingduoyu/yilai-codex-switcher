#ifndef YILAI_DIAGNOSTICS_H
#define YILAI_DIAGNOSTICS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct YilaiDiagnostic YilaiDiagnostic;
// Events are best-effort. A context can exist with logging disabled; then path
// is empty. No caller-supplied config/auth/history contents should be logged.
YilaiDiagnostic *yilai_diagnostic_begin(const char *home_utf8,
                                        const char *operation,
                                        const char *secret_key);
void yilai_diagnostic_event(YilaiDiagnostic *context, const char *stage,
                            const char *message);
// Borrowed UTF-8 path, valid until end(); empty if the log could not be opened.
const char *yilai_diagnostic_path(const YilaiDiagnostic *context);
// malloc-owned sanitized result; release with yilai_config_free or free.
// Works with a null context, but then only generic secret patterns are known.
char *yilai_diagnostic_sanitize(const YilaiDiagnostic *context,
                                const char *message);
// Whether the log is still writable. No business operation depends on this.
int yilai_diagnostic_available(const YilaiDiagnostic *context);
// Writes final outcome, releases context, and returns whether the log was saved.
int yilai_diagnostic_finish(YilaiDiagnostic *context, int success, const char *message);
// Records the final outcome and releases the context. Never throws.
void yilai_diagnostic_end(YilaiDiagnostic *context, int success,
                          const char *message);
int yilai_diagnostic_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
