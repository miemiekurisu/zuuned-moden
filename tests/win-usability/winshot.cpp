// winshot - Windows-only window capture and paint evidence for the
// usability gate (tests/win-usability/run.ps1).
//
//   winshot <hwndDecimal> <out.png>
//   winshot --move <hwndDecimal> <width> <height>
//   winshot --foreground <hwndDecimal>
//   winshot --pid <processId>
//
// A capture is only evidence if someone can read it, so every shot prints one
// machine-parsable line with the statistics the gate asserts on:
//
//   ok src=printwindow rect=1600x900 distinct=18413 dominant=0.3119
//      mean=41.7 black=0.3102 white=0.0014
//
// Exit codes: 0 captured, 1 API failure, 2 usage, 3 captured but blank (fewer
// than four distinct colours, or one colour over 99.5% of the frame).
//
// Qt Quick on Windows presents through the window's own swapchain.  PrintWindow
// with PW_RENDERFULLCONTENT is the honest off-screen capture and works with the
// D3D/ANGLE path; where it still hands back an empty bitmap we retry as a screen
// BitBlt of the window rect, which reports what the user actually sees.  The
// line names the path that produced the frame, so a failure says which one lied.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// IStream: WIN32_LEAN_AND_MEAN hides it, and gdiplus.h needs it.
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const UINT kRenderFullContent = 0x2;  // PW_RENDERFULLCONTENT, Windows 8.1+

void makeDpiAware() {
    // Match the target window's real pixels: on a scaled desktop a
    // DPI-unaware caller sees virtualised coordinates for both the rect and
    // any MoveWindow it issues.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return;
    const HANDLE awareV2 = reinterpret_cast<HANDLE>(-4);  // PER_MONITOR_AWARE_V2
    using SetCtxFn = BOOL(WINAPI *)(HANDLE);
    auto setCtx = reinterpret_cast<SetCtxFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx && setCtx(awareV2))
        return;
    using SetThreadFn = BOOL(WINAPI *)(DWORD, HANDLE);
    auto setThread = reinterpret_cast<SetThreadFn>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (setThread)
        setThread(GetCurrentThreadId(), awareV2);
}

std::wstring wide(const char *utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring out(n > 1 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
    return out;
}

bool pngEncoderClsid(CLSID &out) {
    UINT count = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0)
        return false;
    std::vector<unsigned char> buffer(size);
    auto *info = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, size, info) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i) {
        if (info[i].MimeType && wcscmp(info[i].MimeType, L"image/png") == 0) {
            out = info[i].Clsid;
            return true;
        }
    }
    return false;
}

struct Stats {
    size_t distinct = 0;
    double dominant = 1.0, mean = 0.0, black = 1.0, white = 0.0;
    bool blank = true;
};

Stats measure(const std::vector<uint32_t> &px) {
    Stats s;
    if (px.empty())
        return s;
    std::unordered_map<uint32_t, uint64_t> histogram;
    histogram.reserve(1 << 14);
    double sum = 0.0;
    size_t black = 0, white = 0;
    for (uint32_t p : px) {
        ++histogram[p & 0xFFFFFFu];
        const int b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF;
        sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (r < 8 && g < 8 && b < 8)
            ++black;
        if (r >= 250 && g >= 250 && b >= 250)
            ++white;
    }
    uint64_t top = 0;
    for (const auto &kv : histogram)
        top = kv.second > top ? kv.second : top;
    const double total = static_cast<double>(px.size());
    s.distinct = histogram.size();
    s.dominant = static_cast<double>(top) / total;
    s.mean = sum / total;
    s.black = static_cast<double>(black) / total;
    s.white = static_cast<double>(white) / total;
    s.blank = s.distinct < 4 || s.dominant > 0.995;
    return s;
}

HBITMAP newTopDown32(int w, int h, BITMAPINFO &bi) {
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // negative == top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    ReleaseDC(nullptr, screen);
    return bmp;
}

