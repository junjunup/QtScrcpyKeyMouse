#ifndef KEYHINTOVERLAY_H
#define KEYHINTOVERLAY_H

#include <QTimer>
#include <QWidget>

#include "../QtScrcpyCore/include/QtScrcpyCoreDef.h"

// Drawn over the video: the keys of the current scene, a corner badge with the
// scene, and a short message when mapping, shooting mode or the scene changes.
// It follows the size of its parent (the video widget) and lets input through.
class KeyHintOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit KeyHintOverlay(QWidget *parent);

    void setStatus(const qsc::KeyMapStatus &status);
    void setShowHints(bool show);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void showMessage(const QString &text);

    qsc::KeyMapStatus m_status;
    bool m_showHints = true;
    QString m_message;
    QTimer m_messageTimer;
};

#endif // KEYHINTOVERLAY_H
