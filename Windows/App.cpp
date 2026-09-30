#include <windows.h>

#include "Platform.h"
#include "Updates.h"
#include "ConfigRewrite.h"
#include "../Sources/Shared/update_protocol.hpp"
#include "resource.h"
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <memory>
#include <thread>
#include <shlobj.h>

namespace {
constexpr UINT Done = WM_APP + 1;
constexpr UINT Progress = WM_APP + 2;
constexpr UINT Checked = WM_APP + 3;
ULONGLONG operationStarted = 0;
std::wstring progressStage;
constexpr int Api = 1001, Official = 1002, Reset = 1003, Eye = 1004, RepairHistory = 1006;
constexpr int Software = 1007, Models = 1008, Notes = 1009;
constexpr int ModelList = 1010, CheckSoftware = 1011, InstallSoftware = 1012;
app::UpdateInfo latest;
bool checkingUpdate = false;
std::wstring updateText = L"软件 v3.4.0";
struct CheckResult {
  app::UpdateInfo info;
  std::wstring error;
};
int currentOperation = 0;
constexpr COLORREF Canvas = RGB(245, 247, 251), Ink = RGB(24, 35, 56),
                   Muted = RGB(111, 123, 144), Blue = RGB(37, 99, 235),
                   Border = RGB(225, 231, 240);
HWND keyBox, windowHandle, modelBox;
std::filesystem::path savedKeyPath() {
  wchar_t path[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) return {};
  return std::filesystem::path(path) / L"YilaiCodexSwitcher" / L"api-key.txt";
}
void loadSavedKey() {
  auto path = savedKeyPath();
  std::ifstream in(path, std::ios::binary);
  if (!in) return;
  std::string value((std::istreambuf_iterator<char>(in)), {});
  int n = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
  std::wstring key(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), key.data(), n);
  SetWindowTextW(keyBox, key.c_str());
}
void saveKey(const std::wstring &key) {
  auto path = savedKeyPath();
  if (path.empty()) return;
  std::filesystem::create_directories(path.parent_path());
  int n = WideCharToMultiByte(CP_UTF8, 0, key.data(), int(key.size()), nullptr, 0, nullptr, nullptr);
  std::string value(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, key.data(), int(key.size()), value.data(), n, nullptr, nullptr);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(value.data(), std::streamsize(value.size()));
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
}
HFONT bodyFont, titleFont, smallFont, buttonFont;
HBRUSH background, whiteBrush;
bool busy = false, showKey = false, failed = false, warning = false;
std::wstring modeText, statusText = L"准备就绪。填写 Key，即可配置 API 和生图。";
struct Result {
  bool ok = false;
  bool warning = false;
  std::wstring message;
  int operation;
};
std::wstring fromUtf8(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}
HFONT font(int size, int weight = FW_NORMAL) {
  return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
}
void text(HDC dc, const std::wstring &value, RECT rect, HFONT f, COLORREF color,
          UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER) {
  auto previous = SelectObject(dc, f);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, value.c_str(), -1, &rect, flags);
  SelectObject(dc, previous);
}
void rounded(HDC dc, RECT r, COLORREF fill, COLORREF stroke, int radius) {
  auto brush = CreateSolidBrush(fill);
  auto pen = CreatePen(PS_SOLID, 1, stroke);
  auto oldBrush = SelectObject(dc, brush);
  auto oldPen = SelectObject(dc, pen);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}