bool readBits(HBITMAP bmp, int w, int h, BITMAPINFO &bi,
              std::vector<uint32_t> &pixels) {
    pixels.assign(static_cast<size_t>(w) * h, 0);
    HDC dc = GetDC(nullptr);
    const int rows = GetDIBits(dc, bmp, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return rows == h;
}

bool paintInto(HWND hwnd, int w, int h, HBITMAP bmp, bool fromScreen) {
    HDC dest = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = SelectObject(dest, bmp);
    bool ok = false;
    if (fromScreen) {
        RECT rc{};
        if (GetWindowRect(hwnd, &rc)) {
            HDC screen = GetDC(nullptr);
            ok = BitBlt(dest, 0, 0, w, h, screen, rc.left, rc.top, SRCCOPY) != FALSE;
            ReleaseDC(nullptr, screen);
        }
    } else {
        ok = PrintWindow(hwnd, dest, kRenderFullContent) != FALSE;
    }
    SelectObject(dest, previous);
    DeleteDC(dest);
    return ok;
}

bool savePng(const std::vector<uint32_t> &px, int w, int h, const char *path) {
    CLSID clsid;
    if (!pngEncoderClsid(clsid)) {
        fprintf(stderr, "winshot: no PNG encoder\n");
        return false;
    }
    std::vector<uint32_t> pixels = px;  // Gdiplus needs a mutable buffer
    Gdiplus::Bitmap bitmap(w, h, w * 4, PixelFormat32bppARGB,
                           reinterpret_cast<unsigned char *>(pixels.data()));
    const std::wstring utf16 = wide(path);
    const Gdiplus::Status status = bitmap.Save(utf16.c_str(), &clsid, nullptr);
    if (status != Gdiplus::Ok) {
        fprintf(stderr, "winshot: save %s failed (%d)\n", path, int(status));
        return false;
    }
    return true;
}

int shoot(HWND hwnd, const char *outPath) {
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) {
        fprintf(stderr, "winshot: GetWindowRect failed (%lu)\n", GetLastError());
        return 1;
    }
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 1 || h <= 1) {
        fprintf(stderr, "winshot: window is %dx%d - not visible\n", w, h);
        return 1;
    }
    BITMAPINFO bi{};
    HBITMAP bmp = newTopDown32(w, h, bi);
    if (!bmp) {
        fprintf(stderr, "winshot: CreateCompatibleBitmap failed (%lu)\n",
                GetLastError());
        return 1;
    }
    std::vector<uint32_t> pixels;
    const char *source = "printwindow";
    bool have = paintInto(hwnd, w, h, bmp, false) &&
                readBits(bmp, w, h, bi, pixels);
    Stats stats = have ? measure(pixels) : Stats();
    if (!have || stats.blank) {
        // The GPU path gave nothing usable; read the screen instead.
        std::vector<uint32_t> screenPixels;
        if (paintInto(hwnd, w, h, bmp, true) &&
            readBits(bmp, w, h, bi, screenPixels)) {
            const Stats screenStats = measure(screenPixels);
            if (!have || !screenStats.blank) {
                pixels = std::move(screenPixels);
                stats = screenStats;
                source = "screen";
            }
        }
    }
    DeleteObject(bmp);
    printf("ok src=%s rect=%dx%d distinct=%zu dominant=%.4f mean=%.1f "
           "black=%.4f white=%.4f\n",
           source, w, h, stats.distinct, stats.dominant, stats.mean, stats.black,
           stats.white);
    fflush(stdout);
    if (stats.blank) {
        fprintf(stderr, "winshot: frame is blank via %s\n", source);
        return 3;
    }
    if (outPath && *outPath && !savePng(pixels, w, h, outPath))
        return 1;
    return 0;
}

