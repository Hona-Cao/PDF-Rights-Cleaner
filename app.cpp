#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <compressapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "cabinet.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace fs = std::filesystem;

constexpr wchar_t kClassName[] = L"PdfRightsCleaner.MainWindow";
constexpr wchar_t kProductName[] = L"PDF 权限清理器";
constexpr wchar_t kProductVersion[] = L"1.1";
constexpr wchar_t kProjectUrl[] = L"https://github.com/Hona-Cao/PDF-Rights-Cleaner";
constexpr wchar_t kEngineVersion[] = L"12.3.2";

enum ControlId {
    ID_ADD = 1001, ID_REMOVE = 1002, ID_PROCESS = 1003,
    ID_INPUT = 1101, ID_OUTPUT = 1102,
    ID_CACHE_EDIT = 1201, ID_CACHE_BROWSE = 1202, ID_CACHE_OPEN = 1203,
    ID_SAVE = 1301, ID_CLEAR_CACHE = 1302, ID_ABOUT = 1303,
    ID_STATUS = 1401,
    ID_CTX_INPUT_OPEN = 2001, ID_CTX_INPUT_LOCATION = 2002, ID_CTX_INPUT_REMOVE = 2003,
    ID_CTX_INPUT_CLEAR = 2004, ID_CTX_INPUT_SELECT_ALL = 2005,
    ID_CTX_OUTPUT_OPEN = 2101, ID_CTX_OUTPUT_LOCATION = 2102, ID_CTX_OUTPUT_SAVE = 2103,
    ID_CTX_OUTPUT_COPY_PATH = 2104, ID_CTX_OUTPUT_REMOVE = 2105, ID_CTX_OUTPUT_SELECT_ALL = 2106
};

constexpr UINT WM_APP_RESULT = WM_APP + 1;
constexpr UINT WM_APP_FINISHED = WM_APP + 2;

struct SourceItem {
    std::wstring path;
    std::wstring status = L"等待处理";
};

struct OutputItem {
    std::wstring source;
    std::wstring path;
    std::wstring status;
    std::uintmax_t size = 0;
    bool exported = false;
};

struct WorkerResult {
    size_t source_index = 0;
    std::wstring status;
    std::wstring output;
    std::uintmax_t size = 0;
};

struct PackedHeader {
    std::uint32_t magic;
    std::uint32_t algorithm;
    std::uint64_t original_size;
    std::uint64_t packed_size;
};

HINSTANCE g_instance{};
HWND g_main{}, g_title{}, g_subtitle{}, g_add{}, g_remove{}, g_process{};
HWND g_input_label{}, g_output_label{}, g_input{}, g_output{};
HWND g_cache_label{}, g_cache_edit{}, g_cache_browse{}, g_cache_open{};
HWND g_save{}, g_clear_cache{}, g_about{}, g_status{};
HFONT g_font{}, g_font_bold{}, g_font_title{};
HBRUSH g_background_brush{}, g_header_brush{};
UINT g_dpi = 96;
std::vector<SourceItem> g_sources;
std::vector<OutputItem> g_outputs;
std::vector<std::wstring> g_startup_paths;
std::atomic_bool g_busy = false;
POINT g_output_drag_start{}, g_input_drag_start{};
bool g_output_drag_tracking = false;
bool g_input_drag_tracking = false;
bool g_input_dragging = false;

constexpr COLORREF kBackground = RGB(242, 246, 255);
constexpr COLORREF kNavy = RGB(24, 39, 78);
constexpr COLORREF kBlue = RGB(37, 99, 235);
constexpr COLORREF kIndigo = RGB(79, 70, 229);
constexpr COLORREF kPurple = RGB(109, 40, 217);
constexpr COLORREF kTeal = RGB(13, 148, 136);
constexpr COLORREF kSoftBlue = RGB(226, 235, 255);

void RemoveSelectedInputs();
void ShowInputContextMenu(POINT screen_point);
void ShowOutputContextMenu(POINT screen_point);

int Px(int value) { return MulDiv(value, static_cast<int>(g_dpi), 96); }

std::wstring GetModuleDirectory() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size()) return L".";
    return fs::path(std::wstring(buf.data(), n)).parent_path().wstring();
}

std::wstring GetDataDirectory() {
    wchar_t path[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    fs::path base = (n && n < MAX_PATH) ? fs::path(path) : fs::temp_directory_path();
    return (base / L"PDF权限解除工具").wstring();
}

std::wstring GetSettingsPath() {
    fs::path dir(GetDataDirectory());
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / L"settings.ini").wstring();
}

std::wstring DefaultCachePath() {
    return (fs::path(GetDataDirectory()) / L"Cache").wstring();
}

std::wstring ReadCacheSetting() {
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"Settings", L"CachePath", DefaultCachePath().c_str(), value,
                             static_cast<DWORD>(std::size(value)), GetSettingsPath().c_str());
    return value;
}

void SaveCacheSetting(std::wstring const& value) {
    WritePrivateProfileStringW(L"Settings", L"CachePath", value.c_str(), GetSettingsPath().c_str());
}

std::wstring WindowText(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(n) + 1, L'\0');
    if (n) GetWindowTextW(hwnd, text.data(), n + 1);
    text.resize(static_cast<size_t>(n));
    return text;
}

std::wstring FormatBytes(std::uintmax_t size) {
    wchar_t buf[64];
    if (size >= 1024ull * 1024ull) swprintf_s(buf, L"%.2f MB", size / 1048576.0);
    else if (size >= 1024) swprintf_s(buf, L"%.1f KB", size / 1024.0);
    else swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(size));
    return buf;
}

std::wstring FileName(std::wstring const& path) {
    return fs::path(path).filename().wstring();
}

void SetStatus(std::wstring const& text) {
    SetWindowTextW(g_status, text.c_str());
}

void SetFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_font_bold) DeleteObject(g_font_bold);
    if (g_font_title) DeleteObject(g_font_title);
    g_font = CreateFontW(-Px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_bold = CreateFontW(-Px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_title = CreateFontW(-Px(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HWND controls[] = {g_title,g_subtitle,g_add,g_remove,g_process,g_input_label,g_output_label,
                       g_input,g_output,g_cache_label,g_cache_edit,g_cache_browse,g_cache_open,
                       g_save,g_clear_cache,g_about,g_status};
    for (HWND h : controls) if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    if (g_title) SendMessageW(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_font_title), TRUE);
    if (g_input_label) SendMessageW(g_input_label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font_bold), TRUE);
    if (g_output_label) SendMessageW(g_output_label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font_bold), TRUE);
}

