#include "includes.hpp"
#include <limits>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
// FIX: 2.2081 uses PlayerSelf for many core hooks
#include <Geode/modify/PlayerSelf.hpp> 
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <tulip/TulipHook.hpp>

constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

constexpr InputEvent EMPTY_INPUT = InputEvent {
	.time = 0,
	.inputType = PlayerButton::Jump,
	.inputState = false,
	.isPlayer1 = false,
};
constexpr Step EMPTY_STEP = Step {
	.input = EMPTY_INPUT,
	.deltaFactor = 1.0,
	.endStep = true,
};

std::deque<struct InputEvent> inputQueue;
std::deque<struct InputEvent> inputQueueCopy;
std::deque<struct Step> stepQueue;

std::atomic<bool> softToggle;

InputEvent nextInput = EMPTY_INPUT;

TimestampType lastFrameTime;
TimestampType currentFrameTime;

bool firstFrame = true; 
bool skipUpdate = true; 
bool enableInput = false;
bool linuxNative = false;
bool lateCutoff; 

std::array<std::unordered_set<size_t>, 6> inputBinds;
std::unordered_set<uint16_t> heldInputs;

std::mutex inputQueueLock;
std::mutex keybindsLock;

std::atomic<bool> enableRightClick;
bool threadPriority;

void buildStepQueue(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	nextInput = EMPTY_INPUT;
	stepQueue = {}; 

	if (lateCutoff) { 
		currentFrameTime = getCurrentTimestamp();
		#ifdef GEODE_IS_WINDOWS
		if (linuxNative) {
			linuxCheckInputs();
		}
		#endif

		std::lock_guard lock(inputQueueLock);
		inputQueueCopy = inputQueue;
		inputQueue = {};
	}
	else { 
		#ifdef GEODE_IS_WINDOWS
		if (linuxNative) linuxCheckInputs();
		#endif

		std::lock_guard lock(inputQueueLock);
		while (!inputQueue.empty() && inputQueue.front().time <= currentFrameTime) {
			inputQueueCopy.push_back(inputQueue.front());
			inputQueue.pop_front();
		}
	}

	skipUpdate = false;
	if (firstFrame) {
		skipUpdate = true;
		firstFrame = false;
		lastFrameTime = currentFrameTime;
		if (!lateCutoff) inputQueueCopy = {};
		return;
	}

	TimestampType deltaTime = currentFrameTime - lastFrameTime;
	TimestampType stepDelta = (deltaTime / stepCount) + 1; 

	for (int i = 0; i < stepCount; i++) { 
		double elapsedTime = 0.0;
		while (!inputQueueCopy.empty()) { 
			InputEvent front = inputQueueCopy.front();

			if (front.time - lastFrameTime < stepDelta * (i + 1)) { 
				double inputTime = static_cast<double>((front.time - lastFrameTime) % stepDelta) / stepDelta; 
				stepQueue.emplace_back(Step{ front, std::clamp(inputTime - elapsedTime, SMALLEST_FLOAT, 1.0), false });
				inputQueueCopy.pop_front();
				elapsedTime = inputTime;
			}
			else break; 
		}

		stepQueue.emplace_back(Step{ EMPTY_INPUT, std::max(SMALLEST_FLOAT, 1.0 - elapsedTime), true });
	}

	lastFrameTime = currentFrameTime;
}

Step popStepQueue() {
	if (stepQueue.empty()) return EMPTY_STEP;

	Step front = stepQueue.front();
	double deltaFactor = front.deltaFactor;

	if (nextInput.time != 0) {
		PlayLayer* playLayer = PlayLayer::get();

		enableInput = true;
		// FIX: Updated for 2.2081 - removed (int) cast because 2.2081 uses PlayerButton type directly
		playLayer->handleButton(nextInput.inputState, nextInput.inputType, nextInput.isPlayer1);
		enableInput = false;
	}

	nextInput = front.input;
	stepQueue.pop_front();

	return front;
}

// FIX: Updated PlayerObject reset to handle 2.2081 memory layout safely
void decomp_resetCollisionLog(PlayerObject* p) {
	if (!p) return;
	p->m_collisionLogTop->removeAllObjects();
    p->m_collisionLogBottom->removeAllObjects();
    p->m_collisionLogLeft->removeAllObjects();
    p->m_collisionLogRight->removeAllObjects();
	p->m_lastCollisionLeft = -1;
	p->m_lastCollisionRight = -1;
	p->m_lastCollisionBottom = -1;
	p->m_lastCollisionTop = -1;
}

double averageDelta = 0.0;
bool physicsBypass;
bool legacyBypass;

int calculateStepCount(float delta, float timewarp, bool forceVanilla) {
	if (!physicsBypass || forceVanilla) { 
		return std::round(std::max(1.0, ((delta * 60.0) / std::min(1.0f, timewarp)) * 4.0)); 
	}
	else if (legacyBypass) { 
		return std::round(std::max(4.0, delta * 240.0) / std::min(1.0f, timewarp));
	}
	else { 
		double animationInterval = CCDirector::sharedDirector()->getAnimationInterval();
		averageDelta = (0.05 * delta) + (0.95 * averageDelta); 
		if (averageDelta > animationInterval * 10) averageDelta = animationInterval * 10; 

		bool laggingOneFrame = animationInterval < delta - (1.0 / 240.0); 
		bool laggingManyFrames = averageDelta - animationInterval > 0.0005; 

		if (!laggingOneFrame && !laggingManyFrames) { 
			return std::round(std::ceil((animationInterval * 240.0) - 0.0001) / std::min(1.0f, timewarp));
		}
		else if (!laggingOneFrame) { 
			return std::round(std::ceil(averageDelta * 240.0) / std::min(1.0f, timewarp));
		}
		else { 
			return std::round(std::ceil(delta * 240.0) / std::min(1.0f, timewarp));
		}
	}
}

bool safeMode;

// Note: I left the PlayLayer class open because your snippet cut off there. 
// Make sure the rest of your file is closed with }; properly!
class $modify(PlayLayer) {