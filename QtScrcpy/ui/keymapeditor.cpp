#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "config.h"
#include "keymapeditor.h"

namespace {
const char *const kClick = "KMT_CLICK";
const char *const kClickTwice = "KMT_CLICK_TWICE";
const char *const kClickMulti = "KMT_CLICK_MULTI";
const char *const kDrag = "KMT_DRAG";
const char *const kSteerWheel = "KMT_STEER_WHEEL";
const char *const kAndroidKey = "KMT_ANDROID_KEY";
const char *const kMouseMove = "mouseMoveMap";

const int kMaxUndo = 200;

double round4(double v)
{
    return qRound(v * 10000.0) / 10000.0;
}

QPointF clampPos(const QPointF &pos)
{
    return QPointF(qBound(0.0, pos.x(), 1.0), qBound(0.0, pos.y(), 1.0));
}

bool inScreen(const QPointF &pos)
{
    return pos.x() >= 0.0 && pos.x() <= 1.0 && pos.y() >= 0.0 && pos.y() <= 1.0;
}

QPointF mousePos(const QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

QFont labelFont(const QFont &base)
{
    QFont font = base;
    font.setBold(true);
    return font;
}

QPushButton *makeButton(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    // a QDialog would otherwise click it when Enter is pressed in a field
    button->setAutoDefault(false);
    return button;
}

QDoubleSpinBox *makeSpin(double min, double max, double step, int decimals, QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    // only report finished values, not every typed digit
    spin->setKeyboardTracking(false);
    return spin;
}

QFormLayout *makeForm(QWidget *parent)
{
    auto *form = new QFormLayout(parent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}
} // namespace

//
// KeyMapModel
//

KeyMapModel::KeyMapModel()
{
    reset();
}

void KeyMapModel::reset()
{
    m_root = QJsonObject();
    m_root.insert("switchKey", "Key_QuoteLeft");
    m_nodes.clear();
    m_mouse = QJsonObject();
    m_hasMouse = false;
}

bool KeyMapModel::load(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = tr("%1 (at character %2)").arg(parseError.errorString()).arg(parseError.offset);
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = tr("the file is not a json object");
        }
        return false;
    }

    QJsonObject root = doc.object();
    QVector<QJsonObject> nodes;
    const QJsonArray array = root.value("keyMapNodes").toArray();
    for (int i = 0; i < array.size(); ++i) {
        if (!array.at(i).isObject()) {
            if (error) {
                *error = tr("keyMapNodes must only contain objects");
            }
            return false;
        }
        nodes.append(array.at(i).toObject());
    }

    m_hasMouse = root.value(kMouseMove).isObject();
    m_mouse = root.value(kMouseMove).toObject();
    root.remove("keyMapNodes");
    root.remove(kMouseMove);
    m_root = root;
    m_nodes = nodes;
    return true;
}

