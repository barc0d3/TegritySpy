#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include "resource.h"

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#pragma pack(push, 1)
struct DBWIN_BUFFER_T { DWORD pid; char data[4096 - sizeof(DWORD)]; };
#pragma pack(pop)

HINSTANCE g_hInst{};
HWND g_hwndMain{};
HWND g_hwndDebugCopy{}, g_hwndProbeCopy{};
HWND g_hwndDebugConsole{}, g_hwndProbeConsole{};
HWND g_hwndClearBoth{}, g_hwndNexusStatus{};
HANDLE g_hBufferReady{}, g_hDataReady{}, g_hMap{};
LPVOID g_pView{};
std::thread g_thread;
std::atomic<bool> g_running{ true };
std::mutex g_nameMx;
std::map<DWORD, std::wstring> g_pidName;
bool g_nexusConnected = false;

static const COLORREF COL_OUTER_BG = RGB(30, 30, 30);
static const COLORREF COL_INNER_BG = RGB(18, 18, 18);
static const COLORREF COL_TEXT_DEBUG = RGB(255, 100, 100);
static const COLORREF COL_TEXT_PROBE = RGB(0, 255, 128);
static const COLORREF COL_TEXT_ICON = RGB(255, 255, 255);
static const COLORREF COL_NEXUS    = RGB(255, 255, 0);
HBRUSH g_hOuterBrush{}, g_hInnerBrush{};

static const int TOP_H = 50, BOT_H = 50, PAD_X = 16, PAD_Y = 10;
enum ControlID { ID_DEBUG_COPY = 1001, ID_PROBE_COPY = 1002, ID_CLEAR_BOTH = 1003, ID_STATUS_TIMER = 1005 };
enum AppMessage { WM_APP_APPEND_DEBUG = WM_APP + 1, WM_APP_APPEND_PROBE, WM_APP_NEXUS_PING = WM_APP + 66 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ListenerLoop();
void PostAppendMessage(const std::wstring& s);
void SetStatusText(const std::wstring& text, int duration_ms = 3000);

std::wstring ProcName(DWORD pid) {
    std::lock_guard<std::mutex> lock(g_nameMx);
    auto it = g_pidName.find(pid);
    if (it != g_pidName.end()) return it->second;
    wchar_t name[MAX_PATH] = L"";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h) { if (GetModuleBaseNameW(h, NULL, name, MAX_PATH)) {} CloseHandle(h); }
    return (g_pidName[pid] = name);
}

int APIENTRY wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int nShow) {
    g_hInst = h;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc; wc.hInstance = g_hInst; wc.lpszClassName = L"TegrityListenerWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_SPYICON));
    RegisterClassW(&wc);

    g_hwndMain = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"TegritySpy 1.0",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, g_hInst, nullptr);
    
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(g_hwndMain, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    ShowWindow(g_hwndMain, nShow); UpdateWindow(g_hwndMain);
    g_thread = std::thread(ListenerLoop);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    
    KillTimer(g_hwndMain, ID_STATUS_TIMER);
    g_running.store(false); if (g_hDataReady) SetEvent(g_hDataReady); if (g_thread.joinable()) g_thread.join();
    if (g_pView) UnmapViewOfFile(g_pView); if (g_hMap) CloseHandle(g_hMap);
    if (g_hBufferReady) CloseHandle(g_hBufferReady); if (g_hDataReady) CloseHandle(g_hDataReady);
    if (g_hOuterBrush) DeleteObject(g_hOuterBrush); if (g_hInnerBrush) DeleteObject(g_hInnerBrush);
    return 0;
}

static void Layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int midX = rc.right / 2;
    const int buttonSize = 40; const int byTop = (TOP_H - buttonSize) / 2;
    const int yBot  = rc.bottom - PAD_Y - buttonSize; const int statusWidth = 250;
    const int statusHeight = 28; const int statusY = (TOP_H - statusHeight) / 2;

    MoveWindow(g_hwndDebugCopy, PAD_X, byTop, buttonSize, buttonSize, TRUE);
    MoveWindow(g_hwndProbeCopy, midX + (PAD_X / 2), byTop, buttonSize, buttonSize, TRUE);
    MoveWindow(g_hwndNexusStatus, rc.right - statusWidth - PAD_X, statusY, statusWidth, statusHeight, TRUE);
    int xBot = (rc.right - buttonSize) / 2;
    MoveWindow(g_hwndClearBoth, xBot, yBot, buttonSize, buttonSize, TRUE);
    int consolesTop = TOP_H; int consolesBottom = rc.bottom - BOT_H;
    int consolesHeight = consolesBottom - consolesTop;
    MoveWindow(g_hwndDebugConsole, PAD_X, TOP_H, midX - PAD_X - (PAD_X / 2), consolesHeight, TRUE);
    MoveWindow(g_hwndProbeConsole, midX + (PAD_X / 2), TOP_H, midX - PAD_X - (PAD_X / 2), consolesHeight, TRUE);
}

