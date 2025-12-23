#include <QMouseEvent>
#include "seizure_analyzer.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDebug>
#include <QTextStream>
#include <QDataStream>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QMessageBox>
#include <QDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QTimeZone>

// These GUI display constants must match the FPGA/ASIC datapath configuration.
// See fpga/halo_seizure datapath `define`s:
//   THRESHOLD_VALUE, WINDOW_TIMEOUT, TRANSITION_COUNT, CHANNELS_PER_PACKET.
static const int kCfgThresholdValue    = 25000;
static const int kCfgWindowTimeout     = 200;
static const int kCfgTransitionCount   = 30;
static const int kCfgChannelsPerPacket = 32;

#include <QVector>
#include <chrono>
#include <algorithm>

SeizureAnalyzer::SeizureAnalyzer(QWidget *parent)
    : QMainWindow(parent)
    , centralWidget(nullptr)
    , fileWatcher(nullptr)
    , updateTimer(nullptr)
{
    // Get the directory where the executable is located
    QString appDir = QCoreApplication::applicationDirPath();
    
    // Check if we're running from the build directory or from data-analyser directory
    if (appDir.contains("build/seizure_analyzer.app/Contents/MacOS")) {
        logsDirectory = QDir(appDir).absoluteFilePath("../../../../logs");
    } else {
        logsDirectory = QDir(appDir).absoluteFilePath("logs");
    }
    
    setupUI();
    scanLogFiles();
    
    // Set up file watcher
    fileWatcher = new QFileSystemWatcher(this);
    fileWatcher->addPath(logsDirectory);
    connect(fileWatcher, &QFileSystemWatcher::directoryChanged, this, &SeizureAnalyzer::onFileChanged);
    
    // Set up update timer (check every 5 seconds)
    
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &SeizureAnalyzer::updateDisplay);
    updateTimer->start(5000);
    
    setWindowTitle("Seizure Detection Analyzer");
    setMinimumSize(800, 600);
}

SeizureAnalyzer::~SeizureAnalyzer()
{
}

