#ifndef YILAI_OPERATION_GUARD_H
#define YILAI_OPERATION_GUARD_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct YilaiOperationLock YilaiOperationLock;
/* Locks the whole operation for one Codex home. Error is malloc-owned. */
YilaiOperationLock *yilai_operation_lock(const char *home, char **error);
void yilai_operation_unlock(YilaiOperationLock *lock);
#ifdef __cplusplus
}
#endif
#endif
