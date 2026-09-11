#include "Platform.h"
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <thread>
#include <memory>
#include <iostream>
#include <fstream>
namespace {
constexpr UINT Done=WM_APP+1;
constexpr int Images=1001,Configure=1002,Sync=1003,Undo=1004,Cleanup=1005,Eye=1006;
HWND keyBox,modeLabel,resultBox;HFONT bodyFont,titleFont;HBRUSH background;bool busy=false,showKey=false;
struct Result{bool ok;std::wstring message;int operation;};
std::wstring fromUtf8(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),int(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),int(s.size()),out.data(),n);return out;}
void refresh(){try{SetWindowTextW(modeLabel,(L"当前连接："+app::mode(app::home())).c_str());}catch(...){SetWindowTextW(modeLabel,L"当前连接：配置需要检查");}}
HWND control(HWND parent,LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int w,int h,int id=0){HWND child=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,HMENU(INT_PTR(id)),nullptr,nullptr);SendMessageW(child,WM_SETFONT,WPARAM(bodyFont),TRUE);return child;}
void begin(HWND window,int id){
    if(busy)return;
    if(id==Cleanup&&MessageBoxW(window,L"仅用于易来连接的旧登录干扰。旧登录和本工具旧备份会移入回收站，旧固定模型目录会解除引用。",L"清理旧登录",MB_OKCANCEL|MB_ICONWARNING|MB_DEFBUTTON2)!=IDOK)return;
    if(id==Sync&&MessageBoxW(window,L"将当前 Codex 目录内全部本地会话统一为 custom 分类。先自动备份，再更新会话和索引；登录、消息内容保持不变。\n\n若使用 CCS 切换官方，请同时开启 CCS 的“统一 Codex 会话历史”。",L"同步全部本地历史",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return;
    if(id==Undo&&MessageBoxW(window,L"按上次同步备份还原会话归属。保留同步后新增的消息和会话；已被其他工具改过的连接配置不覆盖。",L"撤销上次同步",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return;
    int length=GetWindowTextLengthW(keyBox);std::wstring key(size_t(length)+1,L'\0');GetWindowTextW(keyBox,key.data(),length+1);key.resize(length);
    auto action=id==Images?app::Action::Images:id==Configure?app::Action::Configure:id==Sync?app::Action::Sync:id==Undo?app::Action::Undo:app::Action::Cleanup;
    busy=true;for(int button:{Images,Configure,Sync,Undo,Cleanup})EnableWindow(GetDlgItem(window,button),FALSE);
    SetWindowTextW(resultBox,L"正在处理，请勿启动 Codex 或 CCS。历史较多时请等待完成。");
    std::thread([window,key=std::move(key),action,id](){auto result=std::make_unique<Result>();result->operation=id;try{result->message=app::run(action,app::home(),key);result->ok=true;}catch(const std::exception& e){result->ok=false;result->message=fromUtf8(e.what());}PostMessageW(window,Done,0,LPARAM(result.release()));}).detach();
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        bodyFont=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        titleFont=CreateFontW(-28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        auto title=control(window,L"STATIC",L"易来 Codex 配置器",SS_LEFT,32,24,760,44);SendMessageW(title,WM_SETFONT,WPARAM(titleFont),TRUE);
        control(window,L"STATIC",L"CCS 配置增强 · 本地历史统一     v3.3.0",SS_LEFT,32,72,800,32);
        modeLabel=control(window,L"STATIC",L"当前连接",SS_LEFT,32,114,800,30);
        control(window,L"BUTTON",L"启用生图 · 保留现有模型和登录",BS_DEFPUSHBUTTON|WS_TABSTOP,32,155,800,50,Images);
        control(window,L"STATIC",L"易来 API Key（仅配置连接时填写；启用生图无需填写）",SS_LEFT,32,228,800,28);
        keyBox=control(window,L"EDIT",L"",ES_AUTOHSCROLL|ES_PASSWORD|WS_TABSTOP,32,265,532,38);SendMessageW(keyBox,EM_SETPASSWORDCHAR,L'●',0);
        control(window,L"BUTTON",L"显示",BS_PUSHBUTTON|WS_TABSTOP,574,265,64,38,Eye);
        control(window,L"BUTTON",L"配置易来连接",BS_PUSHBUTTON|WS_TABSTOP,650,265,182,38,Configure);
        control(window,L"BUTTON",L"同步全部本地历史",BS_PUSHBUTTON|WS_TABSTOP,32,333,258,46,Sync);
        control(window,L"BUTTON",L"撤销上次同步",BS_PUSHBUTTON|WS_TABSTOP,304,333,258,46,Undo);
        control(window,L"BUTTON",L"清理旧登录",BS_PUSHBUTTON|WS_TABSTOP,576,333,256,46,Cleanup);
        control(window,L"STATIC",L"操作前完全退出 Codex 和 CC-Switch。切回官方请使用 CCS。",SS_LEFT,32,399,800,30);
        control(window,L"STATIC",L"历史同步只处理本机已有记录，自动备份；不会下载其他账号的云端历史。",SS_LEFT,32,436,800,48);
        resultBox=control(window,L"EDIT",L"准备就绪。默认启用生图不会改写模型目录或清理登录。",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,32,496,800,98);
        refresh();return 0;}
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){int id=LOWORD(w);if(id==Eye){showKey=!showKey;SendMessageW(keyBox,EM_SETPASSWORDCHAR,showKey?0:L'●',0);InvalidateRect(keyBox,nullptr,TRUE);SetWindowTextW(GetDlgItem(window,Eye),showKey?L"隐藏":L"显示");}else if(id>=Images&&id<=Cleanup)begin(window,id);}return 0;
    case Done:{std::unique_ptr<Result> result(reinterpret_cast<Result*>(l));busy=false;for(int id:{Images,Configure,Sync,Undo,Cleanup})EnableWindow(GetDlgItem(window,id),TRUE);SetWindowTextW(resultBox,result->message.c_str());if(result->ok&&result->operation==Configure)SetWindowTextW(keyBox,L"");refresh();return 0;}
    case WM_CTLCOLORSTATIC:SetTextColor(HDC(w),RGB(30,48,70));SetBkColor(HDC(w),RGB(248,250,253));return LRESULT(background);
    case WM_CLOSE:if(!busy)DestroyWindow(window);return 0;
    case WM_DESTROY:DeleteObject(bodyFont);DeleteObject(titleFont);DeleteObject(background);PostQuitMessage(0);return 0;
    }return DefWindowProcW(window,message,w,l);
}
void capture(HWND window,const wchar_t* filename){
    RECT r;GetClientRect(window,&r);HDC dc=GetDC(window);HDC mem=CreateCompatibleDC(dc);HBITMAP image=CreateCompatibleBitmap(dc,r.right,r.bottom);auto prior=SelectObject(mem,image);PrintWindow(window,mem,PW_CLIENTONLY|PW_RENDERFULLCONTENT);
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICBitmap> bitmap;Microsoft::WRL::ComPtr<IWICStream> stream;Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    auto ok=[](HRESULT result){if(FAILED(result))throw std::runtime_error("UI capture failed");};
    ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())));ok(factory->CreateBitmapFromHBITMAP(image,nullptr,WICBitmapUseAlpha,bitmap.GetAddressOf()));ok(factory->CreateStream(stream.GetAddressOf()));ok(stream->InitializeFromFilename(filename,GENERIC_WRITE));ok(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf()));ok(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ok(encoder->CreateNewFrame(frame.GetAddressOf(),nullptr));ok(frame->Initialize(nullptr));ok(frame->WriteSource(bitmap.Get(),nullptr));ok(frame->Commit());ok(encoder->Commit());SelectObject(mem,prior);DeleteObject(image);DeleteDC(mem);ReleaseDC(window,dc);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1&&std::wstring(argv[1])==L"--self-test"){std::wstring error;bool passed=app::selfTest(error);if(!passed){std::ofstream log("self-test-error.txt");for(wchar_t c:error)log<<(c<128?char(c):'?');}LocalFree(argv);return passed?0:1;}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);background=CreateSolidBrush(RGB(248,250,253));
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"YilaiRewriteV330";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP));cls.hbrBackground=background;RegisterClassW(&cls);
    RECT size{0,0,864,624};AdjustWindowRect(&size,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    HWND window=CreateWindowW(cls.lpszClassName,L"易来 Codex 配置器 v3.3.0",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    bool screenshot=argc>2&&std::wstring(argv[1])==L"--screenshot";ShowWindow(window,screenshot?SW_SHOWNOACTIVATE:show);UpdateWindow(window);
    if(screenshot){try{capture(window,argv[2]);DestroyWindow(window);LocalFree(argv);CoUninitialize();return 0;}catch(...){LocalFree(argv);return 1;}}
    LocalFree(argv);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}CoUninitialize();return 0;
}
