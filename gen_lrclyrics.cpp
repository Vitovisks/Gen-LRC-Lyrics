// gen_lrclyrics.cpp  (v1.1.0)
// Plugin geral (gen_*.dll) para WACUP / Winamp 5.x - 32 bits (x86)
//
// A janela de letras e integrada ao sistema de janelas do WACUP via
// IPC_GET_EMBEDIF (borda, barra de titulo e skin ficam por conta do WACUP).
//
// De onde vem a letra (nesta ordem; da pra inverter pelo menu):
//   1) arquivo .lrc com o mesmo nome da musica, na mesma pasta
//        C:\Musicas\Artista - Faixa.mp3
//        C:\Musicas\Artista - Faixa.lrc
//   2) letra embutida nas tags do proprio arquivo:
//        MP3 (e qualquer arquivo com ID3v2): USLT e SYLT
//        FLAC, OGG Vorbis, Opus: LYRICS / UNSYNCEDLYRICS
//        M4A / MP4: (c)lyr
//
// Recursos:
//   - Linha atual em destaque, rolagem suave, linhas vizinhas esmaecidas
//   - Varios tempos por linha, [offset:], tags <mm:ss.xx> (LRC estendido)
//   - UTF-8 (com ou sem BOM), UTF-16 e ANSI
//   - Letra sem tempos tambem funciona (texto fixo, roda com o scroll do mouse)
//   - Duplo clique numa linha pula para aquele ponto da musica
//   - Botao direito: fonte, sempre no topo, ajuste de sincronia, recarregar
//   - Cores editaveis no gen_lrclyrics.ini

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>

#ifndef CLEARTYPE_QUALITY
#define CLEARTYPE_QUALITY 5
#endif

// ---------------------------------------------------------------------------
// SDK do Winamp (so o que usamos)
// ---------------------------------------------------------------------------
#define GPPHDR_VER 0x10

struct winampGeneralPurposePlugin
{
    int        version;
    char*      description;
    int      (*init)();
    void     (*config)();
    void     (*quit)();
    HWND       hwndParent;
    HINSTANCE  hDllInstance;
};

#define WM_WA_IPC             WM_USER
#define IPC_ISPLAYING         104   // 1 = tocando, 3 = pausado, 0 = parado
#define IPC_GETOUTPUTTIME     105   // wParam 0: posicao em ms (-1 se parado)
#define IPC_JUMPTOTIME        106   // wParam: ms
#define IPC_GETLISTPOS        125   // posicao atual na playlist
#define IPC_GETPLAYLISTFILE   211   // char*    (wParam = indice)
#define IPC_GETPLAYLISTFILEW  214   // wchar_t* (wParam = indice)
#define IPC_GETINIDIRECTORY   335   // char* pasta de configuracoes
#define IPC_GET_EMBEDIF       505   // interface de janela integrada do Winamp/WACUP
#define IPC_ADJUST_OPTIONSMENUPOS 280
#define IPC_GET_HMENU         281
#define IPC_GETSKININFO       633
#define ID_MAINWINDOW_MENU    40258
#define ID_GENLRC_MENU        48883

// Estrutura embedWindowState do SDK Winamp (build x86). Mantida completa para
// que o WACUP possa usar os campos internos sem incompatibilidade de tamanho.
typedef struct embedWindowState embedWindowState;
typedef int (CALLBACK *FFCALLBACK)(embedWindowState*, INT, LPARAM);
struct embedWindowState
{
    HWND me;
    int flags;
    RECT r;
    void* user_ptr;
    union
    {
#pragma pack(push, 1)
        struct
        {
            embedWindowState* link;
            intptr_t attached;
            intptr_t padding1[2];
            GUID guid;
            FFCALLBACK callback;
            intptr_t padding2[52];
            intptr_t hostcount;
            intptr_t reparenting;
            void* wasabi_window;
        };
#pragma pack(pop)
        intptr_t extra_data[64];
    };
};
typedef HWND (*embedWindow_t)(embedWindowState*);

// ---------------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------------
struct LyricLine
{
    int          ms;
    std::wstring text;
};

static const int          kMargin      = 18;
static const wchar_t      kClassName[] = L"WACUP_LrcLyricsWnd";
static const std::wstring kNote        = L"\u266A";
static const std::wstring kBlank       = L" ";

enum
{
    ID_TOPMOST = 1,
    ID_FONT_UP,
    ID_FONT_DOWN,
    ID_SYNC_EARLIER,
    ID_SYNC_LATER,
    ID_SYNC_RESET,
    ID_PREFER_EMBEDDED,
    ID_RELOAD,
    ID_CLOSE
};

static HWND      g_hwndWA = NULL;     // janela principal do WACUP
static HWND      g_hwnd   = NULL;     // area cliente com as letras
static HWND      g_embedFrame = NULL; // moldura criada pelo WACUP
static embedWindowState* g_ews = NULL;
static WNDPROC g_oldEmbedProc = NULL;
static WNDPROC g_oldMainProc = NULL;
static bool g_menuAdded = false;

// --- Abertura adiada ---------------------------------------------------------
// A moldura nasce escondida e so e mostrada depois que a janela principal do
// WACUP aparece (kStartupAfterMainMs de folga). kStartupMaxMs e o limite para o
// caso de o WACUP iniciar oculto/minimizado.
static const UINT  kStartupTimerId     = 2;
static const UINT  kStartupPollMs      = 100;
static const DWORD kStartupAfterMainMs = 0;
static const DWORD kStartupMaxMs       = 6000;
static bool  g_startupPending = false;
static bool  g_mainSeen       = false;
static DWORD g_startupT0      = 0;
static DWORD g_mainSeenT0     = 0;

static HINSTANCE g_hInst  = NULL;

static std::vector<LyricLine> g_lines;
static bool          g_synced = false;
static std::wstring  g_curFile;
static std::wstring  g_status = L"Aguardando uma m\u00FAsica...";
static std::wstring  g_title  = L"Letras";
static std::wstring  g_source;               // ".lrc" ou "embutida"
static int           g_cur    = -1;
static double        g_scrollY = 0.0;
static bool          g_snap   = true;
static bool          g_forceReload = false;

static std::vector<int> g_top, g_hgt;
static int  g_contentH = 0;
static int  g_layoutW  = -1;
static int  g_lineH    = 20;
static bool g_layoutDirty = true;

static int  g_fontPt         = 16;
static bool g_topmost        = false;
static bool g_preferEmbedded = false;
static int  g_userOffset     = 0;      // ms; positivo = letra aparece mais cedo
static bool g_visible        = true;

static COLORREF g_cBg  = RGB(16, 16, 22);
static COLORREF g_cCur = RGB(255, 255, 255);
static COLORREF g_cDim = RGB(150, 150, 168);

static int  g_dpi      = 96;

static HFONT g_fontNormal = NULL;
static HFONT g_fontBold   = NULL;
static std::wstring g_iniPath;

// ---------------------------------------------------------------------------
// Configuracao (.ini na pasta de configuracoes do player)
// ---------------------------------------------------------------------------
static int GetIniInt(const wchar_t* key, int def)
{
    wchar_t buf[32];
    GetPrivateProfileStringW(L"lyrics", key, L"", buf, 32, g_iniPath.c_str());
    if (!buf[0]) return def;
    return (int)wcstol(buf, NULL, 10);
}

static void PutIniInt(const wchar_t* key, int v)
{
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%d", v);
    buf[31] = 0;
    WritePrivateProfileStringW(L"lyrics", key, buf, g_iniPath.c_str());
}

