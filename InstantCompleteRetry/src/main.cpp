#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace geode::prelude;

namespace {
    bool g_pendingCompletionRestart = false;

    bool instantRetryEnabled() {
        return Mod::get()->getSettingValue<bool>("instant-retry-after-complete");
    }
}

class $modify(InstantCompletePlayLayer, PlayLayer) {
    void levelComplete() {
        if (!instantRetryEnabled()) {
            g_pendingCompletionRestart = false;
            PlayLayer::levelComplete();
            return;
        }

        g_pendingCompletionRestart = true;
        PlayLayer::levelComplete();

        auto self = static_cast<PlayLayer*>(this);
        self->retain();

        geode::queueInMainThread([self] {
            if (g_pendingCompletionRestart) {
                const bool shouldRestart = instantRetryEnabled();
                g_pendingCompletionRestart = false;

                if (shouldRestart) {
                    self->resetLevel();
                }
            }

            self->release();
        });
    }
};

class $modify(InstantCompleteEndLevelLayer, EndLevelLayer) {
    void showLayer(bool instant) {
        if (instantRetryEnabled() && g_pendingCompletionRestart) {
            g_pendingCompletionRestart = false;
            this->onReplay(nullptr);
            return;
        }

        EndLevelLayer::showLayer(instant);
    }
};
