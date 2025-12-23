#ifndef SEIZURE_ANALYZER_H
#define SEIZURE_ANALYZER_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QDate>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QListWidget>
#include <QSet>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
QT_END_NAMESPACE

struct SeizureRange {
    QDateTime start;
    QDateTime end;
    int channelIndex; // 0-31
    QString filePath;
    double durationSec;
};

class SeizureAnalyzer : public QMainWindow
{
    Q_OBJECT

public:
    SeizureAnalyzer(QWidget *parent = nullptr);
    ~SeizureAnalyzer();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void reloadData();
    void updateDisplay();
    void onFileChanged(const QString &path);
    void onChannelItemChanged(QListWidgetItem *item);
    void showChannelPopup();
    void onDailySelectionChanged();
    void onDetectionSelectionChanged();
    void onOpenDetectionClicked();

private:
    void setupUI();
    void scanLogFiles();
    void parseHdf5File(const QString &filePath);
    void parseDetectionBin(const QString &filePath);
    void updateSeizureCounts();
    void updateLatestDetections();
    void updateDailyCounts();
    void updateChannelData();
    
    // UI Components
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    QGridLayout *statsLayout;
    QGridLayout *dailyLayout;
    
    // Channel selection
    QPushButton *channelButton;
    QListWidget *channelList;
    QWidget *channelPopup;
    
    QPushButton *reloadButton;
    QLabel *totalSeizuresLabel;
    QLabel *todaySeizuresLabel;
    QLabel *monthlySeizuresLabel;
    QLabel *lastUpdateLabel;
    QLabel *thresholdLabel;
    QLabel *windowTimeoutLabel;
    QLabel *transitionCountLabel;
    QLabel *channelsPerPacketLabel;
    
    QTableWidget *latestDetectionsTable;
    QTableWidget *dailyCountsTable;
    
    // Data
    QList<SeizureRange> allDetections;
    QMap<QDate, int> dailyCounts;
    QMap<QString, int> monthlyCounts;
    QFileSystemWatcher *fileWatcher;
    QTimer *updateTimer;
    
    QString logsDirectory;
    QSet<int> selectedChannels; // 0-based channel indices
    QDate selectedDate;
    QList<SeizureRange> visibleDetections;
    
    bool channelSelected(int channelIndex) const;
    void openRawForDetection(const SeizureRange& detection);
};

#endif // SEIZURE_ANALYZER_H