QByteArray KeyMapModel::toJson() const
{
    QJsonObject root = m_root;
    if (m_hasMouse) {
        root.insert(kMouseMove, m_mouse);
    }
    QJsonArray array;
    for (const QJsonObject &node : m_nodes) {
        array.append(node);
    }
    root.insert("keyMapNodes", array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

int KeyMapModel::nodeCount() const
{
    return m_nodes.size();
}

bool KeyMapModel::hasMouseMove() const
{
    return m_hasMouse;
}

bool KeyMapModel::hasItem(int item) const
{
    if (item == MouseItem) {
        return m_hasMouse;
    }
    return item >= 0 && item < m_nodes.size();
}

QJsonObject KeyMapModel::item(int item) const
{
    if (item == MouseItem) {
        return m_mouse;
    }
    if (item >= 0 && item < m_nodes.size()) {
        return m_nodes.at(item);
    }
    return QJsonObject();
}

void KeyMapModel::setItem(int item, const QJsonObject &obj)
{
    if (item == MouseItem) {
        m_mouse = obj;
        m_hasMouse = true;
    } else if (item >= 0 && item < m_nodes.size()) {
        m_nodes[item] = obj;
    }
}

QString KeyMapModel::itemType(int item) const
{
    if (item == MouseItem) {
        return kMouseMove;
    }
    return this->item(item).value("type").toString();
}

QString KeyMapModel::itemTitle(int item) const
{
    const QJsonObject obj = this->item(item);
    const QString type = itemType(item);
    QString keys;
    if (item == MouseItem) {
        keys = tr("Mouse");
    } else if (type == kSteerWheel) {
        keys = QStringList({ shortKeyLabel(obj.value("upKey").toString()),
                             shortKeyLabel(obj.value("leftKey").toString()),
                             shortKeyLabel(obj.value("downKey").toString()),
                             shortKeyLabel(obj.value("rightKey").toString()) })
                   .join(" ");
    } else {
        keys = shortKeyLabel(obj.value("key").toString());
    }
    const QString comment = obj.value("comment").toString().trimmed();
    return QString("[%1] %2").arg(keys, comment.isEmpty() ? typeName(type) : comment);
}

QString KeyMapModel::switchKey() const
{
    return m_root.value("switchKey").toString();
}

void KeyMapModel::setSwitchKey(const QString &key)
{
    m_root.insert("switchKey", key);
}

int KeyMapModel::addItem(const QString &type, const QPointF &at)
{
    const QPointF pos = clampPos(at);
    if (type == kMouseMove) {
        if (m_hasMouse) {
            return MouseItem;
        }
        QJsonObject mouse;
        writePos(mouse, "startPos", pos);
        mouse.insert("speedRatioX", 3.25);
        mouse.insert("speedRatioY", 1.25);
        // the view finger area must contain the start point
        QJsonObject bounds;
        bounds.insert("left", round4(qMax(0.0, qMin(0.35, pos.x() - 0.1))));
        bounds.insert("top", round4(qMax(0.0, qMin(0.05, pos.y() - 0.1))));
        bounds.insert("right", round4(qMin(1.0, qMax(0.98, pos.x() + 0.1))));
        bounds.insert("bottom", round4(qMin(1.0, qMax(0.95, pos.y() + 0.1))));
        mouse.insert("bounds", bounds);
        mouse.insert("recenterDelayMs", 16);
        mouse.insert("autoReleaseMs", 500);
        m_mouse = mouse;
        m_hasMouse = true;
        return MouseItem;
    }

    QJsonObject node;
    node.insert("comment", "");
    node.insert("type", type);
    if (type == kClick || type == kClickTwice) {
        node.insert("key", "");
        writePos(node, "pos", pos);
        if (type == kClick) {
            node.insert("switchMap", false);
        }
    } else if (type == kClickMulti) {
        node.insert("key", "");
        QJsonArray clickNodes;
        for (int i = 0; i < 2; ++i) {
            QJsonObject clickNode;
            clickNode.insert("delay", i == 0 ? 0 : 100);
            writePos(clickNode, "pos", clampPos(pos + QPointF(0.04 * i, 0)));
            clickNodes.append(clickNode);
        }
        node.insert("clickNodes", clickNodes);
    } else if (type == kDrag) {
        node.insert("key", "");
        writePos(node, "startPos", pos);
        writePos(node, "endPos", clampPos(pos + QPointF(0.1, 0)));
    } else if (type == kSteerWheel) {
        writePos(node, "centerPos", pos);
        node.insert("leftOffset", 0.1);
        node.insert("rightOffset", 0.1);
        node.insert("upOffset", 0.27);
        node.insert("downOffset", 0.2);
        node.insert("leftKey", "Key_A");
        node.insert("rightKey", "Key_D");
        node.insert("upKey", "Key_W");
        node.insert("downKey", "Key_S");
        node.insert("modifierScale", 0.5);
        node.insert("stepLength", 0.01);
        node.insert("stepIntervalMs", 4);
        node.insert("releaseDelayMs", 30);
    } else {
        return NoItem;
    }
    m_nodes.append(node);
    return m_nodes.size() - 1;
}

void KeyMapModel::removeItem(int item)
{
    if (item == MouseItem) {
        m_mouse = QJsonObject();
        m_hasMouse = false;
    } else if (item >= 0 && item < m_nodes.size()) {
        m_nodes.remove(item);
    }
}

void KeyMapModel::addMultiClickPoint(int item)
{
    if (itemType(item) != kClickMulti) {
        return;
    }
    QJsonObject node = m_nodes.at(item);
    QJsonArray clickNodes = node.value("clickNodes").toArray();
    QPointF last(0.5, 0.5);
    if (!clickNodes.isEmpty()) {
        last = readPos(clickNodes.last().toObject(), "pos");
    }
    QJsonObject clickNode;
    clickNode.insert("delay", 100);
    writePos(clickNode, "pos", clampPos(last + QPointF(0.04, 0)));
    clickNodes.append(clickNode);
    node.insert("clickNodes", clickNodes);
    m_nodes[item] = node;
}

void KeyMapModel::removeMultiClickPoint(int item)
{
    if (itemType(item) != kClickMulti) {
        return;
    }
    QJsonObject node = m_nodes.at(item);
    QJsonArray clickNodes = node.value("clickNodes").toArray();
    if (clickNodes.size() > 1) {
        clickNodes.removeLast();
    }
    node.insert("clickNodes", clickNodes);
    m_nodes[item] = node;
}

QVector<KeyMapHandle> KeyMapModel::handles(int item) const
{
    QVector<KeyMapHandle> result;
    if (!hasItem(item)) {
        return result;
    }
    const QJsonObject obj = this->item(item);
    const QString type = itemType(item);
    if (item == MouseItem) {
        const QRectF bounds = mouseBounds(obj);
        result.append({ item, PartMouseStart, readPos(obj, "startPos"), tr("View"), false });
        result.append({ item, PartBoundsTopLeft, bounds.topLeft(), QString(), true });
        result.append({ item, PartBoundsBottomRight, bounds.bottomRight(), QString(), true });
        if (obj.value("smallEyes").isObject()) {
            const QJsonObject smallEyes = obj.value("smallEyes").toObject();
            result.append({ item, PartSmallEyes, readPos(smallEyes, "pos"), shortKeyLabel(smallEyes.value("key").toString()), false });
        }
    } else if (type == kClick || type == kClickTwice) {
        result.append({ item, PartPos, readPos(obj, "pos"), shortKeyLabel(obj.value("key").toString()), false });
    } else if (type == kClickMulti) {
        const QString key = shortKeyLabel(obj.value("key").toString());
        const QJsonArray clickNodes = obj.value("clickNodes").toArray();
        for (int i = 0; i < clickNodes.size(); ++i) {
            result.append({ item, PartMultiFirst + i, readPos(clickNodes.at(i).toObject(), "pos"), QString("%1·%2").arg(key).arg(i + 1), false });
        }
    } else if (type == kDrag) {
        result.append({ item, PartDragStart, readPos(obj, "startPos"), shortKeyLabel(obj.value("key").toString()), false });
        result.append({ item, PartDragEnd, readPos(obj, "endPos"), QString(), true });
    } else if (type == kSteerWheel) {
        const QPointF center = readPos(obj, "centerPos");
        result.append({ item, PartSteerCenter, center, tr("Stick"), false });
        result.append({ item, PartSteerUp, center - QPointF(0, obj.value("upOffset").toDouble()), shortKeyLabel(obj.value("upKey").toString()), false });
        result.append({ item, PartSteerDown, center + QPointF(0, obj.value("downOffset").toDouble()), shortKeyLabel(obj.value("downKey").toString()), false });
        result.append({ item, PartSteerLeft, center - QPointF(obj.value("leftOffset").toDouble(), 0), shortKeyLabel(obj.value("leftKey").toString()), false });
        result.append({ item, PartSteerRight, center + QPointF(obj.value("rightOffset").toDouble(), 0), shortKeyLabel(obj.value("rightKey").toString()), false });
    }
    return result;
}

QVector<KeyMapHandle> KeyMapModel::allHandles() const
{
    QVector<KeyMapHandle> result;
    const QVector<int> ids = items();
    for (int id : ids) {
        result += handles(id);
    }
    return result;
}

QVector<KeyMapShape> KeyMapModel::shapes(int item) const
{
    QVector<KeyMapShape> result;
    const QJsonObject obj = this->item(item);
    const QString type = itemType(item);
    if (item == MouseItem && m_hasMouse) {
        result.append({ item, true, QLineF(), mouseBounds(obj) });
    } else if (type == kDrag) {
        result.append({ item, false, QLineF(readPos(obj, "startPos"), readPos(obj, "endPos")), QRectF() });
    } else if (type == kClickMulti) {
        const QJsonArray clickNodes = obj.value("clickNodes").toArray();
        for (int i = 1; i < clickNodes.size(); ++i) {
            result.append({ item, false, QLineF(readPos(clickNodes.at(i - 1).toObject(), "pos"), readPos(clickNodes.at(i).toObject(), "pos")), QRectF() });
        }
    } else if (type == kSteerWheel) {
        const QPointF center = readPos(obj, "centerPos");
        const QPointF topLeft = center - QPointF(obj.value("leftOffset").toDouble(), obj.value("upOffset").toDouble());
        const QPointF bottomRight = center + QPointF(obj.value("rightOffset").toDouble(), obj.value("downOffset").toDouble());
        result.append({ item, true, QLineF(), QRectF(topLeft, bottomRight) });
        const QVector<KeyMapHandle> points = handles(item);
        for (const KeyMapHandle &point : points) {
            if (point.part != PartSteerCenter) {
                result.append({ item, false, QLineF(center, point.pos), QRectF() });
            }
        }
    }
    return result;
}

QVector<int> KeyMapModel::items() const
{
    QVector<int> result;
    if (m_hasMouse) {
        result.append(MouseItem);
    }
    for (int i = 0; i < m_nodes.size(); ++i) {
        result.append(i);
    }
    return result;
}

QPointF KeyMapModel::handlePos(int item, int part) const
{
    const QVector<KeyMapHandle> list = handles(item);
    for (const KeyMapHandle &handle : list) {
        if (handle.part == part) {
            return handle.pos;
        }
    }
    return QPointF(0.5, 0.5);
}

void KeyMapModel::moveHandle(int item, int part, const QPointF &to)
{
    if (!hasItem(item)) {
        return;
    }
    const QPointF pos = clampPos(to);
    QJsonObject obj = this->item(item);
    switch (part) {
    case PartPos:
        writePos(obj, "pos", pos);
        break;
    case PartDragStart:
        writePos(obj, "startPos", pos);
        break;
    case PartDragEnd:
        writePos(obj, "endPos", pos);
        break;
    case PartSteerCenter:
        writePos(obj, "centerPos", pos);
        break;
    case PartSteerLeft:
        obj.insert("leftOffset", round4(qMax(0.01, readPos(obj, "centerPos").x() - pos.x())));
        break;
    case PartSteerRight:
        obj.insert("rightOffset", round4(qMax(0.01, pos.x() - readPos(obj, "centerPos").x())));
        break;
    case PartSteerUp:
        obj.insert("upOffset", round4(qMax(0.01, readPos(obj, "centerPos").y() - pos.y())));
        break;
    case PartSteerDown:
        obj.insert("downOffset", round4(qMax(0.01, pos.y() - readPos(obj, "centerPos").y())));
        break;
    case PartMouseStart:
        writePos(obj, "startPos", pos);
        break;
    case PartBoundsTopLeft:
    case PartBoundsBottomRight: {
        const QRectF bounds = mouseBounds(obj);
        double left = bounds.left();
        double top = bounds.top();
        double right = bounds.right();
        double bottom = bounds.bottom();
        if (part == PartBoundsTopLeft) {
            left = qMin(pos.x(), right - 0.05);
            top = qMin(pos.y(), bottom - 0.05);
        } else {
            right = qMax(pos.x(), left + 0.05);
            bottom = qMax(pos.y(), top + 0.05);
        }
        QJsonObject boundsObj;
        boundsObj.insert("left", round4(left));
        boundsObj.insert("top", round4(top));
        boundsObj.insert("right", round4(right));
        boundsObj.insert("bottom", round4(bottom));
        obj.insert("bounds", boundsObj);
    } break;
    case PartSmallEyes: {
        QJsonObject smallEyes = obj.value("smallEyes").toObject();
        writePos(smallEyes, "pos", pos);
        obj.insert("smallEyes", smallEyes);
    } break;
    default:
        if (part >= PartMultiFirst) {
            QJsonArray clickNodes = obj.value("clickNodes").toArray();
            const int index = part - PartMultiFirst;
            if (index < clickNodes.size()) {
                QJsonObject clickNode = clickNodes.at(index).toObject();
                writePos(clickNode, "pos", pos);
                clickNodes[index] = clickNode;
                obj.insert("clickNodes", clickNodes);
            }
        }
        break;
    }
    setItem(item, obj);
}

int KeyMapModel::primaryPart(int item) const
{
    const QVector<KeyMapHandle> list = handles(item);
    return list.isEmpty() ? -1 : list.first().part;
}

QStringList KeyMapModel::problems() const
{
    QStringList result;
    if (!isValidKey(switchKey())) {
        result << tr("The mapping on/off key is not set");
    }

    QMap<QString, QStringList> keyUsers;
    if (m_hasMouse) {
        const QString title = itemTitle(MouseItem);
        if (!m_mouse.value("speedRatioX").isDouble() && !m_mouse.value("speedRatio").isDouble()) {
            result << tr("%1: the sensitivity (speedRatioX) is missing").arg(title);
        }
        const QRectF bounds = mouseBounds(m_mouse);
        if (!m_mouse.value("startPos").isObject()) {
            result << tr("%1: the start point is missing").arg(title);
        } else if (!bounds.contains(readPos(m_mouse, "startPos"))) {
            result << tr("%1: the start point must be inside the view area (dashed box)").arg(title);
        }
        if (m_mouse.value("smallEyes").isObject()) {
            const QJsonObject smallEyes = m_mouse.value("smallEyes").toObject();
            const QString key = smallEyes.value("key").toString();
            if (!isValidKey(key)) {
                result << tr("%1: the small eyes key is not set").arg(title);
            } else {
                keyUsers[key] << tr("small eyes");
            }
            if (!bounds.contains(readPos(smallEyes, "pos"))) {
                result << tr("%1: small eyes must be inside the view area (dashed box)").arg(title);
            }
        }
    }

    int steerWheels = 0;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const QJsonObject &node = m_nodes.at(i);
        const QString type = node.value("type").toString();
        const QString title = QString("#%1 %2").arg(i + 1).arg(itemTitle(i));
        QStringList keyFields;
        QStringList posFields;
        if (type == kClick || type == kClickTwice) {
            keyFields << "key";
            posFields << "pos";
            if (type == kClick && !node.value("switchMap").isBool()) {
                result << tr("%1: switchMap is missing").arg(title);
            }
        } else if (type == kClickMulti) {
            keyFields << "key";
            const QJsonArray clickNodes = node.value("clickNodes").toArray();
            if (clickNodes.isEmpty()) {
                result << tr("%1: there is no click point").arg(title);
            }
            for (int j = 0; j < clickNodes.size(); ++j) {
                if (!inScreen(readPos(clickNodes.at(j).toObject(), "pos"))) {
                    result << tr("%1: a click point is outside the screen").arg(title);
                }
            }
        } else if (type == kDrag) {
            keyFields << "key";
            posFields << "startPos" << "endPos";
        } else if (type == kSteerWheel) {
            ++steerWheels;
            keyFields << "upKey" << "downKey" << "leftKey" << "rightKey";
            posFields << "centerPos";
            if (node.contains("modifierKey") && !isKeyboardKey(node.value("modifierKey").toString())) {
                result << tr("%1: the walk key must be a keyboard key").arg(title);
            }
        } else if (type == kAndroidKey) {
            keyFields << "key";
            if (!node.value("androidKey").isDouble()) {
                result << tr("%1: androidKey is missing").arg(title);
            }
        } else {
            result << tr("%1: unknown type \"%2\"").arg(title, type);
            continue;
        }

        for (const QString &field : keyFields) {
            const QString key = node.value(field).toString();
            if (!isValidKey(key)) {
                result << tr("%1: the key (%2) is not set").arg(title, field);
            } else {
                keyUsers[key] << title;
            }
        }
        for (const QString &field : posFields) {
            if (!node.value(field).isObject()) {
                result << tr("%1: %2 is missing").arg(title, field);
            } else if (!inScreen(readPos(node, field))) {
                result << tr("%1: %2 is outside the screen").arg(title, field);
            }
        }
    }

    if (steerWheels > 1) {
        result << tr("Only one steer wheel takes effect (the last one)");
    }
    for (auto it = keyUsers.constBegin(); it != keyUsers.constEnd(); ++it) {
        if (it.value().size() > 1) {
            result << tr("Key %1 is used more than once: %2").arg(shortKeyLabel(it.key()), it.value().join(", "));
        }
    }
    return result;
}

bool KeyMapModel::isValidKey(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    bool ok = false;
    QMetaEnum::fromType<Qt::MouseButtons>().keyToValue(name.toLatin1().constData(), &ok);
    return ok || isKeyboardKey(name);
}

bool KeyMapModel::isKeyboardKey(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    bool ok = false;
    QMetaEnum::fromType<Qt::Key>().keyToValue(name.toLatin1().constData(), &ok);
    return ok;
}

QString KeyMapModel::shortKeyLabel(const QString &name)
{
    if (name.isEmpty()) {
        return "?";
    }
    static const QMap<QString, QString> special = {
        { "LeftButton", tr("LMB") },     { "RightButton", tr("RMB") },     { "MiddleButton", tr("MMB") },  { "MidButton", tr("MMB") },
        { "BackButton", tr("Side 1") },  { "ForwardButton", tr("Side 2") }, { "Key_QuoteLeft", "~" },      { "Key_Control", "Ctrl" },
        { "Key_Escape", "Esc" },         { "Key_Return", "Enter" },        { "Key_Equal", "=" },           { "Key_Minus", "-" },
        { "Key_Comma", "," },            { "Key_Period", "." },            { "Key_Slash", "/" },           { "Key_Semicolon", ";" },
        { "Key_Apostrophe", "'" },       { "Key_BracketLeft", "[" },       { "Key_BracketRight", "]" },    { "Key_Backslash", "\\" },
        { "Key_CapsLock", "Caps" },      { "Key_Space", tr("Space") },
    };
    auto it = special.constFind(name);
    if (it != special.constEnd()) {
        return it.value();
    }
    if (name.startsWith("Key_")) {
        return name.mid(4);
    }
    return name;
}

QString KeyMapModel::typeName(const QString &type)
{
    if (type == kClick) {
        return tr("Tap / hold");
    } else if (type == kClickTwice) {
        return tr("Toggle (tap on press and on release)");
    } else if (type == kClickMulti) {
        return tr("Tap sequence");
    } else if (type == kDrag) {
        return tr("Swipe");
    } else if (type == kSteerWheel) {
        return tr("Movement stick (WASD)");
    } else if (type == kAndroidKey) {
        return tr("Android key");
    } else if (type == kMouseMove) {
        return tr("Mouse look");
    }
    return type;
}

QPointF KeyMapModel::readPos(const QJsonObject &obj, const QString &name)
{
    const QJsonObject pos = obj.value(name).toObject();
    return QPointF(pos.value("x").toDouble(), pos.value("y").toDouble());
}

void KeyMapModel::writePos(QJsonObject &obj, const QString &name, const QPointF &pos)
{
    QJsonObject value;
    value.insert("x", round4(pos.x()));
    value.insert("y", round4(pos.y()));
    obj.insert(name, value);
}

QRectF KeyMapModel::mouseBounds(const QJsonObject &mouse)
{
    // same defaults as the keymap parser
    const QJsonObject bounds = mouse.value("bounds").toObject();
    return QRectF(QPointF(bounds.value("left").toDouble(0.05), bounds.value("top").toDouble(0.05)),
                  QPointF(bounds.value("right").toDouble(0.95), bounds.value("bottom").toDouble(0.95)));
}

//
// KeyCaptureButton
//

KeyCaptureButton::KeyCaptureButton(QWidget *parent) : QPushButton(parent)
{
    setAutoDefault(false);
    setFocusPolicy(Qt::StrongFocus);
    // long key names must not widen the side panel
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(80);
    setToolTip(tr("Click, then press a key, or click here with the mouse button to use. Esc cancels."));
    connect(this, &QPushButton::clicked, this, &KeyCaptureButton::startCapture);
    updateText();
}

void KeyCaptureButton::setKeyName(const QString &name)
{
    m_keyName = name;
    if (!m_capturing) {
        updateText();
    }
}

QString KeyCaptureButton::keyName() const
{
    return m_keyName;
}

void KeyCaptureButton::startCapture()
{
    m_capturing = true;
    setFocus(Qt::OtherFocusReason);
    setText(tr("Press a key or mouse button..."));
}

bool KeyCaptureButton::event(QEvent *event)
{
    if (m_capturing) {
        // keep shortcuts (Ctrl+S, Tab focus change...) from eating the key
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent *>(event));
            return true;
        }
    }
    return QPushButton::event(event);
}