static COLORREF GetIniColor(const wchar_t* key, COLORREF def)
{
    wchar_t buf[32];
    GetPrivateProfileStringW(L"lyrics", key, L"", buf, 32, g_iniPath.c_str());
    const wchar_t* s = buf;
    if (*s == L'#') s++;
    if (wcslen(s) != 6) return def;
    wchar_t* endp = NULL;
    unsigned long v = wcstoul(s, &endp, 16);
    if (endp == s || *endp != 0) return def;
    return RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static void PutIniColor(const wchar_t* key, COLORREF c)
{
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%02X%02X%02X", (unsigned)GetRValue(c), (unsigned)GetGValue(c), (unsigned)GetBValue(c));
    buf[15] = 0;
    WritePrivateProfileStringW(L"lyrics", key, buf, g_iniPath.c_str());
}

static void InitIniPath()
{
    wchar_t dir[MAX_PATH * 2] = L"";

    LRESULT r = SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GETINIDIRECTORY);
    if ((UINT_PTR)r > 0xFFFF)
    {
        MultiByteToWideChar(CP_ACP, 0, (const char*)r, -1, dir, MAX_PATH);
    }
    if (!dir[0])
    {
        GetModuleFileNameW(g_hInst, dir, MAX_PATH);
        wchar_t* s = wcsrchr(dir, L'\\');
        if (s) *s = 0;
    }
    g_iniPath = std::wstring(dir) + L"\\gen_lrclyrics.ini";
}

static void SaveSettings()
{
    PutIniInt(L"fontsize",        g_fontPt);
    PutIniInt(L"topmost",         g_topmost ? 1 : 0);
    PutIniInt(L"prefer_embedded", g_preferEmbedded ? 1 : 0);
    PutIniInt(L"offset",          g_userOffset);
    PutIniInt(L"visible",         g_visible ? 1 : 0);
    PutIniColor(L"cor_fundo",  g_cBg);
    PutIniColor(L"cor_atual",  g_cCur);
    PutIniColor(L"cor_outras", g_cDim);

    if (g_ews)
    {
        PutIniInt(L"x", g_ews->r.left);
        PutIniInt(L"y", g_ews->r.top);
        PutIniInt(L"w", g_ews->r.right - g_ews->r.left);
        PutIniInt(L"h", g_ews->r.bottom - g_ews->r.top);
    }
}

static void LoadSettings()
{
    g_fontPt = GetIniInt(L"fontsize", 16);
    if (g_fontPt < 8)  g_fontPt = 8;
    if (g_fontPt > 60) g_fontPt = 60;
    g_topmost        = GetIniInt(L"topmost", 0) != 0;
    g_preferEmbedded = GetIniInt(L"prefer_embedded", 0) != 0;
    g_userOffset     = GetIniInt(L"offset", 0);
    g_visible        = GetIniInt(L"visible", 1) != 0;
    g_cBg  = GetIniColor(L"cor_fundo",  RGB(16, 16, 22));
    g_cCur = GetIniColor(L"cor_atual",  RGB(255, 255, 255));
    g_cDim = GetIniColor(L"cor_outras", RGB(150, 150, 168));
}

// ---------------------------------------------------------------------------
// Fontes e metricas
// ---------------------------------------------------------------------------
static HFONT MakeFont(int height, int weight)
{
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void CreateFonts()
{
    if (g_fontNormal) DeleteObject(g_fontNormal);
    if (g_fontBold)   DeleteObject(g_fontBold);

    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    if (g_dpi < 72) g_dpi = 96;

    int h = -MulDiv(g_fontPt, g_dpi, 72);
    g_fontNormal = MakeFont(h, FW_NORMAL);
    g_fontBold   = MakeFont(h, FW_BOLD);

    g_layoutDirty = true;
    g_snap = true;
}

// ---------------------------------------------------------------------------
// Utilitarios de texto e arquivo
// ---------------------------------------------------------------------------
static bool ReadWholeFile(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 8 * 1024 * 1024)
    {
        CloseHandle(h);
        return false;
    }
    out.assign((size_t)sz.QuadPart, '\0');
    DWORD rd = 0;
    BOOL ok = TRUE;
    if (sz.QuadPart > 0) ok = ReadFile(h, &out[0], (DWORD)sz.QuadPart, &rd, NULL);
    CloseHandle(h);
    out.resize(rd);
    return ok != 0;
}

// Leitor de arquivo com acesso por posicao (usado nas tags embutidas)
class FileSrc
{
public:
    FileSrc() : h(INVALID_HANDLE_VALUE), sz(0) {}
    ~FileSrc() { Close(); }

    bool Open(const std::wstring& path)
    {
        Close();
        h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER s;
        if (!GetFileSizeEx(h, &s)) { Close(); return false; }
        sz = (unsigned long long)s.QuadPart;
        return true;
    }

    void Close()
    {
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    }

    unsigned long long Size() const { return sz; }

    // le exatamente n bytes a partir de off (falha se passar do fim)
    bool ReadAt(unsigned long long off, void* buf, size_t n)
    {
        if (h == INVALID_HANDLE_VALUE || off > sz || n > sz - off) return false;
        if (n == 0) return true;
        LARGE_INTEGER p;
        p.QuadPart = (long long)off;
        if (!SetFilePointerEx(h, p, NULL, FILE_BEGIN)) return false;
        DWORD rd = 0;
        if (!ReadFile(h, buf, (DWORD)n, &rd, NULL)) return false;
        return rd == n;
    }

    // le ate n bytes (corta no fim do arquivo)
    bool ReadStr(unsigned long long off, size_t n, std::string& out)
    {
        out.clear();
        if (off >= sz) return false;
        if (n > sz - off) n = (size_t)(sz - off);
        out.assign(n, '\0');
        return n == 0 || ReadAt(off, &out[0], n);
    }

private:
    HANDLE h;
    unsigned long long sz;
};

static unsigned int BE32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static unsigned int LE32(const unsigned char* p)
{
    return ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) | ((unsigned)p[1] << 8) | (unsigned)p[0];
}

static unsigned int SyncSafe(const unsigned char* p)
{
    return ((unsigned)(p[0] & 0x7F) << 21) | ((unsigned)(p[1] & 0x7F) << 14) |
           ((unsigned)(p[2] & 0x7F) << 7)  |  (unsigned)(p[3] & 0x7F);
}

// UTF-8 (com ou sem BOM), UTF-16 (com BOM) ou ANSI
static std::wstring DecodeText(const std::string& b)
{
    const unsigned char* u = (const unsigned char*)b.data();
    size_t n = b.size();

    // UTF-16 LE
    if (n >= 2 && u[0] == 0xFF && u[1] == 0xFE)
        return std::wstring((const wchar_t*)(u + 2), (n - 2) / 2);

    // UTF-16 BE
    if (n >= 2 && u[0] == 0xFE && u[1] == 0xFF)
    {
        std::wstring w((n - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); i++)
            w[i] = (wchar_t)((u[2 + 2 * i] << 8) | u[3 + 2 * i]);
        return w;
    }

    const char* p = b.data();
    int len = (int)n;
    if (len >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) { p += 3; len -= 3; }
    if (len <= 0) return std::wstring();

    UINT  cp = CP_UTF8;
    DWORD fl = MB_ERR_INVALID_CHARS;
    int wl = MultiByteToWideChar(cp, fl, p, len, NULL, 0);
    if (wl == 0)                       // nao e UTF-8 valido -> ANSI
    {
        cp = CP_ACP;
        fl = 0;
        wl = MultiByteToWideChar(cp, fl, p, len, NULL, 0);
    }
    std::wstring w((size_t)wl, L'\0');
    if (wl > 0) MultiByteToWideChar(cp, fl, p, len, &w[0], wl);
    return w;
}

static void Trim(std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n' || s[a] == 0xFEFF || s[a] == 0x3000)) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n' || s[b - 1] == 0x3000)) b--;
    s = s.substr(a, b - a);
}

