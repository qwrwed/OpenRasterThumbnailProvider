// OraThumbnailProvider.cpp
// Windows Shell thumbnail handler for .ora (OpenRaster) files.
//
// ORA files are ZIP archives containing Thumbnails/thumbnail.png.
// This handler extracts that PNG and returns it to Explorer.
//
// Dependencies: miniz.h + miniz.c (place in third_party/miniz/)
//   https://github.com/richgel999/miniz/releases
//
// Build (Developer PowerShell, x64):
//   cl /LD /EHsc /O2 OraThumbnailProvider.cpp third_party/miniz/miniz.c /link /DEF:OraThumbnailProvider.def ole32.lib oleaut32.lib shlwapi.lib gdiplus.lib /OUT:OraThumbnailProvider.dll
//
// Register (elevated):
//   regsvr32 OraThumbnailProvider.dll
//
// Unregister:
//   regsvr32 /u OraThumbnailProvider.dll

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <gdiplus.h>
#include "third_party/miniz/miniz.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

// {8F6A1D3E-2C4B-4F7A-9E0D-1B5C3A8F2E6D}
// Generate your own GUID: https://guidgenerator.com
static const CLSID CLSID_OraThumbnailProvider =
    {0x8F6A1D3E, 0x2C4B, 0x4F7A,
     {0x9E, 0x0D, 0x1B, 0x5C, 0x3A, 0x8F, 0x2E, 0x6D}};

static LONG      g_cDllRef      = 0;
static HINSTANCE g_hInst        = nullptr;
static ULONG_PTR g_gdiplusToken = 0;

// ---------------------------------------------------------------------------
// Read all bytes from an IStream into a heap buffer (CoTaskMemAlloc).
// ---------------------------------------------------------------------------
static HRESULT StreamToBytes(IStream* pStream, BYTE** ppData, DWORD* pSize)
{
    STATSTG stat{};
    HRESULT hr = pStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) return hr;

    DWORD size = static_cast<DWORD>(stat.cbSize.QuadPart);
    BYTE* buf  = static_cast<BYTE*>(CoTaskMemAlloc(size));
    if (!buf) return E_OUTOFMEMORY;

    LARGE_INTEGER li{};
    pStream->Seek(li, STREAM_SEEK_SET, nullptr);

    DWORD read = 0;
    hr = pStream->Read(buf, size, &read);
    if (FAILED(hr) || read != size) {
        CoTaskMemFree(buf);
        return FAILED(hr) ? hr : E_FAIL;
    }

    *ppData = buf;
    *pSize  = size;
    return S_OK;
}

// ---------------------------------------------------------------------------
// Extract Thumbnails/thumbnail.png from an in-memory ZIP using miniz.
// Returns a buffer allocated by miniz (free with mz_free).
// ---------------------------------------------------------------------------
static HRESULT ExtractThumbnailPng(const BYTE* zipData, DWORD zipLen,
                                    void** ppPng, size_t* pPngLen)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, zipData, zipLen, 0))
        return E_FAIL;

    void* pngData = mz_zip_reader_extract_file_to_heap(
        &zip, "Thumbnails/thumbnail.png", pPngLen, 0);

    mz_zip_reader_end(&zip);

    if (!pngData) return E_FAIL;

    *ppPng = pngData;
    return S_OK;
}

// ---------------------------------------------------------------------------
// COM object
// ---------------------------------------------------------------------------
class OraThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider
{
public:
    OraThumbnailProvider() : m_cRef(1), m_pStream(nullptr)
    {
        InterlockedIncrement(&g_cDllRef);
    }

