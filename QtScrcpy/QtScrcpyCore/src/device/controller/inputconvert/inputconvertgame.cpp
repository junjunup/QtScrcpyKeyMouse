#include <QDebug>
#include <QCursor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QGuiApplication>
#include <QTimer>
#include <QTime>
#include <QRandomGenerator>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "inputconvertgame.h"

#define CURSOR_POS_CHECK 50
// pseudo keys that own the steer wheel and view fingers in m_multiTouchID
#define STEER_WHEEL_TOUCH_KEY Qt::ExtraButton23
#define MOUSE_MOVE_TOUCH_KEY Qt::ExtraButton24

static QPointF toQPointF(gamemap::Vec2 v) { return QPointF(v.x, v.y); }
static gamemap::Vec2 toVec2(const QPointF &p) { return { p.x(), p.y() }; }

InputConvertGame::InputConvertGame(Controller *controller) : InputConvertNormal(controller) {
    m_ctrlSteerWheel.stepTimer = new QTimer(this);
    m_ctrlSteerWheel.stepTimer->setSingleShot(true);
    connect(m_ctrlSteerWheel.stepTimer, &QTimer::timeout, this, &InputConvertGame::onSteerWheelTimer);

    m_ctrlSteerWheel.releaseTimer = new QTimer(this);
    m_ctrlSteerWheel.releaseTimer->setSingleShot(true);
    connect(m_ctrlSteerWheel.releaseTimer, &QTimer::timeout, this, &InputConvertGame::onSteerWheelReleaseTimer);
}

InputConvertGame::~InputConvertGame()
{
    // a new keymap replaces this one while mapping may be on: leave no finger
    // on the screen and give the cursor back
    if (m_gameMap) {
        releaseFingers();
    }
    m_gameMap = false;
    updateCursorState();
}

void InputConvertGame::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    // 处理开关按键
    if (m_keyMap->isSwitchOnKeyboard() == false && m_keyMap->getSwitchKey() == static_cast<int>(from->button())) {
        if (from->type() != QEvent::MouseButtonPress) {
            return;
        }
        switchGameMap();
        return;
    }

    if (!m_needBackMouseMove && m_gameMap) {
        updateSize(frameSize, showSize);
        // mouse move
        if (m_keyMap->isValidMouseMoveMap()) {
            if (processMouseMove(from)) {
                return;
            }
        }
        // mouse click
        if (processMouseClick(from)) {
            return;
        }
    }
    InputConvertNormal::mouseEvent(from, frameSize, showSize);
}

void InputConvertGame::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_gameMap) {
        updateSize(frameSize, showSize);
    } else {
        InputConvertNormal::wheelEvent(from, frameSize, showSize);
    }
}

void InputConvertGame::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    // 处理开关按键
    if (m_keyMap->isSwitchOnKeyboard() && m_keyMap->getSwitchKey() == from->key()) {
        if (QEvent::KeyPress != from->type()) {
            return;
        }
        switchGameMap();
        return;
    }

    if (m_gameMap && m_shootKey != Qt::Key_unknown && from->key() == m_shootKey) {
        if (QEvent::KeyPress == from->type() && !from->isAutoRepeat()) {
            toggleShooting();
        }
        return;
    }

    const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeKey(from->key());
    // 处理特殊按键：可以释放出鼠标的按键
    if (m_needBackMouseMove && KeyMap::KMT_CLICK == node.type && node.data.click.switchMap) {
        updateSize(frameSize, showSize);
        // Qt::Key_Tab Qt::Key_M for PUBG mobile
        processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
        return;
    }

    if (m_gameMap) {
        updateSize(frameSize, showSize);
        if (!from || from->isAutoRepeat()) {
            return;
        }
        const bool press = QEvent::KeyPress == from->type();
        if (press) {
            m_heldKeys.insert(from->key());
        } else {
            m_heldKeys.remove(from->key());
        }

        // scene hot key: switch to that scene, or back to the first one
        for (int i = 0; i < m_scenes.size(); ++i) {
            if (m_scenes[i].hotKey != Qt::Key_unknown && m_scenes[i].hotKey == from->key()) {
                if (press) {
                    switchScene(i == m_currentScene ? 0 : i);
                }
                return;
            }
        }

        // walk/sprint modifier of the steer wheel
        if (m_keyMap->isValidSteerWheelMap() && from->key() != Qt::Key_unknown
            && from->key() == m_keyMap->getSteerWheelMap().data.steerWheel.modifierKey) {
            processSteerWheelModifier(from);
            return;
        }

        // small eyes
        if (m_keyMap->isValidMouseMoveMap() && from->key() == m_keyMap->getMouseMoveMap().data.mouseMove.smallEyes.key) {
            m_ctrlMouseMove.smallEyes = (QEvent::KeyPress == from->type());

            if (QEvent::KeyPress == from->type()) {
                m_processMouseMove = false;
                int delay = 30;
                QTimer::singleShot(delay, this, [this]() { mouseMoveStopTouch(); });
                QTimer::singleShot(delay * 2, this, [this]() {
                    mouseMoveStartTouch(nullptr);
                    m_processMouseMove = true;
                });

                stopMouseMoveTimer();
            } else {
                mouseMoveStopTouch();
                mouseMoveStartTouch(nullptr);
            }
            return;
        }

        processKeyNode(node, from);

        // e.g. "get in the car": the tap is done, now use the driving keys
        if (!press) {
            const int target = m_scenes.value(m_currentScene).switchKeys.value(from->key(), -1);
            if (target >= 0) {
                switchScene(target);
            }
        }
    } else {
        InputConvertNormal::keyEvent(from, frameSize, showSize);
    }
}

