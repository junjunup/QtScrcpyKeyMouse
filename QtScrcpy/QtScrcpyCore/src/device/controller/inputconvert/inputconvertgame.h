#ifndef INPUTCONVERTGAME_H
#define INPUTCONVERTGAME_H

#include <QHash>
#include <QPointF>
#include <QQueue>
#include <QSet>
#include <QVector>

#include "inputconvertnormal.h"
#include "gamemapcore.h"
#include "keymap.h"

#define MULTI_TOUCH_MAX_NUM 10
class InputConvertGame : public InputConvertNormal
{
    Q_OBJECT
public:
    InputConvertGame(Controller *controller);
    virtual ~InputConvertGame();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual bool isCurrentCustomKeymap();
    virtual void releaseInput();
    virtual void publishStatus();

    void loadKeyMap(const QString &json);

protected:
    void updateSize(const QSize &frameSize, const QSize &showSize);
    void sendTouchDownEvent(int id, QPointF pos);
    void sendTouchMoveEvent(int id, QPointF pos);
    void sendTouchUpEvent(int id, QPointF pos);
    void sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action);
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);
    QPointF calcFrameAbsolutePos(QPointF relativePos);
    QPointF calcScreenAbsolutePos(QPointF relativePos);

    // multi touch id
    int attachTouchID(int key);
    void detachTouchID(int key);
    int getTouchID(int key);

    // steer wheel
    void processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from);
    void processSteerWheelModifier(const QKeyEvent *from);
    void retargetSteerWheel(const KeyMap::KeyMapNode &node);
    void steerWheelTouchUp();

    void processKeyNode(const KeyMap::KeyMapNode &node, const QKeyEvent *from);

    // scenes
    int findScene(const QString &name) const;
    void switchScene(int index);
    // shootKey: lock the cursor for mouse look, or free it to click the screen
    void toggleShooting();
    void releaseFingers();
    // grab and hide the cursor only while the current scene has mouse look
    void updateCursorState();

    // click
    void processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from);

    // click mutil
    void processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from);

    // drag
    void processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from);

    // android key
    void processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from);

    // mouse
    bool processMouseClick(const QMouseEvent *from);
    bool processMouseMove(const QMouseEvent *from);
    void moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel);
    void mouseMoveStartTouch(const QMouseEvent *from);
    void mouseMoveStopTouch();
    void startMouseMoveTimer();
    void stopMouseMoveTimer();
    void applyViewOps(const std::vector<gamemap::TouchOp> &ops);
    void scheduleViewRecenter();

    bool switchGameMap();
    bool checkCursorPos(const QMouseEvent *from);
    void hideMouseCursor(bool hide);

    void getDelayQueue(const QPointF& start, const QPointF& end,
                       const double& distanceStep, const double& posStepconst,
                       quint32 lowestTimer, quint32 highestTimer,
                       QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer);

protected:
    void timerEvent(QTimerEvent *event);

private slots:
    void onSteerWheelTimer();
    void onSteerWheelReleaseTimer();
    void onDragTimer();

private:
    QSize m_frameSize;
    QSize m_showSize;
    bool m_gameMap = false;
    bool m_needBackMouseMove = false;
    int m_multiTouchID[MULTI_TOUCH_MAX_NUM] = { 0 };
    // last sent position per finger, to skip duplicate MOVE events
    QPoint m_lastTouchPos[MULTI_TOUCH_MAX_NUM];
    // keymap of the current scene, owned by m_scenes
    KeyMap *m_keyMap = nullptr;

    struct Scene
    {
        QString name;
        int hotKey = Qt::Key_unknown;
        KeyMap *map = nullptr;
        // node key -> scene to switch to when that key is released
        QHash<int, int> switchKeys;
    };
    QVector<Scene> m_scenes;
    int m_currentScene = 0;
    // keyboard keys held down while mapping, pressed again after a scene switch
    QSet<int> m_heldKeys;
    int m_shootKey = Qt::Key_unknown;
    bool m_cursorGrabbed = false;
    bool m_cursorHidden = false;

    bool m_processMouseMove = true;

    // steer wheel
    struct
    {
        gamemap::SteerKeys keys;
        bool modifier = false;
        bool touching = false;
        QPointF currentPos;
        QTimer *stepTimer = nullptr;
        QTimer *releaseTimer = nullptr;
        quint32 stepIntervalMs = 0;
        QQueue<QPointF> queuePos;
    } m_ctrlSteerWheel;

    // mouse move
    struct
    {
        gamemap::ViewTracker tracker;
        QPointF lastPos = { 0.0, 0.0 };
        int timer = 0;
        bool smallEyes = false;
        // bumped on every release so a pending re-center from an older drag is ignored
        quint32 recenterSerial = 0;
    } m_ctrlMouseMove;

    // for drag delay
    struct {
        QPointF currentPos;
        QTimer* timer = nullptr;
        QQueue<QPointF> queuePos;
        QQueue<quint32> queueTimer;
        int pressKey = 0;
    } m_dragDelayData;
};

#endif // INPUTCONVERTGAME_H
