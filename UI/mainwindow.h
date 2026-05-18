#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QIcon>
#include <QVector>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include <stdint.h>

class QComboBox;

class DistanceChart : public QWidget
{
    Q_OBJECT

public:
    explicit DistanceChart(QWidget *parent = nullptr);
    void addSample(int distanceCm, bool present);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        int distanceCm;
        bool present;
    };

    QVector<Sample> samples;
    int maxSamples;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshSensors();
    void setFanStopped();
    void setFanLow();
    void setFanMedium();
    void setFanHigh();
    void triggerServo();
    void resetServo();

private:
    struct Rd03Report {
        bool valid;
        bool present;
        unsigned int distanceCm;
        uint16_t energy[16];
    };

    void buildUi();
    void openDevices();
    void closeDevices();
    void appendLog(const QString &message);
    QWidget *makeCard(const QString &title, QLabel **valueLabel, const QString &hint);
    QPushButton *makeActionButton(const QString &text, const QIcon &icon);
    void updateFanButtons();
    void updateCurtainButtons();

    bool readDht11(double *humidity, double *temperature);
    int readBinaryDevice(int fd);
    bool writeFan(unsigned char direction, unsigned char speed);
    bool writeServo(unsigned char command);

    bool openRd03Serial();
    void closeRd03Serial();
    void configureRd03ReportMode();
    bool readRd03Serial(Rd03Report *report);
    bool feedRd03Byte(uint8_t byte, Rd03Report *report);

    static uint16_t getLe16(const uint8_t *data);

    QLabel *dhtValueLabel;
    QLabel *sr501ValueLabel;
    QLabel *rd03GpioValueLabel;
    QLabel *rd03UartValueLabel;
    QLabel *fanValueLabel;
    QLabel *servoValueLabel;
    QLabel *deviceStatusLabel;
    QLabel *summaryLabel;
    QTextEdit *logEdit;
    DistanceChart *distanceChart;
    QPushButton *fanStopButton;
    QPushButton *fanLowButton;
    QPushButton *fanMediumButton;
    QPushButton *fanHighButton;
    QPushButton *servoResetButton;
    QPushButton *servoTriggerButton;

    QTimer sensorTimer;

    int dhtFd;
    int sr501Fd;
    int rd03GpioFd;
    int fanFd;
    int servoFd;
    int rd03SerialFd;

    uint8_t rd03Frame[128];
    int rd03FramePos;
    unsigned char fanSpeed;
    bool curtainOpen;
};

#endif