void InputConvertGame::processKeyNode(const KeyMap::KeyMapNode &node, const QKeyEvent *from)
{
    switch (node.type) {
    // 处理方向盘
    case KeyMap::KMT_STEER_WHEEL:
        processSteerWheel(node, from);
        break;
    // 处理普通按键
    case KeyMap::KMT_CLICK:
        processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
        processAndroidKey(node.data.click.keyNode.androidKey, from);
        break;
    case KeyMap::KMT_CLICK_TWICE:
        processKeyClick(node.data.clickTwice.keyNode.pos, true, false, from);
        processAndroidKey(node.data.clickTwice.keyNode.androidKey, from);
        break;
    case KeyMap::KMT_CLICK_MULTI:
        processKeyClickMulti(node.data.clickMulti.keyNode.delayClickNodes, node.data.clickMulti.keyNode.delayClickNodesCount, from);
        break;
    case KeyMap::KMT_DRAG:
        processKeyDrag(node.data.drag.keyNode.pos, node.data.drag.keyNode.extendPos,
                       node.data.drag.startDelay, node.data.drag.dragSpeed, from);
        break;
    case KeyMap::KMT_ANDROID_KEY:
        processAndroidKey(node.data.androidKey.keyNode.androidKey, from);
        break;
    default:
        break;
    }
}

bool InputConvertGame::isCurrentCustomKeymap()
{
    return m_gameMap;
}

void InputConvertGame::loadKeyMap(const QString &json)
{
    for (const Scene &scene : m_scenes) {
        delete scene.map;
    }
    m_scenes.clear();
    m_currentScene = 0;

    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    const QMetaEnum metaKey = QMetaEnum::fromType<Qt::Key>();
    auto keyValue = [&metaKey](const QJsonValue &value) {
        bool ok = false;
        const int key = metaKey.keyToValue(value.toString().toLatin1().constData(), &ok);
        return ok ? key : static_cast<int>(Qt::Key_unknown);
    };

    // the root is the first scene; "scenes" adds more, each a keymap of its own
    // that shares switchKey and, unless it sets its own, the root mouseMoveMap
    QVector<QJsonObject> sceneObjects;
    sceneObjects.append(root);
    const QJsonArray extra = root.value("scenes").toArray();
    for (int i = 0; i < extra.size(); ++i) {
        QJsonObject scene = extra.at(i).toObject();
        scene.insert("switchKey", root.value("switchKey"));
        if (!scene.contains("mouseMoveMap")) {
            if (root.value("mouseMoveMap").isObject()) {
                scene.insert("mouseMoveMap", root.value("mouseMoveMap"));
            }
        } else if (!scene.value("mouseMoveMap").isObject()) {
            // "mouseMoveMap": false turns mouse look off in this scene
            scene.remove("mouseMoveMap");
        }
        sceneObjects.append(scene);
    }

    for (int i = 0; i < sceneObjects.size(); ++i) {
        const QJsonObject &obj = sceneObjects.at(i);
        Scene scene;
        scene.name = obj.value("name").toString();
        if (scene.name.isEmpty()) {
            scene.name = i == 0 ? QString("default") : QString("scene %1").arg(i + 1);
        }
        scene.hotKey = keyValue(obj.value("hotKey"));
        scene.map = new KeyMap(this);
        // the root is passed as is so a broken file still reports its own errors
        scene.map->loadKeyMap(i == 0 ? json : QString::fromUtf8(QJsonDocument(obj).toJson()));
        m_scenes.append(scene);
    }

    for (int i = 0; i < sceneObjects.size(); ++i) {
        const QJsonArray nodes = sceneObjects.at(i).value("keyMapNodes").toArray();
        for (int j = 0; j < nodes.size(); ++j) {
            const QJsonObject node = nodes.at(j).toObject();
            if (!node.contains("switchScene")) {
                continue;
            }
            const int target = findScene(node.value("switchScene").toString());
            const int key = keyValue(node.value("key"));
            if (target < 0 || key == Qt::Key_unknown) {
                qWarning() << "json error: switchScene" << node.value("switchScene").toString() << "needs an existing scene and a keyboard key";
                continue;
            }
            m_scenes[i].switchKeys.insert(key, target);
        }
    }

    m_keyMap = m_scenes.first().map;
    m_shootKey = keyValue(root.value("shootKey"));
    if (m_scenes.size() > 1) {
        qInfo() << "keymap scenes:" << m_scenes.size();
    }
}