void KeyCaptureButton::keyPressEvent(QKeyEvent *event)
{
    if (!m_capturing) {
        QPushButton::keyPressEvent(event);
        return;
    }
    event->accept();
    if (event->isAutoRepeat()) {
        return;
    }
    int key = event->key();
    if (key == Qt::Key_Escape) {
        finishCapture(QString());
        return;
    }
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
    }
    const char *name = QMetaEnum::fromType<Qt::Key>().valueToKey(key);
    if (name) {
        finishCapture(QString::fromLatin1(name));
    }
}

void KeyCaptureButton::mousePressEvent(QMouseEvent *event)
{
    if (!m_capturing) {
        QPushButton::mousePressEvent(event);
        return;
    }
    event->accept();
    const char *name = QMetaEnum::fromType<Qt::MouseButtons>().valueToKey(static_cast<int>(event->button()));
    if (name) {
        finishCapture(QString::fromLatin1(name));
    }
}

void KeyCaptureButton::focusOutEvent(QFocusEvent *event)
{
    if (m_capturing) {
        finishCapture(QString());
    }
    QPushButton::focusOutEvent(event);
}

void KeyCaptureButton::finishCapture(const QString &name)
{
    m_capturing = false;
    if (!name.isEmpty()) {
        m_keyName = name;
    }
    updateText();
    if (!name.isEmpty()) {
        emit keyCaptured(name);
    }
}

void KeyCaptureButton::updateText()
{
    if (m_keyName.isEmpty()) {
        setText(tr("(not set, click to record)"));
    } else {
        setText(QString("%1   (%2)").arg(KeyMapModel::shortKeyLabel(m_keyName), m_keyName));
    }
}

//
// KeyMapCanvas
//

