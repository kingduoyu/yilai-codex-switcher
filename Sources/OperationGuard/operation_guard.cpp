#include "OperationGuard.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
struct YilaiOperationLock {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~YilaiOperationLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
  int handle = -1;
  ~YilaiOperationLock() { if (handle >= 0) { flock(handle, LOCK_UN); close(handle); } }
#endif
};
extern "C" YilaiOperationLock *yilai_operation_lock(const char *home, char **error) {
  if (error) *error = nullptr;
  try {
    if (!home || !*home) throw std::runtime_error("无法确定配置目录。");
    auto root = fs::weakly_canonical(fs::absolute(fs::u8path(home)));
    fs::create_directories(root);
    auto path = root / ".yilai-operation.lock";
    auto status = fs::symlink_status(path);
    if (fs::is_symlink(status) || (fs::exists(status) && !fs::is_regular_file(status)))
      throw std::runtime_error("操作锁不是普通文件，请检查配置目录。");
    auto lock = std::make_unique<YilaiOperationLock>();
#ifdef _WIN32
    lock->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock->handle == INVALID_HANDLE_VALUE) {
      auto code = GetLastError();
      if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
        throw std::runtime_error("另一个配置器正在操作此目录，请等待完成后重试。");
      throw std::runtime_error("无法锁定配置目录，Windows 错误码 " + std::to_string(code));
    }
#else
    lock->handle = open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock->handle < 0)
      throw std::runtime_error("无法创建操作锁，系统错误码 " + std::to_string(errno));
    struct stat info{};
    if (fstat(lock->handle, &info) != 0 || !S_ISREG(info.st_mode))
      throw std::runtime_error("操作锁不是普通文件。");
    if (flock(lock->handle, LOCK_EX | LOCK_NB) != 0) {
      auto code = errno;
      if (code == EWOULDBLOCK || code == EAGAIN)
        throw std::runtime_error("另一个配置器正在操作此目录，请等待完成后重试。");
      throw std::runtime_error("无法锁定配置目录，系统错误码 " + std::to_string(code));
    }
#endif
    return lock.release();
  } catch (const std::exception &failure) {
    if (error) {
      auto n = std::strlen(failure.what()) + 1;
      *error = static_cast<char *>(std::malloc(n));
      if (*error) std::memcpy(*error, failure.what(), n);
    }
  } catch (...) {}
  return nullptr;
}
extern "C" void yilai_operation_unlock(YilaiOperationLock *lock) { delete lock; }