void SeizureAnalyzer::setupUI()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    mainLayout = new QVBoxLayout(centralWidget);
    
    // Button layout
    buttonLayout = new QHBoxLayout();
    reloadButton = new QPushButton("Reload Data", this);
    connect(reloadButton, &QPushButton::clicked, this, &SeizureAnalyzer::reloadData);
    buttonLayout->addWidget(reloadButton);
    
    // Channel selector (multi-select popup with checkboxes)
    buttonLayout->addWidget(new QLabel("Channels:", this));
    channelButton = new QPushButton("Select Channels", this);
    channelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    buttonLayout->addWidget(channelButton);

    channelPopup = new QWidget(this, Qt::Popup);
    channelPopup->setWindowFlag(Qt::FramelessWindowHint);
    QVBoxLayout *popupLayout = new QVBoxLayout(channelPopup);
    popupLayout->setContentsMargins(4,4,4,4);
    channelList = new QListWidget(channelPopup);
    channelList->setSelectionMode(QAbstractItemView::NoSelection);
    for (int i = 0; i < 32; ++i) {
        QString channelName = QString("A-%1").arg(i, 3, 10, QChar('0'));
        QListWidgetItem *item = new QListWidgetItem(channelName, channelList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    channelList->viewport()->installEventFilter(this); // toggle without closing popup
    popupLayout->addWidget(channelList);
    connect(channelList, &QListWidget::itemChanged, this, &SeizureAnalyzer::onChannelItemChanged);
    connect(channelButton, &QPushButton::clicked, this, &SeizureAnalyzer::showChannelPopup);
    
    buttonLayout->addStretch();
    
    // Stats layout
    statsLayout = new QGridLayout();
    totalSeizuresLabel = new QLabel("Total Seizures: 0", this);
    todaySeizuresLabel = new QLabel("Today: 0", this);
    monthlySeizuresLabel = new QLabel("This Month: 0", this);
    lastUpdateLabel = new QLabel("Last Update: Never", this);
    
    totalSeizuresLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    todaySeizuresLabel->setStyleSheet("font-size: 14px;");
    monthlySeizuresLabel->setStyleSheet("font-size: 14px;");
    lastUpdateLabel->setStyleSheet("font-size: 12px; color: gray;");

    // Detection configuration (matches FPGA/ASIC settings)
    thresholdLabel = new QLabel(QString("THR = %1 (Max ADC = 65535)").arg(kCfgThresholdValue), this);
    windowTimeoutLabel = new QLabel(QString("WINDOW_TIMEOUT = %1 samples").arg(kCfgWindowTimeout), this);
    transitionCountLabel = new QLabel(QString("TRANSITION_COUNT = %1").arg(kCfgTransitionCount), this);
    channelsPerPacketLabel = new QLabel(QString("CHANNELS = %1").arg(kCfgChannelsPerPacket), this);
    QString cfgStyle = "font-size: 11px; color: gray;";
    thresholdLabel->setStyleSheet(cfgStyle);
    windowTimeoutLabel->setStyleSheet(cfgStyle);
    transitionCountLabel->setStyleSheet(cfgStyle);
    channelsPerPacketLabel->setStyleSheet(cfgStyle);
    
    statsLayout->addWidget(totalSeizuresLabel,      0, 0);
    statsLayout->addWidget(todaySeizuresLabel,      0, 1);
    statsLayout->addWidget(monthlySeizuresLabel,    0, 2);
    statsLayout->addWidget(lastUpdateLabel,         0, 3);
    statsLayout->addWidget(thresholdLabel,          1, 0);
    statsLayout->addWidget(windowTimeoutLabel,      1, 1);
    statsLayout->addWidget(transitionCountLabel,    1, 2);
    statsLayout->addWidget(channelsPerPacketLabel,  1, 3);
    
    // Daily counts table
    QLabel *dailyLabel = new QLabel("Daily Counts:", this);
    dailyLabel->setStyleSheet("font-weight: bold;");
    
    dailyCountsTable = new QTableWidget(0, 2, this);
    dailyCountsTable->setHorizontalHeaderLabels({"Date", "Seizure Count"});
    dailyCountsTable->horizontalHeader()->setStretchLastSection(true);
    dailyCountsTable->setAlternatingRowColors(true);
    dailyCountsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dailyCountsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(dailyCountsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SeizureAnalyzer::onDailySelectionChanged);
    
    // Latest detections table (will show all for selected day, or all days if none selected)
    QLabel *latestLabel = new QLabel("Detections:", this);
    latestLabel->setStyleSheet("font-weight: bold;");
    
    latestDetectionsTable = new QTableWidget(0, 6, this);
    latestDetectionsTable->setHorizontalHeaderLabels({"Channel", "Start", "End", "Duration (s)", "File", "RAW Waveform"});
    latestDetectionsTable->horizontalHeader()->setStretchLastSection(true);
    latestDetectionsTable->setAlternatingRowColors(true);
    latestDetectionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    latestDetectionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(latestDetectionsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SeizureAnalyzer::onDetectionSelectionChanged);
    
    // Add to main layout
    mainLayout->addLayout(buttonLayout);
    mainLayout->addLayout(statsLayout);
    mainLayout->addWidget(dailyLabel);
    mainLayout->addWidget(dailyCountsTable);
    mainLayout->addWidget(latestLabel);
    mainLayout->addWidget(latestDetectionsTable);
}

void SeizureAnalyzer::reloadData()
{
    scanLogFiles();
    updateDisplay();
}

void SeizureAnalyzer::updateDisplay()
{
    updateSeizureCounts();
    updateLatestDetections();
    updateDailyCounts();
    updateChannelData();
    lastUpdateLabel->setText("Last Update: " + QDateTime::currentDateTime().toString("hh:mm:ss"));
}

void SeizureAnalyzer::onFileChanged(const QString &path)
{
    Q_UNUSED(path)
    // File system changed, reload data
    reloadData();
}

void SeizureAnalyzer::scanLogFiles()
{
    allDetections.clear();
    dailyCounts.clear();
    monthlyCounts.clear();
    
    // Debug: Print current working directory and logs directory
    QString currentDir = QDir::currentPath();
    QString absoluteLogsDir = QDir(logsDirectory).absolutePath();
    
    QDir logsDir(logsDirectory);
    if (!logsDir.exists()) {
        QString errorMsg = QString("Logs directory not found!\n"
                                  "Current directory: %1\n"
                                  "Looking for: %2\n"
                                  "Absolute path: %3")
                          .arg(currentDir)
                          .arg(logsDirectory)
                          .arg(absoluteLogsDir);
        QMessageBox::warning(this, "Warning", errorMsg);
        return;
    }
    
    // Scan all date directories
    QStringList dateDirs = logsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dateDir : dateDirs) {
        QDir dayDir(logsDir.absoluteFilePath(dateDir));
        QStringList binFiles = dayDir.entryList(QStringList() << "*.bin", QDir::Files);
        
        for (const QString &binFile : binFiles) {
            QString filePath = dayDir.absoluteFilePath(binFile);
            parseDetectionBin(filePath);
        }
    }
}

