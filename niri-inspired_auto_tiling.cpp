// ==WindhawkMod==
// @id              niri-inspired-autotiling
// @name            Niri-inspired Auto Tiling
// @description     Scrollable column tiling for Windows, inspired by niri: infinite horizontal strip, animations, focus ring, tabbed columns.
// @version         0.1
// @author          AndrewLu99
// @github          https://github.com/andrewlunardi
// @include         explorer.exe
// @compilerOptions -ldwmapi -lshell32 -ladvapi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Niri-inspired Auto Tiling

Niri-style scrollable tiling for Windows. The mod runs inside `explorer.exe`
and keeps, per monitor, a horizontal **strip of columns**. Each column holds
one or more windows stacked vertically. The strip is wider than the screen
when needed and scrolls to keep the focused column in view.

## What's in 0.1.0

- **Infinite horizontal scroll.** Columns keep their width (default 50%) and
  the strip scrolls instead of squeezing everything into the screen.
- **Real column model.** New windows open as a new column right of the focused
  one. Move/consume/expel work on real columns, not on guessed geometry.
- **Animations** (position and size) with easing.
- **Focus ring** via the DWM border color (Windows 11).
- **Tabbed columns** (windows stacked in the same spot, focus cycles).
- **Minimum-size aware.** Asks windows for their minimum size and also learns it
  from what the window actually ended up with, then widens the column.
- **Correct gaps.** Invisible resize borders of Win10/11 windows are compensated.
- **Non-blocking.** Positioning is asynchronous, so a hung app cannot freeze
  the mod or block unloading.
- Dragging a window uses the real move/size events (not geometry guessing).
- File Explorer windows can be tiled. Elevated windows are detected and skipped.
- Ignore lists for processes and window classes.
- Windows parked off-screen are restored on unload, and rescued after a crash.
- Hardening (0.3.1): robust focus switching, stuck-drag recovery, no false
  "minimum size" learning from slow apps, windows that Windows pulls back
  from the parking area are re-parked, no animation across neighboring
  monitors (avoids DPI-change glitches), safer handling of reused window handles.

## Hotkeys (default modifier: Ctrl+Alt, configurable)

| Shortcut | Action |
|----------|--------|
| `Left / Right` | Focus column left / right |
| `Up / Down` | Focus window above / below in the column |
| `Home / End` | Focus first / last column |
| `Shift+Left / Right` | Move column left / right |
| `Shift+Up / Down` | Move window up / down inside the column |
| `[` / `]` | Consume window into the left/right column, or expel it out of a stack |
| `W` | Cycle column width 33 / 50 / 67 / 100% |
| `Space` | Toggle full-width column |
| `C` | Center the focused column |
| `T` | Toggle tabbed column |
| `Shift+F` | Toggle floating |
| `R` | Retile (also un-floats everything) |

Some combinations (for example Ctrl+Alt+Arrows when Intel graphics hotkeys are
enabled, or Win+R / Win+T) are already taken. Pick the Ctrl+Alt+Win modifier in
the settings to avoid most conflicts. Registration failures are written to the log.

## Behavior on multiple monitors

Windows on Windows cannot be clipped to a monitor. Columns that are not
(fully) visible are therefore parked just outside the virtual desktop, so they
never show up on the neighboring monitor. Partially visible columns ("peeking")
are only shown on sides that have no neighboring monitor.

## Limitations that cannot be fixed from inside a Windhawk mod

- **Elevated (admin) windows** cannot be moved by a non-elevated Explorer
  (UIPI). They are detected and left floating.
- **Minimum size:** the mod can only respect it. It cannot make an app shrink
  below what it allows, so the column becomes wider and the strip scrolls.
- **Focus ring** is the 1px DWM border color (Windows 11 only), not a thick
  compositor ring. A thick ring would need an overlay window.
- **Tabbed columns** stack the windows in place. There is no tab bar.
- Workspaces are not implemented. Windows from all virtual desktops share the
  strip; windows on other desktops are simply skipped while they are cloaked.
- Fullscreen apps and presentations pause the mod.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- gap: 12
  $name: Gap (pixels)
  $description: Space between windows and between windows and the work area edge.
- defaultColumnWidthPercent: 50
  $name: New column width (%)
  $description: Width of a new column as a percentage of the work area. The width hotkey cycles 33 / 50 / 67 / 100%.
- singleWindowWidthPercent: 70
  $name: Lone column width (%)
  $description: Width used while only one column exists (until you resize it).
- centerSingleColumn: true
  $name: Center a lone column
- alwaysCenterFocusedColumn: false
  $name: Always center the focused column
  $description: Off = scroll the minimum amount needed to reveal the focused column.
- showPartialColumns: true
  $name: Show partially visible columns at screen edges
  $description: Only on sides without a neighboring monitor. Otherwise they are parked off-screen.
- animationMs: 160
  $name: Animation duration (ms)
  $description: 0 disables animations.
- animateResize: true
  $name: Animate size changes
  $description: Off = the size snaps immediately and only the position animates (less flicker in heavy apps).
- focusRing: true
  $name: Focus ring
  $description: Colors the border of the focused window. Windows 11 only.
- focusRingColor: "#5BA8FF"
  $name: Focus ring color (hex RRGGBB)
- automaticallyFloatWhenMoved: true
  $name: Float windows that are dragged or resized by hand
  $description: Off = a dragged window snaps back, or joins the monitor it was dropped on.
- tileFileExplorer: true
  $name: Tile File Explorer windows
- tileUwpApps: false
  $name: Tile UWP apps (ApplicationFrameWindow), experimental
- enableHotkeys: true
  $name: Enable hotkeys
- hotkeyModifiers: ctrlalt
  $name: Hotkey modifier
  $options:
  - ctrlalt: Ctrl+Alt
  - win: Win (some combinations are reserved by Windows)
  - ctrlaltwin: Ctrl+Alt+Win (avoids most conflicts, e.g. Intel graphics hotkeys)
- retileIntervalMs: 1500
  $name: Safety-net rescan interval (ms)
  $description: Catches changes that raise no event (for example maximize). 0 disables the timer.
- ignoredProcesses: [mstsc.exe]
  $name: Ignored processes
  $description: Windows of these executables are never tiled.
- ignoredClasses: ["#32770"]
  $name: Ignored window classes
  $description: Windows of these classes are never tiled.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr int   kParkGap            = 64;               // distance outside the virtual desktop
constexpr UINT  kMsgSettings        = WM_APP + 0x52;
constexpr DWORD kDwmaBorderColor    = 34;               // DWMWA_BORDER_COLOR (Win11)
constexpr DWORD kDwmColorDefault    = 0xFFFFFFFF;       // DWMWA_COLOR_DEFAULT
constexpr int   kMinWindowW         = 120;
constexpr int   kMinWindowH         = 80;
constexpr int   kSizeLearnTolerance = 16;               // bigger than asked => app minimum
constexpr int   kMaxLearnTries      = 3;

enum HotkeyId {
    kHkFocusLeft = 1, kHkFocusRight, kHkFocusUp, kHkFocusDown,
    kHkMoveColLeft, kHkMoveColRight, kHkMoveWinUp, kHkMoveWinDown,
    kHkConsumeLeft, kHkConsumeRight,
    kHkCycleWidth, kHkFullWidth, kHkCenter, kHkTabbed,
    kHkToggleFloat, kHkRetile, kHkFirst, kHkLast,
};