void RefreshInputList() {
    ListView_DeleteAllItems(g_input);
    for (size_t i = 0; i < g_sources.size(); ++i) {
        auto filename = FileName(g_sources[i].path);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = filename.data();
        item.lParam = static_cast<LPARAM>(i);
        int row = ListView_InsertItem(g_input, &item);
        ListView_SetItemText(g_input, row, 1, const_cast<wchar_t*>(g_sources[i].status.c_str()));
        ListView_SetItemText(g_input, row, 2, const_cast<wchar_t*>(g_sources[i].path.c_str()));
    }
}

void RefreshOutputList() {
    ListView_DeleteAllItems(g_output);
    for (size_t i = 0; i < g_outputs.size(); ++i) {
        auto filename = FileName(g_outputs[i].path);
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = filename.data();
        item.lParam = static_cast<LPARAM>(i);
        int row = ListView_InsertItem(g_output, &item);
        ListView_SetItemText(g_output, row, 1, const_cast<wchar_t*>(g_outputs[i].status.c_str()));
        auto size = FormatBytes(g_outputs[i].size);
        ListView_SetItemText(g_output, row, 2, size.data());
        auto exported = g_outputs[i].exported ? L"已导出" : L"可拖出";
        ListView_SetItemText(g_output, row, 3, const_cast<wchar_t*>(exported));
    }
}

void LoadCachedResults() {
    fs::path results = fs::path(ReadCacheSetting()) / L"Results";
    std::error_code ec;
    if (!fs::is_directory(results, ec)) return;
    for (fs::directory_iterator it(results, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), L".pdf") == 0) {
            g_outputs.push_back({L"", it->path().wstring(), L"已从缓存恢复", it->file_size(ec), false});
        }
    }
    RefreshOutputList();
    if (!g_outputs.empty()) SetStatus(L"已从缓存恢复 " + std::to_wstring(g_outputs.size()) + L" 个处理结果。");
}

void InitColumns() {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = const_cast<wchar_t*>(L"文件名"); c.cx = Px(190); c.iSubItem = 0; ListView_InsertColumn(g_input, 0, &c);
    c.pszText = const_cast<wchar_t*>(L"状态"); c.cx = Px(150); c.iSubItem = 1; ListView_InsertColumn(g_input, 1, &c);
    c.pszText = const_cast<wchar_t*>(L"原始位置"); c.cx = Px(360); c.iSubItem = 2; ListView_InsertColumn(g_input, 2, &c);
    c.pszText = const_cast<wchar_t*>(L"文件名"); c.cx = Px(195); c.iSubItem = 0; ListView_InsertColumn(g_output, 0, &c);
    c.pszText = const_cast<wchar_t*>(L"结果"); c.cx = Px(120); c.iSubItem = 1; ListView_InsertColumn(g_output, 1, &c);
    c.pszText = const_cast<wchar_t*>(L"大小"); c.cx = Px(85); c.iSubItem = 2; ListView_InsertColumn(g_output, 2, &c);
    c.pszText = const_cast<wchar_t*>(L"导出"); c.cx = Px(75); c.iSubItem = 3; ListView_InsertColumn(g_output, 3, &c);
}

void ResizeColumns() {
    RECT rc{};
    GetClientRect(g_input, &rc);
    int w = rc.right - rc.left;
    ListView_SetColumnWidth(g_input, 0, std::max(Px(130), w * 28 / 100));
    ListView_SetColumnWidth(g_input, 1, std::max(Px(120), w * 25 / 100));
    ListView_SetColumnWidth(g_input, 2, std::max(Px(180), w - ListView_GetColumnWidth(g_input,0) - ListView_GetColumnWidth(g_input,1) - Px(4)));
    GetClientRect(g_output, &rc);
    w = rc.right - rc.left;
    ListView_SetColumnWidth(g_output, 0, std::max(Px(150), w * 44 / 100));
    ListView_SetColumnWidth(g_output, 1, std::max(Px(95), w * 25 / 100));
    ListView_SetColumnWidth(g_output, 2, Px(90));
    ListView_SetColumnWidth(g_output, 3, std::max(Px(65), w - ListView_GetColumnWidth(g_output,0) - ListView_GetColumnWidth(g_output,1) - ListView_GetColumnWidth(g_output,2) - Px(4)));
}

void Layout(HWND hwnd) {
    // Resize every child as one visual transaction. Repainting individual
    // controls while the user drags the frame leaves stale text on some DPI
    // combinations, especially for static labels.
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    RECT rc{}; GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    int m = Px(22), gap = Px(18), header = Px(92), toolbar = Px(48), labels = Px(32);
    int cache_h = Px(46), bottom = Px(52);
    MoveWindow(g_title, m, Px(17), w - 2*m, Px(34), FALSE);
    MoveWindow(g_subtitle, m, Px(52), w - 2*m, Px(25), FALSE);
    int x = m, y = header;
    MoveWindow(g_add, x, y, Px(120), Px(34), FALSE); x += Px(128);
    MoveWindow(g_remove, x, y, Px(105), Px(34), FALSE); x += Px(113);
    MoveWindow(g_process, x, y, Px(145), Px(34), FALSE);
    MoveWindow(g_about, w - m - Px(70), y, Px(70), Px(34), FALSE);
    y += toolbar;
    int pane_w = std::max(Px(280), (w - 2*m - gap) / 2);
    int list_h = std::max(Px(180), h - y - labels - cache_h - bottom - Px(12));
    MoveWindow(g_input_label, m, y, pane_w, labels, FALSE);
    MoveWindow(g_output_label, m + pane_w + gap, y, pane_w, labels, FALSE);
    y += labels;
    MoveWindow(g_input, m, y, pane_w, list_h, FALSE);
    MoveWindow(g_output, m + pane_w + gap, y, pane_w, list_h, FALSE);
    y += list_h + Px(12);
    MoveWindow(g_cache_label, m, y + Px(7), Px(75), Px(28), FALSE);
    int bx = w - m;
    bx -= Px(118); MoveWindow(g_clear_cache, bx, y, Px(118), Px(34), FALSE);
    bx -= Px(8) + Px(105); MoveWindow(g_cache_open, bx, y, Px(105), Px(34), FALSE);
    bx -= Px(8) + Px(86); MoveWindow(g_cache_browse, bx, y, Px(86), Px(34), FALSE);
    MoveWindow(g_cache_edit, m + Px(75), y, std::max(Px(150), bx - (m + Px(75)) - Px(8)), Px(34), FALSE);
    y += cache_h;
    MoveWindow(g_status, m, y + Px(7), std::max(Px(200), w - 2*m - Px(165)), Px(30), FALSE);
    MoveWindow(g_save, w - m - Px(155), y, Px(155), Px(36), FALSE);
    ResizeColumns();
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void AddPath(fs::path const& path, std::set<std::wstring>& seen) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), L".pdf") == 0)
                AddPath(it->path(), seen);
        }
        return;
    }
    if (!fs::is_regular_file(path, ec) || _wcsicmp(path.extension().c_str(), L".pdf") != 0) return;
    auto absolute = fs::absolute(path, ec).lexically_normal().wstring();
    std::wstring key = absolute;
    std::transform(key.begin(), key.end(), key.begin(), towlower);
    if (seen.insert(key).second) g_sources.push_back({absolute, L"等待处理"});
}

