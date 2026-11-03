#include "devices.h"

#include <windows.h>
#include <objbase.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <set>

namespace ahwin {

std::wstring toWide(const char* s) {
    if (!s || !*s) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    w.pop_back();  // buang terminator yang ikut terhitung
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

namespace {

ComPtr<IMMDeviceEnumerator> deviceEnumerator() {
    ComPtr<IMMDeviceEnumerator> en;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    return en;
}

std::wstring lowerCase(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

std::wstring friendlyName(IMMDevice* dev) {
    ComPtr<IPropertyStore> props;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return L"";
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) out = v.pwszVal;
    PropVariantClear(&v);
    return out;
}

// Nama file exe tanpa ".exe", mis. "Spotify". Kosong jika proses tidak bisa dibuka (izin atau sudah berhenti).
std::wstring processName(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t path[1024];
    DWORD len = 1024;
    std::wstring out;
    if (QueryFullProcessImageNameW(h, 0, path, &len)) {
        std::wstring full(path, len);
        size_t slash = full.find_last_of(L"\\");
        out = slash == std::wstring::npos ? full : full.substr(slash + 1);
        if (out.size() > 4 && lowerCase(out.substr(out.size() - 4)) == L".exe") out.resize(out.size() - 4);
    }
    CloseHandle(h);
    return out;
}

// Penerima hasil ActivateAudioInterfaceAsync. Dipanggil dari thread lain; pemanggil menunggu event done.
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivationHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() {
        if (done_) CloseHandle(done_);
    }

    HANDLE done() const { return done_; }
    HRESULT result() const { return hr_; }
    ComPtr<IAudioClient> client() const { return client_; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrActivate = E_FAIL;
        ComPtr<IUnknown> unk;
        HRESULT hr = op->GetActivateResult(&hrActivate, &unk);
        if (SUCCEEDED(hr) && SUCCEEDED(hrActivate) && unk) {
            hr = unk->QueryInterface(IID_PPV_ARGS(&client_));
        }
        hr_ = FAILED(hr) ? hr : hrActivate;
        SetEvent(done_);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HANDLE done_ = nullptr;
    HRESULT hr_ = E_FAIL;
    ComPtr<IAudioClient> client_;
};

}  // namespace

ComPtr<IMMDevice> findEndpoint(EDataFlow flow, const std::wstring& contains, std::wstring* nameOut) {
    ComPtr<IMMDevice> found;
    ComPtr<IMMDeviceEnumerator> en = deviceEnumerator();
    if (!en || contains.empty()) return found;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) return found;
    UINT count = 0;
    col->GetCount(&count);
    std::wstring needle = lowerCase(contains);
    for (UINT i = 0; i < count; i++) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev))) continue;
        std::wstring name = friendlyName(dev.Get());
        if (lowerCase(name).find(needle) != std::wstring::npos) {
            if (nameOut) *nameOut = name;
            return dev;
        }
    }
    return found;
}

bool mixFormat(IMMDevice* dev, int* channels, uint32_t* sampleRate) {
    ComPtr<IAudioClient> client;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) return false;
    WAVEFORMATEX* wf = nullptr;
    if (FAILED(client->GetMixFormat(&wf)) || !wf) return false;
    if (channels) *channels = wf->nChannels;
    if (sampleRate) *sampleRate = wf->nSamplesPerSec;
    CoTaskMemFree(wf);
    return true;
}

ComPtr<IAudioClient> activateProcessLoopback(uint32_t pid, std::string& err) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivationHandler* handler = new ActivationHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                             handler, &op);
    if (FAILED(hr)) {
        handler->Release();
        char buf[128];
        std::snprintf(buf, sizeof buf, "Gagal memulai tangkap audio proses %lu (0x%08lX)", (unsigned long)pid,
                      (unsigned long)hr);
        err = buf;
        return nullptr;
    }
    WaitForSingleObject(handler->done(), 5000);
    HRESULT rh = handler->result();
    ComPtr<IAudioClient> client = handler->client();
    if (op) op->Release();
    handler->Release();
    if (FAILED(rh) || !client) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "Tangkap audio proses %lu gagal (0x%08lX)", (unsigned long)pid,
                      (unsigned long)rh);
        err = buf;
        return nullptr;
    }
    return client;
}

int setSessionMute(uint32_t pid, bool mute, bool* prevMute) {
    ComPtr<IMMDeviceEnumerator> en = deviceEnumerator();
    ComPtr<IMMDevice> dev;
    if (!en || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return 0;
    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &mgr))) return 0;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(mgr->GetSessionEnumerator(&sessions))) return 0;
    int count = 0;
    sessions->GetCount(&count);
    int changed = 0;
    for (int i = 0; i < count; i++) {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(sessions->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> c2;
        if (FAILED(ctl.As(&c2))) continue;
        DWORD sessionPid = 0;
        if (FAILED(c2->GetProcessId(&sessionPid)) || sessionPid != pid) continue;
        ComPtr<ISimpleAudioVolume> vol;
        if (FAILED(ctl.As(&vol))) continue;
        BOOL current = FALSE;
        vol->GetMute(&current);
        if (prevMute && changed == 0) *prevMute = current != FALSE;
        if (SUCCEEDED(vol->SetMute(mute ? TRUE : FALSE, nullptr))) changed++;
    }
    return changed;
}

std::string listEndpointsText() {
    std::string out;
    ComPtr<IMMDeviceEnumerator> en = deviceEnumerator();
    if (!en) return out;
    struct Flow {
        EDataFlow flow;
        const char* tag;
    };
    const Flow flows[] = {{eCapture, "in"}, {eRender, "out"}};
    for (const Flow& fl : flows) {
        ComPtr<IMMDeviceCollection> col;
        if (FAILED(en->EnumAudioEndpoints(fl.flow, DEVICE_STATE_ACTIVE, &col))) continue;
        UINT count = 0;
        col->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            ComPtr<IMMDevice> dev;
            if (FAILED(col->Item(i, &dev))) continue;
            int channels = 0;
            uint32_t rate = 0;
            if (!mixFormat(dev.Get(), &channels, &rate)) continue;
            out += std::string(fl.tag) + "\t" + toUtf8(friendlyName(dev.Get())) + "\t" + std::to_string(rate) +
                   "\t" + std::to_string(channels) + "\n";
        }
    }
    return out;
}

std::string listProcessesText() {
    std::string out;
    ComPtr<IMMDeviceEnumerator> en = deviceEnumerator();
    ComPtr<IMMDevice> dev;
    if (!en || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return out;
    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &mgr))) return out;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(mgr->GetSessionEnumerator(&sessions))) return out;
    int count = 0;
    sessions->GetCount(&count);
    std::set<DWORD> seen;
    for (int i = 0; i < count; i++) {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(sessions->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> c2;
        if (FAILED(ctl.As(&c2))) continue;
        if (c2->IsSystemSoundsSession() == S_OK) continue;
        DWORD pid = 0;
        if (FAILED(c2->GetProcessId(&pid)) || pid == 0 || seen.count(pid)) continue;
        seen.insert(pid);
        std::wstring name = processName(pid);
        if (name.empty()) continue;  // proses tidak bisa dibaca; tidak ditawarkan
        out += std::to_string(pid) + "\t" + toUtf8(name) + "\n";
    }
    return out;
}

}  // namespace ahwin