// ---------------------------------------------------------------------------
// Interpretacao do .lrc (ou de qualquer texto no formato LRC)
// ---------------------------------------------------------------------------
// aceita mm:ss  mm:ss.x  mm:ss.xx  mm:ss.xxx  mm:ss:xx
static bool ParseTimeTag(const wchar_t* s, size_t n, int& out)
{
    size_t i = 0, st = 0;
    long mm = 0, ss = 0, frac = 0;
    int  fd = 0;

    while (i < n && s[i] >= L'0' && s[i] <= L'9') { mm = mm * 10 + (s[i] - L'0'); i++; if (i - st > 4) return false; }
    if (i == st || i >= n || s[i] != L':') return false;
    i++;
    st = i;
    while (i < n && s[i] >= L'0' && s[i] <= L'9') { ss = ss * 10 + (s[i] - L'0'); i++; if (i - st > 3) return false; }
    if (i == st) return false;

    if (i < n && (s[i] == L'.' || s[i] == L':'))
    {
        i++;
        st = i;
        while (i < n && s[i] >= L'0' && s[i] <= L'9')
        {
            if (fd < 3) { frac = frac * 10 + (s[i] - L'0'); fd++; }
            i++;
        }
        if (i == st) return false;
        while (fd < 3) { frac *= 10; fd++; }
    }
    if (i != n) return false;

    out = (int)(mm * 60000 + ss * 1000 + frac);
    return true;
}

static void ParseLrc(const std::wstring& text, std::vector<LyricLine>& out, bool& synced)
{
    out.clear();
    synced = false;

    int offset = 0;
    std::vector<std::wstring> plain;

    size_t pos = 0, n = text.size();
    while (pos <= n)
    {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos) e = n;
        std::wstring line = text.substr(pos, e - pos);
        pos = e + 1;

        Trim(line);
        if (line.empty())
        {
            // linha em branco: separa estrofes quando o texto nao tem tempos
            if (!plain.empty() && !plain.back().empty()) plain.push_back(std::wstring());
            continue;
        }

        std::vector<int> times;
        bool meta = false;
        size_t k = 0;

        while (k < line.size() && line[k] == L'[')
        {
            size_t c = line.find(L']', k);
            if (c == std::wstring::npos) break;

            std::wstring tag = line.substr(k + 1, c - k - 1);
            int t = 0;
            if (ParseTimeTag(tag.c_str(), tag.size(), t))
            {
                times.push_back(t);
            }
            else
            {
                if (tag.size() > 7 && _wcsnicmp(tag.c_str(), L"offset:", 7) == 0)
                    offset = _wtoi(tag.c_str() + 7);
                meta = true;     // [ar:..] [ti:..] [al:..] [Verso 1] etc.
            }
            k = c + 1;
        }

        std::wstring body = line.substr(k);

        // remove tags de palavra <mm:ss.xx> (LRC estendido)
        size_t a = 0;
        while ((a = body.find(L'<', a)) != std::wstring::npos)
        {
            size_t b = body.find(L'>', a);
            if (b == std::wstring::npos) break;
            std::wstring inner = body.substr(a + 1, b - a - 1);
            int tmp = 0;
            if (ParseTimeTag(inner.c_str(), inner.size(), tmp)) body.erase(a, b - a + 1);
            else a = b + 1;
        }
        Trim(body);

        if (!times.empty())
        {
            for (size_t i = 0; i < times.size(); i++)
            {
                LyricLine ll;
                ll.ms = times[i];
                ll.text = body;
                out.push_back(ll);
            }
        }
        else if (!meta && !body.empty())
        {
            plain.push_back(body);
        }
    }

    if (!out.empty())
    {
        for (size_t i = 0; i < out.size(); i++)
        {
            int v = out[i].ms - offset;      // offset positivo = letra mais cedo
            out[i].ms = v < 0 ? 0 : v;
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const LyricLine& a, const LyricLine& b) { return a.ms < b.ms; });
        synced = true;
    }
    else
    {
        while (!plain.empty() && plain.back().empty()) plain.pop_back();
        for (size_t i = 0; i < plain.size(); i++)
        {
            LyricLine ll;
            ll.ms = 0;
            ll.text = plain[i];
            out.push_back(ll);
        }
        synced = false;
    }
}

// ---------------------------------------------------------------------------
// Letras embutidas nas tags dos arquivos
// ---------------------------------------------------------------------------
struct EmbeddedLyrics
{
    std::vector<LyricLine>    synced;   // de frames SYLT (ID3)
    std::vector<std::wstring> texts;    // USLT, LYRICS=..., (c)lyr: texto livre ou LRC
};

static void StripNulls(std::wstring& t)
{
    while (!t.empty() && t[t.size() - 1] == L'\0') t.erase(t.size() - 1);
}

static std::wstring Utf16ToW(const unsigned char* p, size_t n, bool be)
{
    if (n >= 2)
    {
        if (p[0] == 0xFF && p[1] == 0xFE)      { be = false; p += 2; n -= 2; }
        else if (p[0] == 0xFE && p[1] == 0xFF) { be = true;  p += 2; n -= 2; }
    }
    std::wstring w(n / 2, L'\0');
    for (size_t i = 0; i < w.size(); i++)
        w[i] = (wchar_t)(be ? ((p[2 * i] << 8) | p[2 * i + 1]) : ((p[2 * i + 1] << 8) | p[2 * i]));
    return w;
}

// encoding ID3: 0 = ISO-8859-1, 1 = UTF-16 com BOM, 2 = UTF-16BE, 3 = UTF-8
static std::wstring Id3Text(int enc, const unsigned char* p, size_t n)
{
    std::wstring t;
    if (enc == 1)      t = Utf16ToW(p, n, false);
    else if (enc == 2) t = Utf16ToW(p, n, true);
    else               t = DecodeText(std::string((const char*)p, n));   // 0 e 3
    StripNulls(t);
    return t;
}

// procura o terminador de string a partir de 'from'; devolve n se nao achar
static size_t FindTerm(int enc, const unsigned char* p, size_t n, size_t from, size_t& termLen)
{
    termLen = (enc == 1 || enc == 2) ? 2 : 1;
    if (termLen == 1)
    {
        for (size_t i = from; i < n; i++) if (p[i] == 0) return i;
        return n;
    }
    for (size_t i = from; i + 1 < n; i += 2) if (p[i] == 0 && p[i + 1] == 0) return i;
    return n;
}

static void Unsync(std::string& s)          // FF 00 -> FF
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        o.push_back(s[i]);
        if ((unsigned char)s[i] == 0xFF && i + 1 < s.size() && s[i + 1] == '\0') i++;
    }
    s.swap(o);
}

static void ParseUslt(const unsigned char* fd, size_t fl, EmbeddedLyrics& el)
{
    if (fl < 5) return;
    int enc = fd[0];
    size_t termLen = 1;
    size_t e = FindTerm(enc, fd, fl, 4, termLen);      // descritor comeca no byte 4
    if (e >= fl) return;
    size_t start = e + termLen;
    if (start > fl) return;
    std::wstring t = Id3Text(enc, fd + start, fl - start);
    if (!t.empty()) el.texts.push_back(t);
}

static void ParseSylt(const unsigned char* fd, size_t fl, EmbeddedLyrics& el)
{
    if (fl < 7) return;
    int enc = fd[0];
    int tsf = fd[4];                  // 1 = quadros MPEG, 2 = milissegundos
    if (tsf != 2) return;             // sem a taxa de quadros nao da pra converter

    size_t termLen = 1;
    size_t e = FindTerm(enc, fd, fl, 6, termLen);      // descritor comeca no byte 6
    if (e >= fl) return;
    size_t pos = e + termLen;

    while (pos < fl)
    {
        size_t te = FindTerm(enc, fd, fl, pos, termLen);
        if (te >= fl) break;
        std::wstring t = Id3Text(enc, fd + pos, te - pos);
        pos = te + termLen;
        if (pos + 4 > fl) break;
        unsigned ms = BE32(fd + pos);
        pos += 4;

        Trim(t);                      // alguns programas gravam "\n" no comeco
        LyricLine ll;
        ll.ms = (int)ms;
        ll.text = t;
        el.synced.push_back(ll);
    }
}