void AddPaths(std::vector<std::wstring> const& paths) {
    std::set<std::wstring> seen;
    for (auto const& item : g_sources) {
        std::wstring key = item.path;
        std::transform(key.begin(), key.end(), key.begin(), towlower);
        seen.insert(std::move(key));
    }
    size_t before = g_sources.size();
    for (auto const& p : paths) AddPath(fs::path(p), seen);
    RefreshInputList();
    SetStatus(L"已添加 " + std::to_wstring(g_sources.size() - before) + L" 个 PDF，共 " + std::to_wstring(g_sources.size()) + L" 个。");
}

std::vector<std::wstring> PickPdfFiles() {
    std::vector<std::wstring> result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD options{}; dlg->GetOptions(&options); dlg->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    COMDLG_FILTERSPEC filters[] = {{L"PDF 文件", L"*.pdf"}, {L"所有文件", L"*.*"}};
    dlg->SetFileTypes(2, filters);
    if (SUCCEEDED(dlg->Show(g_main))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD count{}; items->GetCount(&count);
            for (DWORD i=0; i<count; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result.emplace_back(path); CoTaskMemFree(path); }
                    item->Release();
                }
            }
            items->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring PickFolder(std::wstring const& title) {
    std::wstring result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD options{}; dlg->GetOptions(&options); dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title.c_str());
    if (SUCCEEDED(dlg->Show(g_main))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = path; CoTaskMemFree(path); }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

bool ExtractResourceFile(int id, fs::path const& output) {
    HRSRC res = FindResourceW(g_instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(g_instance, res);
    DWORD bytes = SizeofResource(g_instance, res);
    auto data = static_cast<unsigned char const*>(LockResource(loaded));
    if (!data || bytes < sizeof(PackedHeader)) return false;
    auto header = reinterpret_cast<PackedHeader const*>(data);
    if (header->magic != 0x31585050 || header->packed_size + sizeof(PackedHeader) > bytes) return false;
    std::vector<unsigned char> unpacked(static_cast<size_t>(header->original_size));
    DECOMPRESSOR_HANDLE dec = nullptr;
    if (!CreateDecompressor(header->algorithm, nullptr, &dec)) return false;
    SIZE_T written{};
    BOOL ok = Decompress(dec, data + sizeof(PackedHeader), static_cast<SIZE_T>(header->packed_size),
                         unpacked.data(), unpacked.size(), &written);
    CloseDecompressor(dec);
    if (!ok || written != unpacked.size()) return false;
    std::error_code ec; fs::create_directories(output.parent_path(), ec);
    fs::path temp = output; temp += L".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<char const*>(unpacked.data()), static_cast<std::streamsize>(unpacked.size()));
    out.close();
    if (!out) return false;
    fs::rename(temp, output, ec);
    if (ec) { fs::remove(output, ec); ec.clear(); fs::rename(temp, output, ec); }
    return !ec;
}

bool EnsureEngine(fs::path const& cache, fs::path& executable) {
    fs::path dir = cache / L"_engine" / kEngineVersion;
    executable = dir / L"qpdf.exe";
    struct File { int id; wchar_t const* name; } files[] = {
        {IDR_QPDF_EXE,L"qpdf.exe"},{IDR_QPDF_DLL,L"qpdf30.dll"},{IDR_MSVCP_DLL,L"msvcp140.dll"},
        {IDR_VCRUNTIME_DLL,L"vcruntime140.dll"},{IDR_VCRUNTIME1_DLL,L"vcruntime140_1.dll"}
    };
    for (auto const& file : files) {
        fs::path target = dir / file.name;
        std::error_code ec;
        if (!fs::exists(target, ec) && !ExtractResourceFile(file.id, target)) return false;
    }
    return true;
}

std::wstring Quote(std::wstring const& value) {
    std::wstring q = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') { q.append(slashes * 2 + 1, L'\\'); q += ch; slashes = 0; continue; }
        q.append(slashes, L'\\'); slashes = 0; q += ch;
    }
    q.append(slashes * 2, L'\\'); q += L'\"';
    return q;
}

DWORD RunProcess(fs::path const& exe, std::wstring const& args, std::wstring& diagnostic) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_pipe{}, write_pipe{};
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return 0xffffffff;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe; si.hStdError = write_pipe; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring command = Quote(exe.wstring()) + L" " + args;
    std::vector<wchar_t> mutable_cmd(command.begin(), command.end()); mutable_cmd.push_back(L'\0');
    std::wstring cwd = exe.parent_path().wstring();
    BOOL created = CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
    CloseHandle(write_pipe);
    if (!created) { CloseHandle(read_pipe); return 0xffffffff; }
    std::string bytes;
    char buffer[2048]; DWORD got{};
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got) {
        if (bytes.size() < 16384) bytes.append(buffer, buffer + std::min<DWORD>(got, static_cast<DWORD>(16384 - bytes.size())));
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code{}; GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (!bytes.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        diagnostic.resize(n);
        if (n) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), diagnostic.data(), n);
    }
    return exit_code;
}

fs::path UniqueOutput(fs::path const& cache, fs::path const& source) {
    std::error_code ec; fs::create_directories(cache, ec);
    std::wstring stem = source.stem().wstring() + L"_unlocked";
    fs::path candidate = cache / (stem + L".pdf");
    for (int i=2; fs::exists(candidate, ec); ++i) candidate = cache / (stem + L" (" + std::to_wstring(i) + L").pdf");
    return candidate;
}

