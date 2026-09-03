#include "worker.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

constexpr qint64 CHUNK_SIZE = 4 * 1024 * 1024; // 4 MB

Worker::Worker(const ProcessingConfig& config, QObject* parent)
    : QObject(parent), m_config(config) {}

void Worker::pause() {
    m_isPaused.store(true);
    emit statusUpdated("Приостановлено пользователем");
}

void Worker::resume() {
    {
        QMutexLocker locker(&m_pauseMutex);
        m_isPaused.store(false);
    }
    m_pauseCondition.wakeAll();
    emit statusUpdated("Возобновление обработки");
}

void Worker::stop() {
    m_isStopped.store(true);
    m_isPaused.store(false);
    m_pauseCondition.wakeAll();
    emit statusUpdated("Остановка процесса...");
}

void Worker::process() {
    QDir dir(m_config.inputDir);
    QStringList filters = m_config.fileMask.split(';', Qt::SkipEmptyParts);
    if (filters.isEmpty()) filters << "*.*";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    int total = files.size();

    if (total == 0) {
        emit statusUpdated("Файлы по маске не найдены.");
        emit finished();
        return;
    }

    for (int i = 0; i < total && !m_isStopped.load(); ++i) {
        emit overallProgress(i + 1, total);
        processSingleFile(files[i].absoluteFilePath());
    }

    if (!m_isStopped.load()) {
        emit statusUpdated("Обработка всех файлов завершена.");
    }
    emit finished();
}

QString Worker::resolveOutputPath(const QString& sourcePath) const {
    QFileInfo fi(sourcePath);
    QString baseName = fi.completeBaseName();
    QString suffix = fi.suffix().isEmpty() ? "" : "." + fi.suffix();
    QString target = QDir(m_config.outputDir).filePath(fi.fileName());

    if (m_config.collisionPolicy == CollisionPolicy::AppendCounter) {
        int counter = 1;
        while (QFile::exists(target)) {
            target = QDir(m_config.outputDir).filePath(
                QString("%1_%2%3").arg(baseName).arg(counter++).arg(suffix)
            );
        }
    }
    return target;
}

void Worker::processSingleFile(const QString& filePath) {
    QFile inFile(filePath);
    if (!inFile.open(QIODevice::ReadOnly)) {
        emit statusUpdated("Ошибка открытия входного файла: " + filePath);
        return;
    }

    QString outPath = resolveOutputPath(filePath);
    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit statusUpdated("Ошибка создания выходного файла: " + outPath);
        inFile.close();
        return;
    }

    qint64 totalBytes = inFile.size();
    qint64 bytesProcessed = 0;
    QByteArray buffer(CHUNK_SIZE, Qt::Uninitialized);

    emit statusUpdated("Обработка файла: " + QFileInfo(filePath).fileName());

    while (!inFile.atEnd() && !m_isStopped.load()) {
        // Проверка паузы
        {
            QMutexLocker locker(&m_pauseMutex);
            while (m_isPaused.load() && !m_isStopped.load()) {
                m_pauseCondition.wait(&m_pauseMutex);
            }
        }

        if (m_isStopped.load()) {
            break;
        }

        qint64 bytesRead = inFile.read(buffer.data(), CHUNK_SIZE);
        if (bytesRead <= 0) break;

        // XOR 8-байтными словами
        uint64_t* words = reinterpret_cast<uint64_t*>(buffer.data());
        qint64 wordCount = bytesRead / sizeof(uint64_t);
        for (qint64 i = 0; i < wordCount; ++i) {
            words[i] ^= m_config.xorKey;
        }

        // Обработка остатка, если размер блока не кратен 8 байтам
        qint64 remainder = bytesRead % sizeof(uint64_t);
        if (remainder > 0) {
            char* tail = buffer.data() + (wordCount * sizeof(uint64_t));
            const char* keyBytes = reinterpret_cast<const char*>(&m_config.xorKey);
            for (qint64 j = 0; j < remainder; ++j) {
                tail[j] ^= keyBytes[j];
            }
        }

        outFile.write(buffer.constData(), bytesRead);
        bytesProcessed += bytesRead;

        if (totalBytes > 0) {
            int progress = static_cast<int>((bytesProcessed * 100) / totalBytes);
            emit fileProgress(progress);
        }
    }

    inFile.close();
    outFile.close();


    if (m_isStopped.load()) {
        QFile::remove(outPath);
        return;
    }


    if (m_config.deleteSource) {
        QFile::remove(filePath);
    }
}