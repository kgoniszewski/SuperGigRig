#include "cabinet.hpp"

#include <gigrig/gigrig.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "blocks/dsp_math.hpp"

namespace gigrig {

static_assert(Cabinet::kSlots == GIGRIG_CAB_SLOT_COUNT);
static_assert(kMaxIrSeconds == GIGRIG_CAB_MAX_IR_SECONDS);

std::unique_ptr<CabIr> CabIr::create(SourceIr source, double sampleRate) {
    auto ir = std::make_unique<CabIr>();
    ir->source = std::move(source);
    if (!ir->rebuild(sampleRate)) {
        return nullptr;
    }
    return ir;
}

bool CabIr::rebuild(double rate) {
    sampleRate = rate;
    const std::vector<float> prepared = prepareIr(source, rate);
    convolver.build(prepared.data(), static_cast<int>(prepared.size()));
    return !prepared.empty();
}

Cabinet::~Cabinet() {
    for (Slot& s : slots_) {
        delete s.current;
        delete s.next;
        delete s.retiring;
    }
}

void Cabinet::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kFadeSeconds * sampleRate)));
    room_.prepare(sampleRate);
    wet_.prepare(sampleRate, kFadeSeconds);
    for (Slot& s : slots_) {
        s.gain.prepare(sampleRate, kFadeSeconds);
        s.send.prepare(sampleRate, kFadeSeconds);
        // Not on the audio thread here, so finished and replaced IRs are freed directly.
        if (s.fading) {
            delete s.current;
            s.current = s.next;
            s.next = nullptr;
            s.fading = false;
        }
        delete s.retiring;
        s.retiring = nullptr;
        if (s.current != nullptr && s.current->sampleRate != sampleRate && !s.current->rebuild(sampleRate)) {
            delete s.current;
            s.current = nullptr;
        }
        s.length.store(s.current != nullptr ? s.current->convolver.length() : 0, std::memory_order_relaxed);
    }
    reset();
}

void Cabinet::resetAudio() noexcept {
    for (Slot& s : slots_) {
        if (s.current != nullptr) {
            s.current->convolver.reset();
        }
        if (s.next != nullptr) {
            s.next->convolver.reset();
        }
        s.gain.snap(s.gain.target());
        s.send.snap(s.send.target());
    }
    room_.reset();
}

void Cabinet::reset() noexcept {
    resetAudio();
    const float w = enabled_ && hasIr() ? 1.0f : 0.0f;
    wet_.snap(w);
    idle_ = w == 0.0f;
}

bool Cabinet::hasIr() const noexcept {
    return std::any_of(slots_.begin(), slots_.end(),
                       [](const Slot& s) { return s.current != nullptr || s.next != nullptr; });
}

std::optional<Retired> Cabinet::applyLoad(int slot, CabIr* ir) noexcept {
    const Retired refuse{ir, &destroyObject<CabIr>};
    if (slot < 0 || slot >= kSlots) {
        return refuse;
    }
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    if (s.fading || s.retiring != nullptr) {
        return std::nullopt;
    }
    if (ir != nullptr && ir->sampleRate != sampleRate_) {
        // Built before a sample rate change; the app reloads it if irLength shows it missing.
        refused_.fetch_add(1, std::memory_order_relaxed);
        return refuse;
    }
    if (ir != nullptr) {
        ir->convolver.reset();
    }
    s.next = ir;
    s.fading = true;
    s.fadePos = 0;
    return Retired{};
}

void Cabinet::finishFade(Slot& s) noexcept {
    s.retiring = s.current;
    s.current = s.next;
    s.next = nullptr;
    s.fading = false;
    s.length.store(s.current != nullptr ? s.current->convolver.length() : 0, std::memory_order_relaxed);
}

int Cabinet::irLength(int slot) const noexcept {
    if (slot < 0 || slot >= kSlots) {
        return 0;
    }
    return slots_[static_cast<std::size_t>(slot)].length.load(std::memory_order_relaxed);
}

void Cabinet::setParameters(bool on, const std::array<CabSlotParams, kSlots>& params, int roomSize) noexcept {
    enabled_ = on;
    for (std::size_t i = 0; i < kSlots; ++i) {
        Slot& s = slots_[i];
        const CabSlotParams& p = params[i];
        // pow only when the level, switch or polarity actually changed.
        if (p.on != s.last.on || p.levelDb != s.last.levelDb || p.invertPhase != s.last.invertPhase ||
            !s.paramsSeen) {
            float g = 0.0f;
            if (p.on) {
                g = static_cast<float>(dbToGain(static_cast<double>(p.levelDb)));
                if (p.invertPhase) {
                    g = -g;
                }
            }
            s.gain.setTarget(g);
        }
        s.send.setTarget(std::clamp(p.roomSend, 0.0f, 1.0f));
        s.last = p;
        s.paramsSeen = true;
    }
    room_.setSize(roomSize);

    const float w = on && hasIr() ? 1.0f : 0.0f;
    if (idle_ && w > 0.0f) {
        // Waking up: whatever the convolvers and room held is stale.
        resetAudio();
        idle_ = false;
    }
    wet_.setTarget(w);
}

void Cabinet::processChunk(const float* in, float* out, int n) noexcept {
    float a[Convolver::kSmallBlock];
    float b[Convolver::kSmallBlock];
    float wet[Convolver::kSmallBlock] = {};
    float send[Convolver::kSmallBlock] = {};
    const auto bytes = static_cast<std::size_t>(n) * sizeof(float);

    for (Slot& s : slots_) {
        if (s.current != nullptr) {
            s.current->convolver.process(in, a, n);
        } else {
            std::memset(a, 0, bytes);
        }
        if (s.fading) {
            if (s.next != nullptr) {
                s.next->convolver.process(in, b, n);
            } else {
                std::memset(b, 0, bytes);
            }
            const float step = 1.0f / static_cast<float>(fadeLength_);
            for (int i = 0; i < n; ++i) {
                const float g = std::min(1.0f, static_cast<float>(s.fadePos) * step);
                a[i] += g * (b[i] - a[i]);
                if (s.fadePos < fadeLength_) {
                    ++s.fadePos;
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            const float v = a[i] * s.gain.next();
            wet[i] += v;
            send[i] += v * s.send.next();
        }
        if (s.fading && s.fadePos >= fadeLength_) {
            finishFade(s);
        }
    }
    for (int i = 0; i < n; ++i) {
        wet[i] += room_.processSample(send[i]);
        const float w = wet_.next();
        out[i] = (1.0f - w) * in[i] + w * wet[i];
    }
}

void Cabinet::process(const float* in, float* out, int n) noexcept {
    if (idle_) {
        if (out != in) {
            std::memcpy(out, in, static_cast<std::size_t>(n) * sizeof(float));
        }
        // Nothing is heard, but a pending crossfade still has to finish so the slot frees up.
        for (Slot& s : slots_) {
            if (s.fading) {
                s.fadePos = std::min(fadeLength_, s.fadePos + n);
                if (s.fadePos >= fadeLength_) {
                    finishFade(s);
                }
            }
        }
        return;
    }
    for (int done = 0; done < n;) {
        const int chunk = std::min(Convolver::kSmallBlock, n - done);
        processChunk(in + done, out + done, chunk);
        done += chunk;
    }
    if (wet_.target() == 0.0f && !wet_.isSmoothing()) {
        idle_ = true;
    }
}

}  // namespace gigrig
