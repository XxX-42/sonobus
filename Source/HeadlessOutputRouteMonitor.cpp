#include "HeadlessOutputRouteMonitor.h"

#include <CoreAudio/CoreAudio.h>

#include <vector>

struct HeadlessOutputRouteMonitor::Impl
{
    explicit Impl(std::function<void()> callback)
        : onRouteChange(std::move(callback))
    {
        const AudioObjectPropertyAddress addresses[] = {
            { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain },
            { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain }
        };

        for (const auto& address : addresses)
        {
            const auto status = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address,
                                                               &Impl::propertyChanged, this);
            if (status == noErr)
                listeners.push_back(address);
        }
    }

    ~Impl()
    {
        for (const auto& address : listeners)
            AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address,
                                              &Impl::propertyChanged, this);
    }

    static OSStatus propertyChanged(AudioObjectID, UInt32,
                                    const AudioObjectPropertyAddress[], void* clientData)
    {
        auto* self = static_cast<Impl*>(clientData);
        if (self != nullptr && self->onRouteChange)
            self->onRouteChange();
        return noErr;
    }

    std::function<void()> onRouteChange;
    std::vector<AudioObjectPropertyAddress> listeners;
};

HeadlessOutputRouteMonitor::HeadlessOutputRouteMonitor(std::function<void()> onRouteChange)
    : impl(std::make_unique<Impl>(std::move(onRouteChange)))
{
}

HeadlessOutputRouteMonitor::~HeadlessOutputRouteMonitor() = default;