void ProcessWorker(std::vector<std::wstring> paths, fs::path cache) {
    fs::path qpdf;
    if (!EnsureEngine(cache, qpdf)) {
        for (size_t i=0; i<paths.size(); ++i) PostMessageW(g_main, WM_APP_RESULT, 0, reinterpret_cast<LPARAM>(new WorkerResult{i,L"内部 PDF 引擎释放失败",L"",0}));
        PostMessageW(g_main, WM_APP_FINISHED, 0, 0); return;
    }
    for (size_t i=0; i<paths.size(); ++i) {
        auto result = std::make_unique<WorkerResult>(); result->source_index = i;
        std::wstring diagnostic;
        DWORD type = RunProcess(qpdf, L"--requires-password " + Quote(paths[i]), diagnostic);
        if (type == 0) {
            result->status = L"需要打开密码，不支持";
        } else if (type == 2) {
            result->status = L"未加密，无需处理";
        } else if (type == 3) {
            fs::path output = UniqueOutput(cache / L"Results", paths[i]);
            diagnostic.clear();
            DWORD code = RunProcess(qpdf, L"--decrypt " + Quote(paths[i]) + L" " + Quote(output.wstring()), diagnostic);
            std::error_code ec;
            if ((code == 0 || code == 3) && fs::exists(output, ec)) {
                result->status = L"已解除权限限制";
                result->output = output.wstring();
                result->size = fs::file_size(output, ec);
            } else {
                result->status = L"处理失败";
                fs::remove(output, ec);
            }
        } else {
            result->status = L"文件损坏或格式不支持";
        }
        PostMessageW(g_main, WM_APP_RESULT, 0, reinterpret_cast<LPARAM>(result.release()));
    }
    PostMessageW(g_main, WM_APP_FINISHED, 0, 0);
}

void StartProcessing() {
    if (g_busy || g_sources.empty()) return;
    std::wstring cache_text = WindowText(g_cache_edit);
    if (cache_text.empty()) { MessageBoxW(g_main, L"请先设置缓存位置。", kProductName, MB_OK | MB_ICONINFORMATION); return; }
    std::error_code ec; fs::create_directories(cache_text, ec);
    if (ec) { MessageBoxW(g_main, L"无法创建缓存目录，请选择其他位置。", kProductName, MB_OK | MB_ICONERROR); return; }
    SaveCacheSetting(cache_text);
    std::vector<std::wstring> paths;
    for (auto& item : g_sources) { item.status = L"等待处理"; paths.push_back(item.path); }
    RefreshInputList();
    g_busy = true;
    EnableWindow(g_process, FALSE); EnableWindow(g_add, FALSE); EnableWindow(g_remove, FALSE);
    SetStatus(L"正在本地处理 0 / " + std::to_wstring(paths.size()) + L" …");
    std::thread(ProcessWorker, std::move(paths), fs::path(cache_text)).detach();
}

std::vector<std::wstring> SelectedOutputPaths() {
    std::vector<std::wstring> paths;
    int row = -1;
    while ((row = ListView_GetNextItem(g_output, row, LVNI_SELECTED)) != -1) {
        LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = row;
        if (ListView_GetItem(g_output, &item)) {
            size_t i = static_cast<size_t>(item.lParam);
            if (i < g_outputs.size() && fs::exists(g_outputs[i].path)) paths.push_back(g_outputs[i].path);
        }
    }
    return paths;
}

fs::path UniqueDestination(fs::path const& folder, fs::path const& source) {
    std::error_code ec; fs::path candidate = folder / source.filename();
    std::wstring stem = source.stem().wstring(), ext = source.extension().wstring();
    for (int i=2; fs::exists(candidate, ec); ++i) candidate = folder / (stem + L" (" + std::to_wstring(i) + L")" + ext);
    return candidate;
}

void SaveSelected() {
    auto paths = SelectedOutputPaths();
    if (paths.empty()) { MessageBoxW(g_main, L"请先在右侧选择要保存的文件。", kProductName, MB_OK | MB_ICONINFORMATION); return; }
    auto folder = PickFolder(L"选择保存位置"); if (folder.empty()) return;
    size_t copied = 0;
    for (auto const& path : paths) {
        fs::path destination = UniqueDestination(folder, path);
        if (CopyFileW(path.c_str(), destination.c_str(), TRUE)) ++copied;
        for (auto& item : g_outputs) if (item.path == path) item.exported = true;
    }
    RefreshOutputList();
    SetStatus(L"已保存 " + std::to_wstring(copied) + L" 个文件到：" + folder);
}

class DropSource final : public IDropSource {
    LONG refs_ = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropSource) { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs_); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