// Le uma tag ID3v2 (v2.2, v2.3 ou v2.4) que comeca em 'off'.
// Devolve true se existe uma tag valida; endOff = primeiro byte depois dela.
static bool ReadId3v2(FileSrc& f, unsigned long long off, EmbeddedLyrics& el, unsigned long long& endOff)
{
    unsigned char h[10];
    if (!f.ReadAt(off, h, 10)) return false;
    if (memcmp(h, "ID3", 3) != 0 || h[3] < 2 || h[3] > 4) return false;
    if ((h[6] | h[7] | h[8] | h[9]) & 0x80) return false;

    int ver = h[3];
    unsigned flags = h[5];
    unsigned long long size = SyncSafe(h + 6);
    endOff = off + 10 + size + ((ver == 4 && (flags & 0x10)) ? 10 : 0);

    if (size == 0 || size > 64ull * 1024 * 1024) return true;

    std::string buf;
    if (!f.ReadStr(off + 10, (size_t)size, buf)) return true;
    if (ver <= 3 && (flags & 0x80)) Unsync(buf);       // dessincronizacao da tag inteira

    const unsigned char* d = (const unsigned char*)buf.data();
    size_t n = buf.size(), pos = 0;

    if (flags & 0x40)                                  // cabecalho estendido
    {
        if (n < 4) return true;
        if (ver == 3)      pos = 4 + (size_t)BE32(d);
        else if (ver == 4) pos = (size_t)SyncSafe(d);
    }

    size_t hdr = (ver == 2) ? 6 : 10;
    while (pos + hdr <= n)
    {
        if (d[pos] == 0) break;                        // area de padding

        char id[5] = { 0, 0, 0, 0, 0 };
        size_t fsz = 0;
        unsigned fflags = 0;
        if (ver == 2)
        {
            memcpy(id, d + pos, 3);
            fsz = ((size_t)d[pos + 3] << 16) | ((size_t)d[pos + 4] << 8) | (size_t)d[pos + 5];
        }
        else
        {
            memcpy(id, d + pos, 4);
            fsz = (ver == 4) ? (size_t)SyncSafe(d + pos + 4) : (size_t)BE32(d + pos + 4);
            fflags = ((unsigned)d[pos + 8] << 8) | d[pos + 9];
        }
        pos += hdr;
        if (fsz > n - pos) break;                      // tag corrompida

        const unsigned char* fd = d + pos;
        size_t fl = fsz;
        pos += fsz;

        bool isUslt = (ver == 2) ? (strcmp(id, "ULT") == 0)  : (strcmp(id, "USLT") == 0);
        bool isSylt = (ver == 2) ? (strcmp(id, "SLT") == 0)  : (strcmp(id, "SYLT") == 0);
        if (!isUslt && !isSylt) continue;

        std::string tmp;
        if (ver >= 3)
        {
            bool compressed, encrypted, group, unsyncf = false, dli = false;
            if (ver == 3)
            {
                compressed = (fflags & 0x0080) != 0;
                encrypted  = (fflags & 0x0040) != 0;
                group      = (fflags & 0x0020) != 0;
            }
            else
            {
                compressed = (fflags & 0x0008) != 0;
                encrypted  = (fflags & 0x0004) != 0;
                group      = (fflags & 0x0040) != 0;
                unsyncf    = (fflags & 0x0002) != 0;
                dli        = (fflags & 0x0001) != 0;
            }
            if (compressed || encrypted) continue;
            if (group) { if (fl < 1) continue; fd++; fl--; }
            if (dli)   { if (fl < 4) continue; fd += 4; fl -= 4; }
            if (unsyncf)
            {
                tmp.assign((const char*)fd, fl);
                Unsync(tmp);
                fd = (const unsigned char*)tmp.data();
                fl = tmp.size();
            }
        }

        if (isUslt) ParseUslt(fd, fl, el);
        else        ParseSylt(fd, fl, el);
    }
    return true;
}

// Comentarios Vorbis (FLAC, Ogg Vorbis, Opus)
static void ReadVorbisComments(const unsigned char* d, size_t n, EmbeddedLyrics& el)
{
    if (n < 8) return;
    size_t pos = 4;
    unsigned vl = LE32(d);
    if (vl > n - pos) return;
    pos += vl;
    if (pos + 4 > n) return;
    unsigned cnt = LE32(d + pos);
    pos += 4;

    for (unsigned i = 0; i < cnt && pos + 4 <= n; i++)
    {
        unsigned l = LE32(d + pos);
        pos += 4;
        if (l > n - pos) break;
        const char* s = (const char*)d + pos;
        pos += l;

        const char* eq = (const char*)memchr(s, '=', l);
        if (!eq) continue;

        std::string key(s, (size_t)(eq - s));
        for (size_t k = 0; k < key.size(); k++) key[k] = (char)toupper((unsigned char)key[k]);

        bool isLyrics = (key == "LYRICS" || key == "UNSYNCEDLYRICS" || key == "UNSYNCED LYRICS" ||
                         key == "SYNCEDLYRICS" || key.compare(0, 7, "LYRICS-") == 0);
        if (!isLyrics) continue;

        std::wstring t = DecodeText(std::string(eq + 1, l - (size_t)(eq - s) - 1));
        StripNulls(t);
        if (!t.empty()) el.texts.push_back(t);
    }
}

static void ReadFlac(FileSrc& f, unsigned long long off, EmbeddedLyrics& el)
{
    unsigned long long pos = off + 4;                  // depois de "fLaC"
    for (int guard = 0; guard < 4096; guard++)
    {
        unsigned char h[4];
        if (!f.ReadAt(pos, h, 4)) return;
        bool last = (h[0] & 0x80) != 0;
        int type = h[0] & 0x7F;
        size_t len = ((size_t)h[1] << 16) | ((size_t)h[2] << 8) | (size_t)h[3];
        pos += 4;
        if (type == 4)                                 // VORBIS_COMMENT
        {
            if (len > 16 * 1024 * 1024) return;
            std::string b;
            if (f.ReadStr(pos, len, b))
                ReadVorbisComments((const unsigned char*)b.data(), b.size(), el);
        }
        pos += len;
        if (last) return;
    }
}

static void ReadOgg(FileSrc& f, unsigned long long off, EmbeddedLyrics& el)
{
    unsigned long long pos = off;
    std::string cur;                                   // pacote em montagem
    int pktIndex = 0;

    for (int pages = 0; pages < 4096; pages++)
    {
        unsigned char h[27];
        if (!f.ReadAt(pos, h, 27) || memcmp(h, "OggS", 4) != 0) return;
        int nseg = h[26];
        unsigned char seg[255];
        if (nseg > 0 && !f.ReadAt(pos + 27, seg, (size_t)nseg)) return;

        size_t dataLen = 0;
        for (int i = 0; i < nseg; i++) dataLen += seg[i];
        std::string data;
        if (!f.ReadStr(pos + 27 + (unsigned)nseg, dataLen, data) || data.size() != dataLen) return;
        pos += 27 + (unsigned)nseg + dataLen;

        size_t dp = 0;
        for (int i = 0; i < nseg; i++)
        {
            cur.append(data, dp, seg[i]);
            dp += seg[i];
            if (seg[i] < 255)                          // fim do pacote
            {
                if (pktIndex == 1)                     // 2o pacote = comentarios
                {
                    const unsigned char* c = (const unsigned char*)cur.data();
                    size_t cn = cur.size();
                    if (cn > 7 && c[0] == 3 && memcmp(c + 1, "vorbis", 6) == 0)
                        ReadVorbisComments(c + 7, cn - 7, el);
                    else if (cn > 8 && memcmp(c, "OpusTags", 8) == 0)
                        ReadVorbisComments(c + 8, cn - 8, el);
                    return;
                }
                pktIndex++;
                cur.clear();
            }
        }
        if (cur.size() > 16 * 1024 * 1024) return;
    }
}