    ~OraThumbnailProvider()
    {
        if (m_pStream) m_pStream->Release();
        InterlockedDecrement(&g_cDllRef);
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IInitializeWithStream))
        {
            *ppv = static_cast<IInitializeWithStream*>(this);
        }
        else if (IsEqualIID(riid, IID_IThumbnailProvider))
        {
            *ppv = static_cast<IThumbnailProvider*>(this);
        }
        else
        {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        LONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD) override
    {
        if (m_pStream) return E_UNEXPECTED;
        m_pStream = pStream;
        m_pStream->AddRef();
        return S_OK;
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override
    {
        *phbmp    = nullptr;
        *pdwAlpha = WTSAT_UNKNOWN;

        BYTE* zipData = nullptr;
        DWORD zipLen  = 0;
        HRESULT hr = StreamToBytes(m_pStream, &zipData, &zipLen);
        if (FAILED(hr)) return hr;

        void*  pngData = nullptr;
        size_t pngLen  = 0;
        hr = ExtractThumbnailPng(zipData, zipLen, &pngData, &pngLen);
        CoTaskMemFree(zipData);
        if (FAILED(hr)) return hr;

        IStream* pngStream = SHCreateMemStream(
            static_cast<const BYTE*>(pngData), static_cast<UINT>(pngLen));
        mz_free(pngData);
        if (!pngStream) return E_OUTOFMEMORY;

        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pngStream);
        pngStream->Release();

        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
            delete bmp;
            return E_FAIL;
        }

        UINT w = bmp->GetWidth();
        UINT h = bmp->GetHeight();
        UINT newW, newH;
        if (w >= h) {
            newW = cx;
            newH = (h * cx) / max(w, 1u);
        } else {
            newH = cx;
            newW = (w * cx) / max(h, 1u);
        }
        if (newW == 0) newW = 1;
        if (newH == 0) newH = 1;

        Gdiplus::Bitmap* scaled = new Gdiplus::Bitmap(newW, newH, PixelFormat32bppARGB);
        if (scaled->GetLastStatus() != Gdiplus::Ok) {
            delete scaled;
            delete bmp;
            return E_OUTOFMEMORY;
        }

        Gdiplus::Graphics g(scaled);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(bmp, 0, 0, static_cast<INT>(newW), static_cast<INT>(newH));
        delete bmp;

        scaled->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), phbmp);
        delete scaled;

        *pdwAlpha = WTSAT_ARGB;
        return *phbmp ? S_OK : E_FAIL;
    }

private:
    LONG     m_cRef;
    IStream* m_pStream;
};

// ---------------------------------------------------------------------------
// Class factory
// ---------------------------------------------------------------------------
class OraClassFactory : public IClassFactory
{
public:
    OraClassFactory() : m_cRef(1) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        LONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) override
    {
        *ppv = nullptr;
        if (pOuter) return CLASS_E_NOAGGREGATION;
        auto* p = new OraThumbnailProvider();
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&g_cDllRef);
        else      InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    LONG m_cRef;
};

// ---------------------------------------------------------------------------
// DLL entry points
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hMod;
        DisableThreadLibraryCalls(hMod);
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return g_cDllRef == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_OraThumbnailProvider))
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new OraClassFactory();
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// Self-registration
// ---------------------------------------------------------------------------
STDAPI DllRegisterServer()
{
    wchar_t clsidStr[64];
    wchar_t dllPath[MAX_PATH];
    StringFromGUID2(CLSID_OraThumbnailProvider, clsidStr, 64);
    GetModuleFileNameW(g_hInst, dllPath, MAX_PATH);

    wchar_t key[256];
    HKEY hKey;

    swprintf_s(key, L"CLSID\\%s", clsidStr);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0,
                    KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(L"ORA Thumbnail Provider"),
                   static_cast<DWORD>((wcslen(L"ORA Thumbnail Provider") + 1) * 2));
    RegCloseKey(hKey);

    swprintf_s(key, L"CLSID\\%s\\InProcServer32", clsidStr);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0,
                    KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(dllPath),
                   static_cast<DWORD>((wcslen(dllPath) + 1) * 2));
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(L"Apartment"),
                   static_cast<DWORD>((wcslen(L"Apartment") + 1) * 2));
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CLASSES_ROOT,
                    L".ora\\shellex\\{e357fccd-a995-4576-b01f-234630154e96}",
                    0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(clsidStr),
                   static_cast<DWORD>((wcslen(clsidStr) + 1) * 2));
    RegCloseKey(hKey);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_OraThumbnailProvider, clsidStr, 64);

    wchar_t key[256];
    swprintf_s(key, L"CLSID\\%s", clsidStr);
    SHDeleteKeyW(HKEY_CLASSES_ROOT, key);
    SHDeleteKeyW(HKEY_CLASSES_ROOT,
                 L".ora\\shellex\\{e357fccd-a995-4576-b01f-234630154e96}");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}