HWND handle(const char *arg) {
    char *end = nullptr;
    const unsigned long long raw = strtoull(arg, &end, 10);
    if (!end || *end)
        return nullptr;
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(raw));
}

struct PidSearch {
    DWORD pid;
    HWND titled;
    HWND untitled;
};

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM lParam) {
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    PidSearch *found = reinterpret_cast<PidSearch *>(lParam);
    if (wpid != found->pid || !IsWindowVisible(hwnd))
        return TRUE;
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    if (GetWindowTextLengthW(hwnd) > 0) {
        if (!found->titled)
            found->titled = hwnd;
    } else if (!found->untitled) {
        found->untitled = hwnd;
    }
    return found->titled ? FALSE : TRUE;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage:\n"
                "  winshot <hwndDecimal> <out.png>\n"
                "  winshot --move <hwndDecimal> <width> <height>\n"
                "  winshot --foreground <hwndDecimal>\n"
                "  winshot --pid <processId>\n");
        return 2;
    }
    makeDpiAware();
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    const bool gdiplus =
        Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok;
    struct Guard {
        bool on;
        ULONG_PTR *token;
        ~Guard() { if (on) Gdiplus::GdiplusShutdown(*token); }
    } guard{gdiplus, &token};

    const std::string mode = argv[1];
    if (mode == "--pid") {
        // .NET reports MainWindowHandle as zero for the window Qt Quick really
        // shows, which left the usability gate waiting 30 s for a frame that was
        // on screen the whole time and then killing a healthy app.  Enumerating
        // by process id is the honest way to find what the user sees: a visible,
        // titled, non-tool window wins, and an untitled one is still reported
        // instead of nothing.
        char *end = nullptr;
        const unsigned long pid = strtoul(argv[2], &end, 10);
        if (!end || *end || pid == 0) {
            fprintf(stderr, "winshot: not a process id: %s\n", argv[2]);
            return 2;
        }
        PidSearch search {static_cast<DWORD>(pid), nullptr, nullptr};
        EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&search));
        const HWND found = search.titled ? search.titled : search.untitled;
        if (!found) {
            fprintf(stderr, "winshot: no visible window for pid %lu\n", pid);
            return 2;
        }
        printf("hwnd=%llu pid=%lu titled=%d\n",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(found)),
               pid, search.titled ? 1 : 0);
        return 0;
    }
    if (mode == "--move" || mode == "--foreground") {
        HWND hwnd = handle(argv[2]);
        if (!hwnd || !IsWindow(hwnd)) {
            fprintf(stderr, "winshot: not a window: %s\n", argv[2]);
            return 2;
        }
        if (mode == "--foreground") {
            // PrintWindow works regardless, but the screen fallback needs the
            // window unobstructed.
            ShowWindow(hwnd, SW_RESTORE);
            if (!SetForegroundWindow(hwnd))
                printf("warn SetForegroundWindow failed (%lu)\n", GetLastError());
        } else {
            if (argc < 5)
                return 2;
            RECT rc{};
            if (!GetWindowRect(hwnd, &rc))
                return 1;
            if (!MoveWindow(hwnd, rc.left, rc.top, atoi(argv[3]), atoi(argv[4]),
                            TRUE)) {
                fprintf(stderr, "winshot: MoveWindow failed (%lu)\n",
                        GetLastError());
                return 1;
            }
        }
        RECT after{}, client{};
        if (!GetWindowRect(hwnd, &after))
            return 1;
        GetClientRect(hwnd, &client);
        printf("ok %s rect=%dx%d client=%dx%d\n", mode.c_str() + 2,
               after.right - after.left, after.bottom - after.top,
               client.right - client.left, client.bottom - client.top);
        return 0;
    }
    HWND hwnd = handle(argv[1]);
    if (!hwnd || !IsWindow(hwnd)) {
        fprintf(stderr, "winshot: not a window: %s\n", argv[1]);
        return 2;
    }
    return shoot(hwnd, argv[2]);
}