KeyMapCanvas::KeyMapCanvas(const KeyMapModel *model, QWidget *parent) : QWidget(parent), m_model(model)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(480, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void KeyMapCanvas::setBackground(const QImage &image)
{
    m_background = image;
    update();
}

const QImage &KeyMapCanvas::background() const
{
    return m_background;
}

void KeyMapCanvas::setSelection(int item, int part)
{
    m_selectedItem = item;
    m_selectedPart = part;
    update();
}

QSize KeyMapCanvas::screenSize() const
{
    // without a screenshot assume a common 20:9 landscape phone
    return m_background.isNull() ? QSize(2400, 1080) : m_background.size();
}

QSize KeyMapCanvas::sizeHint() const
{
    return QSize(960, 480);
}

QRectF KeyMapCanvas::screenRect() const
{
    const QSize size = screenSize();
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    if (area.width() <= 0 || area.height() <= 0 || size.isEmpty()) {
        return area;
    }
    const qreal scale = qMin(area.width() / size.width(), area.height() / size.height());
    const QSizeF scaled(size.width() * scale, size.height() * scale);
    return QRectF(area.center().x() - scaled.width() / 2, area.center().y() - scaled.height() / 2, scaled.width(), scaled.height());
}

QPointF KeyMapCanvas::toScreen(const QPointF &ratio) const
{
    const QRectF r = screenRect();
    return QPointF(r.left() + ratio.x() * r.width(), r.top() + ratio.y() * r.height());
}

QPointF KeyMapCanvas::toRatio(const QPointF &widgetPos) const
{
    const QRectF r = screenRect();
    if (r.width() <= 0 || r.height() <= 0) {
        return QPointF();
    }
    return QPointF((widgetPos.x() - r.left()) / r.width(), (widgetPos.y() - r.top()) / r.height());
}

QRectF KeyMapCanvas::handleRect(const KeyMapHandle &handle) const
{
    const QPointF center = toScreen(handle.pos);
    if (handle.compact) {
        return QRectF(center.x() - 5, center.y() - 5, 10, 10);
    }
    const QFontMetricsF metrics(labelFont(font()));
    const qreal height = qMax<qreal>(26, metrics.height() + 8);
    const qreal width = qMax(height, metrics.horizontalAdvance(handle.label) + 14);
    return QRectF(center.x() - width / 2, center.y() - height / 2, width, height);
}

bool KeyMapCanvas::hitTest(const QPointF &widgetPos, int *item, int *part) const
{
    // the selected item is drawn on top, so it wins
    QVector<KeyMapHandle> candidates = m_model->handles(m_selectedItem);
    const QVector<KeyMapHandle> all = m_model->allHandles();
    for (int i = all.size() - 1; i >= 0; --i) {
        candidates.append(all.at(i));
    }
    for (const KeyMapHandle &handle : candidates) {
        if (handleRect(handle).adjusted(-3, -3, 3, 3).contains(widgetPos)) {
            *item = handle.item;
            *part = handle.part;
            return true;
        }
    }
    return false;
}

void KeyMapCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(32, 33, 36));

    const QRectF screen = screenRect();
    if (!m_background.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(screen, m_background);
    } else {
        painter.fillRect(screen, QColor(48, 50, 56));
        painter.setPen(QColor(255, 255, 255, 25));
        for (int i = 1; i < 10; ++i) {
            painter.drawLine(toScreen(QPointF(i / 10.0, 0)), toScreen(QPointF(i / 10.0, 1)));
            painter.drawLine(toScreen(QPointF(0, i / 10.0)), toScreen(QPointF(1, i / 10.0)));
        }
        painter.setPen(QColor(255, 255, 255, 140));
        painter.drawText(screen, Qt::AlignCenter | Qt::TextWordWrap,
                         tr("Open the game on the phone (landscape), then click \"Capture phone screen\" to use it as the background"));
    }

    const QColor selectedColor(255, 193, 7);
    const QColor normalColor(30, 136, 229);
    const QVector<int> items = m_model->items();

    for (int item : items) {
        const bool selected = item == m_selectedItem;
        const QVector<KeyMapShape> shapes = m_model->shapes(item);
        for (const KeyMapShape &shape : shapes) {
            QPen pen(selected ? selectedColor : QColor(100, 181, 246, 200), selected ? 2.0 : 1.5);
            if (shape.isRect) {
                pen.setStyle(Qt::DashLine);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(toScreen(shape.rect.topLeft()), toScreen(shape.rect.bottomRight())));
            } else {
                painter.setPen(pen);
                painter.drawLine(toScreen(shape.line.p1()), toScreen(shape.line.p2()));
            }
        }
    }

    painter.setFont(labelFont(font()));
    // the selected item last, so it is drawn on top
    for (int pass = 0; pass < 2; ++pass) {
        for (int item : items) {
            const bool selected = item == m_selectedItem;
            if (selected != (pass == 1)) {
                continue;
            }
            const QVector<KeyMapHandle> handles = m_model->handles(item);
            for (const KeyMapHandle &handle : handles) {
                const QRectF box = handleRect(handle);
                const bool selectedPart = selected && handle.part == m_selectedPart;
                QColor fill = selected ? selectedColor : normalColor;
                fill.setAlpha(selected ? 235 : 200);
                painter.setPen(QPen(selectedPart ? QColor(Qt::white) : QColor(0, 0, 0, 160), selectedPart ? 2.5 : 1.0));
                painter.setBrush(fill);
                if (handle.compact) {
                    painter.drawRect(box);
                    continue;
                }
                painter.drawRoundedRect(box, box.height() / 2, box.height() / 2);
                painter.setPen(selected ? QColor(Qt::black) : QColor(Qt::white));
                painter.drawText(box, Qt::AlignCenter, handle.label);
            }
        }
    }

    // comment of the selected item under its first handle
    if (m_model->hasItem(m_selectedItem)) {
        const QString comment = m_model->item(m_selectedItem).value("comment").toString().trimmed();
        const int part = m_model->primaryPart(m_selectedItem);
        if (!comment.isEmpty() && part >= 0) {
            const QPointF anchor = toScreen(m_model->handlePos(m_selectedItem, part));
            painter.setFont(font());
            const QFontMetricsF metrics(font());
            const qreal width = qMin<qreal>(metrics.horizontalAdvance(comment) + 12, 320);
            QRectF box(anchor.x() - width / 2, anchor.y() + 18, width, metrics.height() + 6);
            if (box.bottom() > height()) {
                box.moveBottom(anchor.y() - 18);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 190));
            painter.drawRoundedRect(box, 4, 4);
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter, metrics.elidedText(comment, Qt::ElideRight, width - 8));
        }
    }
}

void KeyMapCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int item = KeyMapModel::NoItem;
    int part = -1;
    if (hitTest(mousePos(event), &item, &part)) {
        m_dragging = true;
        emit handlePressed(item, part);
    } else {
        emit emptyPressed();
    }
}

void KeyMapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF ratio = toRatio(mousePos(event));
    emit hovered(ratio, inScreen(ratio));
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        emit handleDragged(m_selectedItem, m_selectedPart, clampPos(ratio));
    }
}

void KeyMapCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void KeyMapCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    int item = KeyMapModel::NoItem;
    int part = -1;
    if (hitTest(event->pos(), &item, &part)) {
        emit handlePressed(item, part);
        QAction *remove = menu.addAction(tr("Delete \"%1\"").arg(m_model->itemTitle(item)));
        if (menu.exec(event->globalPos()) == remove) {
            emit deleteRequested();
        }
        return;
    }

    const QPointF ratio = toRatio(event->pos());
    if (!inScreen(ratio)) {
        return;
    }
    const QStringList types = { kClick, kClickTwice, kClickMulti, kDrag, kSteerWheel, kMouseMove };
    for (const QString &type : types) {
        QAction *action = menu.addAction(tr("Add here: %1").arg(KeyMapModel::typeName(type)));
        action->setData(type);
        if (type == kMouseMove && m_model->hasMouseMove()) {
            action->setEnabled(false);
        }
    }
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen) {
        emit addRequested(chosen->data().toString(), ratio);
    }
}

void KeyMapCanvas::keyPressEvent(QKeyEvent *event)
{
    const qreal step = (event->modifiers() & Qt::ShiftModifier) ? 0.01 : 0.001;
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        emit deleteRequested();
        break;
    case Qt::Key_Left:
        emit nudgeRequested(QPointF(-step, 0));
        break;
    case Qt::Key_Right:
        emit nudgeRequested(QPointF(step, 0));
        break;
    case Qt::Key_Up:
        emit nudgeRequested(QPointF(0, -step));
        break;
    case Qt::Key_Down:
        emit nudgeRequested(QPointF(0, step));
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void KeyMapCanvas::leaveEvent(QEvent *event)
{
    emit hovered(QPointF(), false);
    QWidget::leaveEvent(event);
}

//
// KeyMapEditor
//

KeyMapEditor::KeyMapEditor(const QString &keymapDir, const QString &defaultDir, QWidget *parent)
    : QDialog(parent), m_keymapDir(keymapDir), m_defaultDir(defaultDir)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags((windowFlags() | Qt::WindowMinMaxButtonsHint) & ~Qt::WindowContextHelpButtonHint);
    buildUi();
    resize(1360, 780);

    m_captureProcess = new QProcess(this);
    connect(m_captureProcess, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &KeyMapEditor::onCaptureFinished);
    connect(m_captureProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        m_captureTimeout->stop();
        m_captureBtn->setEnabled(true);
        QMessageBox::warning(this, windowTitle(), tr("Could not run adb: %1").arg(m_captureProcess->program()));
    });
    m_captureTimeout = new QTimer(this);
    m_captureTimeout->setSingleShot(true);
    m_captureTimeout->setInterval(15000);
    connect(m_captureTimeout, &QTimer::timeout, m_captureProcess, &QProcess::kill);

    refreshFileBox(QString());
    refreshList();
    refreshProperties();
    refreshProblems();
    updateTitle();
}

