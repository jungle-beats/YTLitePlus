#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/utils/async.hpp>
#include <arc/time/Sleep.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace geode::prelude;

namespace {
    bool g_pendingCompletionRestart = false;
    std::uint64_t g_restartGeneration = 0;
    EndLevelLayer* g_hiddenEndLayer = nullptr;

    bool instantRetryEnabled() {
        return Mod::get()->getSettingValue<bool>("instant-retry-after-complete");
    }

    double restartDelaySeconds() {
        return std::clamp(
            Mod::get()->getSettingValue<double>("restart-delay-seconds"),
            0.0,
            30.0
        );
    }

    void clearHiddenEndLayer() {
        if (g_hiddenEndLayer) {
            g_hiddenEndLayer->release();
            g_hiddenEndLayer = nullptr;
        }
    }
}

class $modify(InstantCompletePlayLayer, PlayLayer) {
    void levelComplete() {
        if (!instantRetryEnabled()) {
            ++g_restartGeneration;
            g_pendingCompletionRestart = false;
            clearHiddenEndLayer();
            PlayLayer::levelComplete();
            return;
        }

        ++g_restartGeneration;
        const auto generation = g_restartGeneration;
        g_pendingCompletionRestart = true;
        clearHiddenEndLayer();

        // Let Geometry Dash do its real completion bookkeeping first.
        PlayLayer::levelComplete();

        auto self = static_cast<PlayLayer*>(this);
        self->retain();

        const auto delayMs = static_cast<std::uint64_t>(
            std::llround(restartDelaySeconds() * 1000.0)
        );

        // Geode's async completion callback is marshalled back to the main thread.
        async::spawn(
            arc::sleep(asp::Duration::fromMillis(delayMs)),
            [self, generation] {
                // Ignore an obsolete timer if another completion/restart cycle replaced it.
                if (generation != g_restartGeneration) {
                    self->release();
                    return;
                }

                if (!g_pendingCompletionRestart || PlayLayer::get() != self) {
                    g_pendingCompletionRestart = false;
                    clearHiddenEndLayer();
                    self->release();
                    return;
                }

                g_pendingCompletionRestart = false;

                // If GD already created its end layer, use its Replay path so all of
                // GD's normal end-layer cleanup happens, even though we never showed it.
                auto endLayer = g_hiddenEndLayer;
                g_hiddenEndLayer = nullptr;

                if (endLayer && endLayer->m_playLayer == self) {
                    endLayer->onReplay(nullptr);
                    endLayer->release();
                }
                else {
                    if (endLayer) {
                        endLayer->release();
                    }
                    self->resetLevel();
                }

                self->release();
            }
        );
    }
};

class $modify(InstantCompleteEndLevelLayer, EndLevelLayer) {
    void showLayer(bool instant) {
        if (instantRetryEnabled() && g_pendingCompletionRestart) {
            // Keep the end screen completely invisible while the restart timer runs.
            this->setVisible(false);

            if (g_hiddenEndLayer != this) {
                clearHiddenEndLayer();
                g_hiddenEndLayer = this;
                g_hiddenEndLayer->retain();
            }
            return;
        }

        EndLevelLayer::showLayer(instant);
    }
};
