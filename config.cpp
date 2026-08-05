#include "config.h"
#include <shlobj.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

namespace mf {

// ---------------------------------------------------------------- 字符串
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), a.data(), n, nullptr, nullptr);
    return a;
}

// ---------------------------------------------------------------- 路径
std::wstring ConfigDir() {
    wchar_t buf[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)) || buf[0] == 0)
        GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring dir(buf);
    if (dir.empty()) dir = L".";
    dir += L"\\MouseFlow";
    return dir;
}

std::wstring ConfigPath() { return ConfigDir() + L"\\settings.cfg"; }

static std::wstring ExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

// ---------------------------------------------------------------- tweak 访问
static std::wstring TweakKey(const EffectDef& def, bool isTrail) {
    return (isTrail ? L"t:" : L"c:") + std::wstring(def.id);
}

float Settings::GetParam(const EffectDef& def, bool isTrail, int pid) const {
    if (pid < 0 || pid >= P_NUMERIC) return 0.0f;
    auto it = tweaks.find(TweakKey(def, isTrail));
    if (it != tweaks.end() && it->second.hasParam) return it->second.p[pid];
    return def.p[pid];
}

void Settings::SetParam(const EffectDef& def, bool isTrail, int pid, float v) {
    if (pid < 0 || pid >= P_NUMERIC) return;
    auto key = TweakKey(def, isTrail);
    auto& tw = tweaks[key];
    if (!tw.hasParam) {
        for (int i = 0; i < P_NUMERIC; ++i) tw.p[i] = def.p[i];
        tw.hasParam = true;
    }
    tw.p[pid] = v;
}

Color4f Settings::GetColorA(const EffectDef& def, bool isTrail) const {
    auto it = tweaks.find(TweakKey(def, isTrail));
    if (it != tweaks.end() && it->second.hasColor) return it->second.colorA;
    return def.colorA;
}

Color4f Settings::GetColorB(const EffectDef& def, bool isTrail) const {
    auto it = tweaks.find(TweakKey(def, isTrail));
    if (it != tweaks.end() && it->second.hasColor) return it->second.colorB;
    return def.colorB;
}

void Settings::SetColorA(const EffectDef& def, bool isTrail, Color4f c) {
    auto& tw = tweaks[TweakKey(def, isTrail)];
    if (!tw.hasColor) { tw.colorA = def.colorA; tw.colorB = def.colorB; tw.hasColor = true; }
    tw.colorA = c;
}

void Settings::SetColorB(const EffectDef& def, bool isTrail, Color4f c) {
    auto& tw = tweaks[TweakKey(def, isTrail)];
    if (!tw.hasColor) { tw.colorA = def.colorA; tw.colorB = def.colorB; tw.hasColor = true; }
    tw.colorB = c;
}

void Settings::ResetEffect(const EffectDef& def, bool isTrail) {
    tweaks.erase(TweakKey(def, isTrail));
}

EffectDef Settings::ResolveTrail() const {
    int i = FindTrailIndex(trailId);
    EffectDef d = kTrailEffects[i < 0 ? 0 : i];
    auto it = tweaks.find(TweakKey(d, true));
    if (it != tweaks.end()) {
        if (it->second.hasParam) for (int k = 0; k < P_NUMERIC; ++k) d.p[k] = it->second.p[k];
        if (it->second.hasColor) { d.colorA = it->second.colorA; d.colorB = it->second.colorB; }
    }
    return d;
}

EffectDef Settings::ResolveClick() const {
    int i = FindClickIndex(clickId);
    EffectDef d = kClickEffects[i < 0 ? 0 : i];
    auto it = tweaks.find(TweakKey(d, false));
    if (it != tweaks.end()) {
        if (it->second.hasParam) for (int k = 0; k < P_NUMERIC; ++k) d.p[k] = it->second.p[k];
        if (it->second.hasColor) { d.colorA = it->second.colorA; d.colorB = it->second.colorB; }
    }
    return d;
}

// ---------------------------------------------------------------- 文件 IO
static bool ReadAll(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > (1 << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h);
    out.resize(rd);
    return ok != 0;
}

