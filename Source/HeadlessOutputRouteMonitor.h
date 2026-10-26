#pragma once

#include <functional>
#include <memory>

class HeadlessOutputRouteMonitor
{
public:
    explicit HeadlessOutputRouteMonitor(std::function<void()> onRouteChange);
    ~HeadlessOutputRouteMonitor();

    HeadlessOutputRouteMonitor(const HeadlessOutputRouteMonitor&) = delete;
    HeadlessOutputRouteMonitor& operator=(const HeadlessOutputRouteMonitor&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