int InputConvertGame::findScene(const QString &name) const
{
    for (int i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i].name == name) {
            return i;
        }
    }
    return -1;
}

void InputConvertGame::switchScene(int index)
{
    if (index < 0 || index >= m_scenes.size() || index == m_currentScene) {
        return;
    }
    releaseFingers();
    m_currentScene = index;
    m_keyMap = m_scenes[index].map;
    m_needBackMouseMove = false;
    updateCursorState();
    qInfo() << "keymap scene:" << m_scenes[index].name;
    publishStatus();

    // keys still held (W while getting in the car) act in the new scene right away
    const QList<int> held = m_heldKeys.values();
    for (int key : held) {
        bool hotKey = key == m_keyMap->getSwitchKey() || key == m_shootKey;
        for (const Scene &scene : m_scenes) {
            hotKey = hotKey || scene.hotKey == key;
        }
        if (!hotKey) {
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
            keyEvent(&press, m_frameSize, m_showSize);
        }
    }
}

void InputConvertGame::toggleShooting()
{
    if (!m_keyMap->isValidMouseMoveMap()) {
        return;
    }
    m_needBackMouseMove = !m_needBackMouseMove;
    if (m_needBackMouseMove) {
        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }
    updateCursorState();
    publishStatus();
}

void InputConvertGame::publishStatus()
{
    qsc::KeyMapStatus status;
    status.customKeymap = true;
    status.mapping = m_gameMap;
    status.shooting = m_cursorHidden;
    if (m_scenes.size() > 1) {
        status.scene = m_scenes.value(m_currentScene).name;
    }
    if (!m_keyMap) {
        emit keyMapStatusChanged(status);
        return;
    }

    const QMetaEnum keys = QMetaEnum::fromType<Qt::Key>();
    const QMetaEnum buttons = QMetaEnum::fromType<Qt::MouseButtons>();
    auto add = [&](const QPointF &pos, const KeyMap::KeyNode &keyNode) {
        const char *name = keyNode.type == KeyMap::AT_MOUSE ? buttons.valueToKey(keyNode.key) : keys.valueToKey(keyNode.key);
        if (keyNode.type != KeyMap::AT_INVALID && name) {
            status.hints.append({ pos, QString::fromLatin1(name) });
        }
    };
    for (const KeyMap::KeyMapNode &node : m_keyMap->getKeyMapNodes()) {
        switch (node.type) {
        case KeyMap::KMT_CLICK:
        case KeyMap::KMT_CLICK_TWICE:
            add(node.data.click.keyNode.pos, node.data.click.keyNode);
            break;
        case KeyMap::KMT_CLICK_MULTI:
            if (node.data.clickMulti.keyNode.delayClickNodesCount > 0) {
                add(node.data.clickMulti.keyNode.delayClickNodes[0].pos, node.data.clickMulti.keyNode);
            }
            break;
        case KeyMap::KMT_DRAG:
            add(node.data.drag.keyNode.pos, node.data.drag.keyNode);
            break;
        case KeyMap::KMT_STEER_WHEEL: {
            const auto &wheel = node.data.steerWheel;
            add(wheel.centerPos - QPointF(0, wheel.up.extendOffset), wheel.up);
            add(wheel.centerPos + QPointF(0, wheel.down.extendOffset), wheel.down);
            add(wheel.centerPos - QPointF(wheel.left.extendOffset, 0), wheel.left);
            add(wheel.centerPos + QPointF(wheel.right.extendOffset, 0), wheel.right);
        } break;
        case KeyMap::KMT_MOUSE_MOVE:
            add(node.data.mouseMove.smallEyes.pos, node.data.mouseMove.smallEyes);
            break;
        default:
            break;
        }
    }
    emit keyMapStatusChanged(status);
}

