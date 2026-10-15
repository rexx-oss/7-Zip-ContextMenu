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

// CLSID: {7B8933A1-1372-4BD1-9F25-8B27357F7E64}
static const CLSID CLSID_SevenZipCommand = 
    { 0x7b8933a1, 0x1372, 0x4bd1, { 0x9f, 0x25, 0x8b, 0x27, 0x35, 0x7f, 0x7e, 0x64 } };

// Locate 7-Zip installation directory
std::wstring Get7ZipPath(const std::wstring& exeName) {
    WCHAR path[MAX_PATH] = L"C:\\Program Files\\7-Zip\\";
    std::wstring full = std::wstring(path) + exeName;
    if (PathFileExistsW(full.c_str())) return full;
    return L"C:\\Program Files (x86)\\7-Zip\\" + exeName;
}

// Child Command Implementation
class SevenZipChildCommand : public IExplorerCommand
{
    LONG m_cRef = 1;
    std::wstring m_title;
    std::wstring m_exe;
    std::wstring m_args;
    std::wstring m_workDir;
    EXPCMDSTATE m_state;

public:
    SevenZipChildCommand(const std::wstring& title, const std::wstring& exe, const std::wstring& args, const std::wstring& workDir, EXPCMDSTATE state)
        : m_title(title), m_exe(exe), m_args(args), m_workDir(workDir), m_state(state) {}

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

    IFACEMETHODIMP GetTitle(IShellItemArray*, PWSTR* ppszName) override { return SHStrDupW(m_title.c_str(), ppszName); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, PWSTR* ppszIcon) override {
        std::wstring icon = Get7ZipPath(L"7zFM.exe") + L",0";
        return SHStrDupW(icon.c_str(), ppszIcon);
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

    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override {
        ShellExecuteW(nullptr, L"open", m_exe.c_str(), m_args.c_str(), m_workDir.empty() ? nullptr : m_workDir.c_str(), SW_SHOWNORMAL);
        return S_OK;
    }
};

// Cascading Subcommands Enumerator
class SevenZipEnumCommands : public IEnumExplorerCommand
{
    LONG m_cRef = 1;
    ULONG m_index = 0;
    std::vector<IExplorerCommand*> m_commands;

public:
    SevenZipEnumCommands(const std::wstring& targetPath) {
        if (targetPath.empty()) return;

        WCHAR parentDir[MAX_PATH] = { 0 };
        wcscpy_s(parentDir, targetPath.c_str());
        PathRemoveFileSpecW(parentDir);

        LPCWSTR fileName = PathFindFileNameW(targetPath.c_str());
        WCHAR baseName[MAX_PATH] = { 0 };
        wcscpy_s(baseName, fileName);
        PathRemoveExtensionW(baseName);

        LPCWSTR ext = PathFindExtensionW(targetPath.c_str());
        std::wstring extLower = ext;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::towlower);

        BOOL isDir = PathIsDirectoryW(targetPath.c_str());
        
        // Known archive extensions
        static const std::vector<std::wstring> archiveExts = {
            L".7z", L".zip", L".rar", L".001", L".cab", L".iso", L".tar", L".gz",
            L".tgz", L".bz2", L".tbz2", L".xz", L".txz", L".wim", L".dmg", L".vhd"
        };
        BOOL isArchive = !isDir && (std::find(archiveExts.begin(), archiveExts.end(), extLower) != archiveExts.end());
        EXPCMDSTATE extractState = isArchive ? ECS_ENABLED : ECS_HIDDEN;

        std::wstring fmExe = Get7ZipPath(L"7zFM.exe");
        std::wstring guiExe = Get7ZipPath(L"7zG.exe");

        // 1. Open archive
        m_commands.push_back(new SevenZipChildCommand(L"Open archive", fmExe, L"\"" + targetPath + L"\"", parentDir, extractState));
        // 2. Extract files...
        m_commands.push_back(new SevenZipChildCommand(L"Extract files...", guiExe, L"x \"" + targetPath + L"\"", parentDir, extractState));
        // 3. Extract Here
        m_commands.push_back(new SevenZipChildCommand(L"Extract Here", guiExe, L"x \"" + targetPath + L"\" -spe", parentDir, extractState));
        // 4. Extract to <name>\ 
        std::wstring extractToFolder = std::wstring(parentDir) + L"\\" + baseName + L"\\";
        m_commands.push_back(new SevenZipChildCommand(L"Extract to \"" + std::wstring(baseName) + L"\\\"", guiExe, L"x \"" + targetPath + L"\" -o\"" + extractToFolder + L"\"", parentDir, extractState));
        // 5. Test archive
        m_commands.push_back(new SevenZipChildCommand(L"Test archive", guiExe, L"t \"" + targetPath + L"\"", parentDir, extractState));

        // 6. Add to archive...
        m_commands.push_back(new SevenZipChildCommand(L"Add to archive...", guiExe, L"a \"" + targetPath + L"\"", parentDir, ECS_ENABLED));
        // 7. Add to <name>.7z
        std::wstring out7z = std::wstring(parentDir) + L"\\" + baseName + L".7z";
        m_commands.push_back(new SevenZipChildCommand(L"Add to \"" + std::wstring(baseName) + L".7z\"", guiExe, L"a -t7z \"" + out7z + L"\" \"" + targetPath + L"\"", parentDir, ECS_ENABLED));
        // 8. Add to <name>.zip
        std::wstring outZip = std::wstring(parentDir) + L"\\" + baseName + L".zip";
        m_commands.push_back(new SevenZipChildCommand(L"Add to \"" + std::wstring(baseName) + L".zip\"", guiExe, L"a -tzip \"" + outZip + L"\" \"" + targetPath + L"\"", parentDir, ECS_ENABLED));
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
    std::wstring m_targetPath;

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
        m_targetPath.clear();

        if (psia) {
            m_pSelection = psia;
            m_pSelection->AddRef();

            DWORD count = 0;
            psia->GetCount(&count);
            if (count > 0) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(psia->GetItemAt(0, &item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        m_targetPath = path;
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
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
    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pCmdState) override {
        if (!pCmdState) return E_POINTER;
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }
    // ECF_HASSUBCOMMANDS tells Windows 11 this item has a cascading submenu
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override {
        if (!pFlags) return E_POINTER;
        *pFlags = ECF_HASSUBCOMMANDS;
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = new SevenZipEnumCommands(m_targetPath);
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