// ---------------------------------------------------------------------------
// Settings (owned by the worker thread after start-up)
// ---------------------------------------------------------------------------
struct Settings {
    int  gap                       = 12;
    int  defaultColumnWidthPercent = 50;
    int  singleWindowWidthPercent  = 70;
    bool centerSingleColumn        = true;
    bool alwaysCenterFocused       = false;
    bool showPartialColumns        = true;
    int  animationMs               = 160;
    bool animateResize             = true;
    bool focusRing                 = true;
    COLORREF ringColor             = RGB(0x5B, 0xA8, 0xFF);
    bool floatWhenMoved            = true;
    bool tileFileExplorer          = true;
    bool tileUwpApps               = false;
    bool enableHotkeys             = true;
    int  modifierMode              = 0;     // 0 Ctrl+Alt, 1 Win, 2 Ctrl+Alt+Win
    int  retileIntervalMs          = 1500;
    std::vector<std::wstring> ignoredProcesses;
    std::vector<std::wstring> ignoredClasses;
};

Settings g_cfg;

int MaxI(int a, int b) { return a > b ? a : b; }
int MinI(int a, int b) { return a < b ? a : b; }
int ClampI(int v, int lo, int hi) { return MaxI(lo, MinI(v, hi)); }
int RW(const RECT& r) { return r.right - r.left; }
int RH(const RECT& r) { return r.bottom - r.top; }

void LoadStringList(PCWSTR fmt, std::vector<std::wstring>& out) {
    out.clear();
    for (int i = 0; i < 128; ++i) {
        PCWSTR s = Wh_GetStringSetting(fmt, i);
        const bool ok = s && *s;
        if (ok) out.emplace_back(s);
        Wh_FreeStringSetting(s);
        if (!ok) break;
    }
}

COLORREF ParseColor(PCWSTR s, COLORREF fallback) {
    if (!s || !*s) return fallback;
    if (*s == L'#') ++s;
    wchar_t* end = nullptr;
    const unsigned long v = wcstoul(s, &end, 16);
    if (end == s) return fallback;
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void LoadSettings() {
    Settings s;
    s.gap                       = ClampI(Wh_GetIntSetting(L"gap"), 0, 64);
    s.defaultColumnWidthPercent = ClampI(Wh_GetIntSetting(L"defaultColumnWidthPercent"), 20, 100);
    s.singleWindowWidthPercent  = ClampI(Wh_GetIntSetting(L"singleWindowWidthPercent"), 30, 100);
    s.centerSingleColumn        = Wh_GetIntSetting(L"centerSingleColumn") != 0;
    s.alwaysCenterFocused       = Wh_GetIntSetting(L"alwaysCenterFocusedColumn") != 0;
    s.showPartialColumns        = Wh_GetIntSetting(L"showPartialColumns") != 0;
    s.animationMs               = ClampI(Wh_GetIntSetting(L"animationMs"), 0, 1000);
    s.animateResize             = Wh_GetIntSetting(L"animateResize") != 0;
    s.focusRing                 = Wh_GetIntSetting(L"focusRing") != 0;
    s.floatWhenMoved            = Wh_GetIntSetting(L"automaticallyFloatWhenMoved") != 0;
    s.tileFileExplorer          = Wh_GetIntSetting(L"tileFileExplorer") != 0;
    s.tileUwpApps               = Wh_GetIntSetting(L"tileUwpApps") != 0;
    s.enableHotkeys             = Wh_GetIntSetting(L"enableHotkeys") != 0;
    s.retileIntervalMs          = ClampI(Wh_GetIntSetting(L"retileIntervalMs"), 0, 10000);

    PCWSTR color = Wh_GetStringSetting(L"focusRingColor");
    s.ringColor = ParseColor(color, s.ringColor);
    Wh_FreeStringSetting(color);

    PCWSTR mods = Wh_GetStringSetting(L"hotkeyModifiers");
    s.modifierMode = !mods ? 0 : wcscmp(mods, L"win") == 0 ? 1
                   : wcscmp(mods, L"ctrlaltwin") == 0 ? 2 : 0;
    Wh_FreeStringSetting(mods);

    LoadStringList(L"ignoredProcesses[%d]", s.ignoredProcesses);
    LoadStringList(L"ignoredClasses[%d]", s.ignoredClasses);
    g_cfg = std::move(s);
}

// ---------------------------------------------------------------------------
// Model (worker thread only)
// ---------------------------------------------------------------------------
struct WinInfo {
    HMONITOR mon       = nullptr;     // strip this window belongs to
    bool     floating  = false;
    bool     moving    = false;       // user is dragging / resizing it
    bool     ring      = false;

    RECT     original{};              // outer rect before we touched it
    bool     hasOriginal = false;
    RECT     inset{};                 // outer rect minus visible frame
    int      minW = 0, minH = 0;      // minimum *visible* size
    int      learnTries = 0;

    RECT     cur{};                   // visible rect we last commanded
    bool     hasCur = false;
    RECT     from{}, to{};
    DWORD    t0 = 0;
    bool     animating = false;

    DWORD    pid = 0;                 // guards against reused window handles
    DWORD    lastApplyTick = 0;
    int      fixTries = 0;            // position corrections for the current target
    int      candW = 0, candH = 0;    // oversize seen once, awaiting confirmation
    DWORD    movingSince = 0;
};

struct Column {
    std::vector<HWND> wins;           // top to bottom
    HWND   last      = nullptr;       // last focused window in this column
    double prop      = 0.5;           // width as a fraction of the work area
    bool   full      = false;
    bool   tabbed    = false;
    bool   userSized = false;
};

struct Strip {
    std::vector<Column> cols;
    HWND focusWin       = nullptr;
    int  target         = 0;          // scroll offset (strip space)
    bool centerRequest  = false;
};

struct MonInfo {
    HMONITOR hmon;
    RECT     rect;
    RECT     work;
    bool     primary;
};

std::unordered_map<HWND, WinInfo> g_wins;
std::map<HMONITOR, Strip>         g_strips;
std::unordered_map<HWND, DWORD>   g_blocked;     // hwnd -> pid, rejected at discovery

HANDLE g_thread = nullptr;
HANDLE g_ready  = nullptr;
DWORD  g_threadId = 0;

HWINEVENTHOOK g_hooks[5]{};
UINT_PTR g_timerPeriodic = 0, g_timerRelayout = 0, g_timerAnim = 0, g_timerVerify = 0;

bool  g_firstScan   = true;
bool  g_inRelayout  = false;
bool  g_relayoutPending = false;
int   g_verifyChain = 0;
RECT  g_vs{};                        // virtual screen, refreshed every layout
HWND  g_ringWnd     = nullptr;
bool  g_ringErrLogged = false;
HWND  g_intentWnd   = nullptr;       // window we just asked to focus
DWORD g_intentTick  = 0;

void Relayout();
void ReconcileMoving();

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
RECT VirtualScreen() {
    RECT r;
    r.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right  = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

bool IsOffscreen(const RECT& r) {
    RECT out;
    return !IntersectRect(&out, &r, &g_vs);
}

bool VisibleRect(HWND h, RECT* out) {
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, out, sizeof(*out)))) {
        return true;
    }
    return GetWindowRect(h, out) != FALSE;
}

void RefreshInsets(HWND h, WinInfo& w) {
    RECT o{}, f{};
    if (!GetWindowRect(h, &o)) return;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f)))) return;
    RECT in{f.left - o.left, f.top - o.top, o.right - f.right, o.bottom - f.bottom};
    if (in.left < 0 || in.top < 0 || in.right < 0 || in.bottom < 0 ||
        in.left > 64 || in.top > 64 || in.right > 64 || in.bottom > 64) {
        return;
    }
    w.inset = in;
}

void QueryMinSize(HWND h, WinInfo& w) {
    MINMAXINFO mmi{};
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(h, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi),
                             SMTO_ABORTIFHUNG | SMTO_NORMAL, 30, &result)) {
        return;
    }
    const int mw = mmi.ptMinTrackSize.x - (w.inset.left + w.inset.right);
    const int mh = mmi.ptMinTrackSize.y - (w.inset.top + w.inset.bottom);
    w.minW = MaxI(w.minW, MaxI(mw, 0));
    w.minH = MaxI(w.minH, MaxI(mh, 0));
}