void InputConvertGame::releaseInput()
{
    releaseFingers();
    m_heldKeys.clear();
}

void InputConvertGame::releaseFingers()
{
    m_ctrlSteerWheel.keys = {};
    m_ctrlSteerWheel.modifier = false;
    steerWheelTouchUp();

    stopMouseMoveTimer();
    mouseMoveStopTouch();
    m_ctrlMouseMove.smallEyes = false;
    m_processMouseMove = true;

    if (m_dragDelayData.timer) {
        m_dragDelayData.timer->stop();
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;
        m_dragDelayData.queuePos.clear();
        m_dragDelayData.queueTimer.clear();
        m_dragDelayData.pressKey = 0;
    }

    // every other finger (held taps, swipes, mouse buttons) goes up where it is
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; ++i) {
        if (0 == m_multiTouchID[i] || m_frameSize.isEmpty()) {
            m_multiTouchID[i] = 0;
            continue;
        }
        const QPointF pos(static_cast<qreal>(m_lastTouchPos[i].x()) / m_frameSize.width(),
                          static_cast<qreal>(m_lastTouchPos[i].y()) / m_frameSize.height());
        sendTouchUpEvent(i, pos);
        m_multiTouchID[i] = 0;
    }
}

void InputConvertGame::updateSize(const QSize &frameSize, const QSize &showSize)
{
    if (showSize != m_showSize) {
        if (m_cursorGrabbed) {
#ifdef QT_NO_DEBUG
            // show size change, resize grab cursor
            emit grabCursor(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;
}

void InputConvertGame::sendTouchDownEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_DOWN);
}

void InputConvertGame::sendTouchMoveEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_MOVE);
}

void InputConvertGame::sendTouchUpEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_UP);
}

void InputConvertGame::sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action)
{
    if (0 > id || MULTI_TOUCH_MAX_NUM - 1 < id) {
        Q_ASSERT(0);
        return;
    }
    //qDebug() << "id:" << id << " pos:" << pos << " action" << action;
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (!controlMsg) {
        return;
    }

    QPoint absolutePos = calcFrameAbsolutePos(pos).toPoint();
    if (AMOTION_EVENT_ACTION_MOVE == action && m_lastTouchPos[id] == absolutePos) {
        delete controlMsg;
        return;
    }
    m_lastTouchPos[id] = absolutePos;

    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(id),
        action,
        static_cast<AndroidMotioneventButtons>(0),
        static_cast<AndroidMotioneventButtons>(0),
        QRect(absolutePos, m_frameSize),
        AMOTION_EVENT_ACTION_DOWN == action ? 1.0f : 0.0f);
    sendControlMsg(controlMsg);
}

void InputConvertGame::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode) {
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlMsg) {
        return;
    }

    controlMsg->setInjectKeycodeMsgData(action, keyCode, 0, AMETA_NONE);
    sendControlMsg(controlMsg);
}

QPointF InputConvertGame::calcFrameAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_frameSize.width() * relativePos.x());
    absolutePos.setY(m_frameSize.height() * relativePos.y());
    return absolutePos;
}

QPointF InputConvertGame::calcScreenAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_showSize.width() * relativePos.x());
    absolutePos.setY(m_showSize.height() * relativePos.y());
    return absolutePos;
}

int InputConvertGame::attachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (0 == m_multiTouchID[i]) {
            m_multiTouchID[i] = key;
            return i;
        }
    }
    return -1;
}

void InputConvertGame::detachTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            m_multiTouchID[i] = 0;
            return;
        }
    }
}

int InputConvertGame::getTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            return i;
        }
    }
    return -1;
}

// -------- steer wheel event --------

void InputConvertGame::getDelayQueue(const QPointF& start, const QPointF& end,
                                     const double& distanceStep, const double& posStepconst,
                                     quint32 lowestTimer, quint32 highestTimer,
                                     QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer) {
    double x1 = start.x();
    double y1 = start.y();
    double x2 = end.x();
    double y2 = end.y();

    double dx=x2-x1;
    double dy=y2-y1;
    double e=(fabs(dx)>fabs(dy))?fabs(dx):fabs(dy);
    e /= distanceStep;
    dx/=e;
    dy/=e;

    QQueue<QPointF> queue;
    QQueue<quint32> queue2;
    for(int i=1;i<=e;i++) {
        QPointF pos(x1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst), y1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst));
        queue.enqueue(pos);
        queue2.enqueue(QRandomGenerator::global()->bounded(lowestTimer, highestTimer));
        x1+=dx;
        y1+=dy;
    }

    queuePos = queue;
    queueTimer = queue2;
}

