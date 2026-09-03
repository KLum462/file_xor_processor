#pragma once

#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include "worker.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStartClicked();
    void onPauseClicked();
    void onStopClicked();
    void onTimerTimeout();
    void onWorkerFinished();

private:
    Ui::MainWindow* ui;
    QThread* m_thread = nullptr;
    Worker* m_worker = nullptr;
    QTimer m_pollTimer;
    bool m_isPaused = false;

    ProcessingConfig readConfig(bool* ok);
    void setupUIState(bool running);
};