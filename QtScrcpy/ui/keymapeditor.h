#ifndef KEYMAPEDITOR_H
#define KEYMAPEDITOR_H

#include <QCoreApplication>
#include <QDialog>
#include <QImage>
#include <QJsonObject>
#include <QLineF>
#include <QPushButton>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QTimer;

// A draggable point of a keymap item, in screen ratio coordinates (0~1)
struct KeyMapHandle
{
    int item;
    int part;
    QPointF pos;
    QString label;
    // drawn as a small square instead of a labelled marker
    bool compact;
};

// Decoration of a keymap item: a line (drag path, steer spokes) or a dashed area
struct KeyMapShape
{
    int item;
    bool isRect;
    QLineF line;
    QRectF rect;
};

// The keymap json being edited. Fields the editor does not know about are kept as they are.
class KeyMapModel
{
    Q_DECLARE_TR_FUNCTIONS(KeyMapModel)

public:
    enum Part
    {
        PartPos,
        PartDragStart,
        PartDragEnd,
        PartSteerCenter,
        PartSteerLeft,
        PartSteerRight,
        PartSteerUp,
        PartSteerDown,
        PartMouseStart,
        PartBoundsTopLeft,
        PartBoundsBottomRight,
        PartSmallEyes,
        // clickNodes[i] of KMT_CLICK_MULTI is PartMultiFirst + i
        PartMultiFirst = 100
    };
    // item ids: mouseMoveMap, keyMapNodes[i] is i
    enum
    {
        MouseItem = -1,
        NoItem = -2
    };
    // mouse look of the current scene; only later scenes can inherit the first one's
    enum MouseMode
    {
        MouseOwn,
        MouseInherit,
        MouseOff
    };

    KeyMapModel();

    bool load(const QByteArray &json, QString *error);
    QByteArray toJson() const;
    void reset();

    int nodeCount() const;
    bool hasMouseMove() const;
    bool hasItem(int item) const;
    QJsonObject item(int item) const;
    void setItem(int item, const QJsonObject &obj);
    QString itemType(int item) const;
    QString itemTitle(int item) const;

    QString switchKey() const;
    void setSwitchKey(const QString &key);

    // returns the id of the new item
    int addItem(const QString &type, const QPointF &pos);
    void removeItem(int item);
    void addMultiClickPoint(int item);
    void removeMultiClickPoint(int item);

    QVector<KeyMapHandle> handles(int item) const;
    QVector<KeyMapHandle> allHandles() const;
    QVector<KeyMapShape> shapes(int item) const;
    QVector<int> items() const;
    QPointF handlePos(int item, int part) const;
    void moveHandle(int item, int part, const QPointF &pos);
    int primaryPart(int item) const;

    // problems of every scene, plus scene names, hot keys and switches
    QStringList problems() const;

    // scenes: 0 is the root of the file, the others come from "scenes";
    // items, handles and positions always refer to the current scene
    int sceneCount() const;
    int currentScene() const;
    void setCurrentScene(int scene);
    QString sceneName(int scene) const;
    // the name shown to the user, "Default" for an unnamed first scene
    QString sceneTitle(int scene) const;
    QString sceneHotKey(int scene) const;
    int addScene(const QString &name);
    void removeScene(int scene);
    void renameScene(int scene, const QString &name);
    void setSceneHotKey(int scene, const QString &key);
    MouseMode mouseMode() const;
    void setMouseMode(MouseMode mode);

    static bool isValidKey(const QString &name);
    static bool isKeyboardKey(const QString &name);
    static QString shortKeyLabel(const QString &name);
    static QString typeName(const QString &type);
    static QPointF readPos(const QJsonObject &obj, const QString &name);
    static void writePos(QJsonObject &obj, const QString &name, const QPointF &pos);
    static QRectF mouseBounds(const QJsonObject &mouse);

private:
    // the scene object with the current scene's edits written in
    QJsonObject sceneObject(int scene) const;
    void storeCurrentScene();
    void loadScene(int scene);
    QStringList currentSceneProblems() const;

    // root fields other than the first scene's
    QJsonObject m_root;
    QVector<QJsonObject> m_scenes;
    int m_scene = 0;
    // working copy of the current scene
    QVector<QJsonObject> m_nodes;
    QJsonObject m_mouse;
    bool m_hasMouse = false;
    MouseMode m_mouseMode = MouseOff;
};

// Records the next key or mouse button pressed on it, as a keymap key name ("Key_A", "LeftButton")
class KeyCaptureButton : public QPushButton
{
    Q_OBJECT

public:
    explicit KeyCaptureButton(QWidget *parent = nullptr);

    void setKeyName(const QString &name);
    QString keyName() const;
    void startCapture();

signals:
    void keyCaptured(const QString &name);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void finishCapture(const QString &name);
    void updateText();

    QString m_keyName;
    bool m_capturing = false;
};

// Draws the phone screenshot with the keymap items on top and lets the user drag them
class KeyMapCanvas : public QWidget
{
    Q_OBJECT

public:
    KeyMapCanvas(const KeyMapModel *model, QWidget *parent = nullptr);

    void setBackground(const QImage &image);
    const QImage &background() const;
    void setSelection(int item, int part);
    QSize screenSize() const;

signals:
    void handlePressed(int item, int part);
    void handleDragged(int item, int part, const QPointF &pos);
    void emptyPressed();
    void addRequested(const QString &type, const QPointF &pos);
    void deleteRequested();
    void nudgeRequested(const QPointF &delta);
    void hovered(const QPointF &pos, bool inside);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    QRectF screenRect() const;
    QPointF toScreen(const QPointF &ratio) const;
    QPointF toRatio(const QPointF &widgetPos) const;
    bool hitTest(const QPointF &widgetPos, int *item, int *part) const;
    QRectF handleRect(const KeyMapHandle &handle) const;

