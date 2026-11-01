#ifndef FOREGROUNDAPP_H
#define FOREGROUNDAPP_H

#include <QString>
#include <QStringList>

// adb executable, in the same order as the adb process of QtScrcpyCore:
// QTSCRCPY_ADB_PATH, the configured path, the bundled one, then PATH
QString adbExecutablePath();

// adb arguments that print the focused window of the device
QStringList foregroundAppArgs(const QString &serial);

// package of the focused app in that output, empty when there is none
// (screen off, a system popup)
QString parseForegroundPackage(const QString &output);

#endif // FOREGROUNDAPP_H