void KeyMapEditor::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // tool bar
    auto *toolBar = new QHBoxLayout();
    toolBar->addWidget(new QLabel(tr("Keymap:"), this));
    m_fileBox = new QComboBox(this);
    m_fileBox->setMinimumWidth(180);
    connect(m_fileBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, &KeyMapEditor::onFileBoxActivated);
    toolBar->addWidget(m_fileBox);
    QPushButton *newBtn = makeButton(tr("New..."), this);
    connect(newBtn, &QPushButton::clicked, this, &KeyMapEditor::onNewFile);
    toolBar->addWidget(newBtn);
    QPushButton *saveAsBtn = makeButton(tr("Save as..."), this);
    connect(saveAsBtn, &QPushButton::clicked, this, &KeyMapEditor::onSaveAs);
    toolBar->addWidget(saveAsBtn);
    QPushButton *saveBtn = makeButton(tr("Save and apply (Ctrl+S)"), this);
    saveBtn->setToolTip(tr("Writes the keymap and loads it on the connected phone right away"));
    connect(saveBtn, &QPushButton::clicked, this, &KeyMapEditor::onSave);
    toolBar->addWidget(saveBtn);
    toolBar->addSpacing(16);

    m_captureBtn = makeButton(tr("Capture phone screen"), this);
    connect(m_captureBtn, &QPushButton::clicked, this, &KeyMapEditor::onCapture);
    toolBar->addWidget(m_captureBtn);
    QPushButton *openImageBtn = makeButton(tr("Open image..."), this);
    connect(openImageBtn, &QPushButton::clicked, this, &KeyMapEditor::onOpenImage);
    toolBar->addWidget(openImageBtn);
    toolBar->addSpacing(16);

    auto *addBtn = new QToolButton(this);
    addBtn->setText(tr("Add"));
    addBtn->setPopupMode(QToolButton::InstantPopup);
    auto *addMenu = new QMenu(addBtn);
    const QStringList types = { kClick, kClickTwice, kClickMulti, kDrag, kSteerWheel, kMouseMove };
    for (const QString &type : types) {
        QAction *action = addMenu->addAction(KeyMapModel::typeName(type));
        connect(action, &QAction::triggered, this, [this, type] { onAddRequested(type, newItemPos()); });
        if (type == kMouseMove) {
            connect(addMenu, &QMenu::aboutToShow, action, [this, action] { action->setEnabled(!m_model.hasMouseMove()); });
        }
    }
    addBtn->setMenu(addMenu);
    toolBar->addWidget(addBtn);
    QPushButton *deleteBtn = makeButton(tr("Delete"), this);
    connect(deleteBtn, &QPushButton::clicked, this, &KeyMapEditor::onDeleteSelected);
    toolBar->addWidget(deleteBtn);
    QPushButton *undoBtn = makeButton(tr("Undo"), this);
    connect(undoBtn, &QPushButton::clicked, this, &KeyMapEditor::onUndo);
    toolBar->addWidget(undoBtn);
    QPushButton *redoBtn = makeButton(tr("Redo"), this);
    connect(redoBtn, &QPushButton::clicked, this, &KeyMapEditor::onRedo);
    toolBar->addWidget(redoBtn);
    toolBar->addStretch();
    mainLayout->addLayout(toolBar);

    auto *body = new QHBoxLayout();
    mainLayout->addLayout(body, 1);

    // canvas and status line
    auto *left = new QVBoxLayout();
    m_canvas = new KeyMapCanvas(&m_model, this);
    connect(m_canvas, &KeyMapCanvas::handlePressed, this, &KeyMapEditor::onHandlePressed);
    connect(m_canvas, &KeyMapCanvas::handleDragged, this, &KeyMapEditor::onHandleDragged);
    connect(m_canvas, &KeyMapCanvas::emptyPressed, this, &KeyMapEditor::onEmptyPressed);
    connect(m_canvas, &KeyMapCanvas::addRequested, this, &KeyMapEditor::onAddRequested);
    connect(m_canvas, &KeyMapCanvas::deleteRequested, this, &KeyMapEditor::onDeleteSelected);
    connect(m_canvas, &KeyMapCanvas::nudgeRequested, this, &KeyMapEditor::onNudge);
    connect(m_canvas, &KeyMapCanvas::hovered, this, &KeyMapEditor::onHovered);
    left->addWidget(m_canvas, 1);
    m_statusLabel = new QLabel(this);
    left->addWidget(m_statusLabel);
    auto *help = new QLabel(tr("Drag a marker to move it. Right click the screen to add a key there. "
                               "Arrow keys move the selected point (Shift: faster), Delete removes the key."),
                            this);
    help->setWordWrap(true);
    help->setStyleSheet("color: gray;");
    left->addWidget(help);
    body->addLayout(left, 1);

    // side panel
    auto *side = new QWidget(this);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);

    auto *switchForm = makeForm(nullptr);
    m_switchKeyBtn = new KeyCaptureButton(side);
    connect(m_switchKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) {
        pushUndo();
        m_model.setSwitchKey(key);
        modelChanged();
    });
    switchForm->addRow(tr("Mapping on/off key:"), m_switchKeyBtn);
    sideLayout->addLayout(switchForm);

    sideLayout->addWidget(new QLabel(tr("Keys:"), side));
    m_itemList = new QListWidget(side);
    m_itemList->setMinimumHeight(160);
    m_itemList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_itemList, &QListWidget::currentRowChanged, this, &KeyMapEditor::onListRowChanged);
    connect(m_itemList, &QListWidget::itemDoubleClicked, this, [this] {
        if (m_keySection->isVisible()) {
            m_keyBtn->startCapture();
        }
    });
    sideLayout->addWidget(m_itemList, 1);

    auto *propBox = new QGroupBox(tr("Selected key"), side);
    m_propPanel = propBox;
    auto *propLayout = new QVBoxLayout(propBox);

    auto *common = new QWidget(propBox);
    auto *commonForm = makeForm(common);
    m_typeLabel = new QLabel(common);
    m_typeLabel->setWordWrap(true);
    commonForm->addRow(tr("Type:"), m_typeLabel);
    m_commentEdit = new QLineEdit(common);
    m_commentEdit->setPlaceholderText(tr("what this key does, e.g. jump"));
    connect(m_commentEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_updating || !m_model.hasItem(m_selectedItem)) {
            return;
        }
        QJsonObject obj = m_model.item(m_selectedItem);
        const QString text = m_commentEdit->text().trimmed();
        if (obj.value("comment").toString() == text) {
            return;
        }
        obj.insert("comment", text);
        updateItem(obj);
    });
    commonForm->addRow(tr("Note:"), m_commentEdit);
    propLayout->addWidget(common);

    m_keySection = new QWidget(propBox);
    auto *keyForm = makeForm(m_keySection);
    m_keyBtn = new KeyCaptureButton(m_keySection);
    connect(m_keyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("key", key); });
    keyForm->addRow(tr("Key:"), m_keyBtn);
    propLayout->addWidget(m_keySection);

    m_switchMapSection = new QWidget(propBox);
    auto *switchMapLayout = new QVBoxLayout(m_switchMapSection);
    switchMapLayout->setContentsMargins(0, 0, 0, 0);
    m_switchMapCheck = new QCheckBox(tr("Free the cursor after the tap"), m_switchMapSection);
    m_switchMapCheck->setToolTip(tr("For keys that open a backpack, map or menu: the mouse can click the phone screen until the key is pressed again"));
    connect(m_switchMapCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_updating || !m_model.hasItem(m_selectedItem)) {
            return;
        }
        QJsonObject obj = m_model.item(m_selectedItem);
        obj.insert("switchMap", checked);
        updateItem(obj);
    });
    switchMapLayout->addWidget(m_switchMapCheck);
    propLayout->addWidget(m_switchMapSection);

    m_steerSection = new QWidget(propBox);
    auto *steerForm = makeForm(m_steerSection);
    m_upKeyBtn = new KeyCaptureButton(m_steerSection);
    m_downKeyBtn = new KeyCaptureButton(m_steerSection);
    m_leftKeyBtn = new KeyCaptureButton(m_steerSection);
    m_rightKeyBtn = new KeyCaptureButton(m_steerSection);
    connect(m_upKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("upKey", key); });
    connect(m_downKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("downKey", key); });
    connect(m_leftKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("leftKey", key); });
    connect(m_rightKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("rightKey", key); });
    steerForm->addRow(tr("Forward:"), m_upKeyBtn);
    steerForm->addRow(tr("Back:"), m_downKeyBtn);
    steerForm->addRow(tr("Left:"), m_leftKeyBtn);
    steerForm->addRow(tr("Right:"), m_rightKeyBtn);
    auto *modifierRow = new QHBoxLayout();
    m_modifierKeyBtn = new KeyCaptureButton(m_steerSection);
    connect(m_modifierKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) { setKeyField("modifierKey", key); });
    modifierRow->addWidget(m_modifierKeyBtn, 1);
    QPushButton *clearModifierBtn = makeButton(tr("None"), m_steerSection);
    connect(clearModifierBtn, &QPushButton::clicked, this, [this] { setKeyField("modifierKey", QString()); });
    modifierRow->addWidget(clearModifierBtn);
    steerForm->addRow(tr("Walk key:"), modifierRow);
    m_modifierScaleSpin = makeSpin(0.05, 2.0, 0.05, 2, m_steerSection);
    m_modifierScaleSpin->setToolTip(tr("How far the stick is pushed while the walk key is held (1 = full)"));
    connect(m_modifierScaleSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_updating || !m_model.hasItem(m_selectedItem)) {
            return;
        }
        QJsonObject obj = m_model.item(m_selectedItem);
        obj.insert("modifierScale", round4(value));
        updateItem(obj);
    });
    steerForm->addRow(tr("Walk push:"), m_modifierScaleSpin);
    propLayout->addWidget(m_steerSection);

    m_mouseSection = new QWidget(propBox);
    auto *mouseForm = makeForm(m_mouseSection);
    m_speedXSpin = makeSpin(0.001, 100.0, 0.05, 3, m_mouseSection);
    m_speedYSpin = makeSpin(0.001, 100.0, 0.05, 3, m_mouseSection);
    auto speedChanged = [this](const QString &field, double value) {
        if (m_updating || !m_model.hasItem(m_selectedItem)) {
            return;
        }
        QJsonObject obj = m_model.item(m_selectedItem);
        obj.insert(field, round4(value));
        updateItem(obj);
    };
    connect(m_speedXSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [speedChanged](double value) { speedChanged("speedRatioX", value); });
    connect(m_speedYSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            [speedChanged](double value) { speedChanged("speedRatioY", value); });
    mouseForm->addRow(tr("Horizontal speed:"), m_speedXSpin);
    mouseForm->addRow(tr("Vertical speed:"), m_speedYSpin);
    auto *speedHint = new QLabel(tr("Larger values turn slower."), m_mouseSection);
    speedHint->setStyleSheet("color: gray;");
    mouseForm->addRow(speedHint);
    m_smallEyesKeyBtn = new KeyCaptureButton(m_mouseSection);
    connect(m_smallEyesKeyBtn, &KeyCaptureButton::keyCaptured, this, [this](const QString &key) {
        QJsonObject obj = m_model.item(KeyMapModel::MouseItem);
        QJsonObject smallEyes = obj.value("smallEyes").toObject();
        smallEyes.insert("key", key);
        obj.insert("smallEyes", smallEyes);
        updateItem(obj);
    });
    mouseForm->addRow(tr("Small eyes key:"), m_smallEyesKeyBtn);
    m_smallEyesBtn = makeButton(QString(), m_mouseSection);
    connect(m_smallEyesBtn, &QPushButton::clicked, this, [this] {
        QJsonObject obj = m_model.item(KeyMapModel::MouseItem);
        if (obj.value("smallEyes").isObject()) {
            obj.remove("smallEyes");
        } else {
            // free look: a tap button the view finger can hold, so it must be inside the view area
            const QRectF bounds = KeyMapModel::mouseBounds(obj);
            QPointF pos = KeyMapModel::readPos(obj, "startPos") + QPointF(0.2, 0.05);
            pos.setX(qBound(bounds.left() + 0.01, pos.x(), bounds.right() - 0.01));
            pos.setY(qBound(bounds.top() + 0.01, pos.y(), bounds.bottom() - 0.01));
            QJsonObject smallEyes;
            smallEyes.insert("comment", tr("small eyes"));
            smallEyes.insert("type", kClick);
            smallEyes.insert("key", "Key_Alt");
            KeyMapModel::writePos(smallEyes, "pos", pos);
            smallEyes.insert("switchMap", false);
            obj.insert("smallEyes", smallEyes);
        }
        updateItem(obj);
    });
    mouseForm->addRow(m_smallEyesBtn);
    propLayout->addWidget(m_mouseSection);

    m_multiSection = new QWidget(propBox);
    auto *multiLayout = new QHBoxLayout(m_multiSection);
    multiLayout->setContentsMargins(0, 0, 0, 0);
    QPushButton *addPointBtn = makeButton(tr("Add a tap point"), m_multiSection);
    connect(addPointBtn, &QPushButton::clicked, this, [this] {
        pushUndo();
        m_model.addMultiClickPoint(m_selectedItem);
        modelChanged();
    });
    multiLayout->addWidget(addPointBtn);
    QPushButton *removePointBtn = makeButton(tr("Remove the last point"), m_multiSection);
    connect(removePointBtn, &QPushButton::clicked, this, [this] {
        pushUndo();
        m_model.removeMultiClickPoint(m_selectedItem);
        if (m_selectedPart >= KeyMapModel::PartMultiFirst) {
            m_selectedPart = KeyMapModel::PartMultiFirst;
        }
        modelChanged();
    });
    multiLayout->addWidget(removePointBtn);
    propLayout->addWidget(m_multiSection);

    m_posSection = new QWidget(propBox);
    auto *posForm = makeForm(m_posSection);
    m_posLabel = new QLabel(m_posSection);
    posForm->addRow(tr("Point:"), m_posLabel);
    auto *posRow = new QHBoxLayout();
    m_posXSpin = makeSpin(0.0, 1.0, 0.001, 4, m_posSection);
    m_posYSpin = makeSpin(0.0, 1.0, 0.001, 4, m_posSection);
    m_posXSpin->setPrefix("x ");
    m_posYSpin->setPrefix("y ");
    auto posChanged = [this](double) {
        if (m_updating || !m_model.hasItem(m_selectedItem) || m_selectedPart < 0) {
            return;
        }
        pushUndo();
        m_model.moveHandle(m_selectedItem, m_selectedPart, QPointF(m_posXSpin->value(), m_posYSpin->value()));
        modelChanged();
    };
    connect(m_posXSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, posChanged);
    connect(m_posYSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this, posChanged);
    posRow->addWidget(m_posXSpin);
    posRow->addWidget(m_posYSpin);
    posForm->addRow(tr("Position:"), posRow);
    propLayout->addWidget(m_posSection);

    propLayout->addWidget(new QLabel(tr("All fields (json):"), propBox));
    m_jsonEdit = new QPlainTextEdit(propBox);
    m_jsonEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_jsonEdit->setMinimumHeight(120);
    m_jsonEdit->setToolTip(tr("Every field of this key, including ones without a control above. See docs/KeyMapDes_zh.md."));
    propLayout->addWidget(m_jsonEdit);
    QPushButton *applyJsonBtn = makeButton(tr("Apply json"), propBox);
    connect(applyJsonBtn, &QPushButton::clicked, this, &KeyMapEditor::onApplyJson);
    propLayout->addWidget(applyJsonBtn);
    sideLayout->addWidget(propBox);

    m_problemLabel = new QLabel(side);
    m_problemLabel->setWordWrap(true);
    m_problemLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sideLayout->addWidget(m_problemLabel);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(side);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(420);
    body->addWidget(scroll);

    auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &KeyMapEditor::onSave);
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &KeyMapEditor::onUndo);
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, &KeyMapEditor::onRedo);
}