bool SeizureAnalyzer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == channelList->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        QPoint pos = me->pos();
        QListWidgetItem *item = channelList->itemAt(pos);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            return true; // consume to keep popup open
                }
            }
    return QMainWindow::eventFilter(watched, event);
}

void SeizureAnalyzer::updateSeizureCounts()
{
    QList<SeizureRange> channelDetections;
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
            channelDetections.append(detection);
    }
    
    int totalSeizures = channelDetections.size();
    int todaySeizures = 0;
    int monthlySeizures = 0;
    
    QDate today = QDate::currentDate();
    QString currentMonth = today.toString("yyyy-MM");
    
    for (const SeizureRange &detection : channelDetections) {
        if (detection.start.date() == today) {
            todaySeizures++;
        }
        
        QString detectionMonth = detection.start.date().toString("yyyy-MM");
        if (detectionMonth == currentMonth) {
            monthlySeizures++;
        }
    }
    
    totalSeizuresLabel->setText(QString("Total Seizures: %1").arg(totalSeizures));
    todaySeizuresLabel->setText(QString("Today: %1").arg(todaySeizures));
    monthlySeizuresLabel->setText(QString("This Month: %1").arg(monthlySeizures));
}

void SeizureAnalyzer::updateLatestDetections()
{
    // If no day selected, show nothing (user must click a day)
    if (!selectedDate.isValid()) {
        latestDetectionsTable->setRowCount(0);
        visibleDetections.clear();
        updateChannelData();
        return;
    }

    QList<SeizureRange> channelDetections;
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
        if (selectedDate.isValid() && detection.start.date() != selectedDate) continue;
            channelDetections.append(detection);
        }
    std::sort(channelDetections.begin(), channelDetections.end(), 
              [](const SeizureRange &a, const SeizureRange &b) {
                  return a.end > b.end;
              });
    
    visibleDetections = channelDetections;

    int count = channelDetections.size();
    latestDetectionsTable->setRowCount(count);
    
    for (int i = 0; i < count; ++i) {
        const SeizureRange &detection = channelDetections[i];
        latestDetectionsTable->setItem(i, 0, new QTableWidgetItem(QString("A-%1").arg(detection.channelIndex, 3, 10, QChar('0'))));
        latestDetectionsTable->setItem(i, 1, new QTableWidgetItem(detection.start.toString("yyyy-MM-dd hh:mm:ss.zzz")));
        latestDetectionsTable->setItem(i, 2, new QTableWidgetItem(detection.end.toString("yyyy-MM-dd hh:mm:ss.zzz")));
        latestDetectionsTable->setItem(i, 3, new QTableWidgetItem(QString::number(detection.durationSec, 'f', 3)));
        latestDetectionsTable->setItem(i, 4, new QTableWidgetItem(QFileInfo(detection.filePath).fileName()));
        QWidget *btnContainer = new QWidget(latestDetectionsTable);
        QHBoxLayout *btnLayout = new QHBoxLayout(btnContainer);
        btnLayout->setContentsMargins(0, 0, 4, 0);
        btnLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QPushButton *btn = new QPushButton("Open", btnContainer);
        btn->setProperty("detIndex", i);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btnLayout->addWidget(btn);
        btnContainer->setLayout(btnLayout);
        connect(btn, &QPushButton::clicked, this, &SeizureAnalyzer::onOpenDetectionClicked);
        latestDetectionsTable->setCellWidget(i, 5, btnContainer);
    }

}

void SeizureAnalyzer::updateDailyCounts()
{
    dailyCounts.clear();
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
        QDate date = detection.start.date();
            dailyCounts[date]++;
    }
    
    dailyCountsTable->setRowCount(dailyCounts.size());
    
    QList<QDate> dates = dailyCounts.keys();
    std::sort(dates.begin(), dates.end(), std::greater<QDate>());
    
    for (int i = 0; i < dates.size(); ++i) {
        QDate date = dates[i];
        int count = dailyCounts[date];
        
        dailyCountsTable->setItem(i, 0, new QTableWidgetItem(date.toString("yyyy-MM-dd")));
        dailyCountsTable->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
    }

    // Restore selection if date still exists
    if (selectedDate.isValid()) {
        for (int i = 0; i < dates.size(); ++i) {
            if (dates[i] == selectedDate) {
                dailyCountsTable->selectRow(i);
                return;
            }
        }
        selectedDate = QDate(); // clear if not found
    }
}

