#include <windows.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

// CLSID: {7B8933A1-1372-4BD1-9F25-8B27357F7E64}
static const CLSID CLSID_SevenZipCommand = 
    { 0x7b8933a1, 0x1372, 0x4bd1, { 0x9f, 0x25, 0x8b, 0x27, 0x35, 0x7f, 0x7e, 0x64 } };

namespace N7zFlags {
    const UINT32 kExtract     = 1 << 0;  // Extract files...
    const UINT32 kExtractHere = 1 << 1;  // Extract Here
    const UINT32 kExtractTo   = 1 << 2;  // Extract to <Folder>
    const UINT32 kTest        = 1 << 4;  // Test archive
    const UINT32 kOpen        = 1 << 5;  // Open archive
    const UINT32 kCompress    = 1 << 8;  // Add to archive...
    const UINT32 kCompressTo7z= 1 << 9;  // Add to <Archive>.7z
    const UINT32 kCompressToZip=1 << 12; // Add to <Archive>.zip
}

enum SevenZipAction {
    ACTION_OPEN,
    ACTION_EXTRACT_FILES,
    ACTION_EXTRACT_HERE,
    ACTION_EXTRACT_TO_FOLDER,
    ACTION_TEST,
    ACTION_ADD_FILES,
    ACTION_ADD_7Z,
    ACTION_ADD_ZIP,
    ACTION_CRC
};

std::wstring Get7ZipPath(const std::wstring& exeName) {
    WCHAR path[MAX_PATH] = L"C:\\Program Files\\7-Zip\\";
    std::wstring full = std::wstring(path) + exeName;
    if (PathFileExistsW(full.c_str())) return full;
    return L"C:\\Program Files (x86)\\7-Zip\\" + exeName;
}

DWORD Read7ZipConfiguredFlags() {
    DWORD flags = 0x3F37;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip\\Options", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data = 0;
        DWORD size = sizeof(data);
        if (RegQueryValueExW(hKey, L"ContextMenu", nullptr, nullptr, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
            flags = data;
        }
        RegCloseKey(hKey);
    }
    return flags;
}

DWORD ReadInstalledHashMask() {
    DWORD mask = 0x1F; // Default all 5 enabled
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip-ContextMenu", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data = 0;
        DWORD size = sizeof(data);
        if (RegQueryValueExW(hKey, L"HashMask", nullptr, nullptr, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
            mask = data;
        }
        RegCloseKey(hKey);
    }
    return mask;
}

std::wstring GetBaseNameFromItems(IShellItemArray* psiItemArray) {
    if (!psiItemArray) return L"";
    DWORD count = 0;
    if (FAILED(psiItemArray->GetCount(&count)) || count == 0) return L"";
    IShellItem* item = nullptr;
    if (FAILED(psiItemArray->GetItemAt(0, &item))) return L"";
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
        item->Release();
        return L"";
    }
    LPCWSTR fileName = PathFindFileNameW(rawPath);
    WCHAR baseName[MAX_PATH] = { 0 };
    wcscpy_s(baseName, fileName);
    PathRemoveExtensionW(baseName);
    CoTaskMemFree(rawPath);
    item->Release();
    return baseName;
}