static void CreateChildren(HWND hwnd) {
    g_hwndDebugCopy = CreateWindowExW(0, L"BUTTON", L"\uE8C8", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_DEBUG_COPY,  g_hInst, nullptr);
    g_hwndProbeCopy = CreateWindowExW(0, L"BUTTON", L"\uE8C8", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_PROBE_COPY, g_hInst, nullptr);
    g_hwndClearBoth = CreateWindowExW(0, L"BUTTON", L"\uE74D", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)ID_CLEAR_BOTH, g_hInst, nullptr);
    g_hwndNexusStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 0,0,0,0, hwnd, NULL, g_hInst, nullptr);
    
    DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER;
    g_hwndDebugConsole = CreateWindowExW(0, L"EDIT", L"", dwStyle, 0, 0, 0, 0, hwnd, NULL, g_hInst, nullptr);
    g_hwndProbeConsole = CreateWindowExW(0, L"EDIT", L"", dwStyle, 0, 0, 0, 0, hwnd, NULL, g_hInst, nullptr);
    
    SetWindowTheme(g_hwndDebugConsole, L"DarkMode_Explorer", NULL);
    SetWindowTheme(g_hwndProbeConsole, L"DarkMode_Explorer", NULL);

    HFONT hf = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0,0,0,0,0,0,0, FIXED_PITCH | FF_MODERN, L"Consolas");
    HFONT hfIcon = CreateFontW(36, 0, 0, 0, FW_NORMAL, 0,0,0,0,0,0,0, VARIABLE_PITCH | FF_ROMAN, L"Segoe Fluent Icons");
    SendMessageW(g_hwndDebugConsole, WM_SETFONT, (WPARAM)hf, TRUE); SendMessageW(g_hwndProbeConsole, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(g_hwndDebugCopy, WM_SETFONT, (WPARAM)hfIcon, TRUE); SendMessageW(g_hwndProbeCopy, WM_SETFONT, (WPARAM)hfIcon, TRUE);
    SendMessageW(g_hwndClearBoth, WM_SETFONT, (WPARAM)hfIcon, TRUE); SendMessageW(g_hwndNexusStatus, WM_SETFONT, (WPARAM)hf, TRUE);
}

static void CopyAll(HWND edit) {
    int len = GetWindowTextLengthW(edit); if (len <= 0) return;
    std::wstring buf(len + 1, L'\0'); GetWindowTextW(edit, &buf[0], len + 1);
    if (!OpenClipboard(g_hwndMain)) return; EmptyClipboard();
    SIZE_T bytes = (buf.size() * sizeof(wchar_t)); HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hglb) { void* p = GlobalLock(hglb); memcpy(p, buf.c_str(), bytes); GlobalUnlock(hglb); SetClipboardData(CF_UNICODETEXT, hglb); }
    CloseClipboard();
    SetStatusText(L"Output Copied!");
}

void SetStatusText(const std::wstring& text, int duration_ms) {
    KillTimer(g_hwndMain, ID_STATUS_TIMER);
    SetWindowTextW(g_hwndNexusStatus, text.c_str());
    if (duration_ms > 0) SetTimer(g_hwndMain, ID_STATUS_TIMER, duration_ms, NULL);
}

void PostAppendMessage(const std::wstring& s) {
    size_t bytes = (s.size() + 1) * sizeof(wchar_t); wchar_t* heap = (wchar_t*)LocalAlloc(LPTR, bytes); if (!heap) return;
    memcpy(heap, s.c_str(), bytes);
    UINT msg = (s.find(L"[TC PROBE]") != std::wstring::npos || s.find(L"[Listener]") != std::wstring::npos) ? WM_APP_APPEND_PROBE : WM_APP_APPEND_DEBUG;
    PostMessageW(g_hwndMain, msg, (WPARAM)heap, 0);
}

static bool InitDbwinLocalOnly() {
    g_hBufferReady = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, L"DBWIN_BUFFER_READY");
    g_hDataReady = OpenEventW(SYNCHRONIZE, FALSE, L"DBWIN_DATA_READY");
    g_hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, L"DBWIN_BUFFER");
    if (g_hBufferReady && g_hDataReady && g_hMap) return true;
    g_hBufferReady = CreateEventW(nullptr, FALSE, TRUE,  L"DBWIN_BUFFER_READY");
    g_hDataReady   = CreateEventW(nullptr, FALSE, FALSE, L"DBWIN_DATA_READY");
    g_hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, L"DBWIN_BUFFER");
    return g_hBufferReady && g_hDataReady && g_hMap;
}