void SeizureAnalyzer::onChannelItemChanged(QListWidgetItem *item)
{
    if (!item) return;
    int row = channelList->row(item);
    if (row < 0 || row >= 32) return;

    if (item->checkState() == Qt::Checked) {
        selectedChannels.insert(row);
    } else {
        selectedChannels.remove(row);
    }

    updateDisplay();
}

void SeizureAnalyzer::onDailySelectionChanged()
{
    auto sel = dailyCountsTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        selectedDate = QDate();
    } else {
        QModelIndex idx = sel.first();
        QString dateStr = dailyCountsTable->item(idx.row(), 0)->text();
        selectedDate = QDate::fromString(dateStr, "yyyy-MM-dd");
    }
    updateLatestDetections();
}

void SeizureAnalyzer::onDetectionSelectionChanged()
{
    // No-op (waveform view removed)
}

void SeizureAnalyzer::onOpenDetectionClicked()
{
    QObject *senderObj = sender();
    if (!senderObj) return;
    bool ok = false;
    int idx = senderObj->property("detIndex").toInt(&ok);
    if (!ok || idx < 0 || idx >= visibleDetections.size()) return;

    const SeizureRange &det = visibleDetections[idx];
    openRawForDetection(det);
}

void SeizureAnalyzer::updateChannelData()
{
    // No-op (waveform view removed)
}

void SeizureAnalyzer::parseDetectionBin(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open detection bin:" << filePath;
        return;
    }
    
    QFileInfo fi(filePath);
    QDateTime fileBase = fi.lastModified();
    QDate dirDate = QDate::fromString(fi.dir().dirName(), "yyyy-MM-dd");
    if (dirDate.isValid()) {
        // midnight UTC for that date without deprecated ctor
        fileBase = QDateTime(dirDate, QTime(0,0), QTimeZone::UTC);
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    QMap<int, QDateTime> openStarts; // channel -> start time

    while (!in.atEnd()) {
        quint32 word;
        in >> word;
        quint8 type = word & 0x3;
        quint8 ch = (word >> 2) & 0x1F;
        quint32 tsTicks = (word >> 7) & 0x1FFFFFF; // 25 bits

        if (ch == 0 || ch > 32) continue;
        QDateTime ts = fileBase.addMSecs(static_cast<qint64>(tsTicks));
        int channelIndex = ch - 1;

        if (type == 0b10) { // start
            openStarts[channelIndex] = ts;
        } else if (type == 0b01) { // end
            if (openStarts.contains(channelIndex)) {
                SeizureRange range;
                range.start = openStarts[channelIndex];
                range.end = ts;
                range.channelIndex = channelIndex;
                range.filePath = filePath;
                range.durationSec = std::max(0.0, range.start.msecsTo(range.end) / 1000.0);
                allDetections.append(range);
                openStarts.remove(channelIndex);
            }
        }
    }
}

bool SeizureAnalyzer::channelSelected(int channelIndex) const
{
    if (selectedChannels.isEmpty()) return false; // show nothing when none selected
    return selectedChannels.contains(channelIndex);
}

// --- Waveform dialog and raw loader ---

