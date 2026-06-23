#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <cocos2d.h>
#include <algorithm>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

// Không còn static constexpr – mọi giá trị đều lấy từ mod settings

static bool safeToCull(GameObject* obj, float maxSmallSize) {
    if (!obj) return false;
    if (obj->m_isTrigger) return false;

    const CCRect& rect = obj->getObjectRect();
    if (rect.size.width <= maxSmallSize && rect.size.height <= maxSmallSize)
        return true;

    if (obj->m_groupCount > 0) return false;
    return true;
}

class $modify(DebugCullingLayer, PlayLayer) {
public:
    struct Fields {
        int frameCounter = 0;
        CCRect lastViewport;
        bool hasLastViewport = false;
        std::unordered_set<GameObject*> hiddenByUs;

        // ── Debug label trực tiếp trên màn hình ──
        CCLabelBMFont* debugLabel = nullptr;
        int totalChecks = 0;
        int totalHidden = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto label = CCLabelBMFont::create("Culling: chua chay", "bigFont.fnt");
        label->setScale(0.4f);
        label->setAnchorPoint({0, 1});
        label->setPosition({10, CCDirector::sharedDirector()->getWinSize().height - 10});
        label->setID("debug-culling-label"_spr);
        label->setZOrder(1000);
        this->addChild(label, 1000);
        m_fields->debugLabel = label;

        return true;
    }

    CCRect computeVisibleWorldRect(float paddingX, float paddingY) {
        if (!m_objectLayer) return CCRect(0,0,0,0);
        CCSize win = CCDirector::sharedDirector()->getWinSize();
        CCPoint bl = m_objectLayer->convertToNodeSpace(CCPoint(0,0));
        CCPoint tr = m_objectLayer->convertToNodeSpace(CCPoint(win.width, win.height));
        float l = std::min(bl.x, tr.x) - paddingX;
        float r = std::max(bl.x, tr.x) + paddingX;
        float b = std::min(bl.y, tr.y) - paddingY;
        float t = std::max(bl.y, tr.y) + paddingY;
        return CCRect(l, b, r-l, t-b);
    }

    void updateVisibility(float dt) override {
        PlayLayer::updateVisibility(dt);

        // Đọc tất cả cài đặt một lần mỗi lần hàm được gọi (nhẹ, vì chỉ gọi 1-2 lần/frame)
        bool enabled = Mod::get()->getSettingValue<bool>("enable_culling");
        float paddingX = static_cast<float>(Mod::get()->getSettingValue<double>("cull_padding_x"));
        float paddingY = static_cast<float>(Mod::get()->getSettingValue<double>("cull_padding_y"));
        int checkPeriod = Mod::get()->getSettingValue<int64_t>("check_period");
        float changeThreshold = static_cast<float>(Mod::get()->getSettingValue<double>("change_threshold"));
        float maxSmallSize = static_cast<float>(Mod::get()->getSettingValue<double>("max_small_size"));

        if (m_fields->debugLabel) {
            if (!enabled) {
                m_fields->debugLabel->setString("Culling: TAT (setting off)");
            }
        }
        if (!enabled) return;

        if (++m_fields->frameCounter % checkPeriod != 0) return;

        CCRect viewRect = computeVisibleWorldRect(paddingX, paddingY);
        if (m_fields->hasLastViewport) {
            float dx = std::fabs(viewRect.getMinX() - m_fields->lastViewport.getMinX());
            float dy = std::fabs(viewRect.getMinY() - m_fields->lastViewport.getMinY());
            if (dx < changeThreshold && dy < changeThreshold) {
                if (m_fields->debugLabel) {
                    m_fields->debugLabel->setString(
                        ("Culling: ON | dang an: " + std::to_string(m_fields->hiddenByUs.size())
                         + " | checks: " + std::to_string(m_fields->totalChecks)).c_str()
                    );
                }
                return;
            }
        }
        m_fields->lastViewport = viewRect;
        m_fields->hasLastViewport = true;
        ++m_fields->totalChecks;

        int secLeft  = std::max(0, m_leftSectionIndex - 1);
        int secRight = m_rightSectionIndex + 1;

        auto& hiddenSet = m_fields->hiddenByUs;
        int hiddenCount = 0, restoredCount = 0;

        for (int secIdx = secLeft; secIdx <= secRight; ++secIdx) {
            if (secIdx < 0 || secIdx >= (int)m_sections.size()) continue;
            auto* sectionVecPtr = m_sections[secIdx];
            if (!sectionVecPtr) continue;

            for (auto* innerVec : *sectionVecPtr) {
                if (!innerVec) continue;
                for (auto* obj : *innerVec) {
                    if (!obj || !safeToCull(obj, maxSmallSize)) continue;

                    bool inView = obj->getObjectRect().intersectsRect(viewRect);
                    bool isVisible = obj->isVisible();
                    bool isHiddenByUs = hiddenSet.count(obj) > 0;

                    if (isVisible && !inView) {
                        obj->setVisible(false);
                        hiddenSet.insert(obj);
                        ++hiddenCount;
                        ++m_fields->totalHidden;
                    } else if (!isVisible && inView && isHiddenByUs) {
                        obj->setVisible(true);
                        hiddenSet.erase(obj);
                        ++restoredCount;
                    }
                }
            }
        }

        if (m_fields->debugLabel) {
            m_fields->debugLabel->setString(
                ("Culling: ON | dang an: " + std::to_string(hiddenSet.size())
                 + " | lan nay an/hien: " + std::to_string(hiddenCount) + "/" + std::to_string(restoredCount)
                 + " | tong da an: " + std::to_string(m_fields->totalHidden)
                 + " | checks: " + std::to_string(m_fields->totalChecks)).c_str()
            );
        }
    }

    void restoreAndClearHidden() {
        for (auto* obj : m_fields->hiddenByUs) {
            if (obj) obj->setVisible(true);
        }
        m_fields->hiddenByUs.clear();
    }

    void resetLevel() {
        restoreAndClearHidden();
        PlayLayer::resetLevel();
    }

    void onExit() {
        restoreAndClearHidden();
        PlayLayer::onExit();
    }

    void onExitTransitionDidStart() {
        restoreAndClearHidden();
        PlayLayer::onExitTransitionDidStart();
    }
};