    const KeyMapModel *m_model;
    QImage m_background;
    int m_selectedItem = KeyMapModel::NoItem;
    int m_selectedPart = -1;
    bool m_dragging = false;
};

class KeyMapEditor : public QDialog
{
    Q_OBJECT

public:
    // keymapDir: the user keymap directory (saved files go there); defaultDir: the bundled one
    KeyMapEditor(const QString &keymapDir, const QString &defaultDir, QWidget *parent = nullptr);

    void setDeviceSerial(const QString &serial);
    // opens the file, asking to save unsaved changes first
    void openKeymap(const QString &fileName);

signals:
    // the file was written to the user keymap directory
    void keymapSaved(const QString &fileName);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFileBoxActivated(int index);
    void onNewFile();
    void onSaveAs();
    void onSave();
    void onCapture();
    void onCaptureFinished();
    void onOpenImage();
    void onListRowChanged(int row);
    void onHandlePressed(int item, int part);
    void onHandleDragged(int item, int part, const QPointF &pos);
    void onEmptyPressed();
    void onAddRequested(const QString &type, const QPointF &pos);
    void onDeleteSelected();
    void onNudge(const QPointF &delta);
    void onHovered(const QPointF &pos, bool inside);
    void onApplyJson();
    void onUndo();
    void onRedo();
    void onSceneActivated(int index);
    void onAddScene();
    void onRenameScene();
    void onRemoveScene();
    // background of the current scene (a capture or an opened image)
    void setBackground(const QImage &image);

private:
    void buildUi();
    void refreshFileBox(const QString &select);
    bool loadFile(const QString &fileName);
    bool writeFile(const QString &fileName);
    bool saveCurrent();
    QString askFileName(const QString &title, const QString &suggestion);
    bool maybeSave();
    void loadBackground(const QString &fileName);
    // screenshot of a scene: <name>.png for the first, <name>@<n>.png for scene n
    QString imagePathFor(const QString &fileName, int scene) const;
    QImage currentBackground() const;
    void showBackground();
    QString resolveAdbPath() const;

    void select(int item, int part);
    void refreshList();
    void refreshProperties();
    void refreshProblems();
    void refreshSceneBar();
    // redraw everything from the model; modelChanged also marks the file dirty
    void refreshAll();
    void modelChanged();
    void pushUndo();
    void setDirty(bool dirty);
    void updateTitle();
    void updateItem(const QJsonObject &obj);
    void setKeyField(const QString &field, const QString &key);
    QPointF newItemPos() const;

    QString m_keymapDir;
    QString m_defaultDir;
    QString m_serial;
    QString m_fileName;
    bool m_dirty = false;
    bool m_backgroundChanged = false;
    bool m_updating = false;

    KeyMapModel m_model;
    int m_selectedItem = KeyMapModel::NoItem;
    int m_selectedPart = -1;
    struct Snapshot
    {
        QByteArray json;
        int scene = 0;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &state);
    QVector<Snapshot> m_undo;
    QVector<Snapshot> m_redo;
    // state before a handle drag, pushed to m_undo once the drag really moves something
    Snapshot m_pendingUndo;
    // per scene; a null image shows the first scene's
    QVector<QImage> m_backgrounds;

    QProcess *m_captureProcess = nullptr;
    QTimer *m_captureTimeout = nullptr;

    QComboBox *m_fileBox = nullptr;
    QPushButton *m_captureBtn = nullptr;
    KeyMapCanvas *m_canvas = nullptr;
    QLabel *m_statusLabel = nullptr;
    KeyCaptureButton *m_switchKeyBtn = nullptr;
    QComboBox *m_sceneBox = nullptr;
    QPushButton *m_removeSceneBtn = nullptr;
    KeyCaptureButton *m_sceneHotKeyBtn = nullptr;
    QWidget *m_sceneMouseRow = nullptr;
    QComboBox *m_sceneMouseBox = nullptr;
    QListWidget *m_itemList = nullptr;
    QLabel *m_problemLabel = nullptr;

    // property panel sections, shown depending on the selected item type
    QWidget *m_propPanel = nullptr;
    QLabel *m_typeLabel = nullptr;
    QLineEdit *m_commentEdit = nullptr;
    QWidget *m_keySection = nullptr;
    KeyCaptureButton *m_keyBtn = nullptr;
    QWidget *m_switchSceneRow = nullptr;
    QComboBox *m_switchSceneBox = nullptr;
    QWidget *m_switchMapSection = nullptr;
    QCheckBox *m_switchMapCheck = nullptr;
    QWidget *m_steerSection = nullptr;
    KeyCaptureButton *m_upKeyBtn = nullptr;
    KeyCaptureButton *m_downKeyBtn = nullptr;
    KeyCaptureButton *m_leftKeyBtn = nullptr;
    KeyCaptureButton *m_rightKeyBtn = nullptr;
    KeyCaptureButton *m_modifierKeyBtn = nullptr;
    QDoubleSpinBox *m_modifierScaleSpin = nullptr;
    QWidget *m_mouseSection = nullptr;
    QDoubleSpinBox *m_speedXSpin = nullptr;
    QDoubleSpinBox *m_speedYSpin = nullptr;
    KeyCaptureButton *m_smallEyesKeyBtn = nullptr;
    QPushButton *m_smallEyesBtn = nullptr;
    QWidget *m_multiSection = nullptr;
    QWidget *m_posSection = nullptr;
    QLabel *m_posLabel = nullptr;
    QDoubleSpinBox *m_posXSpin = nullptr;
    QDoubleSpinBox *m_posYSpin = nullptr;
    QPlainTextEdit *m_jsonEdit = nullptr;
};

#endif // KEYMAPEDITOR_H