class WaveformCanvas : public QWidget {
public:
    WaveformCanvas(const QVector<float>& data,
                   int windowMs,
                   qint64 windowStartTickMs,
                   int seizureStartIndex,
                   int seizureEndIndex,
                   QWidget* parent = nullptr)
        : QWidget(parent)
        , data_(data)
        , windowMs_(windowMs)
        , windowStartTickMs_(windowStartTickMs)
        , szStart_(seizureStartIndex)
        , szEnd_(seizureEndIndex) {}

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing);

        if (data_.isEmpty()) return;

        const int w = width();
        const int h = height();
        const int n = data_.size();

        const int leftMargin = 40;
        const int rightMargin = 10;
        const int topMargin = 10;
        const int bottomMargin = 30;

        QRect plotRect(leftMargin, topMargin,
                       w - leftMargin - rightMargin,
                       h - topMargin - bottomMargin);

        // Axes
        p.setPen(Qt::black);
        p.drawLine(plotRect.left(), plotRect.bottom(),
                   plotRect.right(), plotRect.bottom()); // x-axis
        p.drawLine(plotRect.left(), plotRect.top(),
                   plotRect.left(), plotRect.bottom());  // y-axis

        // Value range
        float minv = data_.first();
        float maxv = data_.first();
        for (float v : data_) { minv = std::min(minv, v); maxv = std::max(maxv, v); }
        if (maxv - minv < 1e-3f) { maxv = minv + 1.0f; }

        auto yscale = [&](float v) {
            return plotRect.bottom() - ((v - minv) / (maxv - minv)) * plotRect.height();
        };

        // Shaded seizure region
        if (szStart_ >= 0 && szEnd_ > szStart_ && szEnd_ < n) {
            float x0 = plotRect.left() + (float(szStart_) / float(n - 1)) * plotRect.width();
            float x1 = plotRect.left() + (float(szEnd_)   / float(n - 1)) * plotRect.width();
            QRectF szRect(QPointF(x0, plotRect.top()), QPointF(x1, plotRect.bottom()));
            QColor shade(255, 0, 0, 40);
            p.fillRect(szRect, shade);
        }

        // Waveform
        QPainterPath path;
        path.moveTo(plotRect.left(), yscale(data_[0]));
        for (int i = 1; i < n; ++i) {
            float x = plotRect.left() + (float(i) / float(n - 1)) * plotRect.width();
            path.lineTo(x, yscale(data_[i]));
        }

        p.setPen(QPen(Qt::blue, 1.2));
        p.drawPath(path);

        // X-axis ticks (0, mid, end)
        p.setPen(Qt::black);
        QFontMetrics fm(p.font());
        int yAxis = plotRect.bottom();
        auto drawTick = [&](double ms, int x) {
            double absMs = windowStartTickMs_ + ms;
            p.drawLine(x, yAxis, x, yAxis + 4);
            QString label = QString::number(absMs / 1000.0, 'f', 3) + " s";
            int tw = fm.horizontalAdvance(label);
            p.drawText(x - tw / 2, yAxis + 4 + fm.ascent(), label);
        };
        drawTick(0.0, plotRect.left());
        drawTick(windowMs_ / 2.0, plotRect.left() + plotRect.width() / 2);
        drawTick(windowMs_, plotRect.right());

        // Y-axis ticks (min, mid, max) in microvolts
        auto drawYTick = [&](float v) {
            int y = int(yscale(v));
            p.drawLine(plotRect.left() - 4, y, plotRect.left(), y);
            QString label = QString::number(v, 'f', 0);
            int tw = fm.horizontalAdvance(label);
            p.drawText(plotRect.left() - 6 - tw, y + fm.ascent() / 2, label);
        };
        drawYTick(minv);
        drawYTick((minv + maxv) * 0.5f);
        drawYTick(maxv);

        // Label seizure start/end times near the top of the shaded region
        if (szStart_ >= 0 && szEnd_ > szStart_ && szEnd_ < n) {
            double msStart = (double(szStart_) / double(n)) * windowMs_;
            double msEnd   = (double(szEnd_)   / double(n)) * windowMs_;
            double absStart = windowStartTickMs_ + msStart;
            double absEnd   = windowStartTickMs_ + msEnd;
            QString bandLabel = QString("%1 s → %2 s")
                                    .arg(absStart / 1000.0, 0, 'f', 3)
                                    .arg(absEnd / 1000.0,   0, 'f', 3);
            int tw = fm.horizontalAdvance(bandLabel);
            p.setPen(Qt::darkRed);
            p.drawText(plotRect.left() + (plotRect.width() - tw) / 2,
                       plotRect.top() + fm.ascent() + 2,
                       bandLabel);
        }

        // Border
        p.setPen(Qt::gray);
        p.drawRect(plotRect.adjusted(0, 0, -1, -1));
    }

private:
    QVector<float> data_;
    int windowMs_;
    qint64 windowStartTickMs_;
    int szStart_;
    int szEnd_;
};