std::wstring GetClass(HWND h) {
    wchar_t buf[256]{};
    GetClassNameW(h, buf, ARRAYSIZE(buf));
    return buf;
}

std::wstring GetExeName(DWORD pid) {
    std::wstring name;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return name;
    wchar_t path[MAX_PATH]{};
    DWORD n = ARRAYSIZE(path);
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        const wchar_t* slash = wcsrchr(path, L'\\');
        name = slash ? slash + 1 : path;
    }
    CloseHandle(p);
    return name;
}

// True when the process is out of our reach (higher integrity / protected).
bool IsOutOfReach(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return GetLastError() == ERROR_ACCESS_DENIED;
    HANDLE token = nullptr;
    bool denied = false;
    if (!OpenProcessToken(p, TOKEN_QUERY, &token)) {
        denied = GetLastError() == ERROR_ACCESS_DENIED;
    } else {
        CloseHandle(token);
    }
    CloseHandle(p);
    return denied;
}

bool ListContains(const std::vector<std::wstring>& list, const std::wstring& v) {
    for (const auto& s : list) {
        if (_wcsicmp(s.c_str(), v.c_str()) == 0) return true;
    }
    return false;
}

bool IsFullscreenBusy() {
    QUERY_USER_NOTIFICATION_STATE st{};
    if (FAILED(SHQueryUserNotificationState(&st))) return false;
    return st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN ||
           st == QUNS_PRESENTATION_MODE;
}

bool IsCloaked(HWND h) {
    DWORD c = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof(c))) && c != 0;
}

// The window the user is effectively on: a window we just asked to focus wins
// over a foreground that has not caught up yet.
HWND EffectiveForeground() {
    HWND fg = GetForegroundWindow();
    if (g_intentWnd) {
        if (fg == g_intentWnd || !IsWindow(g_intentWnd) || GetTickCount() - g_intentTick >= 250) {
            g_intentWnd = nullptr;
        } else {
            return g_intentWnd;
        }
    }
    return fg;
}

// Is this tracked window taking part in the layout right now?
bool IsTiledActive(HWND h) {
    auto it = g_wins.find(h);
    if (it == g_wins.end()) return false;
    const WinInfo& w = it->second;
    if (w.floating || w.moving) return false;
    if (!IsWindowVisible(h) || IsIconic(h) || IsZoomed(h)) return false;
    return !IsCloaked(h);
}

// ---------------------------------------------------------------------------
// Window discovery
// ---------------------------------------------------------------------------
enum class Verdict { NotNow, Never, Yes };

bool IsExcludedClass(const std::wstring& cls) {
    static const wchar_t* kExcluded[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
        L"TopLevelWindowForOverflowXamlIsland",
        L"Windows.Internal.Shell.TabProxyWindow", L"NotifyIconOverflowWindow",
    };
    for (const wchar_t* e : kExcluded) {
        if (_wcsicmp(cls.c_str(), e) == 0) return true;
    }
    return false;
}

Verdict Classify(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return Verdict::NotNow;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return Verdict::NotNow;
    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow()) return Verdict::Never;

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR ex    = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (style & WS_CHILD) return Verdict::Never;
    if (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST)) return Verdict::NotNow;
    if (!(style & WS_THICKFRAME)) return Verdict::NotNow;      // fixed-size windows
    if (GetWindow(hwnd, GW_OWNER)) return Verdict::NotNow;      // dialogs / popups
    if (IsIconic(hwnd) || IsZoomed(hwnd)) return Verdict::NotNow;
    if (IsCloaked(hwnd)) return Verdict::NotNow;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return Verdict::NotNow;
    if (RW(rc) < kMinWindowW || RH(rc) < kMinWindowH) return Verdict::NotNow;

    const std::wstring cls = GetClass(hwnd);
    if (IsExcludedClass(cls) || ListContains(g_cfg.ignoredClasses, cls)) return Verdict::Never;
    if (!g_cfg.tileUwpApps && _wcsicmp(cls.c_str(), L"ApplicationFrameWindow") == 0) {
        return Verdict::Never;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return Verdict::NotNow;

    if (ListContains(g_cfg.ignoredProcesses, GetExeName(pid))) return Verdict::Never;

    if (pid == GetCurrentProcessId()) {
        // Only real File Explorer windows; everything else in explorer.exe is shell UI.
        return (g_cfg.tileFileExplorer && _wcsicmp(cls.c_str(), L"CabinetWClass") == 0)
                   ? Verdict::Yes : Verdict::Never;
    }
    if (IsOutOfReach(pid)) {
        Wh_Log(L"Skipping window %p: process is out of reach (elevated?)", hwnd);
        return Verdict::Never;
    }
    return Verdict::Yes;
}