// MP4 / M4A: moov > udta > meta > ilst > (c)lyr > data
static bool Mp4FindChild(const unsigned char* d, size_t start, size_t end, const char* type4,
                         size_t& cStart, size_t& cEnd)
{
    size_t pos = start;
    while (pos + 8 <= end)
    {
        unsigned long long sz = BE32(d + pos);
        size_t hdr = 8;
        if (sz == 1)
        {
            if (pos + 16 > end) return false;
            sz = ((unsigned long long)BE32(d + pos + 8) << 32) | BE32(d + pos + 12);
            hdr = 16;
        }
        else if (sz == 0)
        {
            sz = end - pos;
        }
        if (sz < hdr || sz > end - pos) return false;
        if (memcmp(d + pos + 4, type4, 4) == 0)
        {
            cStart = pos + hdr;
            cEnd = pos + (size_t)sz;
            return true;
        }
        pos += (size_t)sz;
    }
    return false;
}

static void ReadMp4(FileSrc& f, EmbeddedLyrics& el)
{
    unsigned long long pos = 0;
    for (int guard = 0; guard < 256; guard++)
    {
        unsigned char h[16];
        if (!f.ReadAt(pos, h, 8)) return;
        unsigned long long sz = BE32(h);
        unsigned long long hdr = 8;
        if (sz == 1)
        {
            if (!f.ReadAt(pos + 8, h + 8, 8)) return;
            sz = ((unsigned long long)BE32(h + 8) << 32) | BE32(h + 12);
            hdr = 16;
        }
        else if (sz == 0)
        {
            sz = f.Size() - pos;
        }
        if (sz < hdr) return;

        if (memcmp(h + 4, "moov", 4) == 0)
        {
            if (sz - hdr > 64ull * 1024 * 1024) return;
            std::string b;
            if (!f.ReadStr(pos + hdr, (size_t)(sz - hdr), b)) return;
            const unsigned char* d = (const unsigned char*)b.data();
            size_t n = b.size();

            size_t s1, e1, s2, e2, s3, e3, s4, e4, s5, e5;
            if (!Mp4FindChild(d, 0, n, "udta", s1, e1)) return;
            if (!Mp4FindChild(d, s1, e1, "meta", s2, e2)) return;
            s2 += 4;                                   // "meta" tem versao+flags
            if (s2 > e2) return;
            if (!Mp4FindChild(d, s2, e2, "ilst", s3, e3)) return;
            const char lyr[4] = { (char)0xA9, 'l', 'y', 'r' };
            if (!Mp4FindChild(d, s3, e3, lyr, s4, e4)) return;
            if (!Mp4FindChild(d, s4, e4, "data", s5, e5)) return;
            if (e5 < s5 + 8) return;

            std::wstring t = DecodeText(std::string((const char*)d + s5 + 8, e5 - s5 - 8));
            StripNulls(t);
            if (!t.empty()) el.texts.push_back(t);
            return;
        }
        pos += sz;
        if (pos >= f.Size()) return;
    }
}

// Escolhe a melhor letra entre as encontradas
static bool FinalizeEmbedded(EmbeddedLyrics& el, std::vector<LyricLine>& out, bool& synced)
{
    out.clear();
    synced = false;

    if (el.synced.size() >= 2)
    {
        std::stable_sort(el.synced.begin(), el.synced.end(),
                         [](const LyricLine& a, const LyricLine& b) { return a.ms < b.ms; });
        out = el.synced;
        synced = true;
        return true;
    }

    std::vector<LyricLine> firstAny;
    bool have = false;
    for (size_t i = 0; i < el.texts.size(); i++)
    {
        std::vector<LyricLine> v;
        bool s = false;
        ParseLrc(el.texts[i], v, s);
        if (v.empty()) continue;
        if (s)
        {
            out.swap(v);
            synced = true;
            return true;                              // texto em formato LRC: ideal
        }
        if (!have) { firstAny.swap(v); have = true; }
    }
    if (have)
    {
        out.swap(firstAny);
        synced = false;
        return true;
    }
    return false;
}

static bool LoadEmbeddedLyrics(const std::wstring& file, std::vector<LyricLine>& out, bool& synced)
{
    out.clear();
    synced = false;
    if (file.empty() || file.find(L"://") != std::wstring::npos) return false;

    FileSrc f;
    if (!f.Open(file)) return false;

    EmbeddedLyrics el;
    unsigned long long off = 0, end = 0;
    if (ReadId3v2(f, 0, el, end)) off = end;           // MP3 e afins; FLAC/Ogg as vezes tem ID3 na frente

    std::string mg;
    if (f.ReadStr(off, 12, mg) && mg.size() >= 8)
    {
        const char* m = mg.data();
        if (memcmp(m, "fLaC", 4) == 0)                        ReadFlac(f, off, el);
        else if (memcmp(m, "OggS", 4) == 0)                   ReadOgg(f, off, el);
        else if (off == 0 && memcmp(m + 4, "ftyp", 4) == 0)   ReadMp4(f, el);
    }
    return FinalizeEmbedded(el, out, synced);
}

// ---------------------------------------------------------------------------
// Escolha da fonte de letra para a faixa atual
// ---------------------------------------------------------------------------
static std::wstring LrcPathFor(const std::wstring& file)
{
    if (file.find(L"://") != std::wstring::npos) return std::wstring();   // streams
    size_t dot   = file.find_last_of(L'.');
    size_t slash = file.find_last_of(L"\\/");
    std::wstring base = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
                        ? file.substr(0, dot) : file;
    return base + L".lrc";
}

static std::wstring TitleFromFile(const std::wstring& file)
{
    if (file.empty()) return L"Letras";
    size_t s = file.find_last_of(L"\\/");
    std::wstring n = (s == std::wstring::npos) ? file : file.substr(s + 1);
    size_t d = n.find_last_of(L'.');
    if (d != std::wstring::npos && d > 0 && n.size() - d <= 6) n = n.substr(0, d);
    return n;
}

static bool LoadFromLrcFile(const std::wstring& lrc)
{
    std::string bytes;
    if (lrc.empty() || !ReadWholeFile(lrc, bytes)) return false;
    ParseLrc(DecodeText(bytes), g_lines, g_synced);
    if (g_lines.empty()) { g_synced = false; return false; }
    return true;
}

static void LoadLyricsFor(const std::wstring& file)
{
    g_lines.clear();
    g_synced = false;
    g_cur = -1;
    g_scrollY = 0.0;
    g_snap = true;
    g_layoutDirty = true;
    g_source.clear();
    g_title = TitleFromFile(file);

    if (file.empty())
    {
        g_status = L"Aguardando uma m\u00FAsica...";
        return;
    }

    std::wstring lrc = LrcPathFor(file);
    if (lrc.empty())
    {
        g_status = L"Letras n\u00E3o dispon\u00EDveis para streams.";
        return;
    }

    bool ok = false;
    if (g_preferEmbedded)
    {
        if (LoadEmbeddedLyrics(file, g_lines, g_synced))   { ok = true; g_source = L"embutida"; }
        else if (LoadFromLrcFile(lrc))                     { ok = true; g_source = L".lrc"; }
    }
    else
    {
        if (LoadFromLrcFile(lrc))                          { ok = true; g_source = L".lrc"; }
        else if (LoadEmbeddedLyrics(file, g_lines, g_synced)) { ok = true; g_source = L"embutida"; }
    }

    if (ok)
    {
        g_status.clear();
        return;
    }

    g_lines.clear();
    g_synced = false;
    size_t s = lrc.find_last_of(L"\\/");
    std::wstring name = (s == std::wstring::npos) ? lrc : lrc.substr(s + 1);
    g_status = L"Nenhuma letra encontrada para esta m\u00FAsica.\n\nProcurei o arquivo:\n" + name +
               L"\n\ne letras embutidas nas tags.";
}

