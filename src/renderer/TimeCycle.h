#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace gvlk {

struct TimeCycleKey {
    float time;
    float r, g, b;
};

class TimeCycle {
public:
    bool Load(const std::string& path);
    void Update();

    uint32_t GetPackedColor() const;
    bool IsEnabled() const { return m_enabled && !m_keys.empty(); }
    float GetMasterTime() const { return m_masterTime; }

private:
    float ReadMasterTime() const;
    void Interpolate(float t);

    std::vector<TimeCycleKey> m_keys;
    float m_currentR = 1.0f, m_currentG = 1.0f, m_currentB = 1.0f;
    float m_masterTime = 0.0f;
    bool m_enabled = true;
};

} // namespace gvlk