// Separator Command for Windows 11 Context Menu
class SevenZipSeparatorCommand : public IExplorerCommand
{
    LONG m_cRef = 1;
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(SevenZipSeparatorCommand, IExplorerCommand),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* ppszName) override { if (ppszName) *ppszName = nullptr; return S_OK; }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* ppszIcon) override { if (ppszIcon) *ppszIcon = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* ppszInfo) override { if (ppszInfo) *ppszInfo = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguid) override { if (pguid) *pguid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pCmdState) override {
        if (!pCmdState) return E_POINTER;
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override {
        if (!pFlags) return E_POINTER;
        *pFlags = ECF_ISSEPARATOR; // Renders the horizontal separating bar
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override { if (ppEnum) *ppEnum = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return S_OK; }
};

class SevenZipChildCommand : public IExplorerCommand
{
    LONG m_cRef = 1;
    SevenZipAction m_action;
    std::wstring m_title;
    std::wstring m_extraParam;
    EXPCMDSTATE m_state;

public:
    SevenZipChildCommand(SevenZipAction action, const std::wstring& title, const std::wstring& extraParam = L"", EXPCMDSTATE state = ECS_ENABLED)
        : m_action(action), m_title(title), m_extraParam(extraParam), m_state(state) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(SevenZipChildCommand, IExplorerCommand),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray* psiItemArray, PWSTR* ppszName) override {
        if (m_action == ACTION_EXTRACT_TO_FOLDER || m_action == ACTION_ADD_7Z || m_action == ACTION_ADD_ZIP) {
            std::wstring baseName = GetBaseNameFromItems(psiItemArray);
            if (baseName.empty()) baseName = L"archive";

            if (m_action == ACTION_EXTRACT_TO_FOLDER)
                return SHStrDupW((L"Extract to \"" + baseName + L"\\\"").c_str(), ppszName);
            if (m_action == ACTION_ADD_7Z)
                return SHStrDupW((L"Add to \"" + baseName + L".7z\"").c_str(), ppszName);
            if (m_action == ACTION_ADD_ZIP)
                return SHStrDupW((L"Add to \"" + baseName + L".zip\"").c_str(), ppszName);
        }
        return SHStrDupW(m_title.c_str(), ppszName);
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* ppszIcon) override {
        if (ppszIcon) *ppszIcon = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* ppszInfo) override { if (ppszInfo) *ppszInfo = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguid) override { if (pguid) *pguid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pCmdState) override {
        if (!pCmdState) return E_POINTER;
        *pCmdState = m_state;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override { if (!pFlags) return E_POINTER; *pFlags = ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override { if (ppEnum) *ppEnum = nullptr; return E_NOTIMPL; }

    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx*) override {
        if (!psiItemArray) return S_OK;

        DWORD count = 0;
        psiItemArray->GetCount(&count);
        if (count == 0) return S_OK;

        IShellItem* item = nullptr;
        if (FAILED(psiItemArray->GetItemAt(0, &item))) return S_OK;

        PWSTR rawPath = nullptr;
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
            item->Release();
            return S_OK;
        }

        std::wstring targetPath = rawPath;
        CoTaskMemFree(rawPath);
        item->Release();

        WCHAR parentDir[MAX_PATH] = { 0 };
        wcscpy_s(parentDir, targetPath.c_str());
        PathRemoveFileSpecW(parentDir);

        LPCWSTR fileName = PathFindFileNameW(targetPath.c_str());
        WCHAR baseName[MAX_PATH] = { 0 };
        wcscpy_s(baseName, fileName);
        PathRemoveExtensionW(baseName);

        std::wstring fmExe = Get7ZipPath(L"7zFM.exe");
        std::wstring guiExe = Get7ZipPath(L"7zG.exe");
        std::wstring exe;
        std::wstring args;

        switch (m_action) {
        case ACTION_OPEN:
            exe = fmExe;
            args = L"\"" + targetPath + L"\"";
            break;
        case ACTION_EXTRACT_FILES:
            exe = guiExe;
            args = L"x -ad -- \"" + targetPath + L"\"";
            break;
        case ACTION_EXTRACT_HERE:
            exe = guiExe;
            args = L"x -spe -y -- \"" + targetPath + L"\"";
            break;
        case ACTION_EXTRACT_TO_FOLDER:
            exe = guiExe;
            args = L"x -o\"" + std::wstring(parentDir) + L"\\" + baseName + L"\\\" -spe -y -- \"" + targetPath + L"\"";
            break;
        case ACTION_TEST:
            exe = guiExe;
            args = L"t -- \"" + targetPath + L"\"";
            break;
        case ACTION_ADD_FILES:
            exe = guiExe;
            args = L"a -ad -- \"" + targetPath + L"\"";
            break;
        case ACTION_ADD_7Z:
            exe = guiExe;
            args = L"a -t7z \"" + std::wstring(parentDir) + L"\\" + baseName + L".7z\" -- \"" + targetPath + L"\"";
            break;
        case ACTION_ADD_ZIP:
            exe = guiExe;
            args = L"a -tzip \"" + std::wstring(parentDir) + L"\\" + baseName + L".zip\" -- \"" + targetPath + L"\"";
            break;
        case ACTION_CRC:
            exe = guiExe;
            args = (m_extraParam == L"*") ? (L"h -- \"" + targetPath + L"\"") : (L"h -scrc" + m_extraParam + L" -- \"" + targetPath + L"\"");
            break;
        }

        ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), parentDir, SW_SHOWNORMAL);
        return S_OK;
    }
};

