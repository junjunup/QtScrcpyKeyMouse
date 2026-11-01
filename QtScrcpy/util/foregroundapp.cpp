#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

#include "config.h"
#include "foregroundapp.h"

QString adbExecutablePath()
{
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

QStringList foregroundAppArgs(const QString &serial)
{
    QStringList args;
    if (!serial.isEmpty()) {
        args << "-s" << serial;
    }
    // filtered on the device, the whole dump is large
    args << "shell" << "dumpsys window | grep -E 'mCurrentFocus|mFocusedApp'";
    return args;
}

QString parseForegroundPackage(const QString &output)
{
    // mCurrentFocus=Window{1a2b u0 com.example.game/com.example.MainActivity}
    // mFocusedApp=ActivityRecord{3c4d u0 com.example.game/.MainActivity t12}
    static const QRegularExpression component("\\s([A-Za-z][\\w]*(?:\\.[\\w]+)+)/[\\w.$]+");
    const QStringList keys = { "mCurrentFocus", "mFocusedApp" };
    const QStringList lines = output.split('\n');
    for (const QString &key : keys) {
        for (const QString &line : lines) {
            if (!line.contains(key)) {
                continue;
            }
            const QRegularExpressionMatch match = component.match(line);
            if (match.hasMatch()) {
                return match.captured(1);
            }
        }
    }
    return QString();
}