void InputConvertGame::onSteerWheelTimer() {
    if (m_ctrlSteerWheel.queuePos.empty() || !m_ctrlSteerWheel.touching) {
        return;
    }
    m_ctrlSteerWheel.currentPos = m_ctrlSteerWheel.queuePos.dequeue();
    sendTouchMoveEvent(getTouchID(STEER_WHEEL_TOUCH_KEY), m_ctrlSteerWheel.currentPos);

    if (!m_ctrlSteerWheel.queuePos.empty()) {
        m_ctrlSteerWheel.stepTimer->start(m_ctrlSteerWheel.stepIntervalMs);
    }
}

void InputConvertGame::onSteerWheelReleaseTimer()
{
    if (m_ctrlSteerWheel.keys.pressedCount() == 0) {
        steerWheelTouchUp();
    }
}

void InputConvertGame::processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from)
{
    int key = from->key();
    bool flag = from->type() == QEvent::KeyPress;
    // identify keys
    if (key == node.data.steerWheel.up.key) {
        m_ctrlSteerWheel.keys.up = flag;
    } else if (key == node.data.steerWheel.right.key) {
        m_ctrlSteerWheel.keys.right = flag;
    } else if (key == node.data.steerWheel.down.key) {
        m_ctrlSteerWheel.keys.down = flag;
    } else { // left
        m_ctrlSteerWheel.keys.left = flag;
    }
    retargetSteerWheel(node);
}

void InputConvertGame::processSteerWheelModifier(const QKeyEvent *from)
{
    m_ctrlSteerWheel.modifier = from->type() == QEvent::KeyPress;
    if (m_ctrlSteerWheel.keys.pressedCount() > 0) {
        retargetSteerWheel(m_keyMap->getSteerWheelMap());
    }
}

void InputConvertGame::retargetSteerWheel(const KeyMap::KeyMapNode &node)
{
    const auto &wheel = node.data.steerWheel;
    m_ctrlSteerWheel.stepTimer->stop();
    m_ctrlSteerWheel.queuePos.clear();

    // last key released: lift now, or after releaseDelayMs so a quick
    // direction change keeps the same finger on the joystick
    if (m_ctrlSteerWheel.keys.pressedCount() == 0) {
        if (wheel.releaseDelayMs > 0 && m_ctrlSteerWheel.touching) {
            m_ctrlSteerWheel.releaseTimer->start(wheel.releaseDelayMs);
        } else {
            steerWheelTouchUp();
        }
        return;
    }
    m_ctrlSteerWheel.releaseTimer->stop();

    // first press, touch down at the center
    if (!m_ctrlSteerWheel.touching) {
        int id = attachTouchID(STEER_WHEEL_TOUCH_KEY);
        if (id < 0) {
            return;
        }
        sendTouchDownEvent(id, wheel.centerPos);
        m_ctrlSteerWheel.currentPos = wheel.centerPos;
        m_ctrlSteerWheel.touching = true;
    }

    gamemap::SteerOffsets offsets { wheel.up.extendOffset, wheel.down.extendOffset, wheel.left.extendOffset, wheel.right.extendOffset };
    double scale = m_ctrlSteerWheel.modifier ? wheel.modifierScale : 1.0;
    gamemap::Vec2 target = gamemap::steerTarget(toVec2(wheel.centerPos), offsets, m_ctrlSteerWheel.keys, scale);

    for (const auto &p : gamemap::interpolate(toVec2(m_ctrlSteerWheel.currentPos), target, wheel.stepLength)) {
        m_ctrlSteerWheel.queuePos.enqueue(toQPointF(p));
    }
    m_ctrlSteerWheel.stepIntervalMs = wheel.stepIntervalMs;
    // first step right away, the rest on the timer
    onSteerWheelTimer();
}

void InputConvertGame::steerWheelTouchUp()
{
    m_ctrlSteerWheel.stepTimer->stop();
    m_ctrlSteerWheel.releaseTimer->stop();
    m_ctrlSteerWheel.queuePos.clear();
    if (m_ctrlSteerWheel.touching) {
        sendTouchUpEvent(getTouchID(STEER_WHEEL_TOUCH_KEY), m_ctrlSteerWheel.currentPos);
        detachTouchID(STEER_WHEEL_TOUCH_KEY);
        m_ctrlSteerWheel.touching = false;
    }
}