class FileDataObject final : public IDataObject {
    LONG refs_ = 1;
    HGLOBAL data_{};
    UINT preferred_effect_format_{};
public:
    explicit FileDataObject(std::vector<std::wstring> const& paths) {
        preferred_effect_format_ = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        size_t chars = 1; for (auto const& p : paths) chars += p.size() + 1;
        data_ = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
        auto base = static_cast<unsigned char*>(GlobalLock(data_));
        auto drop = reinterpret_cast<DROPFILES*>(base); drop->pFiles = sizeof(DROPFILES); drop->fWide = TRUE;
        wchar_t* out = reinterpret_cast<wchar_t*>(base + sizeof(DROPFILES));
        for (auto const& p : paths) { memcpy(out, p.c_str(), (p.size()+1)*sizeof(wchar_t)); out += p.size()+1; }
        *out = L'\0'; GlobalUnlock(data_);
    }
    ~FileDataObject() { if (data_) GlobalFree(data_); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_IDataObject) { *out=this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs_); if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* f, STGMEDIUM* m) override {
        if (!f || !m) return E_INVALIDARG;
        ZeroMemory(m, sizeof(*m));
        if (!(f->tymed & TYMED_HGLOBAL) || f->dwAspect != DVASPECT_CONTENT) return DV_E_FORMATETC;
        if (f->cfFormat == CF_HDROP) {
            SIZE_T bytes=GlobalSize(data_); HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!copy) return E_OUTOFMEMORY;
            void* src=GlobalLock(data_); void* dst=GlobalLock(copy);
            if (!src || !dst) { if(dst)GlobalUnlock(copy); if(src)GlobalUnlock(data_); GlobalFree(copy); return E_OUTOFMEMORY; }
            memcpy(dst,src,bytes); GlobalUnlock(copy); GlobalUnlock(data_);
            m->tymed=TYMED_HGLOBAL; m->hGlobal=copy; m->pUnkForRelease=nullptr; return S_OK;
        }
        if (f->cfFormat == preferred_effect_format_) {
            HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!effect) return E_OUTOFMEMORY;
            auto value = static_cast<DWORD*>(GlobalLock(effect));
            *value = DROPEFFECT_COPY;
            GlobalUnlock(effect);
            m->tymed=TYMED_HGLOBAL; m->hGlobal=effect; m->pUnkForRelease=nullptr; return S_OK;
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* f) override {
        if (!f || !(f->tymed & TYMED_HGLOBAL) || f->dwAspect != DVASPECT_CONTENT) return DV_E_FORMATETC;
        return (f->cfFormat==CF_HDROP || f->cfFormat==preferred_effect_format_) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* out) override { if(out) out->ptd=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        FORMATETC formats[2] = {
            {static_cast<CLIPFORMAT>(CF_HDROP), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
            {static_cast<CLIPFORMAT>(preferred_effect_format_), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}
        };
        return SHCreateStdEnumFmtEtc(2, formats, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

void BeginOutputDrag() {
    auto paths = SelectedOutputPaths(); if (paths.empty()) return;
    // Ask the Windows Shell to build the same data object Explorer uses for
    // real files. Besides CF_HDROP this supplies Shell ID lists and the extra
    // formats consumed by Explorer and common chat applications.
    IDataObject* data = nullptr;
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (auto const& path : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl)
            pidls.push_back(pidl);
    }
    if (pidls.size() == paths.size()) {
        std::vector<PCIDLIST_ABSOLUTE> absolute_pidls(pidls.begin(), pidls.end());
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(
                static_cast<UINT>(pidls.size()),
                absolute_pidls.data(), &items)) && items) {
            items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
            items->Release();
        }
    }
    for (auto pidl : pidls) CoTaskMemFree(pidl);
    if (!data) data = new FileDataObject(paths);

    SetStatus(L"正在拖出 " + std::to_wstring(paths.size()) + L" 个结果文件…");
    auto source = new DropSource(); DWORD effect{};
    DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    source->Release(); data->Release();
    if (effect & DROPEFFECT_COPY) {
        for (auto const& path : paths) for (auto& item : g_outputs) if (item.path==path) item.exported=true;
        RefreshOutputList(); SetStatus(L"已将 " + std::to_wstring(paths.size()) + L" 个文件拖出保存；缓存副本仍然保留。");
    } else SetStatus(L"拖放已取消或目标窗口未接受文件。");
}

LRESULT CALLBACK OutputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_CONTEXTMENU) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ShowOutputContextMenu(point); return 0;
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}

LRESULT CALLBACK InputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_CONTEXTMENU) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ShowInputContextMenu(point); return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        LRESULT result = DefSubclassProc(hwnd,msg,wp,lp);
        g_input_drag_start = {GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        g_input_drag_tracking = true;
        return result;
    }
    if (msg == WM_MOUSEMOVE && g_input_drag_tracking && (wp & MK_LBUTTON)) {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (!g_input_dragging && (abs(point.x-g_input_drag_start.x)>=GetSystemMetrics(SM_CXDRAG) || abs(point.y-g_input_drag_start.y)>=GetSystemMetrics(SM_CYDRAG))) {
            g_input_dragging = true;
            SetCapture(hwnd);
            SetStatus(L"拖出待处理列表并松开，即可从列表移除所选文件（源文件不会删除）。");
        }
        if (g_input_dragging) { SetCursor(LoadCursorW(nullptr,IDC_NO)); return 0; }
    }
    if (msg == WM_LBUTTONUP && g_input_dragging) {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(hwnd,&point);
        RECT list_rect{}; GetWindowRect(hwnd,&list_rect);
        g_input_dragging=false; g_input_drag_tracking=false; ReleaseCapture();
        if (!PtInRect(&list_rect,point)) RemoveSelectedInputs();
        else SetStatus(L"拖拽移除已取消。");
        return 0;
    }
    if (msg == WM_CAPTURECHANGED) { g_input_dragging=false; g_input_drag_tracking=false; }
    if (msg == WM_LBUTTONUP) g_input_drag_tracking=false;
    return DefSubclassProc(hwnd,msg,wp,lp);
}

void RemoveSelectedInputs() {
    if (g_busy) return;
    std::vector<size_t> indices; int row=-1;
    while ((row=ListView_GetNextItem(g_input,row,LVNI_SELECTED))!=-1) {
        LVITEMW item{}; item.mask=LVIF_PARAM; item.iItem=row; if(ListView_GetItem(g_input,&item)) indices.push_back(static_cast<size_t>(item.lParam));
    }
    std::sort(indices.rbegin(),indices.rend());
    for(size_t i:indices) if(i<g_sources.size()) g_sources.erase(g_sources.begin()+static_cast<ptrdiff_t>(i));
    RefreshInputList(); SetStatus(L"待处理列表中有 " + std::to_wstring(g_sources.size()) + L" 个 PDF。");
}

void SelectContextRow(HWND list, POINT screen_point) {
    if (screen_point.x == -1 && screen_point.y == -1) {
        int selected = ListView_GetNextItem(list,-1,LVNI_SELECTED);
        RECT row{};
        if (selected >= 0 && ListView_GetItemRect(list,selected,&row,LVIR_BOUNDS)) {
            POINT point{row.left + Px(24),row.bottom}; ClientToScreen(list,&point); screen_point=point;
        } else GetCursorPos(&screen_point);
    }
    POINT client = screen_point; ScreenToClient(list,&client);
    LVHITTESTINFO hit{}; hit.pt=client; int row=ListView_HitTest(list,&hit);
    if (row >= 0 && !(ListView_GetItemState(list,row,LVIS_SELECTED)&LVIS_SELECTED)) {
        ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_SetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
}

void OpenContainingFolder(std::wstring const& path) {
    std::wstring parameters = L"/select," + Quote(path);
    ShellExecuteW(g_main,L"open",L"explorer.exe",parameters.c_str(),nullptr,SW_SHOWNORMAL);
}

void CopyText(std::wstring const& text) {
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if (memory) {
        void* data=GlobalLock(memory); memcpy(data,text.c_str(),bytes); GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

void ShowInputContextMenu(POINT screen_point) {
    if (g_busy) return;
    SelectContextRow(g_input,screen_point);
    int selected=ListView_GetSelectedCount(g_input);
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_INPUT_OPEN,L"打开文件");
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_INPUT_LOCATION,L"在资源管理器中显示");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_INPUT_REMOVE,L"从列表移除所选项");
    AppendMenuW(menu,MF_STRING|(!g_sources.empty()?MF_ENABLED:MF_GRAYED),ID_CTX_INPUT_CLEAR,L"清空待处理列表");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(!g_sources.empty()?MF_ENABLED:MF_GRAYED),ID_CTX_INPUT_SELECT_ALL,L"全选");
    if (screen_point.x==-1&&screen_point.y==-1) GetCursorPos(&screen_point);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen_point.x,screen_point.y,0,g_main,nullptr);
    DestroyMenu(menu);
    if (command) SendMessageW(g_main,WM_COMMAND,command,0);
}

