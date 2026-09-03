#pragma once

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "settings.h"

class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(const ProcessingConfig& config, QObject* parent = nullptr);

public slots:
    void process();
    void pause();
    void resume();
    void stop();

signals:
    void statusUpdated(const QString& message);
    void fileProgress(int percentage);
    void overallProgress(int currentFile, int totalFiles);
    void finished();

private:
    void processSingleFile(const QString& filePath);
    QString resolveOutputPath(const QString& sourcePath) const;

    ProcessingConfig m_config;
    std::atomic<bool> m_isPaused{ false };
    std::atomic<bool> m_isStopped{ false };
    QMutex m_pauseMutex;
    QWaitCondition m_pauseCondition;
};