#include "volume_service.h"

#include <algorithm>
#include <comdef.h>

VolumeService::VolumeService() {
    RefreshDevice();
}

VolumeService::~VolumeService() {
    ReleaseDevice();
}

void VolumeService::RefreshDevice() {
    ReleaseDevice();

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&m_enumerator));

    if (FAILED(hr) || !m_enumerator) {
        return;
    }

    hr = m_enumerator->GetDefaultAudioEndpoint(
        eRender,
        eMultimedia,
        &m_device);

    if (FAILED(hr) || !m_device) {
        return;
    }

    hr = m_device->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&m_endpoint));

    if (FAILED(hr)) {
        m_endpoint = nullptr;
    }
}

void VolumeService::ReleaseDevice() {
    if (m_endpoint) {
        m_endpoint->Release();
        m_endpoint = nullptr;
    }
    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }
    if (m_enumerator) {
        m_enumerator->Release();
        m_enumerator = nullptr;
    }
}

int VolumeService::GetVolume() {
    if (!m_endpoint) {
        RefreshDevice();
    }
    if (!m_endpoint) {
        return 100;
    }

    float level = 1.0f;
    HRESULT hr = m_endpoint->GetMasterVolumeLevelScalar(&level);
    if (FAILED(hr)) {
        return 100;
    }

    return static_cast<int>(level * 100.0f);
}

void VolumeService::SetVolume(int vol) {
    if (!m_endpoint) {
        RefreshDevice();
    }
    if (!m_endpoint) {
        return;
    }

    vol = std::clamp(vol, 0, 100);
    float level = static_cast<float>(vol) / 100.0f;
    m_endpoint->SetMasterVolumeLevelScalar(level, nullptr);
}

bool VolumeService::IsMuted() {
    if (!m_endpoint) {
        RefreshDevice();
    }
    if (!m_endpoint) {
        return false;
    }

    BOOL muted = FALSE;
    HRESULT hr = m_endpoint->GetMute(&muted);
    return SUCCEEDED(hr) && muted;
}

void VolumeService::SetMute(bool mute) {
    if (!m_endpoint) {
        RefreshDevice();
    }
    if (!m_endpoint) {
        return;
    }

    m_endpoint->SetMute(mute ? TRUE : FALSE, nullptr);
}
