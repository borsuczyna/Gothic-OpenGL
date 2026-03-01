#include "TimeCycle.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace gvlk {

// Gothic II 2.6 fix memory addresses
static constexpr uintptr_t ADDR_OCGAME_PTR       = 0x00AB0884;
static constexpr uintptr_t OFF_ZCSESSION_WORLD   = 0x08;
static constexpr uintptr_t OFF_SKY_CTRL_OUTDOOR  = 0x0E4;
static constexpr uintptr_t OFF_MASTER_TIME        = 0x80;

float TimeCycle::ReadMasterTime() const {
    uintptr_t game = *reinterpret_cast<uintptr_t*>(ADDR_OCGAME_PTR);
    if (!game) return -1.0f;

    uintptr_t world = *reinterpret_cast<uintptr_t*>(game + OFF_ZCSESSION_WORLD);
    if (!world) return -1.0f;

    uintptr_t skyCtrl = *reinterpret_cast<uintptr_t*>(world + OFF_SKY_CTRL_OUTDOOR);
    if (!skyCtrl) return -1.0f;

    return *reinterpret_cast<float*>(skyCtrl + OFF_MASTER_TIME);
}

bool TimeCycle::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("[GVulkan] TimeCycle: could not open %s\n", path.c_str());
        m_enabled = false;
        return false;
    }

    m_keys.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Strip inline comments
        auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);

        std::istringstream ss(line);
        TimeCycleKey key;
        if (ss >> key.time >> key.r >> key.g >> key.b) {
            m_keys.push_back(key);
        }
    }

    std::sort(m_keys.begin(), m_keys.end(),
              [](const TimeCycleKey& a, const TimeCycleKey& b) { return a.time < b.time; });

    printf("[GVulkan] TimeCycle: loaded %zu keyframes from %s\n", m_keys.size(), path.c_str());
    m_enabled = !m_keys.empty();
    return m_enabled;
}

void TimeCycle::Interpolate(float t) {
    if (m_keys.empty()) return;

    // Wrap t to [0, 1)
    t = t - (float)(int)t;
    if (t < 0.0f) t += 1.0f;

    // Find surrounding keyframes
    const TimeCycleKey* lo = &m_keys.back();
    const TimeCycleKey* hi = &m_keys.front();

    for (size_t i = 0; i < m_keys.size(); i++) {
        if (m_keys[i].time > t) {
            hi = &m_keys[i];
            lo = (i > 0) ? &m_keys[i - 1] : &m_keys.back();
            break;
        }
        lo = &m_keys[i];
        hi = (i + 1 < m_keys.size()) ? &m_keys[i + 1] : &m_keys.front();
    }

    float range = hi->time - lo->time;
    if (range < 0.0f) range += 1.0f;
    float frac = (range > 0.0001f) ? ((t - lo->time + (t < lo->time ? 1.0f : 0.0f)) / range) : 0.0f;
    frac = std::max(0.0f, std::min(1.0f, frac));

    m_currentR = lo->r + (hi->r - lo->r) * frac;
    m_currentG = lo->g + (hi->g - lo->g) * frac;
    m_currentB = lo->b + (hi->b - lo->b) * frac;
}

void TimeCycle::Update() {
    if (!m_enabled) return;

    float mt = ReadMasterTime();
    if (mt < 0.0f) return;

    m_masterTime = mt;
    Interpolate(mt);
}

uint32_t TimeCycle::GetPackedColor() const {
    auto clamp = [](float v) -> uint8_t {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    };
    uint8_t r = clamp(m_currentR);
    uint8_t g = clamp(m_currentG);
    uint8_t b = clamp(m_currentB);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16)
                       | (static_cast<uint32_t>(g) << 8)
                       | static_cast<uint32_t>(b);
}

} // namespace gvlk
