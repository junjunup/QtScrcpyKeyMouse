#include <QEvent>
#include <QPainter>

#include "keyhintoverlay.h"
#include "keymapeditor.h"

KeyHintOverlay::KeyHintOverlay(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    m_messageTimer.setSingleShot(true);
    m_messageTimer.setInterval(1800);
    connect(&m_messageTimer, &QTimer::timeout, this, [this]() {
        m_message.clear();
        update();
    });
    if (parent) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    raise();
}

void KeyHintOverlay::setStatus(const qsc::KeyMapStatus &status)
{
    const qsc::KeyMapStatus old = m_status;
    m_status = status;

    QStringList parts;
    if (status.customKeymap && status.mapping != old.mapping) {
        parts << (status.mapping ? tr("Key mapping on") : tr("Key mapping off"));
    }
    if (status.mapping && !status.scene.isEmpty() && (status.scene != old.scene || status.mapping != old.mapping)) {
        parts << tr("Scene: %1").arg(status.scene);
    }
    if (status.mapping && old.mapping && status.shooting != old.shooting) {
        parts << (status.shooting ? tr("Shooting mode: the mouse turns the view") : tr("Cursor free: the mouse clicks the screen"));
    }
    if (!parts.isEmpty()) {
        showMessage(parts.join("  ·  "));
    }
    update();
}

void KeyHintOverlay::setShowHints(bool show)
{
    m_showHints = show;
    update();
}

void KeyHintOverlay::showMessage(const QString &text)
{
    m_message = text;
    m_messageTimer.start();
    raise();
    update();
}

bool KeyHintOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void KeyHintOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_status.mapping && m_showHints) {
        QFont font = this->font();
        font.setBold(true);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        for (const qsc::KeyMapHint &hint : m_status.hints) {
            const QString label = KeyMapModel::shortKeyLabel(hint.key);
            const QPointF center(hint.pos.x() * width(), hint.pos.y() * height());
            const qreal h = metrics.height() + 4;
            const qreal w = qMax(h, metrics.horizontalAdvance(label) + 10);
            const QRectF box(center.x() - w / 2, center.y() - h / 2, w, h);
            // faint, so the game stays readable underneath
            painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
            painter.setBrush(QColor(0, 0, 0, 90));
            painter.drawRoundedRect(box, h / 2, h / 2);
            painter.setPen(QColor(255, 255, 255, 190));
            painter.drawText(box, Qt::AlignCenter, label);
        }
    }

    // corner badge: which scene, and whether the mouse turns the view
    if (m_status.mapping) {
        QStringList badge;
        if (!m_status.scene.isEmpty()) {
            badge << m_status.scene;
        }
        badge << (m_status.shooting ? tr("view") : tr("cursor"));
        const QString text = badge.join(" · ");
        const QFontMetricsF metrics(font());
        const QRectF box(8, height() - metrics.height() - 14, metrics.horizontalAdvance(text) + 16, metrics.height() + 6);
        painter.setFont(font());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 120));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(QColor(255, 255, 255, 200));
        painter.drawText(box, Qt::AlignCenter, text);
    }

    if (!m_message.isEmpty()) {
        QFont font = this->font();
        font.setPointSizeF(font.pointSizeF() * 1.3);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        const qreal w = metrics.horizontalAdvance(m_message) + 32;
        const qreal h = metrics.height() + 16;
        const QRectF box((width() - w) / 2, height() * 0.12, w, h);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(box, 8, 8);
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, m_message);
    }
}