BOOL CALLBACK EnumTopLevel(HWND hwnd, LPARAM lp) {
    if (IsWindowVisible(hwnd)) {
        reinterpret_cast<std::vector<HWND>*>(lp)->push_back(hwnd);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// Column helpers
// ---------------------------------------------------------------------------
int FindColumn(const Strip& s, HWND h) {
    if (!h) return -1;
    for (size_t i = 0; i < s.cols.size(); ++i) {
        const auto& w = s.cols[i].wins;
        if (std::find(w.begin(), w.end(), h) != w.end()) return static_cast<int>(i);
    }
    return -1;
}

void RemoveFromColumns(HWND h) {
    for (auto& [mon, s] : g_strips) {
        for (size_t ci = 0; ci < s.cols.size(); ++ci) {
            auto& w = s.cols[ci].wins;
            auto p = std::find(w.begin(), w.end(), h);
            if (p == w.end()) continue;

            const size_t idx = static_cast<size_t>(p - w.begin());
            w.erase(p);
            HWND next = nullptr;
            if (w.empty()) {
                s.cols.erase(s.cols.begin() + static_cast<std::ptrdiff_t>(ci));
                if (s.focusWin == h && !s.cols.empty()) {
                    const size_t k = ci > 0 ? ci - 1 : 0;
                    next = s.cols[k].wins.front();
                }
            } else if (s.focusWin == h) {
                next = w[MinI(static_cast<int>(idx), static_cast<int>(w.size()) - 1)];
            }
            if (s.focusWin == h) s.focusWin = next;
            return;   // a stale Column::last is harmless: PickActive re-validates it
        }
    }
}

void InsertColumnAfterFocus(HMONITOR mon, HWND h) {
    Strip& s = g_strips[mon];
    Column c;
    c.wins = {h};
    c.last = h;
    c.prop = g_cfg.defaultColumnWidthPercent / 100.0;

    const int fc = FindColumn(s, s.focusWin);
    const size_t pos = (g_firstScan || fc < 0) ? s.cols.size() : static_cast<size_t>(fc) + 1;
    s.cols.insert(s.cols.begin() + static_cast<std::ptrdiff_t>(pos), std::move(c));
    if (!g_firstScan) s.focusWin = h;
}

void AddWindow(HWND h) {
    WinInfo w;
    GetWindowThreadProcessId(h, &w.pid);
    w.mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    if (GetWindowRect(h, &w.original)) w.hasOriginal = true;
    RefreshInsets(h, w);
    if (VisibleRect(h, &w.cur)) w.hasCur = true;
    w.to = w.cur;
    QueryMinSize(h, w);
    const HMONITOR mon = w.mon;
    g_wins.emplace(h, std::move(w));
    InsertColumnAfterFocus(mon, h);
}

void DiscoverWindows() {
    std::vector<HWND> all;
    EnumWindows(EnumTopLevel, reinterpret_cast<LPARAM>(&all));

    std::vector<HWND> fresh;
    for (HWND h : all) {
        if (g_wins.count(h)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        auto b = g_blocked.find(h);
        if (b != g_blocked.end()) {
            if (b->second == pid) continue;
            g_blocked.erase(b);            // handle was reused by another process
        }
        const Verdict v = Classify(h);
        if (v == Verdict::Yes) fresh.push_back(h);
        else if (v == Verdict::Never) g_blocked[h] = pid;
    }
    // EnumWindows is top-to-bottom in z-order: add the oldest first.
    std::reverse(fresh.begin(), fresh.end());
    for (HWND h : fresh) AddWindow(h);
}

void PruneDead() {
    for (auto it = g_wins.begin(); it != g_wins.end();) {
        bool alive = IsWindow(it->first) != FALSE;
        if (alive) {
            DWORD pid = 0;
            GetWindowThreadProcessId(it->first, &pid);
            alive = pid == it->second.pid;      // reused handle => treat as dead
        }
        if (alive) { ++it; continue; }
        RemoveFromColumns(it->first);
        if (g_ringWnd == it->first) g_ringWnd = nullptr;
        if (g_intentWnd == it->first) g_intentWnd = nullptr;
        it = g_wins.erase(it);
    }
    for (auto it = g_blocked.begin(); it != g_blocked.end();) {
        it = IsWindow(it->first) ? std::next(it) : g_blocked.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------
BOOL CALLBACK MonitorEnumProc(HMONITOR hm, HDC, LPRECT, LPARAM lp) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hm, &mi)) {
        reinterpret_cast<std::vector<MonInfo>*>(lp)->push_back(
            {hm, mi.rcMonitor, mi.rcWork, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    return TRUE;
}

std::vector<MonInfo> GetMonitors() {
    std::vector<MonInfo> v;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&v));
    return v;
}

// A monitor that disappeared hands its columns to the primary one.
void ReconcileMonitors(const std::vector<MonInfo>& mons) {
    if (mons.empty()) return;
    HMONITOR primary = mons.front().hmon;
    for (const auto& m : mons) if (m.primary) primary = m.hmon;

    for (auto it = g_strips.begin(); it != g_strips.end();) {
        const bool exists = std::any_of(mons.begin(), mons.end(),
                                        [&](const MonInfo& m) { return m.hmon == it->first; });
        if (exists) { ++it; continue; }
        Strip& dst = g_strips[primary];
        for (Column& c : it->second.cols) {
            for (HWND h : c.wins) g_wins[h].mon = primary;
            dst.cols.push_back(std::move(c));
        }
        it = g_strips.erase(it);
    }
}

bool HasNeighbor(const MonInfo& m, const std::vector<MonInfo>& mons, int side) {
    for (const auto& o : mons) {
        if (o.hmon == m.hmon) continue;
        if (!(o.rect.top < m.rect.bottom && o.rect.bottom > m.rect.top)) continue;
        if (side < 0 && o.rect.right <= m.rect.left + 1) return true;
        if (side > 0 && o.rect.left >= m.rect.right - 1) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Positioning and animation
// ---------------------------------------------------------------------------
void ApplyRect(HWND h, WinInfo& w, const RECT& v) {
    w.lastApplyTick = GetTickCount();
    const int x  = v.left - w.inset.left;
    const int y  = v.top  - w.inset.top;
    const int cw = RW(v) + w.inset.left + w.inset.right;
    const int ch = RH(v) + w.inset.top  + w.inset.bottom;
    // ASYNC: never block on a hung app, and safe for explorer's own windows.
    SetWindowPos(h, nullptr, x, y, cw, ch,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
}

void SetTarget(HWND h, WinInfo& w, const RECT& to, DWORD now, bool snapOffscreen) {
    if (!w.hasCur) {
        w.cur = to; w.to = to; w.hasCur = true; w.animating = false;
        ApplyRect(h, w, to);
        return;
    }
    if (EqualRect(&w.to, &to) && (w.animating || EqualRect(&w.cur, &to))) return;

    if (!EqualRect(&w.to, &to)) { w.fixTries = 0; w.candW = w.candH = 0; }
    w.to = to;
    const bool offscreenMove = IsOffscreen(w.cur) || IsOffscreen(to);
    // Never slide a window across a neighboring monitor: apps get DPI-change
    // messages mid-flight and resize themselves.
    const bool snap = g_cfg.animationMs <= 0 || (IsOffscreen(w.cur) && IsOffscreen(to)) ||
                      (snapOffscreen && offscreenMove);
    if (snap) {
        w.animating = false;
        w.cur = to;
        ApplyRect(h, w, to);
        return;
    }
    w.from = w.cur;
    if (!g_cfg.animateResize) {
        w.from.right  = w.from.left + RW(to);
        w.from.bottom = w.from.top  + RH(to);
    }
    w.t0 = now;
    w.animating = true;
}

bool AnyAnimating() {
    for (const auto& [h, w] : g_wins) if (w.animating) return true;
    return false;
}

void EnsureAnimTimer() {
    if (!g_timerAnim) g_timerAnim = SetTimer(nullptr, 0, 16, nullptr);
}

void ScheduleVerify() {
    if (g_timerVerify) KillTimer(nullptr, g_timerVerify);
    g_timerVerify = SetTimer(nullptr, 0, 220, nullptr);
}

void ScheduleRelayout(UINT ms) {
    if (!g_timerRelayout) g_timerRelayout = SetTimer(nullptr, 0, ms ? ms : 1, nullptr);
}

LONG Lerp(LONG a, LONG b, double t) {
    return a + static_cast<LONG>(std::lround((b - a) * t));
}

bool StepAnimations(DWORD now) {
    bool any = false;
    for (auto& [h, w] : g_wins) {
        if (!w.animating) continue;
        if (w.moving || !IsWindow(h)) { w.animating = false; continue; }

        const double t = (std::min)(1.0, static_cast<double>(now - w.t0) /
                                             static_cast<double>(MaxI(g_cfg.animationMs, 1)));
        const double e = 1.0 - std::pow(1.0 - t, 3.0);   // ease-out cubic
        RECT r;
        if (t >= 1.0) {
            r = w.to;
            w.animating = false;
        } else {
            r = {Lerp(w.from.left, w.to.left, e), Lerp(w.from.top, w.to.top, e),
                 Lerp(w.from.right, w.to.right, e), Lerp(w.from.bottom, w.to.bottom, e)};
            any = true;
        }
        w.cur = r;
        ApplyRect(h, w, r);
    }
    return any;
}

// ---------------------------------------------------------------------------
// Layout: one scrolling strip per monitor
// ---------------------------------------------------------------------------
void LayoutStrip(Strip& s, const MonInfo& m, const std::vector<MonInfo>& mons, DWORD now) {
    const RECT wa  = m.work;
    const int  gap = g_cfg.gap;
    const int  usableW = RW(wa) - gap * 2;
    const int  usableH = RH(wa) - gap * 2;
    if (usableW < 200 || usableH < 150) return;

    struct Item {
        int col = 0;
        std::vector<HWND> act;
        int x = 0, w = 0;
    };
    std::vector<Item> items;
    for (size_t i = 0; i < s.cols.size(); ++i) {
        Item it;
        it.col = static_cast<int>(i);
        for (HWND h : s.cols[i].wins) if (IsTiledActive(h)) it.act.push_back(h);
        if (!it.act.empty()) items.push_back(std::move(it));
    }
    if (items.empty()) return;

    // ----- widths and strip positions -----
    int x = 0;
    for (Item& it : items) {
        const Column& col = s.cols[it.col];
        int w;
        if (col.full) {
            w = usableW;
        } else if (items.size() == 1 && g_cfg.centerSingleColumn && !col.userSized) {
            w = usableW * g_cfg.singleWindowWidthPercent / 100;
        } else {
            w = static_cast<int>(std::lround(col.prop * (usableW + gap) - gap));
        }
        int minW = 0;
        for (HWND h : it.act) minW = MaxI(minW, g_wins[h].minW);
        it.w = ClampI(MaxI(w, minW), kMinWindowW, usableW);
        it.x = x;
        x += it.w + gap;
    }
    const int total = x - gap;

    // ----- which column is focused? -----
    int fi = 0;
    const int fcol = FindColumn(s, s.focusWin);
    if (fcol >= 0) {
        int best = 1 << 30;
        for (size_t k = 0; k < items.size(); ++k) {
            const int d = std::abs(items[k].col - fcol);
            if (d < best) { best = d; fi = static_cast<int>(k); }
        }
    }

    // ----- scroll offset -----
    if (total <= usableW) {
        s.target = (items.size() == 1 && g_cfg.centerSingleColumn)
                       ? -(usableW - total) / 2 : 0;
        s.centerRequest = false;
    } else {
        const int fx = items[fi].x, fw = items[fi].w;
        if (s.centerRequest || g_cfg.alwaysCenterFocused) {
            s.target = fx + fw / 2 - usableW / 2;
            s.centerRequest = false;
        } else if (fx < s.target) {
            s.target = fx;
        } else if (fx + fw > s.target + usableW) {
            s.target = fx + fw - usableW;
        }
        s.target = ClampI(s.target, 0, total - usableW);
    }

    const bool nbL = HasNeighbor(m, mons, -1);
    const bool nbR = HasNeighbor(m, mons, +1);
    const bool peekL = g_cfg.showPartialColumns && !nbL;
    const bool peekR = g_cfg.showPartialColumns && !nbR;
    const bool snapParked = nbL || nbR;

    // ----- assign rectangles -----
    for (Item& it : items) {
        const Column& col = s.cols[it.col];
        const int left  = wa.left + gap - s.target + it.x;
        const int right = left + it.w;

        int park = 0;   // -1 = park left of the desktop, +1 = park right of it
        if (right <= wa.left)                  park = -1;
        else if (left >= wa.right)             park = +1;
        else if (left < wa.left && !peekL)     park = -1;
        else if (right > wa.right && !peekR)   park = +1;

        const int px = park < 0 ? g_vs.left - it.w - kParkGap
                     : park > 0 ? g_vs.right + kParkGap : left;

        const int n = static_cast<int>(it.act.size());
        std::vector<int> hs(n, usableH);
        if (!col.tabbed && n > 1) {
            std::vector<bool> fixed(n, false);
            int remaining = usableH - gap * (n - 1);
            int freeCount = n;
            for (int pass = 0; pass < n; ++pass) {
                const int share = freeCount > 0 ? remaining / freeCount : 0;
                bool changed = false;
                for (int i = 0; i < n; ++i) {
                    const int mh = g_wins[it.act[i]].minH;
                    if (!fixed[i] && mh > share) {
                        fixed[i] = true; hs[i] = mh; remaining -= mh; --freeCount; changed = true;
                    }
                }
                if (!changed) break;
            }
            if (freeCount > 0) {
                const int share = MaxI(remaining / freeCount, kMinWindowH);
                int extra = remaining - share * freeCount;
                int lastFree = -1;
                for (int i = 0; i < n; ++i) if (!fixed[i]) { hs[i] = share; lastFree = i; }
                if (lastFree >= 0 && extra > 0) hs[lastFree] += extra;
            }
        }

        int top = wa.top + gap;
        for (int i = 0; i < n; ++i) {
            RECT to{px, top, px + it.w, top + hs[i]};
            SetTarget(it.act[i], g_wins[it.act[i]], to, now, snapParked);
            if (!col.tabbed) top += hs[i] + gap;
        }
    }
}

void SyncFocusFromForeground() {
    HWND fg = EffectiveForeground();
    auto it = g_wins.find(fg);
    if (it == g_wins.end() || it->second.floating) return;
    auto st = g_strips.find(it->second.mon);
    if (st == g_strips.end()) return;
    const int ci = FindColumn(st->second, fg);
    if (ci < 0) return;
    st->second.focusWin = fg;
    st->second.cols[ci].last = fg;
}

// ---------------------------------------------------------------------------
// Focus ring (DWM border color, Windows 11)
// ---------------------------------------------------------------------------
void SetBorderColor(HWND h, COLORREF c) {
    const HRESULT hr = DwmSetWindowAttribute(h, kDwmaBorderColor, &c, sizeof(c));
    if (FAILED(hr) && !g_ringErrLogged) {
        g_ringErrLogged = true;
        Wh_Log(L"Focus ring not supported on this system (hr=0x%08X)", static_cast<unsigned>(hr));
    }
}

void ClearRing() {
    if (g_ringWnd && IsWindow(g_ringWnd)) SetBorderColor(g_ringWnd, kDwmColorDefault);
    g_ringWnd = nullptr;
}

void UpdateRing() {
    HWND want = nullptr;
    if (g_cfg.focusRing) {
        HWND fg = EffectiveForeground();
        if (g_wins.count(fg)) want = fg;
    }
    if (g_ringWnd && g_ringWnd != want) ClearRing();
    if (want && want != g_ringWnd) {
        SetBorderColor(want, g_cfg.ringColor);
        g_ringWnd = want;
    }
}

// ---------------------------------------------------------------------------
// Re-layout driver
// ---------------------------------------------------------------------------
void Relayout() {
    if (g_inRelayout) {            // e.g. an event delivered while we wait on an app
        g_relayoutPending = true;
        return;
    }
    g_inRelayout = true;

    if (!IsFullscreenBusy()) {
        const std::vector<MonInfo> mons = GetMonitors();
        g_vs = VirtualScreen();
        ReconcileMonitors(mons);
        ReconcileMoving();
        PruneDead();
        DiscoverWindows();
        g_firstScan = false;
        SyncFocusFromForeground();

        const DWORD now = GetTickCount();
        for (const MonInfo& m : mons) {
            auto it = g_strips.find(m.hmon);
            if (it != g_strips.end()) LayoutStrip(it->second, m, mons, now);
        }
        g_verifyChain = 0;
        if (AnyAnimating()) EnsureAnimTimer(); else ScheduleVerify();
        UpdateRing();
    }

    g_inRelayout = false;
    if (g_relayoutPending) {
        g_relayoutPending = false;
        ScheduleRelayout(5);
    }
}

// After things settle: reapply drifted windows and learn minimum sizes.
// A size is only learned when the *same* oversize is seen on two consecutive
// checks, long after our last request - a slow app that has not processed the
// resize yet must not be mistaken for an app with a large minimum size.
void VerifyPlacement() {
    bool relayout = false, again = false;
    const DWORD now = GetTickCount();
    for (auto& [h, w] : g_wins) {
        if (w.floating || w.moving || w.animating || !w.hasCur) continue;
        if (!IsTiledActive(h)) continue;
        if (now - w.lastApplyTick < 400) { again = true; continue; }
        if (IsHungAppWindow(h)) continue;

        RECT a{};
        if (!VisibleRect(h, &a)) continue;

        if (IsOffscreen(w.to)) {      // parked: Windows may have pulled it back on-screen
            if (!IsOffscreen(a) && w.fixTries < kMaxLearnTries) {
                ++w.fixTries;
                w.cur = w.to;
                ApplyRect(h, w, w.to);
                again = true;
            }
            continue;
        }

        RefreshInsets(h, w);
        const int dw = RW(a) - RW(w.to);
        const int dh = RH(a) - RH(w.to);
        const bool oversize = dw > kSizeLearnTolerance || dh > kSizeLearnTolerance;

        if (oversize && w.learnTries < kMaxLearnTries) {
            if (w.candW == RW(a) && w.candH == RH(a)) {
                if (dw > kSizeLearnTolerance) w.minW = MaxI(w.minW, RW(a));
                if (dh > kSizeLearnTolerance) w.minH = MaxI(w.minH, RH(a));
                ++w.learnTries;
                w.candW = w.candH = 0;
                relayout = true;
                Wh_Log(L"Learned minimum size for %p: %dx%d", h, w.minW, w.minH);
            } else {
                w.candW = RW(a);
                w.candH = RH(a);
                again = true;
            }
        } else {
            w.candW = w.candH = 0;
            const bool off = std::abs(a.left - w.to.left) > 3 || std::abs(a.top - w.to.top) > 3 ||
                             std::abs(dw) > kSizeLearnTolerance || std::abs(dh) > kSizeLearnTolerance;
            if (off && !oversize && w.fixTries < kMaxLearnTries) {
                ++w.fixTries;
                w.cur = w.to;
                ApplyRect(h, w, w.to);
                again = true;
            }
        }
    }
    if (relayout) {
        Relayout();
    } else if (again && g_verifyChain < 5) {
        ++g_verifyChain;
        ScheduleVerify();
    }
}

// ---------------------------------------------------------------------------
// Floating / monitor changes
// ---------------------------------------------------------------------------
bool IsFullyOffscreenOrInvalid(const RECT& r) {
    return RW(r) <= 0 || RH(r) <= 0 || IsOffscreen(r);
}

void FloatWindow(HWND h, bool restoreOriginal) {
    auto it = g_wins.find(h);
    if (it == g_wins.end() || it->second.floating) return;
    WinInfo& w = it->second;
    RemoveFromColumns(h);
    w.floating = true;
    w.animating = false;
    if (restoreOriginal && w.hasOriginal && !IsFullyOffscreenOrInvalid(w.original)) {
        SetWindowPos(h, nullptr, w.original.left, w.original.top, RW(w.original), RH(w.original),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
    }
}

void UnfloatWindow(HWND h) {
    auto it = g_wins.find(h);
    if (it == g_wins.end() || !it->second.floating) return;
    WinInfo& w = it->second;
    w.floating = false;
    w.mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    RECT o{};
    if (GetWindowRect(h, &o)) { w.original = o; w.hasOriginal = true; }
    RefreshInsets(h, w);
    w.hasCur = VisibleRect(h, &w.cur);
    w.to = w.cur;
    InsertColumnAfterFocus(w.mon, h);
}

void MoveToStrip(HWND h, HMONITOR mon) {
    auto it = g_wins.find(h);
    if (it == g_wins.end() || it->second.floating) return;
    RemoveFromColumns(h);
    it->second.mon = mon;
    InsertColumnAfterFocus(mon, h);
}

void OnMoveSizeStart(HWND h) {
    auto it = g_wins.find(h);
    if (it == g_wins.end() || it->second.floating) return;
    it->second.moving = true;
    it->second.movingSince = GetTickCount();
    it->second.animating = false;
}

// Ends a drag/resize: float the window, or put it back into the right strip.
void FinishMove(HWND h, WinInfo& w) {
    w.moving = false;
    if (!IsWindow(h)) return;

    if (g_cfg.floatWhenMoved) {
        RECT r{};
        if (GetWindowRect(h, &r)) { w.original = r; w.hasOriginal = true; }
        FloatWindow(h, /*restoreOriginal=*/false);
    } else {
        POINT pt{};
        GetCursorPos(&pt);
        const HMONITOR nm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        if (nm && nm != w.mon) MoveToStrip(h, nm);
        w.hasCur = VisibleRect(h, &w.cur);   // animate back from wherever the drag ended
        w.to = w.cur;
    }
}

void OnMoveSizeEnd(HWND h) {
    auto it = g_wins.find(h);
    if (it == g_wins.end() || !it->second.moving) return;
    FinishMove(h, it->second);
    Relayout();
}

bool InMoveSize(HWND h) {
    const DWORD tid = GetWindowThreadProcessId(h, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    return tid && GetGUIThreadInfo(tid, &gti) && (gti.flags & GUI_INMOVESIZE) != 0;
}

// A missed MOVESIZEEND must never leave a window excluded from the layout.
void ReconcileMoving() {
    const DWORD now = GetTickCount();
    for (auto& [h, w] : g_wins) {
        if (!w.moving || now - w.movingSince < 500) continue;
        if (IsWindow(h) && InMoveSize(h)) continue;
        FinishMove(h, w);
    }
}

// ---------------------------------------------------------------------------
// Hotkey actions
// ---------------------------------------------------------------------------
Strip* CurrentStrip() {
    SyncFocusFromForeground();
    HWND fg = EffectiveForeground();
    auto it = g_wins.find(fg);
    if (it != g_wins.end() && !it->second.floating) {
        auto st = g_strips.find(it->second.mon);
        if (st != g_strips.end()) return &st->second;
    }
    POINT pt{};
    GetCursorPos(&pt);
    auto st = g_strips.find(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY));
    return st != g_strips.end() ? &st->second : nullptr;
}

HWND PickActive(const Column& c) {
    if (c.last && IsTiledActive(c.last) &&
        std::find(c.wins.begin(), c.wins.end(), c.last) != c.wins.end()) {
        return c.last;
    }
    for (HWND h : c.wins) if (IsTiledActive(h)) return h;
    return nullptr;
}

// SetForegroundWindow can be refused by the foreground lock. Fall back to
// briefly sharing input state with the current foreground thread.
void ForceForeground(HWND h) {
    if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    SetForegroundWindow(h);
    if (GetForegroundWindow() == h || IsHungAppWindow(h)) return;

    const DWORD me = GetCurrentThreadId();
    HWND fg = GetForegroundWindow();
    const DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD tt = GetWindowThreadProcessId(h, nullptr);
    const bool a1 = ft && ft != me && AttachThreadInput(me, ft, TRUE);
    const bool a2 = tt && tt != me && tt != ft && AttachThreadInput(me, tt, TRUE);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (a2) AttachThreadInput(me, tt, FALSE);
    if (a1) AttachThreadInput(me, ft, FALSE);
}

void FocusWindow(Strip& s, HWND h) {
    if (!h) return;
    const int ci = FindColumn(s, h);
    s.focusWin = h;
    if (ci >= 0) s.cols[ci].last = h;
    g_intentWnd = h;
    g_intentTick = GetTickCount();
    ForceForeground(h);
    Relayout();
}

void FocusColumnDelta(int d) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int cur = FindColumn(*s, s->focusWin);
    if (cur < 0) return;
    for (int i = cur + d; i >= 0 && i < static_cast<int>(s->cols.size()); i += d) {
        if (HWND h = PickActive(s->cols[i])) { FocusWindow(*s, h); return; }
    }
}

void FocusColumnEdge(bool last) {
    Strip* s = CurrentStrip();
    if (!s || s->cols.empty()) return;
    const int n = static_cast<int>(s->cols.size());
    for (int k = 0; k < n; ++k) {
        const int i = last ? n - 1 - k : k;
        if (HWND h = PickActive(s->cols[i])) { FocusWindow(*s, h); return; }
    }
}

void FocusWindowDelta(int d) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    const Column& col = s->cols[ci];
    std::vector<HWND> act;
    for (HWND h : col.wins) if (IsTiledActive(h)) act.push_back(h);
    if (act.size() < 2) return;
    auto p = std::find(act.begin(), act.end(), s->focusWin);
    if (p == act.end()) return;
    int idx = static_cast<int>(p - act.begin()) + d;
    const int n = static_cast<int>(act.size());
    if (col.tabbed) idx = (idx + n) % n;
    else if (idx < 0 || idx >= n) return;
    FocusWindow(*s, act[idx]);
}

void MoveColumnDelta(int d) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    for (int j = ci + d; j >= 0 && j < static_cast<int>(s->cols.size()); j += d) {
        if (PickActive(s->cols[j])) {
            std::swap(s->cols[ci], s->cols[j]);
            Relayout();
            return;
        }
    }
}

void MoveWindowVertical(int d) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    auto& w = s->cols[ci].wins;
    auto p = std::find(w.begin(), w.end(), s->focusWin);
    if (p == w.end()) return;
    for (int j = static_cast<int>(p - w.begin()) + d; j >= 0 && j < static_cast<int>(w.size()); j += d) {
        if (IsTiledActive(w[j])) {
            std::swap(*p, w[j]);
            Relayout();
            return;
        }
    }
}

void ConsumeOrExpel(int d) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    const HWND h = s->focusWin;

    int activeCount = 0;
    for (HWND w : s->cols[ci].wins) if (IsTiledActive(w)) ++activeCount;

    if (activeCount > 1) {                                 // expel into a new column
        auto& w = s->cols[ci].wins;
        w.erase(std::find(w.begin(), w.end(), h));
        if (s->cols[ci].last == h) s->cols[ci].last = nullptr;
        Column c;
        c.wins = {h};
        c.last = h;
        c.prop = g_cfg.defaultColumnWidthPercent / 100.0;
        const size_t pos = d > 0 ? static_cast<size_t>(ci) + 1 : static_cast<size_t>(ci);
        s->cols.insert(s->cols.begin() + static_cast<std::ptrdiff_t>(pos), std::move(c));
    } else {                                               // consume into the nearest visible neighbor
        int nj = ci + d;
        while (nj >= 0 && nj < static_cast<int>(s->cols.size()) && !PickActive(s->cols[nj])) nj += d;
        if (nj < 0 || nj >= static_cast<int>(s->cols.size())) return;
        s->cols[nj].wins.push_back(h);
        s->cols[nj].last = h;
        auto& w = s->cols[ci].wins;
        w.erase(std::find(w.begin(), w.end(), h));
        if (w.empty()) s->cols.erase(s->cols.begin() + ci);   // keep columns that still hold hidden windows
    }
    Relayout();
}

void CycleWidth() {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    Column& c = s->cols[ci];
    static const double kPresets[] = {1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0};
    const double cur = c.full ? 1.0 : c.prop;
    double next = kPresets[0];
    for (double p : kPresets) if (p > cur + 0.01) { next = p; break; }
    c.prop = next;
    c.full = false;
    c.userSized = true;
    Relayout();
}

void ToggleColumnFlag(bool Column::*flag) {
    Strip* s = CurrentStrip();
    if (!s) return;
    const int ci = FindColumn(*s, s->focusWin);
    if (ci < 0) return;
    Column& c = s->cols[ci];
    c.*flag = !(c.*flag);
    if (flag == &Column::full) c.userSized = true;
    Relayout();
}

void CenterColumn() {
    Strip* s = CurrentStrip();
    if (!s) return;
    s->centerRequest = true;
    Relayout();
}

void ToggleFloating() {
    Strip* s = CurrentStrip();
    HWND h = EffectiveForeground();
    if (!g_wins.count(h)) h = s ? s->focusWin : nullptr;
    auto it = g_wins.find(h);
    if (it == g_wins.end()) return;
    if (it->second.floating) UnfloatWindow(h);
    else FloatWindow(h, /*restoreOriginal=*/true);
    Relayout();
}

void RetileAll() {
    std::vector<HWND> floating;
    for (auto& [h, w] : g_wins) if (w.floating) floating.push_back(h);
    for (HWND h : floating) UnfloatWindow(h);
    for (auto& [h, w] : g_wins) {          // forget learned minimums: apps change them
        w.minW = w.minH = 0;
        w.learnTries = 0;
        w.candW = w.candH = 0;
        QueryMinSize(h, w);
    }
    Relayout();
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------
void RegisterModHotkeys() {
    for (int id = 1; id <= kHkLast; ++id) UnregisterHotKey(nullptr, id);
    if (!g_cfg.enableHotkeys) return;

    const UINT modBits = g_cfg.modifierMode == 1 ? MOD_WIN
                       : g_cfg.modifierMode == 2 ? (MOD_CONTROL | MOD_ALT | MOD_WIN)
                                                 : (MOD_CONTROL | MOD_ALT);
    const UINT base = modBits | MOD_NOREPEAT;
    const UINT shift = base | MOD_SHIFT;

    struct Def { int id; UINT mods; UINT vk; const wchar_t* name; };
    const Def defs[] = {
        {kHkFocusLeft,    base,  VK_LEFT,  L"focus column left"},
        {kHkFocusRight,   base,  VK_RIGHT, L"focus column right"},
        {kHkFocusUp,      base,  VK_UP,    L"focus window up"},
        {kHkFocusDown,    base,  VK_DOWN,  L"focus window down"},
        {kHkFirst,        base,  VK_HOME,  L"focus first column"},
        {kHkLast,         base,  VK_END,   L"focus last column"},
        {kHkMoveColLeft,  shift, VK_LEFT,  L"move column left"},
        {kHkMoveColRight, shift, VK_RIGHT, L"move column right"},
        {kHkMoveWinUp,    shift, VK_UP,    L"move window up"},
        {kHkMoveWinDown,  shift, VK_DOWN,  L"move window down"},
        {kHkConsumeLeft,  base,  VK_OEM_4, L"consume/expel left"},
        {kHkConsumeRight, base,  VK_OEM_6, L"consume/expel right"},
        {kHkCycleWidth,   base,  'W',      L"cycle width"},
        {kHkFullWidth,    base,  VK_SPACE, L"full width"},
        {kHkCenter,       base,  'C',      L"center column"},
        {kHkTabbed,       base,  'T',      L"toggle tabbed"},
        {kHkToggleFloat,  shift, 'F',      L"toggle floating"},
        {kHkRetile,       base,  'R',      L"retile"},
    };
    for (const Def& d : defs) {
        if (!RegisterHotKey(nullptr, d.id, d.mods, d.vk)) {
            Wh_Log(L"Hotkey registration failed: %ls (err=%lu)", d.name, GetLastError());
        }
    }
}

void HandleHotkey(int id) {
    switch (id) {
        case kHkFocusLeft:    FocusColumnDelta(-1);  break;
        case kHkFocusRight:   FocusColumnDelta(+1);  break;
        case kHkFocusUp:      FocusWindowDelta(-1);  break;
        case kHkFocusDown:    FocusWindowDelta(+1);  break;
        case kHkFirst:        FocusColumnEdge(false); break;
        case kHkLast:         FocusColumnEdge(true);  break;
        case kHkMoveColLeft:  MoveColumnDelta(-1);   break;
        case kHkMoveColRight: MoveColumnDelta(+1);   break;
        case kHkMoveWinUp:    MoveWindowVertical(-1); break;
        case kHkMoveWinDown:  MoveWindowVertical(+1); break;
        case kHkConsumeLeft:  ConsumeOrExpel(-1);    break;
        case kHkConsumeRight: ConsumeOrExpel(+1);    break;
        case kHkCycleWidth:   CycleWidth();          break;
        case kHkFullWidth:    ToggleColumnFlag(&Column::full);   break;
        case kHkCenter:       CenterColumn();        break;
        case kHkTabbed:       ToggleColumnFlag(&Column::tabbed); break;
        case kHkToggleFloat:  ToggleFloating();      break;
        case kHkRetile:       RetileAll();           break;
    }
}

// ---------------------------------------------------------------------------
// WinEvent hooks (out-of-context: delivered on the worker thread)
// ---------------------------------------------------------------------------
void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                           LONG idChild, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND) {
        ScheduleRelayout(15);
        return;
    }
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART:
            OnMoveSizeStart(hwnd);
            break;
        case EVENT_SYSTEM_MOVESIZEEND:
            OnMoveSizeEnd(hwnd);
            break;
        case EVENT_OBJECT_DESTROY:
            if (g_wins.count(hwnd)) ScheduleRelayout(30);
            break;
        default:   // show / hide / minimize / cloak changes
            if (GetAncestor(hwnd, GA_ROOT) == hwnd) ScheduleRelayout(30);
            break;
    }
}