void KeyMapEditor::setDeviceSerial(const QString &serial)
{
    m_serial = serial;
    m_captureBtn->setToolTip(serial.isEmpty() ? tr("Takes a screenshot of the phone through adb")
                                              : tr("Takes a screenshot of %1 through adb").arg(serial));
}

void KeyMapEditor::openKeymap(const QString &fileName)
{
    if (fileName.isEmpty() || fileName == m_fileName) {
        return;
    }
    if (!maybeSave()) {
        return;
    }
    loadFile(fileName);
}

void KeyMapEditor::refreshFileBox(const QString &select)
{
    QStringList names;
    const QStringList dirs = { m_keymapDir, m_defaultDir };
    for (const QString &dir : dirs) {
        const QStringList files = QDir(dir).entryList(QStringList() << "*.json", QDir::Files);
        for (const QString &file : files) {
            if (!names.contains(file)) {
                names << file;
            }
        }
    }
    // a new file is not on disk until it is saved
    if (!select.isEmpty() && !names.contains(select)) {
        names << select;
    }
    names.sort(Qt::CaseInsensitive);

    m_fileBox->blockSignals(true);
    m_fileBox->clear();
    m_fileBox->addItems(names);
    m_fileBox->setCurrentIndex(names.indexOf(select));
    m_fileBox->blockSignals(false);
}

bool KeyMapEditor::loadFile(const QString &fileName)
{
    QString path = QDir(m_keymapDir).filePath(fileName);
    if (!QFileInfo::exists(path)) {
        path = QDir(m_defaultDir).filePath(fileName);
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot open %1").arg(path));
        return false;
    }
    QString error;
    KeyMapModel model;
    if (!model.load(file.readAll(), &error)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot read %1: %2").arg(fileName, error));
        return false;
    }

    m_model = model;
    m_fileName = fileName;
    m_undo.clear();
    m_redo.clear();
    m_pendingUndo.clear();
    loadBackground(fileName);
    setDirty(false);
    refreshFileBox(fileName);
    select(KeyMapModel::NoItem, -1);
    refreshList();
    refreshProblems();
    m_canvas->update();
    return true;
}