static int FindLine(int posMs)
{
    int lo = 0, hi = (int)g_lines.size();
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (g_lines[mid].ms <= posMs) lo = mid + 1; else hi = mid;
    }
    return lo - 1;
}

// ---------------------------------------------------------------------------
// Layout e desenho
// ---------------------------------------------------------------------------
static const std::wstring& LineText(size_t i)
{
    if (!g_lines[i].text.empty()) return g_lines[i].text;
    return g_synced ? kNote : kBlank;
}

static void EnsureLayout(HDC dc, int W)
{
    if (!g_layoutDirty && g_layoutW == W && g_top.size() == g_lines.size()) return;
    g_layoutDirty = false;
    g_layoutW = W;

    size_t n = g_lines.size();
    g_top.assign(n, 0);
    g_hgt.assign(n, 0);

    HGDIOBJ old = SelectObject(dc, g_fontBold);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    g_lineH = (int)tm.tmHeight;
    int gap = g_lineH / 2;
    int availW = std::max<int>(40, W - 2 * kMargin);

    int y = 0;
    for (size_t i = 0; i < n; i++)
    {
        const std::wstring& t = LineText(i);
        RECT r = { 0, 0, availW, 0 };
        DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
        int h = std::max<int>((int)r.bottom, g_lineH);
        g_top[i] = y;
        g_hgt[i] = h;
        y += h + gap;
    }
    g_contentH = y;
    SelectObject(dc, old);
}

static void UpdateLayoutNow()
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    HDC dc = GetDC(g_hwnd);
    EnsureLayout(dc, (int)rc.right);
    ReleaseDC(g_hwnd, dc);
}

static COLORREF Mix(COLORREF a, COLORREF b, double t)
{
    int r  = (int)(GetRValue(a) + ((int)GetRValue(b) - (int)GetRValue(a)) * t + 0.5);
    int g  = (int)(GetGValue(a) + ((int)GetGValue(b) - (int)GetGValue(a)) * t + 0.5);
    int bl = (int)(GetBValue(a) + ((int)GetBValue(b) - (int)GetBValue(a)) * t + 0.5);
    return RGB(r, g, bl);
}

// altura da area de letras (a barra de titulo e da moldura do WACUP)
static int ContentH(int H)
{
    return H < 1 ? 1 : H;
}

static double ViewTop(int Hc)
{
    return g_synced ? g_scrollY - Hc / 2.0 : g_scrollY;
}

static void DrawStatus(HDC dc, int W, int H)
{
    SelectObject(dc, g_fontNormal);
    SetTextColor(dc, Mix(g_cDim, g_cBg, 0.10));

    int right = std::max<int>(kMargin + 40, W - kMargin);
    RECT r = { kMargin, 0, right, 0 };
    DrawTextW(dc, g_status.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
    int h = (int)r.bottom;
    int top = (H - h) / 2;
    if (top < 0) top = 0;
    RECT r2 = { kMargin, top, right, top + h };
    DrawTextW(dc, g_status.c_str(), -1, &r2, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
}

static void Paint(HDC hdcWin, int W, int H)
{
    HDC dc = CreateCompatibleDC(hdcWin);
    HBITMAP bmp = CreateCompatibleBitmap(hdcWin, W, H);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    RECT all = { 0, 0, W, H };
    HBRUSH br = CreateSolidBrush(g_cBg);
    FillRect(dc, &all, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);

    if (g_lines.empty())
    {
        DrawStatus(dc, W, H);
    }
    else
    {
        EnsureLayout(dc, W);
        double viewTop = ViewTop(ContentH(H));
        IntersectClipRect(dc, 1, 0, W - 1, H - 1);

        for (size_t i = 0; i < g_lines.size(); i++)
        {
            int top = (int)floor(g_top[i] - viewTop + 0.5);
            int h   = g_hgt[i];
            if (top + h < 0) continue;
            if (top > H) break;

            bool cur = (g_synced && (int)i == g_cur);
            COLORREF col;
            if (!g_synced)
            {
                col = Mix(g_cCur, g_cBg, 0.15);
            }
            else if (cur)
            {
                col = g_cCur;
            }
            else
            {
                int d = (g_cur < 0) ? (int)i + 1 : abs((int)i - g_cur);
                if (d > 6) d = 6;
                col = Mix(g_cDim, g_cBg, d * 0.12);
            }

            SelectObject(dc, cur ? g_fontBold : g_fontNormal);
            SetTextColor(dc, col);
            RECT r = { kMargin, top, W - kMargin, top + h };
            const std::wstring& t = LineText(i);
            DrawTextW(dc, t.c_str(), (int)t.size(), &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectClipRgn(dc, NULL);
    }


    BitBlt(hdcWin, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static int LineAtY(int y)
{
    if (g_lines.empty() || g_top.size() != g_lines.size() || y < 0) return -1;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    double cy = y + ViewTop(ContentH((int)rc.bottom));
    for (size_t i = 0; i < g_lines.size(); i++)
        if (cy >= g_top[i] && cy < g_top[i] + g_hgt[i]) return (int)i;
    return -1;
}

// ---------------------------------------------------------------------------
// Leitura do estado do player (a cada tick do timer)
// ---------------------------------------------------------------------------
static std::wstring GetCurrentTrackFile()
{
    std::wstring file;
    int idx = (int)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GETLISTPOS);
    if (idx < 0) return file;

    LRESULT r = SendMessage(g_hwndWA, WM_WA_IPC, (WPARAM)idx, IPC_GETPLAYLISTFILEW);
    if ((UINT_PTR)r > 0xFFFF)
    {
        file = (const wchar_t*)r;
    }
    else
    {
        r = SendMessage(g_hwndWA, WM_WA_IPC, (WPARAM)idx, IPC_GETPLAYLISTFILE);
        if ((UINT_PTR)r > 0xFFFF)
        {
            wchar_t tmp[MAX_PATH * 2];
            if (MultiByteToWideChar(CP_ACP, 0, (const char*)r, -1, tmp, MAX_PATH * 2) > 0)
                file = tmp;
        }
    }
    return file;
}

static void OnTick()
{
    if (!g_hwnd || !g_hwndWA) return;
    bool changed = false;

    std::wstring file = GetCurrentTrackFile();
    if (file != g_curFile || g_forceReload)
    {
        g_curFile = file;
        g_forceReload = false;
        LoadLyricsFor(file);
        changed = true;
    }

    int pos = (int)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
    int newCur = -1;
    if (g_synced && pos >= 0) newCur = FindLine(pos + g_userOffset);
    if (newCur != g_cur) { g_cur = newCur; changed = true; }

    if (!g_lines.empty())
    {
        UpdateLayoutNow();
        if (g_top.size() == g_lines.size())
        {
            if (g_synced)
            {
                int ci = g_cur < 0 ? 0 : g_cur;
                double target = g_top[ci] + g_hgt[ci] / 2.0;
                double d = target - g_scrollY;
                if (g_snap || fabs(d) < 0.5)
                {
                    if (g_scrollY != target) changed = true;
                    g_scrollY = target;
                }
                else
                {
                    g_scrollY += d * 0.20;
                    changed = true;
                }
            }
            else
            {
                RECT rc;
                GetClientRect(g_hwnd, &rc);
                double maxS = (double)std::max<int>(0, g_contentH - ContentH((int)rc.bottom) + kMargin);
                if (g_scrollY > maxS) { g_scrollY = maxS; changed = true; }
                if (g_scrollY < 0.0)  { g_scrollY = 0.0;  changed = true; }
            }
            g_snap = false;
        }
    }

    if (changed && IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd))
        InvalidateRect(g_hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Janela
// ---------------------------------------------------------------------------
static void ApplyTopmost()
{
    HWND target = g_embedFrame ? g_embedFrame : g_hwnd;
    if (target && IsWindow(target))
        SetWindowPos(target, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void ShowContextMenu(HWND h, int sx, int sy)
{
    HMENU m = CreatePopupMenu();

    wchar_t info[96];
    _snwprintf(info, 96, L"Ajuste de sincronia: %+.1f s", g_userOffset / 1000.0);
    info[95] = 0;

    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, info);
    AppendMenuW(m, MF_STRING, ID_SYNC_EARLIER, L"Adiantar letra (+0,5 s)");
    AppendMenuW(m, MF_STRING, ID_SYNC_LATER,   L"Atrasar letra (-0,5 s)");
    AppendMenuW(m, MF_STRING, ID_SYNC_RESET,   L"Zerar ajuste de sincronia");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_FONT_UP,      L"Aumentar fonte");
    AppendMenuW(m, MF_STRING, ID_FONT_DOWN,    L"Diminuir fonte");
    AppendMenuW(m, MF_STRING | (g_topmost ? MF_CHECKED : 0), ID_TOPMOST, L"Sempre no topo");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_preferEmbedded ? MF_CHECKED : 0), ID_PREFER_EMBEDDED,
                L"Preferir letra embutida na m\u00FAsica");
    AppendMenuW(m, MF_STRING, ID_RELOAD,       L"Recarregar letra");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_CLOSE,        L"Fechar janela");

    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, h, NULL);
    DestroyMenu(m);

    switch (cmd)
    {
    case ID_TOPMOST:         g_topmost = !g_topmost; ApplyTopmost(); break;
    case ID_FONT_UP:         if (g_fontPt < 60) { g_fontPt += 2; CreateFonts(); } break;
    case ID_FONT_DOWN:       if (g_fontPt > 8)  { g_fontPt -= 2; CreateFonts(); } break;
    case ID_SYNC_EARLIER:    g_userOffset += 500; break;
    case ID_SYNC_LATER:      g_userOffset -= 500; break;
    case ID_SYNC_RESET:      g_userOffset = 0; break;
    case ID_PREFER_EMBEDDED: g_preferEmbedded = !g_preferEmbedded; g_forceReload = true; break;
    case ID_RELOAD:          g_forceReload = true; break;
    case ID_CLOSE:           PostMessage(h, WM_CLOSE, 0, 0); break;
    default: return;
    }
    SaveSettings();
    InvalidateRect(h, NULL, FALSE);
}

static void UpdateMainMenuCheck(bool visible)
{
    if (!g_hwndWA || !IsWindow(g_hwndWA) || !g_menuAdded) return;
    HMENU menu = (HMENU)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GET_HMENU);
    if (!menu) return;
    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    mii.fState = visible ? MFS_CHECKED : MFS_UNCHECKED;
    SetMenuItemInfoW(menu, ID_GENLRC_MENU, FALSE, &mii);
}

static void AddMainMenuItem()
{
    // O menu de janelas do skin classico e o mesmo mecanismo usado pelo Thinger.
    if (!g_hwndWA || SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GETSKININFO) != 1) return;
    HMENU menu = (HMENU)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GET_HMENU);
    if (!menu) return;

    int count = GetMenuItemCount(menu);
    for (int i = count - 1; i >= 0; --i)
    {
        if (GetMenuItemID(menu, i) == ID_MAINWINDOW_MENU)
        {
            int pos = i + 1;
            while (pos < GetMenuItemCount(menu) && GetMenuItemID(menu, pos) != 0xFFFFFFFF)
            {
                if (GetMenuItemID(menu, pos) == ID_GENLRC_MENU) return;
                ++pos;
            }
            if (pos < GetMenuItemCount(menu))
            {
                if (InsertMenuW(menu, pos, MF_BYPOSITION | MF_STRING, ID_GENLRC_MENU, L"Gen LRC Lyrics"))
                {
                    g_menuAdded = true;
                    SendMessage(g_hwndWA, WM_WA_IPC, 1, IPC_ADJUST_OPTIONSMENUPOS);
                    UpdateMainMenuCheck(g_visible);
                }
            }
            return;
        }
    }
}

