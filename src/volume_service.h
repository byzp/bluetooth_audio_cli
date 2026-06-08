#pragma once

#include <mmdeviceapi.h>
#include <endpointvolume.h>

/// Controls the Windows system master volume and mute state
/// using the Core Audio API (IMMDeviceEnumerator / IAudioEndpointVolume).
class VolumeService {
public:
    VolumeService();
    ~VolumeService();

    VolumeService(const VolumeService&) = delete;
    VolumeService& operator=(const VolumeService&) = delete;

    /// Get master volume (0 - 100).
    int GetVolume();

    /// Set master volume (0 - 100). Values are clamped.
    void SetVolume(int vol);

    /// Check if the master output is muted.
    bool IsMuted();

    /// Mute or unmute the master output.
    void SetMute(bool mute);

private:
    IMMDeviceEnumerator* m_enumerator = nullptr;
    IMMDevice*           m_device     = nullptr;
    IAudioEndpointVolume* m_endpoint  = nullptr;

    void RefreshDevice();
    void ReleaseDevice();
};