bool KeyMapEditor::writeFile(const QString &fileName)
{
    QDir().mkpath(m_keymapDir);
    QSaveFile file(QDir(m_keymapDir).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly) || file.write(m_model.toJson()) < 0 || !file.commit()) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1: %2").arg(file.fileName(), file.errorString()));
        return false;
    }
    if (m_backgroundChanged && !m_canvas->background().isNull()) {
        // the screenshot is kept next to the keymap so it shows up again next time
        m_canvas->background().save(imagePathFor(fileName), "PNG");
        m_backgroundChanged = false;
    }
    return true;
}

bool KeyMapEditor::saveCurrent()
{
    if (m_fileName.isEmpty()) {
        const QString name = askFileName(tr("Save keymap"), "my_keymap.json");
        if (name.isEmpty()) {
            return false;
        }
        m_fileName = name;
    }

    const QStringList problems = m_model.problems();
    if (!problems.isEmpty()) {
        const QString text = tr("This keymap has problems, keys with problems may not work:\n\n%1\n\nSave anyway?")
                                 .arg("• " + problems.join("\n• "));
        if (QMessageBox::question(this, windowTitle(), text) != QMessageBox::Yes) {
            return false;
        }
    }
    if (!writeFile(m_fileName)) {
        return false;
    }
    setDirty(false);
    refreshFileBox(m_fileName);
    m_statusLabel->setText(tr("Saved %1").arg(QDir(m_keymapDir).filePath(m_fileName)));
    emit keymapSaved(m_fileName);
    return true;
}

QString KeyMapEditor::askFileName(const QString &title, const QString &suggestion)
{
    while (true) {
        bool ok = false;
        QString name = QInputDialog::getText(this, title, tr("File name:"), QLineEdit::Normal, suggestion, &ok).trimmed();
        if (!ok || name.isEmpty()) {
            return QString();
        }
        if (!name.endsWith(".json", Qt::CaseInsensitive)) {
            name += ".json";
        }
        if (name.contains(QRegularExpression("[\\\\/:*?\"<>|]"))) {
            QMessageBox::warning(this, title, tr("A file name cannot contain \\ / : * ? \" < > |"));
            continue;
        }
        if (name != m_fileName && QFileInfo::exists(QDir(m_keymapDir).filePath(name))
            && QMessageBox::question(this, title, tr("%1 already exists. Replace it?").arg(name)) != QMessageBox::Yes) {
            continue;
        }
        return name;
    }
}

bool KeyMapEditor::maybeSave()
{
    if (!m_dirty) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, windowTitle(), tr("Save the changes to %1?").arg(m_fileName.isEmpty() ? tr("the new keymap") : m_fileName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return saveCurrent();
    }
    return answer == QMessageBox::Discard;
}

QString KeyMapEditor::imagePathFor(const QString &fileName) const
{
    return QDir(m_keymapDir).filePath(QFileInfo(fileName).completeBaseName() + ".png");
}

void KeyMapEditor::loadBackground(const QString &fileName)
{
    QImage image;
    if (image.load(imagePathFor(fileName))) {
        m_canvas->setBackground(image);
        m_backgroundChanged = false;
    } else {
        // keep the current screenshot, and store it with this keymap on save
        m_backgroundChanged = !m_canvas->background().isNull();
    }
}

QString KeyMapEditor::resolveAdbPath() const
{
    // same order as the adb process of QtScrcpyCore
    QStringList candidates;
    candidates << QString::fromLocal8Bit(qgetenv("QTSCRCPY_ADB_PATH")) << Config::getInstance().getAdbPath();
#ifdef Q_OS_WIN32
    candidates << QCoreApplication::applicationDirPath() + "/adb.exe";
#else
    candidates << QCoreApplication::applicationDirPath() + "/adb";
#endif
    for (const QString &path : candidates) {
        if (!path.isEmpty() && QFileInfo(path).isFile()) {
            return path;
        }
    }
    return "adb";
}

void KeyMapEditor::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void KeyMapEditor::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        // QDialog would reject() and skip the unsaved changes check
        close();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // no default button
        break;
    default:
        QDialog::keyPressEvent(event);
        return;
    }
    event->accept();
}

void KeyMapEditor::onFileBoxActivated(int index)
{
    const QString name = m_fileBox->itemText(index);
    if (name == m_fileName) {
        return;
    }
    if (!maybeSave() || !loadFile(name)) {
        refreshFileBox(m_fileName);
    }
}

void KeyMapEditor::onNewFile()
{
    if (!maybeSave()) {
        refreshFileBox(m_fileName);
        return;
    }
    const QString name = askFileName(tr("New keymap"), "my_game.json");
    if (name.isEmpty()) {
        return;
    }
    // start with what every shooter needs
    m_model.reset();
    m_model.addItem(kMouseMove, QPointF(0.57, 0.3));
    m_model.addItem(kSteerWheel, QPointF(0.16, 0.75));
    m_fileName = name;
    m_undo.clear();
    m_redo.clear();
    m_backgroundChanged = !m_canvas->background().isNull();
    refreshFileBox(name);
    select(KeyMapModel::NoItem, -1);
    modelChanged();
    m_statusLabel->setText(tr("New keymap: drag the stick and the view point onto the game, then right click to add keys"));
}

void KeyMapEditor::onSaveAs()
{
    const QString name = askFileName(tr("Save keymap as"), m_fileName.isEmpty() ? QString("my_keymap.json") : m_fileName);
    if (name.isEmpty()) {
        return;
    }
    const QString oldName = m_fileName;
    const bool oldBackgroundChanged = m_backgroundChanged;
    m_fileName = name;
    m_backgroundChanged = !m_canvas->background().isNull();
    if (!saveCurrent()) {
        m_fileName = oldName;
        m_backgroundChanged = oldBackgroundChanged;
        refreshFileBox(m_fileName);
    }
    updateTitle();
}

void KeyMapEditor::onSave()
{
    saveCurrent();
}

void KeyMapEditor::onCapture()
{
    if (m_captureProcess->state() != QProcess::NotRunning) {
        return;
    }
    QStringList args;
    if (!m_serial.isEmpty()) {
        args << "-s" << m_serial;
    }
    args << "exec-out" << "screencap" << "-p";
    m_captureBtn->setEnabled(false);
    m_statusLabel->setText(tr("Capturing the phone screen..."));
    m_captureTimeout->start();
    m_captureProcess->start(resolveAdbPath(), args);
}

void KeyMapEditor::onCaptureFinished()
{
    m_captureTimeout->stop();
    m_captureBtn->setEnabled(true);
    QImage image;
    if (!image.loadFromData(m_captureProcess->readAllStandardOutput(), "PNG")) {
        const QString error = QString::fromLocal8Bit(m_captureProcess->readAllStandardError()).trimmed();
        m_statusLabel->setText(QString());
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not capture the phone screen. Is the phone connected and USB debugging allowed?\n\n%1").arg(error));
        return;
    }
    m_canvas->setBackground(image);
    m_backgroundChanged = true;
    setDirty(true);
    if (image.height() > image.width()) {
        m_statusLabel->setText(tr("The screenshot is portrait. Key positions follow the screen orientation, so open the game first and capture again."));
    } else {
        m_statusLabel->setText(tr("Captured %1 x %2").arg(image.width()).arg(image.height()));
    }
}

void KeyMapEditor::onOpenImage()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open image"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    QImage image;
    if (!image.load(path)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot open %1").arg(path));
        return;
    }
    m_canvas->setBackground(image);
    m_backgroundChanged = true;
    setDirty(true);
}

void KeyMapEditor::onListRowChanged(int row)
{
    if (m_updating || row < 0) {
        return;
    }
    select(m_itemList->item(row)->data(Qt::UserRole).toInt(), -1);
}

void KeyMapEditor::onHandlePressed(int item, int part)
{
    m_pendingUndo = m_model.toJson();
    select(item, part);
}

void KeyMapEditor::onHandleDragged(int item, int part, const QPointF &pos)
{
    if (!m_pendingUndo.isEmpty()) {
        m_undo.append(m_pendingUndo);
        m_redo.clear();
        m_pendingUndo.clear();
    }
    m_model.moveHandle(item, part, pos);
    m_selectedPart = part;
    modelChanged();
}

void KeyMapEditor::onEmptyPressed()
{
    select(KeyMapModel::NoItem, -1);
}

void KeyMapEditor::onAddRequested(const QString &type, const QPointF &pos)
{
    pushUndo();
    const int item = m_model.addItem(type, pos);
    if (item == KeyMapModel::NoItem) {
        return;
    }
    select(item, -1);
    modelChanged();
    // a new key has no key yet: record it right away
    if (m_keySection->isVisible()) {
        m_keyBtn->startCapture();
    }
}