static void RemoveMainMenuItem()
{
    if (!g_hwndWA || !IsWindow(g_hwndWA)) return;
    HMENU menu = (HMENU)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GET_HMENU);
    if (menu && g_menuAdded) RemoveMenu(menu, ID_GENLRC_MENU, MF_BYCOMMAND);
    g_menuAdded = false;
}

static void ShowEmbedFrameAfterStartup()
{
    if (!g_embedFrame || !IsWindow(g_embedFrame)) return;
    ShowWindow(g_embedFrame, SW_SHOW);
}

static void StartupShowTick()
{
    if (!g_startupPending)
    {
        if (g_hwnd) KillTimer(g_hwnd, kStartupTimerId);
        return;
    }

    DWORD now = GetTickCount();
    if (!g_mainSeen && g_hwndWA && IsWindow(g_hwndWA) && IsWindowVisible(g_hwndWA))
    {
        g_mainSeen = true;
        g_mainSeenT0 = now;
    }

    bool ready = (g_mainSeen && (DWORD)(now - g_mainSeenT0) >= kStartupAfterMainMs) ||
                 ((DWORD)(now - g_startupT0) >= kStartupMaxMs);
    if (!ready) return;

    g_startupPending = false;
    if (g_hwnd) KillTimer(g_hwnd, kStartupTimerId);
    if (g_visible) ShowEmbedFrameAfterStartup();
}

static LRESULT CALLBACK MainWindowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_GENLRC_MENU)
    {
        g_startupPending = false;
        g_visible = !g_visible;
        if (g_embedFrame && IsWindow(g_embedFrame)) ShowWindow(g_embedFrame, g_visible ? SW_SHOW : SW_HIDE);
        UpdateMainMenuCheck(g_visible);
        SaveSettings();
        return 0;
    }
    return CallWindowProc(g_oldMainProc, h, m, w, l);
}

