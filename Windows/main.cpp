#include "config.hpp"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace {

constexpr wchar_t kProductName[] = L"易来 Codex 切换器";
constexpr wchar_t kWindowClass[] = L"YilaiCodexSwitcher.Direct2D.Window";
constexpr int kDesignWidth = 960;
constexpr int kDesignHeight = 650;
constexpr int kEditId = 1001;

template <typename T>
void safeRelease(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

std::wstring exceptionText(const std::exception& exception) {
    const std::string value = exception.what();
    if (value.empty()) return L"发生未知错误";
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

D2D1_COLOR_F color(UINT32 rgb, float alpha = 1.0f) {
    return D2D1::ColorF(rgb, alpha);
}

struct Layout {
    float scale = 1.0f;
    float left = 0;
    float top = 0;
    float width = static_cast<float>(kDesignWidth);
    float height = static_cast<float>(kDesignHeight);

    D2D1_RECT_F rect(float x, float y, float w, float h) const {
        return D2D1::RectF(left + x * scale, top + y * scale, left + (x + w) * scale, top + (y + h) * scale);
    }

    D2D1_POINT_2F point(float x, float y) const {
        return D2D1::Point2F(left + x * scale, top + y * scale);
    }

    D2D1_SIZE_F size(float w, float h) const {
        return D2D1::SizeF(w * scale, h * scale);
    }

    D2D1_POINT_2F toDesign(float x, float y) const {
        return D2D1::Point2F((x - left) / scale, (y - top) / scale);
    }
};

bool contains(const D2D1_RECT_F& rectangle, D2D1_POINT_2F point) {
    return point.x >= rectangle.left && point.x <= rectangle.right && point.y >= rectangle.top && point.y <= rectangle.bottom;
}

enum class HotZone {
    None,
    Eye,
    Primary,
    Restore,
    Minimize,
    Close,
};

class App {
public:
    ~App() { discardGraphics(); }

    bool initialize(HINSTANCE instance) {
        instance_ = instance;
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return false;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_))) return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_)))) return false;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory_)))) return false;
        createTextFormats();

        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        windowClass.lpfnWndProc = &App::windowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        windowClass.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&windowClass)) return false;

        const UINT dpi = GetDpiForSystem();
        const int width = MulDiv(kDesignWidth, dpi, 96);
        const int height = MulDiv(kDesignHeight, dpi, 96);
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        window_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kProductName,
                                  WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                                  x, y, width, height, nullptr, nullptr, instance_, this);
        return window_ != nullptr;
    }

    int run() {
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        CoUninitialize();
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        App* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        } else {
            self = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK editSubclass(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<App*>(data);
        if (message == WM_KEYDOWN && wParam == VK_RETURN) {
            self->activate(HotZone::Primary);
            return 0;
        }
        return DefSubclassProc(control, message, wParam, lParam);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            onCreate();
            return 0;
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCHITTEST:
            return onHitTest(lParam);
        case WM_SIZE:
            onSize();
            return 0;
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            onSize();
            return 0;
        }
        case WM_PAINT:
            onPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (LOWORD(wParam) == kEditId) {
                if (HIWORD(wParam) == EN_CHANGE) {
                    updateKeyState();
                } else if (HIWORD(wParam) == EN_SETFOCUS) {
                    editFocused_ = true;
                    invalidate();
                } else if (HIWORD(wParam) == EN_KILLFOCUS) {
                    editFocused_ = false;
                    invalidate();
                }
            }
            return 0;
        case WM_CTLCOLOREDIT: {
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(24, 38, 57));
            return reinterpret_cast<LRESULT>(editBrush_);
        }
        case WM_MOUSEMOVE:
            onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            hover_ = HotZone::None;
            invalidate();
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = zoneAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (pressed_ != HotZone::None) SetCapture(window_);
            invalidate();
            return 0;
        case WM_LBUTTONUP: {
            const HotZone released = zoneAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const HotZone action = pressed_ == released ? pressed_ : HotZone::None;
            pressed_ = HotZone::None;
            ReleaseCapture();
            if (action != HotZone::None) activate(action);
            invalidate();
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && hover_ != HotZone::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_DESTROY:
            if (editFont_) DeleteObject(editFont_);
            if (editBrush_) DeleteObject(editBrush_);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void onCreate() {
        BOOL dark = FALSE;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        const DWORD corner = 2;
        DwmSetWindowAttribute(window_, 33, &corner, sizeof(corner));
        const COLORREF border = RGB(187, 207, 228);
        DwmSetWindowAttribute(window_, 34, &border, sizeof(border));
        editBrush_ = CreateSolidBrush(RGB(250, 252, 255));
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                                0, 0, 0, 0, window_, reinterpret_cast<HMENU>(kEditId), instance_, nullptr);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"粘贴完整 Key，例如 sk-..."));
        SendMessageW(edit_, EM_SETPASSWORDCHAR, 0x25CF, 0);
        SetWindowSubclass(edit_, &App::editSubclass, 1, reinterpret_cast<DWORD_PTR>(this));
        refreshMode();
        onSize();
    }

    void createTextFormats() {
        createFormat(L"Microsoft YaHei UI", 14.0f, DWRITE_FONT_WEIGHT_REGULAR, &body_);
        createFormat(L"Microsoft YaHei UI", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &medium_);
        createFormat(L"Microsoft YaHei UI", 16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &titleBar_);
        createFormat(L"Microsoft YaHei UI", 31.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &hero_);
        createFormat(L"Microsoft YaHei UI", 17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &button_);
        createFormat(L"Microsoft YaHei UI", 12.5f, DWRITE_FONT_WEIGHT_REGULAR, &small_);
        createFormat(L"Segoe Fluent Icons", 18.0f, DWRITE_FONT_WEIGHT_REGULAR, &icon_);
        button_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        button_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        icon_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        icon_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        for (IDWriteTextFormat* format : {body_, medium_, titleBar_, hero_, small_}) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    void createFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** output) {
        if (FAILED(writeFactory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", output))) {
            throw std::runtime_error("Unable to create DirectWrite format");
        }
    }

    bool createGraphics() {
        if (target_) return true;
        const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        const HRESULT targetResult = d2dFactory_->CreateDCRenderTarget(&properties, &target_);
        if (FAILED(targetResult)) {
            return false;
        }
        target_->SetDpi(96.0f, 96.0f);
        const HRESULT brushResult = target_->CreateSolidColorBrush(color(0x142132), &brush_);
        if (FAILED(brushResult)) {
            return false;
        }
        background_ = loadBitmap(IDR_BACKGROUND);
        logo_ = loadBitmap(IDR_LOGO);
        return true;
    }

    ID2D1Bitmap* loadBitmap(int resourceId) {
        HRSRC resource = FindResourceW(instance_, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
        if (!resource) return nullptr;
        HGLOBAL loaded = LoadResource(instance_, resource);
        const DWORD size = SizeofResource(instance_, resource);
        const auto* bytes = static_cast<BYTE*>(LockResource(loaded));
        if (!bytes || !size) return nullptr;

        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        ID2D1Bitmap* bitmap = nullptr;
        if (SUCCEEDED(wicFactory_->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), size)) &&
            SUCCEEDED(wicFactory_->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(wicFactory_->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                            nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
            target_->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
        }
        safeRelease(converter);
        safeRelease(frame);
        safeRelease(decoder);
        safeRelease(stream);
        return bitmap;
    }

    void discardGraphics() {
        safeRelease(background_);
        safeRelease(logo_);
        safeRelease(brush_);
        safeRelease(target_);
        safeRelease(body_);
        safeRelease(medium_);
        safeRelease(titleBar_);
        safeRelease(hero_);
        safeRelease(button_);
        safeRelease(small_);
        safeRelease(icon_);
        safeRelease(wicFactory_);
        safeRelease(writeFactory_);
        safeRelease(d2dFactory_);
    }

    void onSize() {
        RECT client{};
        GetClientRect(window_, &client);
        const float width = static_cast<float>(client.right);
        const float height = static_cast<float>(client.bottom);
        layout_.scale = std::clamp(std::min(width / kDesignWidth, height / kDesignHeight), 0.75f, 1.25f);
        layout_.width = kDesignWidth * layout_.scale;
        layout_.height = kDesignHeight * layout_.scale;
        layout_.left = (width - layout_.width) / 2.0f;
        layout_.top = (height - layout_.height) / 2.0f;
        updateEditLayout();
        updateWindowRegion();
        invalidate();
    }

    void updateWindowRegion() {
        if (IsZoomed(window_)) {
            SetWindowRgn(window_, nullptr, TRUE);
            return;
        }
        RECT client{};
        GetClientRect(window_, &client);
        const int radius = std::max(14, static_cast<int>(18 * layout_.scale));
        HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, radius, radius);
        SetWindowRgn(window_, region, TRUE);
    }

    void updateEditLayout() {
        if (!edit_) return;
        const auto area = layout_.rect(82, 373, 730, 43);
        MoveWindow(edit_, static_cast<int>(area.left), static_cast<int>(area.top),
                   static_cast<int>(area.right - area.left), static_cast<int>(area.bottom - area.top), TRUE);
        if (editFont_) DeleteObject(editFont_);
        const int height = -static_cast<int>(18.0f * layout_.scale);
        editFont_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Microsoft YaHei UI");
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }

    void onPaint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window_, &paint);
        if (createGraphics()) {
            RECT client{};
            GetClientRect(window_, &client);
            if (SUCCEEDED(target_->BindDC(dc, &client))) draw();
        }
        EndPaint(window_, &paint);
    }

    void draw() {
        target_->BeginDraw();
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->Clear(color(0xE7F0F9));

        if (background_) {
            target_->DrawBitmap(background_, layout_.rect(0, 0, kDesignWidth, kDesignHeight), 0.68f,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        fill(layout_.rect(0, 0, kDesignWidth, kDesignHeight), color(0xF7FBFF, 0.56f));
        fill(layout_.rect(0, 0, kDesignWidth, 66), color(0xFBFDFF, 0.90f));
        fill(layout_.rect(0, 65, kDesignWidth, 1), color(0xFFFFFF, 0.95f));

        if (logo_) target_->DrawBitmap(logo_, layout_.rect(27, 17, 32, 32), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        drawText(kProductName, layout_.rect(72, 10, 380, 46), titleBar_, color(0x17263A));
        drawWindowControls();

        if (logo_) target_->DrawBitmap(logo_, layout_.rect(61, 118, 74, 74), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        drawText(kProductName, layout_.rect(154, 115, 700, 48), hero_, color(0x101A29));
        drawText(L"仅适用于 Codex CLI 与 Codex 桌面版", layout_.rect(156, 164, 670, 30), body_, color(0x465C76));

        drawGlassPanel(layout_.rect(56, 220, 848, 86), 13.0f);
        fillEllipse(layout_.point(84, 263), 7.0f * layout_.scale,
                    mode_ == CodexMode::Yilai ? color(0x0877D9) : color(0x119869));
        drawText(L"连接设置", layout_.rect(104, 239, 115, 48), body_, color(0x3E526B));
        fill(layout_.rect(238, 244, 1, 38), color(0xB8C8D9, 0.85f));
        drawText(L"当前模式", layout_.rect(268, 239, 106, 48), body_, color(0x596E86));
        drawText(modeLabel_.c_str(), layout_.rect(390, 239, 420, 48), medium_, color(0x101A29));

        drawText(L"易来 API Key", layout_.rect(56, 318, 260, 34), medium_, color(0x142338));
        drawShadow(layout_.rect(56, 356, 848, 70), 11.0f, editFocused_ ? 0x0A67D8 : 0x9DB4CA);
        rounded(layout_.rect(56, 354, 848, 70), 11.0f, color(0xFBFDFF, 0.96f),
                editFocused_ ? color(0x0A67D8) : color(0xA6BAD0), editFocused_ ? 2.0f : 1.0f);
        drawIcon(showKey_ ? L"\xE8F5" : L"\xE890", layout_.rect(836, 369, 46, 42), color(0x334A65));

        const bool enabled = hasKey_;
        drawButton(layout_.rect(56, 448, 305, 62), L"切换到易来 API", true, enabled, HotZone::Primary);
        drawButton(layout_.rect(379, 448, 235, 62), L"     切换回官方", false, true, HotZone::Restore);
        drawIcon(L"\xE895", layout_.rect(413, 463, 34, 32), color(0x1E3047));
        drawText(L"Key 仅写入你的 Codex 配置文件，不会上传到其他服务。",
                 layout_.rect(56, 514, 690, 28), small_, color(0x5E738B));

        fill(layout_.rect(0, 555, kDesignWidth, 95), color(0xF8FBFE, 0.90f));
        fill(layout_.rect(0, 555, kDesignWidth, 1), color(0xFFFFFF, 0.95f));
        const UINT32 footerColor = messageError_ ? 0xB3263E : (messageSuccess_ ? 0x0D7F57 : 0x40566F);
        drawIcon(L"\xE83D", layout_.rect(56, 578, 32, 34), color(footerColor));
        drawText(message_.c_str(), layout_.rect(92, 572, 600, 46), body_, color(footerColor));
        drawText(L"Windows 10/11  ·  v3.1.0", layout_.rect(700, 572, 216, 46), small_, color(0x62778F), true);

        const HRESULT result = target_->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) {
            safeRelease(background_);
            safeRelease(logo_);
            safeRelease(brush_);
            safeRelease(target_);
        }
    }

    void drawWindowControls() {
        drawCaptionButton(layout_.rect(856, 7, 46, 51), L"\xE921", HotZone::Minimize);
        drawCaptionButton(layout_.rect(902, 7, 46, 51), L"\xE8BB", HotZone::Close);
    }

    void drawCaptionButton(const D2D1_RECT_F& rectangle, const wchar_t* glyph, HotZone zone) {
        if (hover_ == zone || pressed_ == zone) {
            const UINT32 value = zone == HotZone::Close ? 0xD13438 : 0xDDE9F5;
            fill(rectangle, color(value, pressed_ == zone ? 0.92f : 0.72f));
        }
        drawIcon(glyph, rectangle, color(zone == HotZone::Close && hover_ == zone ? 0xFFFFFF : 0x22364E));
    }

    void drawGlassPanel(const D2D1_RECT_F& rectangle, float radius) {
        drawShadow(rectangle, radius, 0x6D91B5);
        rounded(rectangle, radius, color(0xFFFFFF, 0.66f), color(0x9CB6D1, 0.78f), 1.0f);
        const D2D1_RECT_F highlight{rectangle.left + 1, rectangle.top + 1, rectangle.right - 1, rectangle.top + 2};
        fill(highlight, color(0xFFFFFF, 0.92f));
    }

    void drawShadow(const D2D1_RECT_F& rectangle, float radius, UINT32 tint) {
        for (int i = 5; i >= 1; --i) {
            D2D1_RECT_F shadow = rectangle;
            const float spread = i * layout_.scale;
            shadow.left -= spread;
            shadow.right += spread;
            shadow.top += 2.0f * layout_.scale;
            shadow.bottom += spread + 3.0f * layout_.scale;
            rounded(shadow, radius + i, color(tint, 0.012f * (6 - i)), color(tint, 0.0f), 0.0f);
        }
    }

    void drawButton(const D2D1_RECT_F& rectangle, const wchar_t* label, bool primary, bool enabled,
                    HotZone zone) {
        const bool hover = hover_ == zone;
        const bool pressed = pressed_ == zone;
        UINT32 fillColor = 0xF9FCFF;
        UINT32 borderColor = 0xA3B9D0;
        UINT32 textColor = 0x1E3047;
        if (primary) {
            fillColor = enabled ? (pressed ? 0x0752AD : (hover ? 0x0875E1 : 0x0A67D8)) : 0xA9BED5;
            borderColor = fillColor;
            textColor = 0xFFFFFF;
        } else if (pressed) {
            fillColor = 0xE5EFF9;
        } else if (hover) {
            fillColor = 0xF1F7FD;
            borderColor = 0x7FA5CA;
        }
        drawShadow(rectangle, 10.0f, primary ? 0x0A67D8 : 0x6D91B5);
        rounded(rectangle, 10.0f, color(fillColor, 0.97f), color(borderColor), 1.0f);
        drawText(label, rectangle, button_, color(textColor), false, true);
    }

    void rounded(const D2D1_RECT_F& rectangle, float radius, D2D1_COLOR_F fillColor,
                 D2D1_COLOR_F borderColor, float borderWidth) {
        brush_->SetColor(fillColor);
        target_->FillRoundedRectangle(D2D1::RoundedRect(rectangle, radius * layout_.scale, radius * layout_.scale), brush_);
        if (borderWidth > 0.0f && borderColor.a > 0.0f) {
            brush_->SetColor(borderColor);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle, radius * layout_.scale, radius * layout_.scale),
                                          brush_, borderWidth * layout_.scale);
        }
    }

    void fill(const D2D1_RECT_F& rectangle, D2D1_COLOR_F value) {
        brush_->SetColor(value);
        target_->FillRectangle(rectangle, brush_);
    }

    void fillEllipse(D2D1_POINT_2F center, float radius, D2D1_COLOR_F value) {
        brush_->SetColor(value);
        target_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_);
    }

    void drawText(const wchar_t* text, const D2D1_RECT_F& rectangle, IDWriteTextFormat* format,
                  D2D1_COLOR_F value, bool alignRight = false, bool center = false) {
        const auto oldAlignment = format->GetTextAlignment();
        if (alignRight) format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        else if (center) format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        else format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        brush_->SetColor(value);
        target_->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format, rectangle, brush_,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        format->SetTextAlignment(oldAlignment);
    }

    void drawIcon(const wchar_t* glyph, const D2D1_RECT_F& rectangle, D2D1_COLOR_F value) {
        drawText(glyph, rectangle, icon_, value, false, true);
    }

    LRESULT onHitTest(LPARAM lParam) {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window_, &point);
        const D2D1_POINT_2F design = layout_.toDesign(static_cast<float>(point.x), static_cast<float>(point.y));
        if (design.y >= 0 && design.y < 66 && zoneAt(point.x, point.y) == HotZone::None) return HTCAPTION;
        return HTCLIENT;
    }

    HotZone zoneAt(int x, int y) const {
        const auto point = layout_.toDesign(static_cast<float>(x), static_cast<float>(y));
        if (contains(D2D1::RectF(828, 360, 894, 423), point)) return HotZone::Eye;
        if (contains(D2D1::RectF(56, 448, 361, 510), point) && hasKey_) return HotZone::Primary;
        if (contains(D2D1::RectF(379, 448, 614, 510), point)) return HotZone::Restore;
        if (contains(D2D1::RectF(856, 7, 902, 58), point)) return HotZone::Minimize;
        if (contains(D2D1::RectF(902, 7, 948, 58), point)) return HotZone::Close;
        return HotZone::None;
    }

    void onMouseMove(int x, int y) {
        const HotZone next = zoneAt(x, y);
        if (next != hover_) {
            hover_ = next;
            invalidate();
        }
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
            TrackMouseEvent(&track);
            trackingMouse_ = true;
        }
    }

    void activate(HotZone zone) {
        switch (zone) {
        case HotZone::Eye:
            showKey_ = !showKey_;
            SendMessageW(edit_, EM_SETPASSWORDCHAR, showKey_ ? 0 : 0x25CF, 0);
            InvalidateRect(edit_, nullptr, TRUE);
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            break;
        case HotZone::Primary:
            performSwitch();
            break;
        case HotZone::Restore:
            performRestore();
            break;
        case HotZone::Minimize:
            ShowWindow(window_, SW_MINIMIZE);
            break;
        case HotZone::Close:
            DestroyWindow(window_);
            break;
        default:
            break;
        }
    }

    std::wstring keyText() const {
        const int length = GetWindowTextLengthW(edit_);
        std::wstring value(static_cast<size_t>(length), L'\0');
        if (length > 0) GetWindowTextW(edit_, value.data(), length + 1);
        return value;
    }

    void updateKeyState() {
        hasKey_ = GetWindowTextLengthW(edit_) > 0;
        invalidate();
    }

    void performSwitch() {
        if (!hasKey_) {
            setMessage(L"请先粘贴你在易来创建的完整 API Key。", false, true);
            SetFocus(edit_);
            return;
        }
        try {
            switchToYilai(keyText(), currentPaths());
            SetWindowTextW(edit_, L"");
            setMessage(L"已切换到易来 API，请重新打开 Codex。", true, false);
            refreshMode();
        } catch (const std::exception& exception) {
            setMessage(exceptionText(exception), false, true);
        }
    }

    void performRestore() {
        try {
            switchToOfficial(currentPaths());
            SetWindowTextW(edit_, L"");
            setMessage(L"已切换到 OpenAI 官方配置，请重新打开 Codex。", true, false);
            refreshMode();
        } catch (const std::exception& exception) {
            setMessage(exceptionText(exception), false, true);
        }
    }

    void refreshMode() {
        try {
            mode_ = getMode(currentPaths());
            if (mode_ == CodexMode::Yilai) modeLabel_ = L"易来 API";
            else if (mode_ == CodexMode::Official) modeLabel_ = L"OpenAI 官方";
            else if (mode_ == CodexMode::Other) modeLabel_ = L"其他第三方配置";
            else modeLabel_ = L"尚未配置";
        } catch (const std::exception& exception) {
            mode_ = CodexMode::NotConfigured;
            modeLabel_ = L"读取失败";
            setMessage(exceptionText(exception), false, true);
        }
        invalidate();
    }

    void setMessage(std::wstring value, bool success, bool error) {
        message_ = std::move(value);
        messageSuccess_ = success;
        messageError_ = error;
        invalidate();
    }

    void invalidate() const {
        if (window_) InvalidateRect(window_, nullptr, FALSE);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* writeFactory_ = nullptr;
    IWICImagingFactory* wicFactory_ = nullptr;
    ID2D1DCRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1Bitmap* background_ = nullptr;
    ID2D1Bitmap* logo_ = nullptr;
    IDWriteTextFormat* body_ = nullptr;
    IDWriteTextFormat* medium_ = nullptr;
    IDWriteTextFormat* titleBar_ = nullptr;
    IDWriteTextFormat* hero_ = nullptr;
    IDWriteTextFormat* button_ = nullptr;
    IDWriteTextFormat* small_ = nullptr;
    IDWriteTextFormat* icon_ = nullptr;

    Layout layout_;
    CodexMode mode_ = CodexMode::NotConfigured;
    std::wstring modeLabel_ = L"正在检测";
    std::wstring message_ = L"切换前请完全退出 Codex，完成后重新打开。";
    bool messageSuccess_ = false;
    bool messageError_ = false;
    bool hasKey_ = false;
    bool showKey_ = false;
    bool editFocused_ = false;
    bool trackingMouse_ = false;
    HotZone hover_ = HotZone::None;
    HotZone pressed_ = HotZone::None;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    const bool selfTest = argumentCount > 1 && std::wstring(arguments[1]) == L"--self-test";
    LocalFree(arguments);
    if (selfTest) {
        std::wstring error;
        return runSelfTest(error) ? 0 : 1;
    }
    try {
        App app;
        if (!app.initialize(instance)) return 1;
        return app.run();
    } catch (...) {
        return 1;
    }
}