// Flat NanaZip Style Enumerator
class SevenZipEnumCommands : public IEnumExplorerCommand
{
    LONG m_cRef = 1;
    ULONG m_index = 0;
    std::vector<IExplorerCommand*> m_commands;

public:
    SevenZipEnumCommands() {
        DWORD userFlags = Read7ZipConfiguredFlags();
        DWORD hashMask = ReadInstalledHashMask();

        // 1. Open archive
        if (userFlags & N7zFlags::kOpen)
            m_commands.push_back(new SevenZipChildCommand(ACTION_OPEN, L"Open archive"));

        // 2. Extract files...
        if (userFlags & N7zFlags::kExtract)
            m_commands.push_back(new SevenZipChildCommand(ACTION_EXTRACT_FILES, L"Extract files..."));

        // 3. Extract Here
        if (userFlags & N7zFlags::kExtractHere)
            m_commands.push_back(new SevenZipChildCommand(ACTION_EXTRACT_HERE, L"Extract Here"));

        // 4. Extract to <Folder>\
        if (userFlags & N7zFlags::kExtractTo)
            m_commands.push_back(new SevenZipChildCommand(ACTION_EXTRACT_TO_FOLDER, L"Extract to folder"));

        // 5. Test archive
        if (userFlags & N7zFlags::kTest)
            m_commands.push_back(new SevenZipChildCommand(ACTION_TEST, L"Test archive"));

        // 6. Add to archive...
        if (userFlags & N7zFlags::kCompress)
            m_commands.push_back(new SevenZipChildCommand(ACTION_ADD_FILES, L"Add to archive..."));

        // 7. Add to <Archive>.7z
        if (userFlags & N7zFlags::kCompressTo7z)
            m_commands.push_back(new SevenZipChildCommand(ACTION_ADD_7Z, L"Add to 7z"));

        // 8. Add to <Archive>.zip
        if (userFlags & N7zFlags::kCompressToZip)
            m_commands.push_back(new SevenZipChildCommand(ACTION_ADD_ZIP, L"Add to zip"));

        // Add separating bar ONLY if at least one hash option is selected
        bool hasAnyHash = (hashMask & 0x1F) != 0;
        if (hasAnyHash) {
            m_commands.push_back(new SevenZipSeparatorCommand());

            if (hashMask & (1 << 0)) m_commands.push_back(new SevenZipChildCommand(ACTION_CRC, L"CRC-32", L"CRC32"));
            if (hashMask & (1 << 1)) m_commands.push_back(new SevenZipChildCommand(ACTION_CRC, L"CRC-64", L"CRC64"));
            if (hashMask & (1 << 2)) m_commands.push_back(new SevenZipChildCommand(ACTION_CRC, L"SHA-1", L"SHA1"));
            if (hashMask & (1 << 3)) m_commands.push_back(new SevenZipChildCommand(ACTION_CRC, L"SHA-256", L"SHA256"));
            if (hashMask & (1 << 4)) m_commands.push_back(new SevenZipChildCommand(ACTION_CRC, L"*", L"*"));
        }
    }

    ~SevenZipEnumCommands() {
        for (auto cmd : m_commands) cmd->Release();
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(SevenZipEnumCommands, IEnumExplorerCommand),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched) override {
        ULONG fetched = 0;
        if (pceltFetched) *pceltFetched = 0;
        if (!pUICommand) return E_POINTER;

        while (m_index < m_commands.size() && fetched < celt) {
            pUICommand[fetched] = m_commands[m_index];
            pUICommand[fetched]->AddRef();
            fetched++;
            m_index++;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG celt) override {
        m_index = (std::min)((ULONG)m_commands.size(), m_index + celt);
        return (m_index < m_commands.size()) ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override { m_index = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** ppenum) override { if (ppenum) *ppenum = nullptr; return E_NOTIMPL; }
};

// Parent "7-Zip >" Command
class SevenZipCommand : public IExplorerCommand, public IObjectWithSelection
{
    LONG m_cRef = 1;
    IShellItemArray* m_pSelection = nullptr;

public:
    ~SevenZipCommand() {
        if (m_pSelection) { m_pSelection->Release(); m_pSelection = nullptr; }
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {
            QITABENT(SevenZipCommand, IExplorerCommand),
            QITABENT(SevenZipCommand, IObjectWithSelection),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }

    IFACEMETHODIMP SetSelection(IShellItemArray* psia) override {
        if (m_pSelection) { m_pSelection->Release(); m_pSelection = nullptr; }
        if (psia) {
            m_pSelection = psia;
            m_pSelection->AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetSelection(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        return m_pSelection ? m_pSelection->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* ppszName) override { return SHStrDupW(L"7-Zip", ppszName); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* ppszIcon) override {
        std::wstring icon = Get7ZipPath(L"7zFM.exe") + L",0";
        return SHStrDupW(icon.c_str(), ppszIcon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, PWSTR* ppszInfo) override { if (ppszInfo) *ppszInfo = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* pguid) override { if (!pguid) return E_POINTER; *pguid = CLSID_SevenZipCommand; return S_OK; }

    // HIDES FROM CLASSIC CONTEXT MENU
    IFACEMETHODIMP GetState(IShellItemArray*, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState) override {
        if (!pCmdState) return E_POINTER;
        if (!fOkToBeSlow) {
            *pCmdState = ECS_HIDDEN;
            return S_OK;
        }
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override {
        if (!pFlags) return E_POINTER;
        *pFlags = ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new SevenZipEnumCommands();
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return S_OK; }
};

class ClassFactory : public IClassFactory {
    LONG m_cRef = 1;
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override {
        LONG cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        SevenZipCommand* pCmd = new SevenZipCommand();
        HRESULT hr = pCmd->QueryInterface(riid, ppv);
        pCmd->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) override { return S_OK; }
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid == CLSID_SevenZipCommand) {
        ClassFactory* pFactory = new ClassFactory();
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() { return S_OK; }