static bool WriteAll(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

// ---------------------------------------------------------------- 加载
bool LoadSettings(Settings& s) {
    std::string raw;
    if (!ReadAll(ConfigPath(), raw)) return false;

    size_t pos = 0;
    while (pos < raw.size()) {
        size_t e = raw.find('\n', pos);
        if (e == std::string::npos) e = raw.size();
        std::string line = raw.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        // 切分为 token
        std::vector<std::string> tk;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && line[i] == ' ') ++i;
            size_t j = i;
            while (j < line.size() && line[j] != ' ') ++j;
            if (j > i) tk.push_back(line.substr(i, j - i));
            i = j;
        }
        if (tk.size() < 2) continue;

        const std::string& k = tk[0];
        auto num  = [&](int idx) { return idx < (int)tk.size() ? (float)atof(tk[idx].c_str()) : 0.0f; };
        auto inum = [&](int idx) { return idx < (int)tk.size() ? atoi(tk[idx].c_str()) : 0; };

        if      (k == "master")   s.masterEnabled   = inum(1) != 0;
        else if (k == "trail.on") s.trailEnabled    = inum(1) != 0;
        else if (k == "click.on") s.clickEnabled    = inum(1) != 0;
        else if (k == "trail.id") s.trailId         = Utf8ToWide(tk[1]);
        else if (k == "click.id") s.clickId         = Utf8ToWide(tk[1]);
        else if (k == "opacity")  s.globalOpacity   = Clampf(num(1), 0.05f, 1.0f);
        else if (k == "scale")    s.globalScale     = Clampf(num(1), 0.3f, 3.0f);
        else if (k == "fps")      s.fpsLimit        = inum(1);
        else if (k == "autostart")s.autoStart       = inum(1) != 0;
        else if (k == "silent")   s.silentStart     = inum(1) != 0;
        else if (k == "tray")     s.minimizeToTray  = inum(1) != 0;
        else if (k == "pausefs")  s.pauseFullscreen = inum(1) != 0;
        else if (k == "idle")     s.hideOnIdle      = inum(1) != 0;
        else if (k == "theme")    s.theme           = Clampi(inum(1), 0, 2);
        else if (k == "win" && tk.size() >= 5) {
            s.winX = inum(1); s.winY = inum(2); s.winW = inum(3); s.winH = inum(4);
        }
        else if (k == "fx" && tk.size() >= 15) {
            // fx <key> p0..p9 <hasParam> <hasColor> <colorA> <colorB>
            EffectTweak tw;
            for (int p = 0; p < P_NUMERIC; ++p) tw.p[p] = num(2 + p);
            tw.hasParam = inum(12) != 0;
            tw.hasColor = inum(13) != 0;
            tw.colorA = Rgb((uint32_t)strtoul(tk[14].c_str(), nullptr, 16));
            if (tk.size() >= 16) tw.colorB = Rgb((uint32_t)strtoul(tk[15].c_str(), nullptr, 16));
            s.tweaks[Utf8ToWide(tk[1])] = tw;
        }
    }
    return true;
}

// ---------------------------------------------------------------- 保存
bool SaveSettings(const Settings& s) {
    SHCreateDirectoryExW(nullptr, ConfigDir().c_str(), nullptr);

    std::string o;
    o.reserve(4096);
    char buf[512];
    auto put = [&](const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        o += buf; o += '\n';
    };

    put("# MouseFlow settings - auto generated, do not edit while running");
    put("version 1");
    put("master %d",    s.masterEnabled ? 1 : 0);
    put("trail.on %d",  s.trailEnabled ? 1 : 0);
    put("click.on %d",  s.clickEnabled ? 1 : 0);
    put("trail.id %s",  WideToUtf8(s.trailId).c_str());
    put("click.id %s",  WideToUtf8(s.clickId).c_str());
    put("opacity %.3f", s.globalOpacity);
    put("scale %.3f",   s.globalScale);
    put("fps %d",       s.fpsLimit);
    put("autostart %d", s.autoStart ? 1 : 0);
    put("silent %d",    s.silentStart ? 1 : 0);
    put("tray %d",      s.minimizeToTray ? 1 : 0);
    put("pausefs %d",   s.pauseFullscreen ? 1 : 0);
    put("idle %d",      s.hideOnIdle ? 1 : 0);
    put("theme %d",     s.theme);
    put("win %d %d %d %d", s.winX, s.winY, s.winW, s.winH);

    for (const auto& kv : s.tweaks) {
        const EffectTweak& t = kv.second;
        if (!t.hasParam && !t.hasColor) continue;
        std::string line = "fx " + WideToUtf8(kv.first);
        for (int p = 0; p < P_NUMERIC; ++p) {
            snprintf(buf, sizeof(buf), " %.4g", t.p[p]);
            line += buf;
        }
        snprintf(buf, sizeof(buf), " %d %d %06X %06X",
                 t.hasParam ? 1 : 0, t.hasColor ? 1 : 0,
                 (unsigned)ToHex(t.colorA), (unsigned)ToHex(t.colorB));
        line += buf;
        o += line; o += '\n';
    }

    // 原子写：先写临时文件再替换，避免掉电损坏
    std::wstring tmp = ConfigPath() + L".tmp";
    if (!WriteAll(tmp, o)) return false;
    if (!MoveFileExW(tmp.c_str(), ConfigPath().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 开机自启
static const wchar_t* kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue = L"MouseFlow";

bool IsAutoStartEnabled() {
    HKEY hk{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024]{};
    DWORD cb = sizeof(buf), type = 0;
    LSTATUS st = RegQueryValueExW(hk, kRunValue, nullptr, &type, (LPBYTE)buf, &cb);
    RegCloseKey(hk);
    return st == ERROR_SUCCESS && type == REG_SZ && buf[0];
}

bool SetAutoStart(bool on, bool silent) {
    HKEY hk{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (on) {
        std::wstring cmd = L"\"" + ExePath() + L"\"";
        if (silent) cmd += L" --silent";
        ok = RegSetValueExW(hk, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LSTATUS st = RegDeleteValueW(hk, kRunValue);
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hk);
    return ok;
}

} // namespace mf