void InstallHooks() {
    struct Range { DWORD lo, hi; };
    const Range ranges[5] = {
        {EVENT_SYSTEM_FOREGROUND,    EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY,       EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_CLOAKED,       EVENT_OBJECT_UNCLOAKED},
    };
    for (int i = 0; i < 5; ++i) {
        g_hooks[i] = SetWinEventHook(ranges[i].lo, ranges[i].hi, nullptr, WinEventProc,
                                     0, 0, WINEVENT_OUTOFCONTEXT);
        if (!g_hooks[i]) Wh_Log(L"SetWinEventHook %d failed (err=%lu)", i, GetLastError());
    }
}

void RemoveHooks() {
    for (auto& h : g_hooks) {
        if (h) { UnhookWinEvent(h); h = nullptr; }
    }
}

// ---------------------------------------------------------------------------
// Safety nets: crash recovery and unload
// ---------------------------------------------------------------------------
BOOL CALLBACK RescueProc(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;
    RECT r{};
    if (!GetWindowRect(hwnd, &r) || RW(r) <= 0) return TRUE;
    // Anything fully outside the desktop that sits exactly where we park windows.
    const bool right = r.left >= g_vs.right + kParkGap - 40 && r.left <= g_vs.right + kParkGap + 40;
    const bool left  = r.right <= g_vs.left - kParkGap + 40 && r.right >= g_vs.left - kParkGap - 40;
    if (!(right || left) || !IsOffscreen(r)) return TRUE;

    const RECT* wa = reinterpret_cast<const RECT*>(lp);
    SetWindowPos(hwnd, nullptr, wa->left + 40, wa->top + 40, RW(r), RH(r),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    return TRUE;
}

void RescueParkedWindows() {
    g_vs = VirtualScreen();
    const auto mons = GetMonitors();
    if (mons.empty()) return;
    RECT wa = mons.front().work;
    for (const auto& m : mons) if (m.primary) wa = m.work;
    EnumWindows(RescueProc, reinterpret_cast<LPARAM>(&wa));
}

void RestoreEverything() {
    g_vs = VirtualScreen();
    const auto mons = GetMonitors();
    RECT wa{0, 0, 800, 600};
    if (!mons.empty()) { wa = mons.front().work; for (const auto& m : mons) if (m.primary) wa = m.work; }

    for (auto& [h, w] : g_wins) {
        if (!IsWindow(h) || w.floating || w.moving || !w.hasOriginal) continue;
        if (IsIconic(h) || IsZoomed(h)) continue;
        RECT o = w.original;
        if (IsOffscreen(o)) { o = {wa.left + 40, wa.top + 40, wa.left + 40 + RW(o), wa.top + 40 + RH(o)}; }
        SetWindowPos(h, nullptr, o.left, o.top, RW(o), RH(o),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
    }
    ClearRing();
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
bool WaitForShellExplorerProcess() {
    for (int i = 0; i < 150; ++i) {
        HWND shell = GetShellWindow();
        if (shell) {
            DWORD pid = 0;
            GetWindowThreadProcessId(shell, &pid);
            if (pid != 0) return pid == GetCurrentProcessId();
        }
        MSG msg{};
        if (PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) return false;
        Sleep(100);
    }
    Wh_Log(L"Shell window not found - this Explorer instance stays inactive");
    return false;
}

void RestartPeriodicTimer() {
    if (g_timerPeriodic) { KillTimer(nullptr, g_timerPeriodic); g_timerPeriodic = 0; }
    if (g_cfg.retileIntervalMs > 0) {
        g_timerPeriodic = SetTimer(nullptr, 0, static_cast<UINT>(g_cfg.retileIntervalMs), nullptr);
    }
}

void OnTimer(UINT_PTR id) {
    if (id == g_timerAnim && g_timerAnim) {
        if (!StepAnimations(GetTickCount())) {
            KillTimer(nullptr, g_timerAnim);
            g_timerAnim = 0;
            ScheduleVerify();
        }
    } else if (id == g_timerRelayout && g_timerRelayout) {
        KillTimer(nullptr, g_timerRelayout);
        g_timerRelayout = 0;
        Relayout();
    } else if (id == g_timerVerify && g_timerVerify) {
        KillTimer(nullptr, g_timerVerify);
        g_timerVerify = 0;
        VerifyPlacement();
    } else if (id == g_timerPeriodic && g_timerPeriodic) {
        Relayout();
    }
}

void ApplyChangedSettings() {
    LoadSettings();
    ClearRing();
    RegisterModHotkeys();
    RestartPeriodicTimer();
    Relayout();
}

DWORD WINAPI WorkerThreadProc(void*) {
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // create the queue
    SetEvent(g_ready);

    // GetWindowRect / monitor APIs and DWM frame bounds must agree on pixels.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (!WaitForShellExplorerProcess()) {
        Wh_Log(L"Not the shell Explorer - auto-tiling inactive");
        return 0;
    }

    RescueParkedWindows();
    InstallHooks();
    RegisterModHotkeys();
    RestartPeriodicTimer();
    Relayout();

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) {
            HandleHotkey(static_cast<int>(msg.wParam));
        } else if (msg.message == WM_TIMER && msg.hwnd == nullptr) {
            OnTimer(msg.wParam);
        } else if (msg.message == kMsgSettings) {
            ApplyChangedSettings();
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    for (UINT_PTR* t : {&g_timerPeriodic, &g_timerRelayout, &g_timerAnim, &g_timerVerify}) {
        if (*t) { KillTimer(nullptr, *t); *t = 0; }
    }
    for (int id = 1; id <= kHkLast; ++id) UnregisterHotKey(nullptr, id);
    RemoveHooks();
    RestoreEverything();

    g_wins.clear();
    g_strips.clear();
    g_blocked.clear();
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Windhawk entry points
// ---------------------------------------------------------------------------
BOOL Wh_ModInit() {
    Wh_Log(L"Niri-inspired Auto Tiling v0.3.1 - init");
    LoadSettings();

    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_ready) {
        Wh_Log(L"CreateEvent failed (%lu)", GetLastError());
        return FALSE;
    }

    g_thread = CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, &g_threadId);
    if (!g_thread) {
        Wh_Log(L"CreateThread failed (%lu)", GetLastError());
        CloseHandle(g_ready);
        g_ready = nullptr;
        return FALSE;
    }

    if (WaitForSingleObject(g_ready, 10000) != WAIT_OBJECT_0) {
        Wh_Log(L"Worker thread init timed out");
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        CloseHandle(g_ready);
        g_thread = nullptr;
        g_ready = nullptr;
        g_threadId = 0;
        return FALSE;
    }
    return TRUE;
}

// The mod is being unloaded, free all allocated resources.
void Wh_ModUninit() {
    Wh_Log(L"Niri-inspired Auto Tiling - uninit");
    if (g_threadId) PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);   // safe: positioning is asynchronous
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_ready) { CloseHandle(g_ready); g_ready = nullptr; }
    g_threadId = 0;
}

// The mod setting were changed, reload them.
void Wh_ModSettingsChanged() {
    // Settings are re-read on the worker thread, which owns all state.
    if (g_threadId) PostThreadMessageW(g_threadId, kMsgSettings, 0, 0);
}