class WaveformDialog : public QDialog {
public:
    WaveformDialog(const QVector<float>& data,
                   int windowMs,
                   qint64 windowStartTickMs,
                   int seizureStartIndex,
                   int seizureEndIndex,
                   QWidget* parent = nullptr)
        : QDialog(parent) {
        setModal(true);
        resize(800, 400);
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(new WaveformCanvas(data, windowMs, windowStartTickMs,
                                             seizureStartIndex, seizureEndIndex, this));
    }
};

bool loadRawWindow(const QString& rawPath,
                   int channelIndex,
                   const QDateTime& detStart,
                   int windowMs,
                   QVector<float>& out,
                   qint64& windowStartMsOut,
                   QString& error)
{
    out.clear();
    QFile f(rawPath);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QString("Cannot open raw file: %1").arg(rawPath);
        return false;
    }
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[8];
    if (in.readRawData(magic, 8) != 8 || memcmp(magic, "HALOLOG", 7) != 0) {
        error = "Bad magic in raw file";
        return false;
    }
    quint16 version, reserved;
    quint32 channelCount, samplesPerRecord, sampleBits, tsBits;
    in >> version >> reserved >> channelCount >> samplesPerRecord >> sampleBits >> tsBits;
    if (channelIndex < 0 || channelIndex >= int(channelCount)) {
        error = "Channel out of range in raw file";
        return false;
    }

    // Target window: always show 'windowMs' ms total, centered on detStart within the hour.
    qint64 detMs = detStart.time().msecsSinceStartOfDay() % (3600 * 1000);
    qint64 half = windowMs / 2;
    qint64 startMs = std::max<qint64>(0, detMs - half);
    qint64 endMs = startMs + windowMs;
    windowStartMsOut = startMs;

    const quint64 recordSize = 8 /*ns*/ + 4 + 4 + 512 + (channelCount * samplesPerRecord * 2);
    int firstRec = int(startMs / samplesPerRecord);
    int lastRec = int(endMs / samplesPerRecord) + 1;

    // skip to firstRec
    quint64 headerSize = 8 + 2 + 2 + 4 + 4 + 4 + 4;
    quint64 offset = headerSize + quint64(firstRec) * recordSize;
    if (!f.seek(offset)) {
        error = "Seek failed in raw file";
        return false;
    }

    out.reserve((lastRec - firstRec) * int(samplesPerRecord));
    for (int rec = firstRec; rec < lastRec; ++rec) {
        quint64 ns; quint32 seq; quint32 payload;
        in >> ns >> seq >> payload;
        if (in.status() != QDataStream::Ok) break;

        // ts
        QVector<quint32> ts(samplesPerRecord);
        for (quint32 i = 0; i < samplesPerRecord; ++i) in >> ts[i];
        if (in.status() != QDataStream::Ok) break;

        // wave
        QVector<quint16> wave(channelCount * samplesPerRecord);
        for (quint32 i = 0; i < channelCount * samplesPerRecord; ++i) in >> wave[i];
        if (in.status() != QDataStream::Ok) break;

        for (quint32 i = 0; i < samplesPerRecord; ++i) {
            quint32 t = ts[i];
            if (t < startMs || t > endMs) continue;
            // Convert 16-bit Intan code to microvolts
            int code = int(wave[channelIndex * samplesPerRecord + i]);
            float uv = float(code - 32768) * 0.195f;
            out.append(uv);
        }
    }

    if (out.isEmpty()) {
        error = "No samples found in window";
        return false;
    }
    return true;
}

void SeizureAnalyzer::openRawForDetection(const SeizureRange& detection)
{
    // For now, just open the raw log file with the default system handler (e.g., Intan viewer).
    QString dateStr = detection.start.date().toString("yyyy-MM-dd");
    QString hourStr = detection.start.time().toString("HH");
    QDir dayDir(QDir(logsDirectory).absoluteFilePath(dateStr));
    QString rawPath = dayDir.absoluteFilePath(QString("hour_%1_raw.log").arg(hourStr));

    if (!QFile::exists(rawPath)) {
        QMessageBox::warning(this, "Raw file missing", QString("Raw log not found:\n%1").arg(rawPath));
        return;
    }
    
    QDesktopServices::openUrl(QUrl::fromLocalFile(rawPath));
}

void SeizureAnalyzer::showChannelPopup()
{
    if (!channelPopup) return;
    QPoint globalPos = channelButton->mapToGlobal(QPoint(0, channelButton->height()));
    channelPopup->move(globalPos);
    channelPopup->show();
    channelPopup->raise();
}
