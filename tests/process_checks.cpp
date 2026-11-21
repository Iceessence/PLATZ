#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <wrl/client.h>
#include "Project.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using Microsoft::WRL::ComPtr;
using Element = ComPtr<IUIAutomationElement>;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void checkCall(HRESULT status, const char* message) {
    if (FAILED(status)) throw std::runtime_error(message);
}
template<class F> void waitFor(QProcess& process, const char* failure, F condition) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    do {
        if (condition()) return;
        check(!process.waitForFinished(80), "PLATZ exited before completing the UI check.");
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error(failure);
}
QByteArray digest(const QString& path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "Could not hash project.");
    return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);
}
struct WindowSearch { DWORD pid; HWND window{}; };
BOOL CALLBACK findWindow(HWND window, LPARAM data) {
    auto& state = *reinterpret_cast<WindowSearch*>(data);
    DWORD pid{};
    GetWindowThreadProcessId(window,&pid);
    if (pid == state.pid && IsWindowVisible(window)) {
        wchar_t title[512]{};
        GetWindowTextW(window,title,512);
        if (std::wstring_view(title).find(L"PLATZ Studio") != std::wstring_view::npos) {
            state.window = window;
            return FALSE;
        }
    }
    return TRUE;
}
Element named(IUIAutomation& automation, IUIAutomationElement& root, const std::wstring& name, int type) {
    VARIANT text{};
    text.vt = VT_BSTR;
    text.bstrVal = SysAllocString(name.c_str());
    check(text.bstrVal != nullptr, "UIA text allocation failed.");
    ComPtr<IUIAutomationCondition> nameMatch, typeMatch, both;
    auto status = automation.CreatePropertyCondition(UIA_NamePropertyId,text,&nameMatch);
    VariantClear(&text);
    checkCall(status,"UIA name lookup failed.");
    VARIANT control{};
    control.vt = VT_I4;
    control.lVal = type;
    checkCall(automation.CreatePropertyCondition(UIA_ControlTypePropertyId,control,&typeMatch),"UIA type lookup failed.");
    checkCall(automation.CreateAndCondition(nameMatch.Get(),typeMatch.Get(),&both),"UIA filter failed.");
    Element element;
    checkCall(root.FindFirst(TreeScope_Descendants,both.Get(),&element),"UIA element enumeration failed.");
    return element;
}
std::wstring valueOf(IUIAutomationElement& editor) {
    ComPtr<IUIAutomationValuePattern> pattern;
    checkCall(editor.GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&pattern)),"Object name lacks value access.");
    BSTR value{};
    checkCall(pattern->get_CurrentValue(&value),"Object name could not be read.");
    std::wstring text(value,SysStringLen(value));
    SysFreeString(value);
    return text;
}
void postKey(HWND main, DWORD pid, UINT key) {
    GUITHREADINFO state{};
    state.cbSize=sizeof(state);
    DWORD owner{};
    auto thread=GetWindowThreadProcessId(main,&owner);
    check(owner==pid && GetGUIThreadInfo(thread,&state) && state.hwndFocus,"PLATZ lost keyboard focus.");
    GetWindowThreadProcessId(state.hwndFocus,&owner);
    check(owner==pid,"The focused control belongs to another process.");
    // These messages are addressed to this process only, even if foreground focus changes.
    auto bits=1L | (MapVirtualKeyW(key,MAPVK_VK_TO_VSC) << 16);
    check(PostMessageW(state.hwndFocus,WM_KEYDOWN,key,bits) &&
          PostMessageW(state.hwndFocus,WM_KEYUP,key,bits|0xc0000000UL),"Could not type in PLATZ.");
}
void run(const QString& executable, const QString& source, IUIAutomation& automation) {
    QTemporaryDir dir;
    check(dir.isValid(),"Temporary project folder unavailable.");
    auto copy=dir.filePath("Process restart.platz");
    auto hash=digest(source);
    check(QFile::copy(source,copy),"Could not copy the permanent project.");
    auto project=platz::Project::open(copy.toStdWString());
    check(!project.objects().empty(),"Validation source has no objects.");
    const auto& target=project.objects().front();
    std::wstring identity=QString::fromStdString(target.id).toStdWString();
    auto original=QString::fromStdString(target.name).toStdWString();
    auto changed=original+L"7";
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("PLATZ_SETTINGS_DIR",dir.path());
    environment.insert("QT_ACCESSIBILITY","1");
    for (int launch=0;launch<2;++launch) {
        // Declare the child after the temporary directory. On failure QProcess tears down
        // this disposable process before QTemporaryDir removes its project and preferences.
        QProcess child;
        child.setProcessEnvironment(environment);
        child.setWorkingDirectory(QFileInfo(executable).absolutePath());
        child.start(executable,{copy});
        check(child.waitForStarted(6000),"Could not start the PLATZ executable.");
        auto pid=static_cast<DWORD>(child.processId());
        WindowSearch search{pid};
        waitFor(child,"PLATZ Studio did not open the project.",[&]{
            EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search)); return search.window != nullptr;
        });
        Element root, editor;
        checkCall(automation.ElementFromHandle(search.window,&root),"PLATZ does not expose an accessibility root.");
        waitFor(child,"Object name missing from the accessible Inspector.",[&]{
            editor=named(automation,*root.Get(),L"Object name",UIA_EditControlTypeId);
            return editor.Get() != nullptr;
        });
        check(valueOf(*editor.Get())==(launch==0 ? original : changed),"Reopened Inspector name differs from saved source.");
        check(named(automation,*root.Get(),identity,UIA_TextControlTypeId).Get()!=nullptr,"Selected stable identity missing from Inspector.");
        if (launch==0) {
            checkCall(editor->SetFocus(),"Cannot focus object name.");
            postKey(search.window,pid,VK_END);
            postKey(search.window,pid,'7');
            waitFor(child,"Normal targeted keyboard edit failed.",[&]{ return valueOf(*editor.Get())==changed; });
            auto save=named(automation,*root.Get(),L"Save",UIA_ButtonControlTypeId);
            check(save.Get()!=nullptr,"Save button unavailable.");
            ComPtr<IUIAutomationInvokePattern> invoke;
            checkCall(save->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)),"Save lacks its invokable action.");
            checkCall(invoke->Invoke(),"Invoking Save failed.");
            waitFor(child,"The pending edit did not commit to disk.",[&]{
                auto saved=platz::Project::open(copy.toStdWString());
                auto* item=saved.find(target.id);
                return item && QString::fromStdString(item->name).toStdWString()==changed;
            });
        }
        check(PostMessageW(search.window,WM_CLOSE,0,0),"Could not request normal PLATZ shutdown.");
        check(child.waitForFinished(6000) && child.exitStatus()==QProcess::NormalExit && child.exitCode()==0,
              "PLATZ did not shut down cleanly.");
        std::cout << "PLATZ process " << pid << (launch==0 ? " saved " : " reopened ") << target.id << "\n";
    }
    check(digest(source)==hash,"Permanent project changed while testing its disposable copy.");
}
} // namespace

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        check(app.arguments().size()==3,"Expected an executable and a validation project path.");
        checkCall(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Cannot initialize UI Automation.");
        {
            ComPtr<IUIAutomation> automation;
            checkCall(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)),
                      "UI Automation is unavailable.");
            run(app.arguments()[1],app.arguments()[2],*automation.Get());
        }
        CoUninitialize();
        std::cout<<"PASS two separate application launches, pending Inspector edit, Save, normal shutdown and stable-ID reopen.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
