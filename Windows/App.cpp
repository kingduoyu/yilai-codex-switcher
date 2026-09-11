#include <windows.h>

#include "Platform.h"
#include "resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <memory>
#include <thread>

namespace {
constexpr UINT Done = WM_APP + 1;
constexpr int Api = 1001, Official = 1002, Reset = 1003, Eye = 1004,
              Logs = 1005;
constexpr COLORREF Canvas = RGB(245, 247, 251), Ink = RGB(24, 35, 56),
                   Muted = RGB(111, 123, 144), Blue = RGB(37, 99, 235),
                   Border = RGB(225, 231, 240);
HWND keyBox, windowHandle;
HFONT bodyFont, titleFont, smallFont, buttonFont;
HBRUSH background, whiteBrush;
bool busy = false, showKey = false, failed = false;
std::wstring modeText, statusText = L"准备就绪。填写 Key，选择连接方式即可。";
struct Result {
  bool ok;
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
void refresh() {
  try {
    modeText = app::mode(app::home());
  } catch (...) {
    modeText = L"配置待重置";
  }
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
  text(dc, L"选择连接，继续创作。", {88, 59, 440, 82}, smallFont, Muted);
  rounded(dc, {523, 39, 712, 70}, RGB(234, 240, 250), RGB(234, 240, 250), 30);
  text(dc, modeText, {531, 39, 704, 70}, smallFont, RGB(72, 92, 130),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  rounded(dc, {28, 114, 712, 364}, RGB(255, 255, 255), Border, 20);
  text(dc, L"易来 API Key", {52, 138, 330, 166}, buttonFont, Ink);
  text(dc, L"仅切换到 API 时需要", {424, 138, 688, 166}, smallFont, Muted,
       DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
  rounded(dc, {52, 180, 688, 228}, RGB(255, 255, 255), Border, 10);
  text(dc, L"生图与本地会话历史同步会自动完成", {52, 308, 688, 338}, smallFont,
       Muted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  text(dc,
       failed ? L"暂未完成"
       : busy ? L"正在切换"
              : L"连接状态",
       {32, 385, 132, 410}, smallFont, failed ? RGB(177, 67, 56) : Muted);
  text(dc, statusText, {32, 415, 708, 465}, bodyFont,
       failed ? RGB(157, 55, 46) : Ink, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  text(dc, L"操作前请退出 Codex 和 CC-Switch", {28, 476, 540, 502}, smallFont,
       Muted);
}
void drawButton(const DRAWITEMSTRUCT &item) {
  bool enabled = !(item.itemState & ODS_DISABLED);
  bool down = item.itemState & ODS_SELECTED;
  const bool primary = item.CtlID == Api, quiet = item.CtlID == Reset ||
                                                  item.CtlID == Eye ||
                                                  item.CtlID == Logs;
  COLORREF fill =
      quiet ? ((item.CtlID == Reset || item.CtlID == Logs) ? Canvas
                                                           : RGB(255, 255, 255))
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
  text(item.hDC, label, item.rcItem, quiet ? smallFont : buttonFont,
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
  auto action = id == Api        ? app::Action::Configure
                : id == Official ? app::Action::Official
                                 : app::Action::Cleanup;
  busy = true;
  failed = false;
  statusText =
      id == Reset ? L"正在停用旧配置…" : L"正在配置连接并同步本地历史，请稍候…";
  for (int child : {Api, Official, Reset, Eye, Logs})
    EnableWindow(GetDlgItem(window, child), FALSE);
  EnableWindow(keyBox, FALSE);
  InvalidateRect(window, nullptr, FALSE);
  std::thread([window, action, id, key = std::move(key)] {
    auto result = std::make_unique<Result>();
    result->operation = id;
    try {
      result->message = app::run(action, app::home(), key);
      result->ok = true;
    } catch (const std::exception &error) {
      result->ok = false;
      result->message = fromUtf8(error.what());
    }
    PostMessageW(window, Done, 0, LPARAM(result.release()));
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
    button(window, Eye, L"显示", 628, 190, 48, 28);
    button(window, Api, L"切换到易来 API", 52, 248, 309, 50);
    button(window, Official, L"切换回官方设置", 377, 248, 311, 50);
    button(window, Reset, L"重置配置", 624, 476, 88, 26);
    button(window, Logs, L"查看日志", 624, 382, 88, 28);
    ShowWindow(GetDlgItem(window, Logs), SW_HIDE);
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
      } else if (id == Logs) {
        try {
          auto folder = app::home() / L"yilai-switcher-logs";
          if (INT_PTR(ShellExecuteW(window, L"open", folder.c_str(), nullptr,
                                    nullptr, SW_SHOWNORMAL)) <= 32) {
            statusText += L" 日志目录无法打开，请检查目录权限。";
            InvalidateRect(window, nullptr, FALSE);
          }
        } catch (...) {
          statusText += L" 无法定位日志目录。";
          InvalidateRect(window, nullptr, FALSE);
        }
      } else if (id == Api || id == Official || id == Reset)
        begin(window, id);
    }
    return 0;
  case Done: {
    std::unique_ptr<Result> result(reinterpret_cast<Result *>(l));
    busy = false;
    failed = !result->ok;
    statusText = result->message;
    ShowWindow(GetDlgItem(window, Logs), failed ? SW_SHOW : SW_HIDE);
    for (int id : {Api, Official, Reset, Eye, Logs})
      EnableWindow(GetDlgItem(window, id), TRUE);
    EnableWindow(keyBox, TRUE);
    if (result->ok && result->operation == Api)
      SetWindowTextW(keyBox, L"");
    refresh();
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
  RECT size{0, 0, 740, 520};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&size, style, FALSE);
  HWND window = CreateWindowW(cls.lpszClassName, L"易来 Codex · v3.3.4", style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              size.right - size.left, size.bottom - size.top,
                              nullptr, nullptr, instance, nullptr);
  bool screenshot = argc > 2 && std::wstring(argv[1]) == L"--screenshot";
  ShowWindow(window, screenshot ? SW_SHOWNOACTIVATE : show);
  UpdateWindow(window);
  if (screenshot) {
    if (argc > 3 && std::wstring(argv[3]) == L"--error-state") {
      failed = true;
      statusText =
          L"删除旧登录文件失败：文件正在被占用。请完全退出 Codex 后重试。";
      ShowWindow(GetDlgItem(window, Logs), SW_SHOW);
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