void ShowOutputContextMenu(POINT screen_point) {
    SelectContextRow(g_output,screen_point);
    int selected=ListView_GetSelectedCount(g_output);
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_OPEN,L"打开文件");
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_LOCATION,L"在资源管理器中显示");
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_SAVE,L"另存所选文件…");
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_COPY_PATH,L"复制完整路径");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(selected?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_REMOVE,L"从结果列表移除");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(!g_outputs.empty()?MF_ENABLED:MF_GRAYED),ID_CTX_OUTPUT_SELECT_ALL,L"全选");
    if (screen_point.x==-1&&screen_point.y==-1) GetCursorPos(&screen_point);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen_point.x,screen_point.y,0,g_main,nullptr);
    DestroyMenu(menu);
    if (command) SendMessageW(g_main,WM_COMMAND,command,0);
}

size_t FirstSelectedIndex(HWND list) {
    int row=ListView_GetNextItem(list,-1,LVNI_SELECTED);
    if(row<0) return static_cast<size_t>(-1);
    LVITEMW item{}; item.mask=LVIF_PARAM; item.iItem=row;
    return ListView_GetItem(list,&item)?static_cast<size_t>(item.lParam):static_cast<size_t>(-1);
}

void RemoveSelectedOutputs() {
    std::vector<size_t> indices; int row=-1;
    while((row=ListView_GetNextItem(g_output,row,LVNI_SELECTED))!=-1){
        LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=row;if(ListView_GetItem(g_output,&item))indices.push_back(static_cast<size_t>(item.lParam));
    }
    std::sort(indices.rbegin(),indices.rend());
    for(size_t i:indices)if(i<g_outputs.size())g_outputs.erase(g_outputs.begin()+static_cast<ptrdiff_t>(i));
    RefreshOutputList(); SetStatus(L"已从结果列表移除所选项，缓存文件仍然保留。");
}

