#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

  
    QRegularExpression hexRegex("^[0-9A-Fa-f]{16}$");
    ui->leHexMask->setText("1234567890ABCDEF");

    connect(ui->btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->btnPause, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(ui->btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::onTimerTimeout);

    ui->btnPause->setEnabled(false);
    ui->btnStop->setEnabled(false);
}

MainWindow::~MainWindow() {
    delete ui;
}

ProcessingConfig MainWindow::readConfig(bool* ok) {
    ProcessingConfig cfg;
    cfg.inputDir = ui->leInputDir->text();
    cfg.outputDir = ui->leOutputDir->text();
    cfg.fileMask = ui->leFileMask->text();
    cfg.deleteSource = ui->chkDeleteSource->isChecked();
    cfg.collisionPolicy = ui->radioOverwrite->isChecked()
        ? CollisionPolicy::Overwrite
        : CollisionPolicy::AppendCounter;
    cfg.periodic = ui->chkPeriodic->isChecked();
    cfg.intervalMs = ui->spnIntervalSec->value() * 1000;

    QString hexStr = ui->leHexMask->text().trimmed();
    bool parseOk = false;
    cfg.xorKey = hexStr.toULongLong(&parseOk, 16);

    if (!parseOk || hexStr.length() != 16) {
        QMessageBox::warning(this, "Ошибка", "HEX-маска должна содержать ровно 16 символов (8 байт)!");
        *ok = false;
        return cfg;
    }
    *ok = true;
    return cfg;
}

void MainWindow::onStartClicked() {
    bool ok = false;
    ProcessingConfig cfg = readConfig(&ok);
    if (!ok) return;

    if (cfg.periodic) {
        m_pollTimer.setInterval(cfg.intervalMs);
        m_pollTimer.start();
        ui->lblStatus->setText("Запущен периодический опрос каталога...");
        setupUIState(true);
        return;
    }

    setupUIState(true);

    m_thread = new QThread(this);
    m_worker = new Worker(cfg);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &Worker::process);
    connect(m_worker, &Worker::finished, this, &MainWindow::onWorkerFinished);
    connect(m_worker, &Worker::finished, m_thread, &QThread::quit);
    connect(m_worker, &Worker::finished, m_worker, &Worker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    connect(m_worker, &Worker::fileProgress, ui->progressBarFile, &QProgressBar::setValue);
    connect(m_worker, &Worker::statusUpdated, ui->lblStatus, &QLabel::setText);

    m_thread->start();
}

void MainWindow::onTimerTimeout() {
    if (m_thread && m_thread->isRunning()) return; // Пропускаем тик, если предыдущая итерация еще идет

    bool ok = false;
    ProcessingConfig cfg = readConfig(&ok);
    if (!ok) return;

    m_thread = new QThread(this);
    m_worker = new Worker(cfg);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &Worker::process);
    connect(m_worker, &Worker::finished, m_worker, &Worker::deleteLater);
    connect(m_worker, &Worker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_worker, &Worker::statusUpdated, ui->lblStatus, &QLabel::setText);
    connect(m_worker, &Worker::fileProgress, ui->progressBarFile, &QProgressBar::setValue);

    m_thread->start();
}

void MainWindow::onPauseClicked() {
    if (!m_worker) return;
    if (!m_isPaused) {
        m_worker->pause();
        ui->btnPause->setText("Продолжить");
        m_isPaused = true;
    }
    else {
        m_worker->resume();
        ui->btnPause->setText("Пауза");
        m_isPaused = false;
    }
}

void MainWindow::onStopClicked() {
    if (m_pollTimer.isActive()) {
        m_pollTimer.stop();
    }
    if (m_worker) {
        m_worker->stop();
    }
    setupUIState(false);
}

void MainWindow::onWorkerFinished() {
    if (!ui->chkPeriodic->isChecked()) {
        setupUIState(false);
    }
}

void MainWindow::setupUIState(bool running) {
    ui->btnStart->setEnabled(!running);
    ui->btnPause->setEnabled(running);
    ui->btnStop->setEnabled(running);
    ui->grpConfig->setEnabled(!running);
    m_isPaused = false;
    ui->btnPause->setText("Пауза");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_pollTimer.isActive()) {
        m_pollTimer.stop();
    }
    if (m_worker && m_thread && m_thread->isRunning()) {
        m_worker->stop();
        m_thread->quit();
        // Даем потоку безопасно выйти из цикла и закрыть дескрипторы файлов
        if (!m_thread->wait(3000)) {
            m_thread->terminate();
        }
    }
    event->accept();
}