// -------- key event --------

void InputConvertGame::processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from)
{
    if (switchMap && QEvent::KeyRelease == from->type()) {
        m_needBackMouseMove = !m_needBackMouseMove;
        updateCursorState();
        publishStatus();
    }

    if (QEvent::KeyPress == from->type()) {
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, clickPos);
        if (clickTwice) {
            sendTouchUpEvent(getTouchID(from->key()), clickPos);
            detachTouchID(from->key());
        }
    } else if (QEvent::KeyRelease == from->type()) {
        if (clickTwice) {
            int id = attachTouchID(from->key());
            sendTouchDownEvent(id, clickPos);
        }
        // the finger was already lifted (focus lost, scene switched)
        if (getTouchID(from->key()) < 0) {
            return;
        }
        sendTouchUpEvent(getTouchID(from->key()), clickPos);
        detachTouchID(from->key());
    }
}

void InputConvertGame::processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from)
{
    if (QEvent::KeyPress != from->type()) {
        return;
    }

    int key = from->key();
    int delay = 0;
    QPointF clickPos;

    for (int i = 0; i < count; i++) {
        delay += nodes[i].delay;
        clickPos = nodes[i].pos;
        QTimer::singleShot(delay, this, [this, key, clickPos]() {
            int id = attachTouchID(key);
            sendTouchDownEvent(id, clickPos);
        });

        // Don't up it too fast
        delay += 20;
        QTimer::singleShot(delay, this, [this, key, clickPos]() {
            int id = getTouchID(key);
            sendTouchUpEvent(id, clickPos);
            detachTouchID(key);
        });
    }
}

void InputConvertGame::onDragTimer() {
    if(m_dragDelayData.queuePos.empty()) {
        return;
    }
    int id = getTouchID(m_dragDelayData.pressKey);
    m_dragDelayData.currentPos = m_dragDelayData.queuePos.dequeue();
    sendTouchMoveEvent(id, m_dragDelayData.currentPos);

    if(m_dragDelayData.queuePos.empty()) {
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;

        sendTouchUpEvent(id, m_dragDelayData.currentPos);
        detachTouchID(m_dragDelayData.pressKey);

        m_dragDelayData.currentPos = QPointF();
        m_dragDelayData.pressKey = 0;
        return;
    }

    if(!m_dragDelayData.queuePos.empty()) {
        m_dragDelayData.timer->start(m_dragDelayData.queueTimer.dequeue());
    }
}

void InputConvertGame::processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from)
{
    if (QEvent::KeyPress == from->type()) {
        // stop last
        if (m_dragDelayData.timer && m_dragDelayData.timer->isActive()) {
            m_dragDelayData.timer->stop();
            delete m_dragDelayData.timer;
            m_dragDelayData.timer = nullptr;
            m_dragDelayData.queuePos.clear();
            m_dragDelayData.queueTimer.clear();

            sendTouchUpEvent(getTouchID(m_dragDelayData.pressKey), m_dragDelayData.currentPos);
            detachTouchID(m_dragDelayData.pressKey);

            m_dragDelayData.currentPos = QPointF();
            m_dragDelayData.pressKey = 0;
        }

        // start this
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, startPos);

        m_dragDelayData.timer = new QTimer(this);
        m_dragDelayData.timer->setSingleShot(true);
        connect(m_dragDelayData.timer, &QTimer::timeout, this, &InputConvertGame::onDragTimer);
        m_dragDelayData.pressKey = from->key();
        m_dragDelayData.currentPos = startPos;
        m_dragDelayData.queuePos.clear();
        m_dragDelayData.queueTimer.clear();

        // Clamp dragSpeed to 0-1 range
        const float speed = qBound(0.0f, static_cast<float>(dragSpeed), 1.0f);
        
        // Calculate delays based on dragSpeed
        // dragSpeed = 1 -> minDelay = 1, maxDelay = 2 (fastest)
        // dragSpeed = 0 -> minDelay = 30, maxDelay = 40 (slowest)
        const quint32 minDelay = static_cast<quint32>(1 + (1.0f - speed) * 29);  // 1 to 30
        const quint32 maxDelay = minDelay + static_cast<quint32>((1.0f - speed) * 9) + 1;  // // min + (0 to 9) + 1

        getDelayQueue(startPos, endPos,
                      0.01f, 0.0005f,
                      minDelay,
                      maxDelay,
                      m_dragDelayData.queuePos,
                      m_dragDelayData.queueTimer);

        m_dragDelayData.timer->start(startDelay);
    }
}