void ClearCache() {
    if (g_busy) return;
    if (MessageBoxW(g_main,L"确定清空处理结果和缓存文件吗？\n源 PDF 不会被删除。",kProductName,MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    fs::path cache(WindowText(g_cache_edit)); std::error_code ec;
    fs::path results = fs::absolute(cache / L"Results", ec).lexically_normal();
    fs::path expected_parent = fs::absolute(cache, ec).lexically_normal();
    if (!ec && results.parent_path() == expected_parent && results.filename() == L"Results") fs::remove_all(results, ec);
    g_outputs.clear(); RefreshOutputList();
    // Engine files are intentionally retained for fast startup.
    SetStatus(L"缓存结果已清空，源文件未改动。");
}

void ShowAbout() {
    int choice = MessageBoxW(g_main,
        L"PDF 权限清理器 1.1\n"
        L"轻量、便携、完全本地运行\n\n"
        L"作者：曹虎男\n"
        L"项目主页：https://github.com/Hona-Cao/PDF-Rights-Cleaner\n"
        L"后续更新：https://github.com/Hona-Cao\n\n"
        L"如果这个工具对你有帮助，欢迎前往项目主页点亮 Star ★\n\n"
        L"本工具仅处理无需打开密码即可读取的 PDF 权限限制，"
        L"不支持未知打开密码。PDF 引擎为 qpdf 12.3.2。\n\n"
        L"选择“是”打开项目主页；选择“否”查看 qpdf 许可；选择“取消”关闭。",
        L"关于 PDF 权限清理器", MB_YESNOCANCEL | MB_ICONINFORMATION);
    if (choice == IDYES) {
        ShellExecuteW(g_main, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (choice != IDNO) return;
    HRSRC res = FindResourceW(g_instance, MAKEINTRESOURCEW(IDR_QPDF_LICENSE), RT_RCDATA);
    if (!res) return;
    HGLOBAL loaded = LoadResource(g_instance, res);
    DWORD size = SizeofResource(g_instance, res);
    void const* data = LockResource(loaded);
    fs::path path = fs::path(GetDataDirectory()) / L"qpdf-license.html";
    std::error_code ec; fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out && data && size) out.write(static_cast<char const*>(data), size);
    out.close();
    if (out) ShellExecuteW(g_main, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

COLORREF ButtonColor(int id, bool pressed, bool disabled) {
    COLORREF color = kSoftBlue;
    if (id==ID_PROCESS) color=kBlue;
    else if (id==ID_SAVE) color=kTeal;
    else if (id==ID_ADD) color=kIndigo;
    else if (id==ID_REMOVE || id==ID_CLEAR_CACHE) color=RGB(255,235,238);
    else if (id==ID_CACHE_BROWSE || id==ID_CACHE_OPEN || id==ID_ABOUT) color=RGB(232,238,255);
    if (disabled) return RGB(222,226,236);
    if (pressed) {
        int r=std::max(0,GetRValue(color)-22),g=std::max(0,GetGValue(color)-22),b=std::max(0,GetBValue(color)-22);
        return RGB(r,g,b);
    }
    return color;
}

void DrawModernButton(DRAWITEMSTRUCT const* item) {
    bool pressed=(item->itemState&ODS_SELECTED)!=0, disabled=(item->itemState&ODS_DISABLED)!=0;
    int id=static_cast<int>(item->CtlID);
    COLORREF fill=ButtonColor(id,pressed,disabled);
    bool strong=id==ID_PROCESS||id==ID_SAVE||id==ID_ADD;
    COLORREF text=disabled?RGB(130,138,155):(strong?RGB(255,255,255):(id==ID_REMOVE||id==ID_CLEAR_CACHE?RGB(185,28,28):kNavy));
    HBRUSH brush=CreateSolidBrush(fill);
    FillRect(item->hDC,&item->rcItem,brush);
    DeleteObject(brush);
    HBRUSH border=CreateSolidBrush(strong?fill:RGB(198,210,239));
    FrameRect(item->hDC,&item->rcItem,border);
    DeleteObject(border);
    wchar_t label[128]{};GetWindowTextW(item->hwndItem,label,static_cast<int>(std::size(label)));
    SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,text);SelectObject(item->hDC,g_font_bold);
    RECT rc=item->rcItem;if(pressed)OffsetRect(&rc,0,Px(1));
    DrawTextW(item->hDC,label,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-Px(4),-Px(4));DrawFocusRect(item->hDC,&focus);}
}

void PaintBackground(HWND hwnd) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    FillRect(dc,&rc,g_background_brush);
    int header_height=Px(84);
    RECT header{0,0,rc.right,header_height}; FillRect(dc,&header,g_header_brush);
    HBRUSH accent=CreateSolidBrush(RGB(45,212,191));RECT line{0,header_height,rc.right,header_height+Px(4)};FillRect(dc,&line,accent);DeleteObject(accent);
    EndPaint(hwnd,&ps);
}

void CreateControls(HWND hwnd) {
    auto make=[&](DWORD ex,wchar_t const* cls,wchar_t const* text,DWORD style,int id)->HWND{
        return CreateWindowExW(ex,cls,text,style|WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_instance,nullptr);
    };
    // Transparent extended style keeps the gradient header visible behind both
    // labels on classic themes and at non-100% DPI settings.
    g_title=make(0,L"STATIC",kProductName,SS_LEFT,0);
    g_subtitle=make(0,L"STATIC",L"批量解除无需打开密码的 PDF 权限限制 · 全程本地处理",SS_LEFT,0);
    g_add=make(0,L"BUTTON",L"＋ 添加 PDF",BS_OWNERDRAW,ID_ADD);
    g_remove=make(0,L"BUTTON",L"移除选中",BS_OWNERDRAW,ID_REMOVE);
    g_process=make(0,L"BUTTON",L"开始解除限制",BS_OWNERDRAW,ID_PROCESS);
    g_about=make(0,L"BUTTON",L"关于",BS_OWNERDRAW,ID_ABOUT);
    g_input_label=make(0,L"STATIC",L"待处理文件（可批量拖入文件或文件夹）",SS_LEFT,0);
    g_output_label=make(0,L"STATIC",L"处理结果（多选后可直接拖到资源管理器）",SS_LEFT,0);
    DWORD listStyle=LVS_REPORT|LVS_SHOWSELALWAYS;
    g_input=make(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",listStyle,ID_INPUT);
    g_output=make(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",listStyle,ID_OUTPUT);
    ListView_SetExtendedListViewStyle(g_input,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    ListView_SetExtendedListViewStyle(g_output,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    SetWindowTheme(g_input,L"Explorer",nullptr); SetWindowTheme(g_output,L"Explorer",nullptr);
    SetWindowSubclass(g_input,InputSubclass,1,0);
    SetWindowSubclass(g_output,OutputSubclass,1,0);
    g_cache_label=make(0,L"STATIC",L"缓存位置",SS_LEFT,0);
    g_cache_edit=make(WS_EX_CLIENTEDGE,L"EDIT",ReadCacheSetting().c_str(),ES_AUTOHSCROLL,ID_CACHE_EDIT);
    g_cache_browse=make(0,L"BUTTON",L"选择…",BS_OWNERDRAW,ID_CACHE_BROWSE);
    g_cache_open=make(0,L"BUTTON",L"打开缓存",BS_OWNERDRAW,ID_CACHE_OPEN);
    g_clear_cache=make(0,L"BUTTON",L"清空结果缓存",BS_OWNERDRAW,ID_CLEAR_CACHE);
    g_status=make(0,L"STATIC",L"将 PDF 或文件夹拖入窗口即可开始。源文件永远不会被修改。",SS_LEFT|SS_PATHELLIPSIS,ID_STATUS);
    g_save=make(0,L"BUTTON",L"保存选中项…",BS_OWNERDRAW,ID_SAVE);
    InitColumns(); SetFonts();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        g_main=hwnd; g_dpi=GetDpiForWindow(hwnd);
        g_background_brush=CreateSolidBrush(kBackground);
        g_header_brush=CreateSolidBrush(kIndigo);
        CreateControls(hwnd); DragAcceptFiles(hwnd,TRUE);
        { BOOL dark=FALSE; DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark)); }
        LoadCachedResults();
        return 0;
    case WM_SIZE: Layout(hwnd); return 0;
    case WM_GETMINMAXINFO: {
        auto limits=reinterpret_cast<MINMAXINFO*>(lp);
        limits->ptMinTrackSize.x=Px(820);
        limits->ptMinTrackSize.y=Px(520);
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi=HIWORD(wp); auto suggested=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        SetFonts(); Layout(hwnd); return 0;
    }
    case WM_PAINT: PaintBackground(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wp);HWND control=reinterpret_cast<HWND>(lp);
        SetBkMode(dc,OPAQUE);
        if(control==g_title){SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,kIndigo);return reinterpret_cast<LRESULT>(g_header_brush);}
        if(control==g_subtitle){SetTextColor(dc,RGB(219,234,254));SetBkColor(dc,kIndigo);return reinterpret_cast<LRESULT>(g_header_brush);}
        SetBkColor(dc,kBackground);
        if(control==g_input_label||control==g_output_label||control==g_cache_label)SetTextColor(dc,kNavy);
        else SetTextColor(dc,RGB(66,84,120));
        return reinterpret_cast<LRESULT>(g_background_brush);
    }
    case WM_DRAWITEM:
        if(reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType==ODT_BUTTON){DrawModernButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
        break;
    case WM_DROPFILES: {
        HDROP drop=reinterpret_cast<HDROP>(wp); UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0); std::vector<std::wstring> paths;
        for(UINT i=0;i<count;++i){ UINT n=DragQueryFileW(drop,i,nullptr,0); std::wstring p(n,L'\0'); DragQueryFileW(drop,i,p.data(),n+1); paths.push_back(std::move(p)); }
        DragFinish(drop); if(!g_busy) AddPaths(paths); return 0;
    }
    case WM_NOTIFY: {
        auto header = reinterpret_cast<NMHDR*>(lp);
        if (header && header->idFrom == ID_OUTPUT && header->code == LVN_BEGINDRAG) {
            BeginOutputDrag();
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ID_ADD: AddPaths(PickPdfFiles()); break;
        case ID_REMOVE: RemoveSelectedInputs(); break;
        case ID_PROCESS: StartProcessing(); break;
        case ID_CACHE_BROWSE: { auto p=PickFolder(L"选择缓存位置"); if(!p.empty()){SetWindowTextW(g_cache_edit,p.c_str());SaveCacheSetting(p);} break; }
        case ID_CACHE_OPEN: { auto p=WindowText(g_cache_edit); std::error_code ec; fs::create_directories(p,ec); ShellExecuteW(hwnd,L"open",p.c_str(),nullptr,nullptr,SW_SHOWNORMAL); break; }
        case ID_SAVE: SaveSelected(); break;
        case ID_CLEAR_CACHE: ClearCache(); break;
        case ID_ABOUT: ShowAbout(); break;
        case ID_CTX_INPUT_OPEN: {
            size_t i=FirstSelectedIndex(g_input);if(i<g_sources.size())ShellExecuteW(hwnd,L"open",g_sources[i].path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        }
        case ID_CTX_INPUT_LOCATION: {
            size_t i=FirstSelectedIndex(g_input);if(i<g_sources.size())OpenContainingFolder(g_sources[i].path);break;
        }
        case ID_CTX_INPUT_REMOVE: RemoveSelectedInputs(); break;
        case ID_CTX_INPUT_CLEAR: g_sources.clear();RefreshInputList();SetStatus(L"待处理列表已清空，源文件未改动。");break;
        case ID_CTX_INPUT_SELECT_ALL: ListView_SetItemState(g_input,-1,LVIS_SELECTED,LVIS_SELECTED);break;
        case ID_CTX_OUTPUT_OPEN: {
            size_t i=FirstSelectedIndex(g_output);if(i<g_outputs.size())ShellExecuteW(hwnd,L"open",g_outputs[i].path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        }
        case ID_CTX_OUTPUT_LOCATION: {
            size_t i=FirstSelectedIndex(g_output);if(i<g_outputs.size())OpenContainingFolder(g_outputs[i].path);break;
        }
        case ID_CTX_OUTPUT_SAVE: SaveSelected();break;
        case ID_CTX_OUTPUT_COPY_PATH: {
            auto paths=SelectedOutputPaths();std::wstring joined;for(auto const& p:paths){if(!joined.empty())joined+=L"\r\n";joined+=p;}CopyText(joined);SetStatus(L"已复制所选文件的完整路径。");break;
        }
        case ID_CTX_OUTPUT_REMOVE: RemoveSelectedOutputs();break;
        case ID_CTX_OUTPUT_SELECT_ALL: ListView_SetItemState(g_output,-1,LVIS_SELECTED,LVIS_SELECTED);break;
        }
        return 0;
    case WM_APP_RESULT: {
        std::unique_ptr<WorkerResult> r(reinterpret_cast<WorkerResult*>(lp));
        if(r && r->source_index<g_sources.size()) {
            g_sources[r->source_index].status=r->status;
            if(!r->output.empty()) g_outputs.push_back({g_sources[r->source_index].path,r->output,r->status,r->size,false});
            RefreshInputList(); RefreshOutputList();
            SetStatus(L"正在本地处理 " + std::to_wstring(r->source_index+1) + L" / " + std::to_wstring(g_sources.size()) + L" …");
        }
        return 0;
    }
    case WM_APP_FINISHED:
        g_busy=false; EnableWindow(g_process,TRUE); EnableWindow(g_add,TRUE); EnableWindow(g_remove,TRUE);
        SetStatus(L"处理完成：成功 " + std::to_wstring(g_outputs.size()) + L" 个。可在右侧多选并拖出保存。");
        return 0;
    case WM_CLOSE:
        if(g_busy){MessageBoxW(hwnd,L"文件正在处理中，请等待完成后再关闭。",kProductName,MB_OK|MB_ICONINFORMATION);return 0;}
        SaveCacheSetting(WindowText(g_cache_edit)); DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        RemoveWindowSubclass(g_input,InputSubclass,1);RemoveWindowSubclass(g_output,OutputSubclass,1);
        if(g_background_brush){DeleteObject(g_background_brush);g_background_brush=nullptr;}
        if(g_header_brush){DeleteObject(g_header_brush);g_header_brush=nullptr;}
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    g_instance=instance;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc == 5 && wcscmp(argv[1], L"--process-one") == 0) {
        fs::path qpdf;
        bool engine_ok = EnsureEngine(fs::path(argv[4]), qpdf);
        int result = 30;
        if (engine_ok) {
            std::wstring diagnostic;
            DWORD type = RunProcess(qpdf, L"--requires-password " + Quote(argv[2]), diagnostic);
            if (type == 0) result = 20;
            else if (type == 2) result = 21;
            else if (type == 3) {
                DWORD code = RunProcess(qpdf, L"--decrypt " + Quote(argv[2]) + L" " + Quote(argv[3]), diagnostic);
                result = (code == 0 || code == 3) ? 0 : 22;
            } else result = 23;
        }
        LocalFree(argv);
        return result;
    }
    if (argv) for (int i = 1; i < argc; ++i) g_startup_paths.emplace_back(argv[i]);
    if (argv) LocalFree(argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    OleInitialize(nullptr);
    WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=instance;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON)); wc.hIconSm=wc.hIcon;
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1); wc.lpszClassName=kClassName;
    if(!RegisterClassExW(&wc)) return 1;
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=std::min(1280,static_cast<int>(work.right-work.left)-Px(80));
    int height=std::min(760,static_cast<int>(work.bottom-work.top)-Px(80));
    width=std::max(width,900); height=std::max(height,600);
    HWND hwnd=CreateWindowExW(0,kClassName,kProductName,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                              work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,
                              width,height,nullptr,nullptr,instance,nullptr);
    if(!hwnd){OleUninitialize();return 2;}
    ShowWindow(hwnd,show); UpdateWindow(hwnd);
    if (!g_startup_paths.empty()) AddPaths(g_startup_paths);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(g_font)DeleteObject(g_font);if(g_font_bold)DeleteObject(g_font_bold);if(g_font_title)DeleteObject(g_font_title);
    OleUninitialize(); return static_cast<int>(msg.wParam);
}