void KeyMapEditor::onDeleteSelected()
{
    if (!m_model.hasItem(m_selectedItem)) {
        return;
    }
    pushUndo();
    m_model.removeItem(m_selectedItem);
    select(KeyMapModel::NoItem, -1);
    modelChanged();
}

void KeyMapEditor::onNudge(const QPointF &delta)
{
    if (!m_model.hasItem(m_selectedItem) || m_selectedPart < 0) {
        return;
    }
    pushUndo();
    m_model.moveHandle(m_selectedItem, m_selectedPart, m_model.handlePos(m_selectedItem, m_selectedPart) + delta);
    modelChanged();
}

void KeyMapEditor::onHovered(const QPointF &pos, bool inside)
{
    if (!inside) {
        return;
    }
    const QSize size = m_canvas->screenSize();
    m_statusLabel->setText(tr("x %1   y %2   (%3, %4 px)")
                               .arg(pos.x(), 0, 'f', 3)
                               .arg(pos.y(), 0, 'f', 3)
                               .arg(qRound(pos.x() * size.width()))
                               .arg(qRound(pos.y() * size.height())));
}

void KeyMapEditor::onApplyJson()
{
    if (!m_model.hasItem(m_selectedItem)) {
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(m_jsonEdit->toPlainText().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Not a valid json object: %1").arg(error.error != QJsonParseError::NoError ? error.errorString() : QString()));
        return;
    }
    updateItem(doc.object());
}

void KeyMapEditor::onUndo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    m_redo.append(m_model.toJson());
    m_model.load(m_undo.takeLast(), nullptr);
    if (!m_model.hasItem(m_selectedItem)) {
        select(KeyMapModel::NoItem, -1);
    }
    modelChanged();
}

void KeyMapEditor::onRedo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    m_undo.append(m_model.toJson());
    m_model.load(m_redo.takeLast(), nullptr);
    if (!m_model.hasItem(m_selectedItem)) {
        select(KeyMapModel::NoItem, -1);
    }
    modelChanged();
}

void KeyMapEditor::select(int item, int part)
{
    if (!m_model.hasItem(item)) {
        item = KeyMapModel::NoItem;
        part = -1;
    } else if (part < 0) {
        part = m_model.primaryPart(item);
    }
    m_selectedItem = item;
    m_selectedPart = part;
    m_canvas->setSelection(item, part);

    m_updating = true;
    const QVector<int> ids = m_model.items();
    m_itemList->setCurrentRow(ids.indexOf(item));
    m_updating = false;
    refreshProperties();
}

void KeyMapEditor::refreshList()
{
    m_updating = true;
    m_itemList->clear();
    const QVector<int> ids = m_model.items();
    for (int id : ids) {
        auto *row = new QListWidgetItem(m_model.itemTitle(id), m_itemList);
        row->setData(Qt::UserRole, id);
        row->setToolTip(row->text());
    }
    m_itemList->setCurrentRow(ids.indexOf(m_selectedItem));
    m_updating = false;
}

void KeyMapEditor::refreshProperties()
{
    m_updating = true;
    m_switchKeyBtn->setKeyName(m_model.switchKey());

    const bool hasSelection = m_model.hasItem(m_selectedItem);
    m_propPanel->setEnabled(hasSelection);
    const QJsonObject obj = m_model.item(m_selectedItem);
    const QString type = m_model.itemType(m_selectedItem);
    const bool isMouse = hasSelection && m_selectedItem == KeyMapModel::MouseItem;
    const bool isSteer = hasSelection && type == kSteerWheel;

    m_typeLabel->setText(hasSelection ? KeyMapModel::typeName(type) : tr("Click a key on the screen or in the list"));
    m_commentEdit->setText(obj.value("comment").toString());

    m_keySection->setVisible(hasSelection && !isMouse && !isSteer);
    m_keyBtn->setKeyName(obj.value("key").toString());

    m_switchMapSection->setVisible(hasSelection && type == kClick);
    m_switchMapCheck->setChecked(obj.value("switchMap").toBool());

    m_steerSection->setVisible(isSteer);
    m_upKeyBtn->setKeyName(obj.value("upKey").toString());
    m_downKeyBtn->setKeyName(obj.value("downKey").toString());
    m_leftKeyBtn->setKeyName(obj.value("leftKey").toString());
    m_rightKeyBtn->setKeyName(obj.value("rightKey").toString());
    m_modifierKeyBtn->setKeyName(obj.value("modifierKey").toString());
    m_modifierScaleSpin->setValue(obj.value("modifierScale").toDouble(0.5));

    m_mouseSection->setVisible(isMouse);
    // same fallbacks as the keymap parser
    const double speedRatio = obj.value("speedRatio").toDouble(1.0);
    m_speedXSpin->setValue(obj.value("speedRatioX").toDouble(speedRatio));
    m_speedYSpin->setValue(obj.value("speedRatioY").toDouble(speedRatio / 2.25));
    const bool hasSmallEyes = obj.value("smallEyes").isObject();
    m_smallEyesKeyBtn->setEnabled(hasSmallEyes);
    m_smallEyesKeyBtn->setKeyName(obj.value("smallEyes").toObject().value("key").toString());
    m_smallEyesBtn->setText(hasSmallEyes ? tr("Remove small eyes (free look)") : tr("Add small eyes (free look)"));

    m_multiSection->setVisible(hasSelection && type == kClickMulti);

    m_posSection->setVisible(hasSelection && m_selectedPart >= 0);
    if (m_selectedPart >= 0) {
        QString name;
        switch (m_selectedPart) {
        case KeyMapModel::PartDragStart:
            name = tr("swipe start");
            break;
        case KeyMapModel::PartDragEnd:
            name = tr("swipe end");
            break;
        case KeyMapModel::PartSteerCenter:
            name = tr("stick center");
            break;
        case KeyMapModel::PartSteerUp:
            name = tr("forward push");
            break;
        case KeyMapModel::PartSteerDown:
            name = tr("back push");
            break;
        case KeyMapModel::PartSteerLeft:
            name = tr("left push");
            break;
        case KeyMapModel::PartSteerRight:
            name = tr("right push");
            break;
        case KeyMapModel::PartMouseStart:
            name = tr("view finger start");
            break;
        case KeyMapModel::PartBoundsTopLeft:
            name = tr("view area top left");
            break;
        case KeyMapModel::PartBoundsBottomRight:
            name = tr("view area bottom right");
            break;
        case KeyMapModel::PartSmallEyes:
            name = tr("small eyes button");
            break;
        default:
            name = m_selectedPart >= KeyMapModel::PartMultiFirst ? tr("tap point %1").arg(m_selectedPart - KeyMapModel::PartMultiFirst + 1) : tr("button");
            break;
        }
        m_posLabel->setText(name);
        const QPointF pos = m_model.handlePos(m_selectedItem, m_selectedPart);
        m_posXSpin->setValue(pos.x());
        m_posYSpin->setValue(pos.y());
    }

    m_jsonEdit->setPlainText(hasSelection ? QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)) : QString());
    m_updating = false;
}

void KeyMapEditor::refreshProblems()
{
    const QStringList problems = m_model.problems();
    if (problems.isEmpty()) {
        m_problemLabel->setStyleSheet("color: #2e7d32;");
        m_problemLabel->setText(tr("No problems found."));
    } else {
        m_problemLabel->setStyleSheet("color: #e65100;");
        m_problemLabel->setText("• " + problems.join("\n• "));
    }
}

void KeyMapEditor::modelChanged()
{
    setDirty(true);
    refreshList();
    refreshProperties();
    refreshProblems();
    m_canvas->setSelection(m_selectedItem, m_selectedPart);
}

void KeyMapEditor::pushUndo()
{
    m_undo.append(m_model.toJson());
    if (m_undo.size() > kMaxUndo) {
        m_undo.removeFirst();
    }
    m_redo.clear();
    m_pendingUndo.clear();
}

void KeyMapEditor::setDirty(bool dirty)
{
    m_dirty = dirty;
    updateTitle();
}

void KeyMapEditor::updateTitle()
{
    setWindowTitle(QString("%1 - %2%3").arg(tr("Keymap Editor"), m_fileName.isEmpty() ? tr("untitled") : m_fileName, m_dirty ? " *" : ""));
}

void KeyMapEditor::updateItem(const QJsonObject &obj)
{
    if (!m_model.hasItem(m_selectedItem)) {
        return;
    }
    pushUndo();
    m_model.setItem(m_selectedItem, obj);
    if (m_selectedPart >= 0 && m_model.primaryPart(m_selectedItem) < 0) {
        m_selectedPart = -1;
    }
    modelChanged();
}

void KeyMapEditor::setKeyField(const QString &field, const QString &key)
{
    if (!m_model.hasItem(m_selectedItem)) {
        return;
    }
    QJsonObject obj = m_model.item(m_selectedItem);
    if (key.isEmpty()) {
        if (!obj.contains(field)) {
            return;
        }
        obj.remove(field);
    } else {
        obj.insert(field, key);
    }
    updateItem(obj);
}

QPointF KeyMapEditor::newItemPos() const
{
    // spread new keys a little so they do not stack exactly
    const int n = m_model.nodeCount() % 8;
    return QPointF(0.45 + 0.02 * n, 0.45 + 0.02 * n);
}