void InputConvertGame::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (from->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

// -------- mouse event --------

bool InputConvertGame::processMouseClick(const QMouseEvent *from)
{
    const KeyMap::KeyMapNode &node = m_keyMap->getKeyMapNodeMouse(from->button());
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (QEvent::MouseButtonPress == from->type() || QEvent::MouseButtonDblClick == from->type()) {
        int id = attachTouchID(from->button());
        sendTouchDownEvent(id, node.data.click.keyNode.pos);
        return true;
    }
    if (QEvent::MouseButtonRelease == from->type()) {
        int id = getTouchID(from->button());
        if (id < 0) {
            return true;
        }
        sendTouchUpEvent(id, node.data.click.keyNode.pos);
        detachTouchID(from->button());
        return true;
    }
    return false;
}

bool InputConvertGame::processMouseMove(const QMouseEvent *from)
{
    if (QEvent::MouseMove != from->type()) {
        return false;
    }

    if (checkCursorPos(from)) {
        m_ctrlMouseMove.lastPos = QPointF(0.0, 0.0);
        return true;
    }

    auto lastPos = m_ctrlMouseMove.lastPos;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_ctrlMouseMove.lastPos = from->localPos();
#else
    m_ctrlMouseMove.lastPos = from->position();
#endif

    if (!lastPos.isNull() && m_processMouseMove) {
        QPointF distance_raw{m_ctrlMouseMove.lastPos - lastPos};
        QPointF speedRatio  {m_keyMap->getMouseMoveMap().data.mouseMove.speedRatio};
        gamemap::Vec2 delta { distance_raw.x() / speedRatio.x() / m_showSize.width(),
                              distance_raw.y() / speedRatio.y() / m_showSize.height() };

        mouseMoveStartTouch(from);
        bool wasWaiting = m_ctrlMouseMove.tracker.waitingRecenter();
        applyViewOps(m_ctrlMouseMove.tracker.move(delta));
        if (!wasWaiting && m_ctrlMouseMove.tracker.waitingRecenter()) {
            scheduleViewRecenter();
        }
        startMouseMoveTimer();
    }

    return true;
}

void InputConvertGame::applyViewOps(const std::vector<gamemap::TouchOp> &ops)
{
    for (const auto &op : ops) {
        QPointF pos = toQPointF(op.pos);
        switch (op.type) {
        case gamemap::TouchOp::Down:
            sendTouchDownEvent(attachTouchID(MOUSE_MOVE_TOUCH_KEY), pos);
            break;
        case gamemap::TouchOp::Move:
            sendTouchMoveEvent(getTouchID(MOUSE_MOVE_TOUCH_KEY), pos);
            break;
        case gamemap::TouchOp::Up:
            sendTouchUpEvent(getTouchID(MOUSE_MOVE_TOUCH_KEY), pos);
            detachTouchID(MOUSE_MOVE_TOUCH_KEY);
            break;
        }
    }
}

void InputConvertGame::scheduleViewRecenter()
{
    quint32 serial = m_ctrlMouseMove.recenterSerial;
    QTimer::singleShot(m_keyMap->getMouseMoveMap().data.mouseMove.recenterDelayMs, this, [this, serial]() {
        if (serial != m_ctrlMouseMove.recenterSerial || !m_gameMap) {
            return;
        }
        applyViewOps(m_ctrlMouseMove.tracker.resume());
        if (m_ctrlMouseMove.tracker.waitingRecenter()) {
            scheduleViewRecenter();
        }
    });
}

bool InputConvertGame::checkCursorPos(const QMouseEvent *from)
{
    bool moveCursor = false;
    QPoint pos = from->pos();
    if (pos.x() < CURSOR_POS_CHECK) {
        pos.setX(m_showSize.width() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.x() > m_showSize.width() - CURSOR_POS_CHECK) {
        pos.setX(CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() < CURSOR_POS_CHECK) {
        pos.setY(m_showSize.height() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() > m_showSize.height() - CURSOR_POS_CHECK) {
        pos.setY(CURSOR_POS_CHECK);
        moveCursor = true;
    }

    if (moveCursor) {
        moveCursorTo(from, pos);
    }

    return moveCursor;
}

void InputConvertGame::moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPoint posOffset = from->pos() - localPosPixel;
    QPoint globalPos = from->globalPos();
#else
    QPoint posOffset = from->position().toPoint() - localPosPixel;
    QPoint globalPos = from->globalPosition().toPoint();
#endif
    globalPos -= posOffset;
    //qDebug()<<"move cursor to "<<globalPos<<" offset "<<posOffset;
#ifdef Q_OS_MACOS
    // On macOS, QCursor::setPos() posts a synthetic mouse-moved event (CGEventPost)
    // and does NOT re-associate the hardware mouse with the on-screen cursor, so the
    // warp does not "stick": the next hardware delta snaps the cursor back. That makes
    // mouseMoveMap re-centering a no-op, and the camera can no longer pan past the edge.
    // CGWarpMouseCursorPosition performs a real warp, and
    // CGAssociateMouseAndMouseCursorPosition(true) immediately restores the
    // mouse<->cursor association so subsequent hardware deltas keep flowing. This
    // mirrors the approach already used in util/mousetap/cocoamousetap.mm.
    CGWarpMouseCursorPosition(CGPointMake(globalPos.x(), globalPos.y()));
    CGAssociateMouseAndMouseCursorPosition(true);
#else
    QCursor::setPos(globalPos);
#endif
}

void InputConvertGame::mouseMoveStartTouch(const QMouseEvent *from)
{
    Q_UNUSED(from)
    // a delayed call can arrive after a switch to a scene without mouse look
    if (!m_keyMap->isValidMouseMoveMap()) {
        return;
    }
    const auto &mouseMove = m_keyMap->getMouseMoveMap().data.mouseMove;
    auto &tracker = m_ctrlMouseMove.tracker;
    if (tracker.touching() || tracker.waitingRecenter()) {
        return;
    }
    QPointF startPos = m_ctrlMouseMove.smallEyes ? mouseMove.smallEyes.pos : mouseMove.startPos;
    tracker.setStart(toVec2(startPos));
    tracker.setBounds({ mouseMove.bounds.left(), mouseMove.bounds.top(), mouseMove.bounds.right(), mouseMove.bounds.bottom() });
    applyViewOps(tracker.move({ 0.0, 0.0 }));
}

void InputConvertGame::mouseMoveStopTouch()
{
    ++m_ctrlMouseMove.recenterSerial;
    applyViewOps(m_ctrlMouseMove.tracker.release());
}

void InputConvertGame::startMouseMoveTimer()
{
    stopMouseMoveTimer();
    if (!m_keyMap->isValidMouseMoveMap()) {
        return;
    }
    quint32 autoReleaseMs = m_keyMap->getMouseMoveMap().data.mouseMove.autoReleaseMs;
    if (autoReleaseMs > 0) {
        m_ctrlMouseMove.timer = startTimer(static_cast<int>(autoReleaseMs));
    }
}

void InputConvertGame::stopMouseMoveTimer()
{
    if (0 != m_ctrlMouseMove.timer) {
        killTimer(m_ctrlMouseMove.timer);
        m_ctrlMouseMove.timer = 0;
    }
}

bool InputConvertGame::switchGameMap()
{
    m_gameMap = !m_gameMap;
    qInfo() << QString("current keymap mode: %1").arg(m_gameMap ? "custom" : "normal");

    if (!m_gameMap) {
        // don't leave any finger on the screen
        releaseFingers();
        m_heldKeys.clear();
        m_needBackMouseMove = false;
    }
    updateCursorState();
    publishStatus();
    return m_gameMap;
}

void InputConvertGame::updateCursorState()
{
    const bool grab = m_gameMap && m_keyMap && m_keyMap->isValidMouseMoveMap();
    const bool hide = grab && !m_needBackMouseMove;
    if (grab != m_cursorGrabbed) {
        m_cursorGrabbed = grab;
#ifdef QT_NO_DEBUG
        emit grabCursor(grab);
#endif
    }
    // the override cursor is a stack: push and pop exactly once each
    if (hide != m_cursorHidden) {
        m_cursorHidden = hide;
        hideMouseCursor(hide);
    }
}

void InputConvertGame::hideMouseCursor(bool hide)
{
    if (hide) {
#ifdef QT_NO_DEBUG
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
#else
        QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
#endif
    } else {
        QGuiApplication::restoreOverrideCursor();
    }
}

void InputConvertGame::timerEvent(QTimerEvent *event)
{
    if (m_ctrlMouseMove.timer == event->timerId()) {
        stopMouseMoveTimer();
        // Don't auto-reset view when smallEyes mode is active
        if (!m_ctrlMouseMove.smallEyes) {
            mouseMoveStopTouch();
        }
    }
}