static LRESULT CALLBACK EmbedFrameProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE)
    {
        ShowWindow(h, SW_HIDE);
        g_visible = false;
        UpdateMainMenuCheck(false);
        SaveSettings();
        return 0;
    }
    if (m == WM_SHOWWINDOW)
    {
        LRESULT result = CallWindowProc(g_oldEmbedProc, h, m, w, l);
        if (!g_startupPending)
        {
            g_visible = (w != FALSE);
            UpdateMainMenuCheck(g_visible);
        }
        return result;
    }
    return CallWindowProc(g_oldEmbedProc, h, m, w, l);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CREATE:
        SetTimer(h, 1, 33, NULL);
        return 0;

    case WM_TIMER:
        if (w == 1) OnTick();
        else if (w == kStartupTimerId) StartupShowTick();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        // A janela filha nao controla arraste nem redimensionamento. Isso fica
        // por conta da moldura nativa criada pelo WACUP.
        return HTCLIENT;

    case WM_NCLBUTTONDBLCLK:
        if (w == HTCAPTION) return 0;  // nao maximiza com duplo clique na barra
        break;

    case WM_NCRBUTTONUP:
        if (w == HTCAPTION)
        {
            ShowContextMenu(h, GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        }
        break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = MulDiv(240, g_dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(160, g_dpi, 96);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (rc.right > 0 && rc.bottom > 0) Paint(dc, (int)rc.right, (int)rc.bottom);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        g_layoutDirty = true;
        g_snap = true;
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
        if (!g_synced && !g_lines.empty())
        {
            int d = GET_WHEEL_DELTA_WPARAM(w);
            g_scrollY -= d * (g_lineH * 3) / 120.0;
            InvalidateRect(h, NULL, FALSE);   // o limite e aplicado no proximo tick
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (g_synced)
        {
            int i = LineAtY(GET_Y_LPARAM(l));
            if (i >= 0)
            {
                int t = g_lines[i].ms - g_userOffset;
                if (t < 0) t = 0;
                SendMessage(g_hwndWA, WM_WA_IPC, (WPARAM)t, IPC_JUMPTOTIME);
            }
        }
        return 0;

    case WM_CONTEXTMENU:
    {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (x == -1 && y == -1)
        {
            POINT p = { 20, 20 };
            ClientToScreen(h, &p);
            x = (int)p.x; y = (int)p.y;
        }
        ShowContextMenu(h, x, y);
        return 0;
    }

    case WM_CLOSE:
        if (g_embedFrame) ShowWindow(g_embedFrame, SW_HIDE);
        else ShowWindow(h, SW_HIDE);
        g_visible = false;
        UpdateMainMenuCheck(false);
        SaveSettings();
        return 0;

    case WM_DESTROY:
        KillTimer(h, 1);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

// ---------------------------------------------------------------------------
// Interface do plugin
// ---------------------------------------------------------------------------
static int  PluginInit();
static void PluginConfig();
static void PluginQuit();

static char g_desc[] = "Gen LRC Lyrics v1.1.0";

static winampGeneralPurposePlugin g_plugin =
{
    GPPHDR_VER, g_desc, PluginInit, PluginConfig, PluginQuit, NULL, NULL
};

static int PluginInit()
{
    g_hwndWA = g_plugin.hwndParent;
    g_hInst  = g_plugin.hDllInstance;

    InitIniPath();
    LoadSettings();
    CreateFonts();
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kClassName;
    HICON ic = (HICON)GetClassLongPtrW(g_hwndWA, GCLP_HICON);
    wc.hIcon   = ic ? ic : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 1;

    int x = GetIniInt(L"x", 0x7FFFFFFF);
    int y = GetIniInt(L"y", 0x7FFFFFFF);
    int w = GetIniInt(L"w", 420);
    int hh = GetIniInt(L"h", 600);
    // Tamanho salvo: so volta ao padrao se for invalido. Janelas pequenas sao
    // respeitadas (apenas um minimo de seguranca).
    if (w <= 0 || hh <= 0 || w > 10000 || hh > 10000) { w = 420; hh = 600; }
    if (w < 100)  w = 100;
    if (hh < 80)  hh = 80;

    RECT parentRect;
    GetWindowRect(g_hwndWA, &parentRect);
    if (x == 0x7FFFFFFF || y == 0x7FFFFFFF)
    {
        x = parentRect.left;
        y = parentRect.bottom;
    }
    {
        // se a posicao salva ficou fora de todos os monitores (ex.: monitor
        // removido), volta para perto da janela principal
        RECT want = { x, y, x + w, y + hh };
        if (!MonitorFromRect(&want, MONITOR_DEFAULTTONULL))
        {
            x = parentRect.left;
            y = parentRect.bottom;
        }
    }

    g_ews = (embedWindowState*)GlobalAlloc(GPTR, sizeof(embedWindowState));
    if (!g_ews) return 1;
    g_ews->flags = 0;
    // posicao inicial pedida ao WACUP (coordenadas de tela)
    g_ews->r.left = x; g_ews->r.top = y; g_ews->r.right = x + w; g_ews->r.bottom = y + hh;

    // Jeito do SDK: cria a janela de letras primeiro e a entrega em g_ews->me.
    // O WACUP a coloca dentro da moldura, respeitando a borda da skin, e a
    // redimensiona sozinho.
    g_hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_CLIPCHILDREN,
                             0, 0, w, hh, g_hwndWA, NULL, g_hInst, NULL);
    if (!g_hwnd)
    {
        GlobalFree((HGLOBAL)g_ews); g_ews = NULL;
        UnregisterClassW(kClassName, g_hInst);
        return 1;
    }
    g_ews->me = g_hwnd;

    embedWindow_t embedWindow = (embedWindow_t)SendMessage(g_hwndWA, WM_WA_IPC, 0, IPC_GET_EMBEDIF);
    if (!embedWindow)
    {
        if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
        GlobalFree((HGLOBAL)g_ews); g_ews = NULL;
        UnregisterClassW(kClassName, g_hInst);
        return 1;
    }

    g_embedFrame = embedWindow(g_ews);
    if (!g_embedFrame || !IsWindow(g_embedFrame))
    {
        if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = NULL; }
        GlobalFree((HGLOBAL)g_ews); g_ews = NULL; g_embedFrame = NULL;
        UnregisterClassW(kClassName, g_hInst);
        return 1;
    }

    // A moldura nao pode ficar visivel durante a inicializacao do WACUP.
    ShowWindow(g_embedFrame, SW_HIDE);
    SetWindowTextW(g_embedFrame, L"Gen LRC Lyrics");
    if (!IsChild(g_embedFrame, g_hwnd))
        SetParent(g_hwnd, g_embedFrame);   // o WACUP reparenta mais tarde; o layout e dele
    ShowWindow(g_hwnd, SW_SHOW);   // a filha so aparece quando a moldura for mostrada

    // A moldura e a janela filha ja existem. Agora aplicamos a geometria
    // salva, como no fluxo de inicializacao do Thinger.
    g_ews->r.left = x;
    g_ews->r.top = y;
    g_ews->r.right = x + w;
    g_ews->r.bottom = y + hh;

    // Reposiciona explicitamente a moldura (ainda escondida) com a geometria salva.
    SetWindowPos(g_embedFrame, NULL, x, y, w, hh,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    g_oldEmbedProc = (WNDPROC)SetWindowLongPtrW(g_embedFrame, GWLP_WNDPROC, (LONG_PTR)EmbedFrameProc);

    ApplyTopmost();
    // Nunca mostrar aqui. A janela aparece em StartupShowTick(), depois que o
    // WACUP terminou de iniciar.
    ShowWindow(g_embedFrame, SW_HIDE);
    g_startupPending = g_visible;
    g_mainSeen = false;
    g_startupT0 = GetTickCount();
    if (g_startupPending) SetTimer(g_hwnd, kStartupTimerId, kStartupPollMs, NULL);

    AddMainMenuItem();
    if (g_menuAdded)
        g_oldMainProc = (WNDPROC)SetWindowLongPtrW(g_hwndWA, GWLP_WNDPROC, (LONG_PTR)MainWindowProc);
    return 0;
}

static void PluginConfig()
{
    if (!g_embedFrame) return;
    g_startupPending = false;
    g_visible = true;
    ShowWindow(g_embedFrame, SW_SHOW);
    SetForegroundWindow(g_embedFrame);
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

static void PluginQuit()
{
    g_startupPending = false;
    SaveSettings();
    if (g_oldMainProc && g_hwndWA && IsWindow(g_hwndWA))
        SetWindowLongPtrW(g_hwndWA, GWLP_WNDPROC, (LONG_PTR)g_oldMainProc);
    g_oldMainProc = NULL;
    RemoveMainMenuItem();
    if (g_embedFrame && IsWindow(g_embedFrame) && g_oldEmbedProc)
        SetWindowLongPtrW(g_embedFrame, GWLP_WNDPROC, (LONG_PTR)g_oldEmbedProc);
    g_oldEmbedProc = NULL;

    if (g_hwnd && IsWindow(g_hwnd)) DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    if (g_embedFrame && IsWindow(g_embedFrame)) DestroyWindow(g_embedFrame);
    g_embedFrame = NULL;
    if (g_ews) GlobalFree((HGLOBAL)g_ews);
    g_ews = NULL;

    UnregisterClassW(kClassName, g_hInst);
    if (g_fontNormal) { DeleteObject(g_fontNormal); g_fontNormal = NULL; }
    if (g_fontBold)   { DeleteObject(g_fontBold);   g_fontBold   = NULL; }
}

extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin()
{
    return &g_plugin;
}