void ListenerLoop() {
    if (!InitDbwinLocalOnly()) { PostAppendMessage(L"[Listener] CRITICAL FAILURE: Local DBWIN open/create failed.\r\n"); return; }
    g_pView = MapViewOfFile(g_hMap, FILE_MAP_READ, 0, 0, 0);
    if (!g_pView) { PostAppendMessage(L"[Listener] CRITICAL FAILURE: MapViewOfFile failed.\r\n"); return; }
    PostAppendMessage(L"[Listener] Online. Waiting for SIGINT...\r\n");

    while (g_running.load()) {
        if (g_hBufferReady) SetEvent(g_hBufferReady);
        if (WaitForSingleObject(g_hDataReady, 100) != WAIT_OBJECT_0) continue;
        const DBWIN_BUFFER_T* p = reinterpret_cast<const DBWIN_BUFFER_T*>(g_pView);
        std::string ansi_data(p->data);
        if (!ansi_data.empty()) {
            int need = MultiByteToWideChar(CP_ACP, 0, ansi_data.c_str(), -1, nullptr, 0); 
            std::wstring wmsg(need > 0 ? need - 1 : 0, L'\0');
            if (need > 0) MultiByteToWideChar(CP_ACP, 0, ansi_data.c_str(), -1, &wmsg[0], need);
            std::wstring final_line = L"[PID:" + std::to_wstring(p->pid) + L"] " + wmsg;
            PostAppendMessage(final_line);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hOuterBrush = CreateSolidBrush(COL_OUTER_BG); g_hInnerBrush = CreateSolidBrush(COL_INNER_BG);
        CreateChildren(hwnd); 
        Layout(hwnd);
        return 0;
    case WM_SIZE: Layout(hwnd); return 0;
    case WM_ERASEBKGND: { HDC hdc = (HDC)wParam; RECT rc; GetClientRect(hwnd, &rc); FillRect(hdc, &rc, g_hOuterBrush); return 1; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* pdis = (DRAWITEMSTRUCT*)lParam;
        FillRect(pdis->hDC, &pdis->rcItem, g_hOuterBrush);
        SetTextColor(pdis->hDC, COL_TEXT_ICON);
        SetBkMode(pdis->hDC, TRANSPARENT);
        wchar_t text[2]; GetWindowTextW(pdis->hwndItem, text, 2);
        DrawTextW(pdis->hDC, text, -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, COL_INNER_BG);
        if ((HWND)lParam == g_hwndDebugConsole) SetTextColor(hdc, COL_TEXT_DEBUG);
        else if ((HWND)lParam == g_hwndProbeConsole) SetTextColor(hdc, COL_TEXT_PROBE);
        else if ((HWND)lParam == g_hwndNexusStatus) SetTextColor(hdc, g_nexusConnected ? COL_NEXUS : COL_TEXT_PROBE);
        return (LRESULT)g_hInnerBrush;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            WORD id = LOWORD(wParam);
            if (id == ID_DEBUG_COPY) CopyAll(g_hwndDebugConsole);
            else if (id == ID_PROBE_COPY) CopyAll(g_hwndProbeConsole);
            else if (id == ID_CLEAR_BOTH) {
                SetWindowTextW(g_hwndDebugConsole, L""); SetWindowTextW(g_hwndProbeConsole, L"");
                SetStatusText(L"Output Cleared!");
            }
            return 0;
        } break;
    case WM_APP_APPEND_DEBUG:
    case WM_APP_APPEND_PROBE: {
        wchar_t* p = (wchar_t*)wParam; 
        HWND target = (msg == WM_APP_APPEND_PROBE) ? g_hwndProbeConsole : g_hwndDebugConsole;
        if (p && target) { 
            DWORD end = GetWindowTextLengthW(target); 
            SendMessageW(target, EM_SETSEL, end, end); 
            SendMessageW(target, EM_REPLACESEL, FALSE, (LPARAM)p); 
        }
        if (p) LocalFree(p); 
        return 0;
    }
    case WM_TIMER:
        if (wParam == ID_STATUS_TIMER) {
            KillTimer(hwnd, ID_STATUS_TIMER);
            SetWindowTextW(g_hwndNexusStatus, L"");
        }
        return 0;
    case WM_APP_NEXUS_PING:
        g_nexusConnected = true;
        SetStatusText(L"DEBUG LINK ESTABLISHED");
        InvalidateRect(g_hwndNexusStatus, NULL, TRUE);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}