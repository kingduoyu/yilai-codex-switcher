#ifndef YILAI_DIAGNOSTICS_H
#define YILAI_DIAGNOSTICS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct YilaiDiagnostic YilaiDiagnostic;
// Error contexts redact credentials before a reason is shown in the UI.
// They deliberately do not create process logs or persist operation details.
YilaiDiagnostic *yilai_diagnostic_begin(const char *home_utf8,
                                        const char *operation,
                                        const char *secret_key);
void yilai_diagnostic_event(YilaiDiagnostic *context, const char *stage,
                            const char *message);
// Kept for ABI compatibility; always returns an empty string.
const char *yilai_diagnostic_path(const YilaiDiagnostic *context);
// malloc-owned sanitized result; release with yilai_config_free or free.
// Works with a null context, but then only generic secret patterns are known.
char *yilai_diagnostic_sanitize(const YilaiDiagnostic *context,
                                const char *message);
// Kept for ABI compatibility; always returns zero.
int yilai_diagnostic_available(const YilaiDiagnostic *context);
// Releases the context and returns zero because no log is created.
int yilai_diagnostic_finish(YilaiDiagnostic *context, int success, const char *message);
// Records the final outcome and releases the context. Never throws.
void yilai_diagnostic_end(YilaiDiagnostic *context, int success,
                          const char *message);
int yilai_diagnostic_self_test(char **error);
#ifdef __cplusplus
}
#endif
#endif