void refreshModels() {
  if (!modelBox) return;
  auto models = updates::catalog(yilai_model_catalog());
  try {
    std::ifstream input(app::home() / L"yilai-model-catalog.json", std::ios::binary);
    if (input) {
      std::string data((std::istreambuf_iterator<char>(input)), {});
      models = updates::catalog(data);
    }
  } catch (...) { }
  SendMessageW(modelBox, CB_RESETCONTENT, 0, 0);
  const auto summary = L"当前模型列表（" + std::to_wstring(models["models"].size()) + L"）";
  SendMessageW(modelBox, CB_ADDSTRING, 0, LPARAM(summary.c_str()));
  for (const auto &model : models["models"]) {
    const auto label = fromUtf8(model.at("display_name").get<std::string>() + "  " + model.at("slug").get<std::string>());
    SendMessageW(modelBox, CB_ADDSTRING, 0, LPARAM(label.c_str()));
  }
  SendMessageW(modelBox, CB_SETCURSEL, 0, 0);
}
void refresh() {
  try {
    modeText = app::mode(app::home());
  } catch (...) {
    modeText = L"配置待重置";
  }
  if (windowHandle)
    refreshModels();
  if (windowHandle)
    InvalidateRect(windowHandle, nullptr, FALSE);
}
HWND button(HWND parent, int id, const wchar_t *label, int x, int y, int w,
            int h) {
  return CreateWindowW(L"BUTTON", label,
                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y,
                       w, h, parent, HMENU(INT_PTR(id)), nullptr, nullptr);
}
void paint(HWND window, HDC dc) {
  RECT r;
  GetClientRect(window, &r);
  FillRect(dc, &r, background);
  rounded(dc, {28, 28, 74, 74}, Blue, Blue, 14);
  text(dc, L"Y", {28, 28, 74, 74}, titleFont, RGB(255, 255, 255),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  text(dc, L"易来 Codex", {88, 22, 440, 58}, titleFont, Ink);
  text(dc, L"配置 API，继续创作。", {88, 59, 440, 82}, smallFont, Muted);
  text(dc, updateText, {492, 65, 712, 88}, smallFont, Muted,
       DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  rounded(dc, {28, 114, 712, 410}, RGB(255, 255, 255), Border, 20);
  text(dc, L"易来 API Key", {52, 138, 330, 166}, buttonFont, Ink);
  text(dc, modeText, {424, 138, 688, 166}, smallFont, Muted,
       DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
  rounded(dc, {52, 180, 688, 228}, RGB(255, 255, 255), Border, 10);
  text(dc, L"模型目录", {52, 242, 135, 274}, smallFont, Muted);
  text(dc, L"API 自动启用生图 · 官方使用 ChatGPT 登录", {52, 350, 688, 380}, smallFont,
       Muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  const auto statusColor = failed ? RGB(157, 55, 46)
                           : warning ? RGB(161, 98, 7)
                                     : Ink;
  text(dc, failed ? (currentOperation == RepairHistory ? L"历史修复失败" : L"配置失败")
                  : warning ? (currentOperation == RepairHistory ? L"历史修复需要处理" : L"配置已保留，需要处理")
                  : busy    ? (currentOperation == RepairHistory ? L"正在修复历史" : L"正在切换")
                            : L"连接状态",
       {32, 451, 260, 476}, smallFont,
       failed ? RGB(177, 67, 56) : warning ? RGB(161, 98, 7) : Muted);
  text(dc, statusText, {32, 481, 708, 566}, bodyFont, statusColor,
       DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  text(dc, L"操作前请退出 Codex 和 CC-Switch", {28, 582, 420, 608}, smallFont,
       Muted);
}
void drawButton(const DRAWITEMSTRUCT &item) {
  bool enabled = !(item.itemState & ODS_DISABLED);
  bool down = item.itemState & ODS_SELECTED;
  const bool primary = item.CtlID == Api,
             quiet = item.CtlID == Reset || item.CtlID == RepairHistory || item.CtlID == Eye || item.CtlID == Software;
  COLORREF fill =
      quiet ? (item.CtlID == Eye ? RGB(255, 255, 255) : Canvas)
      : primary
          ? (enabled ? down ? RGB(29, 78, 216) : Blue : RGB(161, 184, 234))
      : down ? RGB(239, 243, 249)
             : RGB(255, 255, 255);
  rounded(item.hDC, item.rcItem, fill,
          quiet     ? fill
          : primary ? fill
                    : Border,
          12);
  wchar_t label[100]{};
  GetWindowTextW(item.hwndItem, label, 100);
  auto labelRect = item.rcItem;
  if (item.CtlID == Software && latest.available) {
    auto brush = CreateSolidBrush(RGB(234, 179, 8));
    auto old = SelectObject(item.hDC, brush);
    auto oldPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
    Ellipse(item.hDC, item.rcItem.left + 8, item.rcItem.top + 13, item.rcItem.left + 16, item.rcItem.top + 21);
    SelectObject(item.hDC, oldPen); SelectObject(item.hDC, old); DeleteObject(brush);
    labelRect.left += 22;
  }
  text(item.hDC, label, labelRect, quiet ? smallFont : buttonFont,
       enabled ? (primary ? RGB(255, 255, 255)
                  : quiet ? Muted
                          : Ink)
               : RGB(158, 170, 190),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (item.itemState & ODS_FOCUS) {
    auto focus = item.rcItem;
    InflateRect(&focus, -4, -4);
    DrawFocusRect(item.hDC, &focus);
  }
}
void begin(HWND window, int id) {
  if (busy)
    return;
  currentOperation = id;
  const int length = GetWindowTextLengthW(keyBox);
  std::wstring key(size_t(length) + 1, L'\0');
  GetWindowTextW(keyBox, key.data(), length + 1);
  key.resize(length);
  if (id == Api && key.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
    failed = true;
    statusText = L"请先粘贴易来 API Key，再点击切换。";
    SetFocus(keyBox);
    InvalidateRect(window, nullptr, FALSE);
    return;
  }
  if (id == Api) saveKey(key);
  auto action = id == Api ? app::Action::Configure : id == Official ? app::Action::Official : id == Models ? app::Action::UpdateModels : id == RepairHistory ? app::Action::RepairHistory : app::Action::Cleanup;
  busy = true;
  operationStarted = GetTickCount64();
  progressStage = id == RepairHistory ? L"正在修复旧易来对话" : L"准备操作";
  SetTimer(window, 1, 1000, nullptr);
  failed = false;
  warning = false;
  statusText =
      id == InstallSoftware ? L"正在下载并校验软件更新…" : id == Models ? L"正在更新模型目录…" : id == RepairHistory ? L"正在修复旧易来对话，请稍候…" : id == Reset ? L"正在停用旧配置…" : id == Official ? L"正在切换官方…" : L"正在配置 API 和生图，请稍候…";
  progressStage = statusText;
  for (int child : {Api, Official, Reset, Eye, RepairHistory, Software, Models, ModelList})
    EnableWindow(GetDlgItem(window, child), FALSE);
  EnableWindow(keyBox, FALSE);
  InvalidateRect(window, nullptr, FALSE);
  const auto update = latest;
  std::thread([window, action, id, update, key = std::move(key)] {
    auto result = std::make_unique<Result>();
    result->operation = id;
    try {
      if (id == InstallSoftware) {
        app::installSoftwareUpdate(update);
        result->message = L"更新已校验，正在重启配置器。";
        result->ok = true;
        if (PostMessageW(window, Done, 0, LPARAM(result.get()))) result.release();
        return;
      }
      auto outcome = app::run(action, app::home(), key, true, {}, [window](const std::wstring &stage) {
        auto value = std::make_unique<std::wstring>(stage);
        if (PostMessageW(window, Progress, 0, LPARAM(value.get()))) value.release();
      });
      result->message = std::move(outcome.message);
      result->warning = outcome.warning;
      result->ok = true;
    } catch (const std::exception &error) {
      result->ok = false;
      result->message = fromUtf8(error.what());
    }
    if (PostMessageW(window, Done, 0, LPARAM(result.get()))) result.release();
  }).detach();
}
void checkUpdates(HWND window) {
  if (checkingUpdate || busy) return;
  checkingUpdate = true;
  updateText = L"正在检查软件更新…";
  EnableWindow(GetDlgItem(window, Software), FALSE);
  InvalidateRect(window, nullptr, FALSE);
  std::thread([window] {
    auto result = std::make_unique<CheckResult>();
    try { result->info = app::checkSoftwareUpdate(); }
    catch (const std::exception &) { result->error = L"检查更新失败，点击重试"; }
    if (PostMessageW(window, Checked, 0, LPARAM(result.get()))) result.release();
  }).detach();
}
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
  case WM_CREATE: {
    windowHandle = window;
    bodyFont = font(15);
    titleFont = font(27, FW_SEMIBOLD);
    smallFont = font(13);
    buttonFont = font(16, FW_MEDIUM);
    keyBox = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 66,
        193, 552, 26, window, nullptr, nullptr, nullptr);
    SendMessageW(keyBox, WM_SETFONT, WPARAM(bodyFont), TRUE);
    SendMessageW(keyBox, EM_SETPASSWORDCHAR, L'●', 0);
    SendMessageW(keyBox, EM_SETCUEBANNER, FALSE, LPARAM(L"粘贴你的 API Key"));
    loadSavedKey();
    button(window, Eye, L"显示", 628, 190, 48, 28);
    modelBox = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        140, 244, 410, 244, window, HMENU(INT_PTR(ModelList)), nullptr, nullptr);
    SendMessageW(modelBox, WM_SETFONT, WPARAM(bodyFont), TRUE);
    SendMessageW(modelBox, CB_SETDROPPEDWIDTH, 548, 0);
    button(window, Models, L"更新模型", 564, 242, 124, 34);
    button(window, Api, L"配置易来 API · 启用生图", 52, 294, 310, 50);
    button(window, Official, L"切换到官方", 378, 294, 310, 50);
    button(window, Reset, L"重置配置", 624, 582, 88, 26);
    button(window, RepairHistory, L"修复旧易来对话", 464, 582, 148, 26);
    button(window, Software, L"版本 v3.4.0", 492, 28, 220, 34);
    const auto previous = app::previousUpdateResult();
    if (!previous.empty()) statusText = previous;
    refresh();
    return 0;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    paint(window, dc);
    EndPaint(window, &ps);
    return 0;
  }
  case WM_PRINTCLIENT:
    paint(window, HDC(w));
    return 0;
  case WM_DRAWITEM:
    drawButton(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
    return TRUE;
  case WM_ERASEBKGND:
    return 1;
  case WM_CTLCOLOREDIT:
    SetTextColor(HDC(w), Ink);
    SetBkColor(HDC(w), RGB(255, 255, 255));
    return LRESULT(whiteBrush);
  case WM_COMMAND:
    if (HIWORD(w) == BN_CLICKED) {
      int id = LOWORD(w);
      if (id == Eye) {
        showKey = !showKey;
        SendMessageW(keyBox, EM_SETPASSWORDCHAR, showKey ? 0 : L'●', 0);
        SetWindowTextW(GetDlgItem(window, Eye), showKey ? L"隐藏" : L"显示");
        InvalidateRect(keyBox, nullptr, TRUE);
      } else if (id == Api || id == Official || id == Reset || id == RepairHistory)
        begin(window, id);
      else if (id == Models) begin(window, id);
      else if (id == Software) {
        if (!busy && !checkingUpdate) {
          HMENU menu = CreatePopupMenu();
          AppendMenuW(menu, MF_STRING, CheckSoftware, L"检查更新");
          if (latest.available) {
            AppendMenuW(menu, MF_STRING, Notes, L"更新说明");
            AppendMenuW(menu, MF_STRING, InstallSoftware, L"一键更新");
          }
          RECT rect; GetWindowRect(GetDlgItem(window, Software), &rect);
          const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN,
                                               rect.right, rect.bottom, 0, window, nullptr);
          DestroyMenu(menu);
          if (selected) SendMessageW(window, WM_COMMAND, selected, 0);
        }
      } else if (id == CheckSoftware) { checkUpdates(window);
      } else if (id == InstallSoftware) {
        if (latest.available) begin(window, id);
      } else if (id == Notes) {
        MessageBoxW(window, latest.notes.empty() ? L"此版本未提供更新说明。" : latest.notes.c_str(),
                    latest.version.c_str(), MB_OK | MB_ICONINFORMATION);
      }
    }
    return 0;
  case Progress: {
    std::unique_ptr<std::wstring> stage(reinterpret_cast<std::wstring *>(l));
    progressStage = *stage;
    SendMessageW(window, WM_TIMER, 1, 0);
    return 0;
  }
  case WM_TIMER:
    if (w == 1 && busy) {
      statusText = progressStage + L" · 已用 " + std::to_wstring((GetTickCount64() - operationStarted) / 1000) + L" 秒";
      InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  case Done: {
    std::unique_ptr<Result> result(reinterpret_cast<Result *>(l));
    busy = false;
    KillTimer(window, 1);
    failed = !result->ok;
    warning = result->ok && result->warning;
    statusText = result->message;
    if (result->ok && result->operation == InstallSoftware) {
      DestroyWindow(window);
      return 0;
    }
    for (int id : {Api, Official, Reset, Eye, RepairHistory, Models, Software, ModelList})
      EnableWindow(GetDlgItem(window, id), TRUE);
    EnableWindow(GetDlgItem(window, Software), !checkingUpdate);
    EnableWindow(keyBox, TRUE);
    if (result->ok && result->operation == Api)
      SetWindowTextW(keyBox, L"");
    refresh();
    return 0;
  }
  case Checked: {
    std::unique_ptr<CheckResult> result(reinterpret_cast<CheckResult *>(l));
    checkingUpdate = false;
    if (result->error.empty()) {
      latest = std::move(result->info);
      updateText = latest.available ? L"当前 v3.4.0" : L"已是最新版本";
      const auto label = latest.available ? L"有新版本 " + latest.version : L"版本 v3.4.0";
      SetWindowTextW(GetDlgItem(window, Software), label.c_str());
    } else { updateText = result->error; }
    EnableWindow(GetDlgItem(window, Software), !busy);
    InvalidateRect(window, nullptr, FALSE);
    return 0;
  }
  case WM_CLOSE:
    if (!busy)
      DestroyWindow(window);
    return 0;
  case WM_QUERYENDSESSION:
    return busy ? FALSE : TRUE;
  case WM_DESTROY:
    DeleteObject(bodyFont);
    DeleteObject(titleFont);
    DeleteObject(smallFont);
    DeleteObject(buttonFont);
    DeleteObject(background);
    DeleteObject(whiteBrush);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
void capture(HWND window, const wchar_t *filename) {
  RECT r;
  GetClientRect(window, &r);
  HDC dc = GetDC(window);
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP image = CreateCompatibleBitmap(dc, r.right, r.bottom);
  auto prior = SelectObject(mem, image);
  FillRect(mem, &r, background);
  paint(window, mem);
  struct CaptureChildren {
    HWND parent;
    HDC dc;
  } context{window, mem};
  EnumChildWindows(
      window,
      [](HWND child, LPARAM data) -> BOOL {
        auto &context = *reinterpret_cast<CaptureChildren *>(data);
        if (GetParent(child) != context.parent || !IsWindowVisible(child))
          return TRUE;
        RECT childRect;
        GetWindowRect(child, &childRect);
        MapWindowPoints(nullptr, context.parent,
                        reinterpret_cast<POINT *>(&childRect), 2);
        int saved = SaveDC(context.dc);
        SetViewportOrgEx(context.dc, childRect.left, childRect.top, nullptr);
        IntersectClipRect(context.dc, 0, 0, childRect.right - childRect.left,
                          childRect.bottom - childRect.top);
        SendMessageW(child, WM_PRINT, WPARAM(context.dc),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND |
                         PRF_CHILDREN);
        RestoreDC(context.dc, saved);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&context));
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
  Microsoft::WRL::ComPtr<IWICStream> stream;
  Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
  Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
  auto ok = [](HRESULT result) {
    if (FAILED(result))
      throw std::runtime_error("UI capture failed");
  };
  ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                      IID_PPV_ARGS(factory.GetAddressOf())));
  ok(factory->CreateBitmapFromHBITMAP(image, nullptr, WICBitmapUseAlpha,
                                      bitmap.GetAddressOf()));
  ok(factory->CreateStream(stream.GetAddressOf()));
  ok(stream->InitializeFromFilename(filename, GENERIC_WRITE));
  ok(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                            encoder.GetAddressOf()));
  ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
  ok(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
  ok(frame->Initialize(nullptr));
  ok(frame->WriteSource(bitmap.Get(), nullptr));
  ok(frame->Commit());
  ok(encoder->Commit());
  SelectObject(mem, prior);
  DeleteObject(image);
  DeleteDC(mem);
  ReleaseDC(window, dc);
}

} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc > 1 && std::wstring(argv[1]) == L"--self-test") {
    std::wstring error;
    bool passed = app::selfTest(error);
    if (!passed) {
      std::ofstream out("self-test-error.txt");
      for (auto c : error)
        out << (c < 128 ? char(c) : '?');
    }
    LocalFree(argv);
    return passed ? 0 : 1;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  SetProcessDPIAware();
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);
  // Load the system CJK UI family for hosts where it is installed but not
  // registered.
  wchar_t windows[MAX_PATH]{};
  GetWindowsDirectoryW(windows, MAX_PATH);
  AddFontResourceExW((std::wstring(windows) + L"\\Fonts\\msyh.ttc").c_str(),
                     FR_PRIVATE, nullptr);
  background = CreateSolidBrush(Canvas);
  whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
  WNDCLASSW cls{};
  cls.hInstance = instance;
  cls.lpszClassName = L"YilaiSwitcherV333";
  cls.lpfnWndProc = procedure;
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  cls.hbrBackground = background;
  RegisterClassW(&cls);
  RECT size{0, 0, 740, 626};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&size, style, FALSE);
  HWND window = CreateWindowW(cls.lpszClassName, L"易来 Codex · v3.4.0", style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              size.right - size.left, size.bottom - size.top,
                              nullptr, nullptr, instance, nullptr);
  bool screenshot = argc > 2 && std::wstring(argv[1]) == L"--screenshot";
  ShowWindow(window, screenshot ? SW_SHOWNOACTIVATE : show);
  UpdateWindow(window);
  if (screenshot) {
    if (argc > 3 && std::wstring(argv[3]) == L"--update-state") {
      latest.available = true;
      latest.version = L"v3.4.1";
      SetWindowTextW(GetDlgItem(window, Software), L"有新版本 v3.4.1");
      updateText = L"当前 v3.4.0";
    } else if (argc > 3 && std::wstring(argv[3]) == L"--error-state") {
      failed = true;
      statusText =
          L"删除旧登录文件失败：文件正在被占用。请完全退出 Codex 后重试。";
    } else if (argc > 3 && std::wstring(argv[3]) == L"--warning-state") {
      warning = true;
      statusText = L"API 已配置并保留，但功能探测未完全通过：生图开关未生效（来源：项目 .codex/config.toml）。不会回滚连接，请截图此提示。";
    }
    try {
      capture(window, argv[2]);
      DestroyWindow(window);
      LocalFree(argv);
      CoUninitialize();
      return 0;
    } catch (...) {
      LocalFree(argv);
      return 1;
    }
  }
  if (!GetEnvironmentVariableW(L"YILAI_SKIP_UPDATE_CHECK", nullptr, 0)) checkUpdates(window);
  LocalFree(argv);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  CoUninitialize();
  return 0;
}
