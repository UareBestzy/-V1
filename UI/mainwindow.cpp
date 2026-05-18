#include "mainwindow.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

namespace {
const char *DHT_DEVICE = "/dev/querydht11";
const char *SR501_DEVICE = "/dev/mysr501";
const char *RD03_GPIO_DEVICE = "/dev/myrd03";
const char *FAN_DEVICE = "/dev/fanmotor";
const char *SERVO_DEVICE = "/dev/sg90";
const char *RD03_SERIAL_DEVICE = "/dev/ttymxc5";

const uint8_t RD03_CMD_OPEN_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00,
    0x01, 0x00, 0x04, 0x03, 0x02, 0x01
};

const uint8_t RD03_CMD_SET_REPORT_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0x12, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03,
    0x02, 0x01
};

const uint8_t RD03_CMD_CLOSE_MODE[] = {
    0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00,
    0x04, 0x03, 0x02, 0x01
};

bool writeAll(int fd, const uint8_t *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t ret = ::write(fd, data + done, size - done);
        if (ret < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return false;
        }
        done += static_cast<size_t>(ret);
    }
    return true;
}

QPixmap makeRoundIcon(const QString &kind, bool on, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QColor bg = on ? QColor("#5eead4") : QColor("#314052");
    QColor fg = on ? QColor("#0f1720") : QColor("#dbe8f6");
    QColor accent = on ? QColor("#0f1720") : QColor("#5eead4");

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));

    QPen pen(fg, qMax(2, size / 13), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const qreal s = size;
    if (kind == "stop") {
        painter.setBrush(fg);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(s * 0.32, s * 0.32, s * 0.36, s * 0.36),
                                s * 0.05, s * 0.05);
    } else if (kind == "fan-low" || kind == "fan-medium" || kind == "fan-high") {
        painter.drawEllipse(QRectF(s * 0.40, s * 0.40, s * 0.20, s * 0.20));
        painter.drawArc(QRectF(s * 0.18, s * 0.17, s * 0.64, s * 0.64), 30 * 16, 80 * 16);
        painter.drawArc(QRectF(s * 0.18, s * 0.17, s * 0.64, s * 0.64), 150 * 16, 80 * 16);
        painter.drawArc(QRectF(s * 0.18, s * 0.17, s * 0.64, s * 0.64), 270 * 16, 80 * 16);

        int bars = kind == "fan-low" ? 1 : (kind == "fan-medium" ? 2 : 3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        for (int i = 0; i < bars; ++i) {
            painter.drawRoundedRect(QRectF(s * (0.30 + 0.14 * i), s * 0.72,
                                           s * 0.08, s * (0.08 + 0.035 * i)),
                                    s * 0.025, s * 0.025);
        }
    } else if (kind == "curtain-close") {
        painter.drawLine(QPointF(s * 0.24, s * 0.27), QPointF(s * 0.76, s * 0.27));
        painter.drawLine(QPointF(s * 0.36, s * 0.31), QPointF(s * 0.36, s * 0.72));
        painter.drawLine(QPointF(s * 0.64, s * 0.31), QPointF(s * 0.64, s * 0.72));
        painter.drawLine(QPointF(s * 0.36, s * 0.52), QPointF(s * 0.64, s * 0.52));
    } else if (kind == "curtain-open") {
        painter.drawLine(QPointF(s * 0.24, s * 0.27), QPointF(s * 0.76, s * 0.27));
        painter.drawLine(QPointF(s * 0.30, s * 0.31), QPointF(s * 0.30, s * 0.72));
        painter.drawLine(QPointF(s * 0.70, s * 0.31), QPointF(s * 0.70, s * 0.72));
        painter.drawLine(QPointF(s * 0.42, s * 0.52), QPointF(s * 0.58, s * 0.52));
    } else {
        painter.setFont(QFont("Sans Serif", size / 2, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, kind.left(1));
    }

    return pixmap;
}

QIcon makeToggleIcon(const QString &kind)
{
    QIcon icon;
    icon.addPixmap(makeRoundIcon(kind, false, 36), QIcon::Normal, QIcon::Off);
    icon.addPixmap(makeRoundIcon(kind, true, 36), QIcon::Normal, QIcon::On);
    icon.addPixmap(makeRoundIcon(kind, true, 36), QIcon::Active, QIcon::On);
    icon.addPixmap(makeRoundIcon(kind, true, 36), QIcon::Selected, QIcon::On);
    return icon;
}

QPixmap makeStatusIcon(const QString &title)
{
    QString mark = "?";
    if (title.contains("温湿度")) {
        mark = "T";
    } else if (title.contains("红外")) {
        mark = "P";
    } else if (title.contains("毫米波")) {
        mark = "R";
    } else if (title.contains("雷达")) {
        mark = "O";
    } else if (title.contains("风扇")) {
        return makeRoundIcon("fan-medium", false, 42);
    } else if (title.contains("窗帘")) {
        return makeRoundIcon("curtain-open", false, 42);
    }

    QPixmap pixmap(42, 42);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#314052"));
    painter.drawEllipse(QRectF(1, 1, 40, 40));
    painter.setPen(QColor("#5eead4"));
    painter.setFont(QFont("Sans Serif", 20, QFont::Bold));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, mark);
    return pixmap;
}
}

DistanceChart::DistanceChart(QWidget *parent)
    : QWidget(parent),
      maxSamples(60)
{
    setMinimumHeight(110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DistanceChart::addSample(int distanceCm, bool present)
{
    Sample sample;
    sample.distanceCm = present ? distanceCm : 0;
    sample.present = present;
    samples.append(sample);
    while (samples.size() > maxSamples) {
        samples.removeFirst();
    }
    update();
}

void DistanceChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#171a21"));

    QRect plot = rect().adjusted(42, 14, -12, -26);
    if (plot.width() <= 4 || plot.height() <= 4) {
        return;
    }

    painter.setPen(QPen(QColor("#303746"), 1));
    for (int i = 0; i <= 3; ++i) {
        int y = plot.top() + plot.height() * i / 3;
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    painter.setPen(QColor("#7f8b9f"));
    painter.drawText(4, plot.top() + 8, "300cm");
    painter.drawText(10, plot.bottom(), "0cm");
    painter.drawText(plot.left(), height() - 8, "最近60秒");

    if (samples.isEmpty()) {
        painter.setPen(QColor("#7f8b9f"));
        painter.drawText(plot, Qt::AlignCenter, "等待 RD03 数据...");
        return;
    }

    QPainterPath path;
    bool hasPoint = false;
    for (int i = 0; i < samples.size(); ++i) {
        const Sample &sample = samples.at(i);
        int clamped = qBound(0, sample.distanceCm, 300);
        qreal x = plot.left();
        if (maxSamples > 1) {
            x += static_cast<qreal>(plot.width()) * i / (maxSamples - 1);
        }
        qreal y = plot.bottom() - static_cast<qreal>(clamped) * plot.height() / 300.0;

        if (!sample.present) {
            y = plot.bottom();
        }

        if (!hasPoint) {
            path.moveTo(x, y);
            hasPoint = true;
        } else {
            path.lineTo(x, y);
        }
    }

    painter.setPen(QPen(QColor("#5eead4"), 3));
    painter.drawPath(path);

    const Sample &last = samples.last();
    painter.setPen(QColor(last.present ? "#7fd1ae" : "#f2b8b5"));
    painter.drawText(plot.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignTop,
                     last.present ? QString("当前 %1 cm").arg(last.distanceCm) : "当前无人");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      dhtValueLabel(nullptr),
      sr501ValueLabel(nullptr),
      rd03GpioValueLabel(nullptr),
      rd03UartValueLabel(nullptr),
      fanValueLabel(nullptr),
      servoValueLabel(nullptr),
      deviceStatusLabel(nullptr),
      summaryLabel(nullptr),
      logEdit(nullptr),
      distanceChart(nullptr),
      fanStopButton(nullptr),
      fanLowButton(nullptr),
      fanMediumButton(nullptr),
      fanHighButton(nullptr),
      servoResetButton(nullptr),
      servoTriggerButton(nullptr),
      dhtFd(-1),
      sr501Fd(-1),
      rd03GpioFd(-1),
      fanFd(-1),
      servoFd(-1),
      rd03SerialFd(-1),
      rd03FramePos(0),
      fanSpeed(0),
      curtainOpen(false)
{
    buildUi();
    openDevices();

    connect(&sensorTimer, SIGNAL(timeout()), this, SLOT(refreshSensors()));
    sensorTimer.start(1000);
    refreshSensors();
}

MainWindow::~MainWindow()
{
    sensorTimer.stop();
    closeDevices();
}

void MainWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(22, 16, 22, 16);
    root->setSpacing(12);

    setStyleSheet(
        "QMainWindow, QWidget { background: #101217; color: #f4f7fb; font-family: 'WenQuanYi Micro Hei', 'Noto Sans CJK SC', sans-serif; }"
        "QLabel { background: transparent; }"
        "QFrame#card { background: #20242e; border: 1px solid #313746; border-radius: 8px; }"
        "QFrame#sensorCard { background: #202a35; border: 1px solid #435368; border-radius: 8px; }"
        "QFrame#primaryCard { background: #25313a; border: 1px solid #3f5863; border-radius: 8px; }"
        "QLabel#cardTitle { color: #c8d2e2; font-size: 16px; font-weight: 600; }"
        "QLabel#cardValue { color: #ffffff; font-size: 22px; font-weight: 700; }"
        "QLabel#cardHint { color: #91a4ba; font-size: 12px; }"
        "QPushButton { background: #2d3442; color: #f5f7fb; border: 1px solid #536177; border-radius: 8px; padding: 6px 12px; font-size: 18px; font-weight: 700; }"
        "QPushButton:pressed, QPushButton:checked { background: #5eead4; color: #0f1720; border-color: #8cf7e7; }"
        "QTextEdit { background: #171a21; color: #d9e2ef; border: 1px solid #303746; border-radius: 8px; font-size: 14px; }"
    );

    QHBoxLayout *header = new QHBoxLayout();
    QLabel *title = new QLabel("智能家居控制中心", central);
    QFont titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    summaryLabel = new QLabel("环境监测 | 安防感知 | 家电控制", central);
    summaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    summaryLabel->setStyleSheet("color: #9fb2c8; font-size: 16px;");
    header->addWidget(title, 2);
    header->addWidget(summaryLabel, 1);
    root->addLayout(header);

    deviceStatusLabel = new QLabel("设备连接中...", central);
    deviceStatusLabel->setAlignment(Qt::AlignLeft);
    deviceStatusLabel->setWordWrap(true);
    deviceStatusLabel->setStyleSheet("color: #7fd1ae; font-size: 16px; font-weight: 600;");
    root->addWidget(deviceStatusLabel);

    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(10);
    grid->addWidget(makeCard("温湿度", &dhtValueLabel, "DHT11 环境采集"), 0, 0);
    grid->addWidget(makeCard("人体红外", &sr501ValueLabel, "SR501 移动检测"), 0, 1);
    grid->addWidget(makeCard("毫米波雷达", &rd03UartValueLabel, "RD03 距离与存在感知"), 0, 2);
    grid->addWidget(makeCard("雷达开关量", &rd03GpioValueLabel, "OT2 高电平表示有人"), 1, 0);
    grid->addWidget(makeCard("风扇状态", &fanValueLabel, "TB6612 风扇控制"), 1, 1);
    grid->addWidget(makeCard("窗帘状态", &servoValueLabel, "SG90 舵机模拟窗帘"), 1, 2);
    root->addLayout(grid, 3);

    QFrame *chartCard = new QFrame(central);
    chartCard->setObjectName("card");
    QVBoxLayout *chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(14, 10, 14, 10);
    chartLayout->setSpacing(6);
    QLabel *chartTitle = new QLabel("雷达距离曲线", chartCard);
    chartTitle->setObjectName("cardTitle");
    distanceChart = new DistanceChart(chartCard);
    chartLayout->addWidget(chartTitle);
    chartLayout->addWidget(distanceChart);
    root->addWidget(chartCard, 2);

    QFrame *controlBar = new QFrame(central);
    controlBar->setObjectName("primaryCard");
    QHBoxLayout *controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(16, 12, 16, 12);
    controlLayout->setSpacing(10);

    QLabel *fanTitle = new QLabel("风扇", controlBar);
    fanTitle->setStyleSheet("font-size: 18px; font-weight: 700;");
    QLabel *curtainTitle = new QLabel("窗帘", controlBar);
    curtainTitle->setStyleSheet("font-size: 18px; font-weight: 700;");

    QHBoxLayout *fanButtons = new QHBoxLayout();
    fanStopButton = makeActionButton("停止", makeToggleIcon("stop"));
    fanLowButton = makeActionButton("低速", makeToggleIcon("fan-low"));
    fanMediumButton = makeActionButton("中速", makeToggleIcon("fan-medium"));
    fanHighButton = makeActionButton("高速", makeToggleIcon("fan-high"));
    fanButtons->addWidget(fanStopButton);
    fanButtons->addWidget(fanLowButton);
    fanButtons->addWidget(fanMediumButton);
    fanButtons->addWidget(fanHighButton);

    QHBoxLayout *servoButtons = new QHBoxLayout();
    servoResetButton = makeActionButton("关闭窗帘", makeToggleIcon("curtain-close"));
    servoTriggerButton = makeActionButton("打开窗帘", makeToggleIcon("curtain-open"));
    servoButtons->addWidget(servoResetButton);
    servoButtons->addWidget(servoTriggerButton);

    controlLayout->addWidget(fanTitle);
    controlLayout->addLayout(fanButtons);
    QFrame *line = new QFrame(controlBar);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet("color: #465266;");
    controlLayout->addWidget(line);
    controlLayout->addWidget(curtainTitle);
    controlLayout->addLayout(servoButtons);

    connect(fanStopButton, SIGNAL(clicked()), this, SLOT(setFanStopped()));
    connect(fanLowButton, SIGNAL(clicked()), this, SLOT(setFanLow()));
    connect(fanMediumButton, SIGNAL(clicked()), this, SLOT(setFanMedium()));
    connect(fanHighButton, SIGNAL(clicked()), this, SLOT(setFanHigh()));
    connect(servoResetButton, SIGNAL(clicked()), this, SLOT(resetServo()));
    connect(servoTriggerButton, SIGNAL(clicked()), this, SLOT(triggerServo()));

    root->addWidget(controlBar);
    updateFanButtons();
    updateCurtainButtons();

    logEdit = new QTextEdit(central);
    logEdit->setReadOnly(true);
    logEdit->setMinimumHeight(115);
    root->addWidget(logEdit, 1);

    setCentralWidget(central);
    resize(800, 480);
}

QWidget *MainWindow::makeCard(const QString &title, QLabel **valueLabel, const QString &hint)
{
    QFrame *card = new QFrame(this);
    card->setObjectName("sensorCard");
    card->setMinimumHeight(82);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QHBoxLayout *layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);

    QLabel *iconLabel = new QLabel(card);
    iconLabel->setPixmap(makeStatusIcon(title));
    iconLabel->setFixedSize(46, 46);
    iconLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout *textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    QLabel *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName("cardTitle");
    *valueLabel = new QLabel("--", card);
    (*valueLabel)->setObjectName("cardValue");
    (*valueLabel)->setWordWrap(true);
    (*valueLabel)->setMinimumHeight(28);
    QLabel *hintLabel = new QLabel(hint, card);
    hintLabel->setObjectName("cardHint");
    hintLabel->setWordWrap(false);

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(*valueLabel);
    textLayout->addWidget(hintLabel);
    layout->addWidget(iconLabel, 0, Qt::AlignVCenter);
    layout->addLayout(textLayout, 1);
    return card;
}

QPushButton *MainWindow::makeActionButton(const QString &text, const QIcon &icon)
{
    QPushButton *button = new QPushButton(text, this);
    button->setIcon(icon);
    button->setIconSize(QSize(36, 36));
    button->setCheckable(true);
    button->setMinimumHeight(54);
    button->setMinimumWidth(112);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

void MainWindow::updateFanButtons()
{
    if (!fanStopButton || !fanLowButton || !fanMediumButton || !fanHighButton) {
        return;
    }

    fanStopButton->setChecked(fanSpeed == 0);
    fanLowButton->setChecked(fanSpeed == 1);
    fanMediumButton->setChecked(fanSpeed == 2);
    fanHighButton->setChecked(fanSpeed == 3);
}

void MainWindow::updateCurtainButtons()
{
    if (!servoResetButton || !servoTriggerButton) {
        return;
    }

    servoResetButton->setChecked(!curtainOpen);
    servoTriggerButton->setChecked(curtainOpen);
}

void MainWindow::openDevices()
{
    dhtFd = ::open(DHT_DEVICE, O_RDONLY);
    sr501Fd = ::open(SR501_DEVICE, O_RDONLY);
    rd03GpioFd = ::open(RD03_GPIO_DEVICE, O_RDONLY);
    fanFd = ::open(FAN_DEVICE, O_RDWR);
    servoFd = ::open(SERVO_DEVICE, O_RDWR);
    openRd03Serial();

    QStringList status;
    status << QString("温湿度:%1").arg(dhtFd >= 0 ? "在线" : "离线");
    status << QString("红外:%1").arg(sr501Fd >= 0 ? "在线" : "离线");
    status << QString("雷达OT2:%1").arg(rd03GpioFd >= 0 ? "在线" : "离线");
    status << QString("雷达串口:%1").arg(rd03SerialFd >= 0 ? "在线" : "离线");
    status << QString("风扇:%1").arg(fanFd >= 0 ? "在线" : "离线");
    status << QString("窗帘:%1").arg(servoFd >= 0 ? "在线" : "离线");
    deviceStatusLabel->setText(status.join("  "));

    appendLog("设备初始化完成。");
    if (rd03SerialFd >= 0) {
        configureRd03ReportMode();
    }
}

void MainWindow::closeDevices()
{
    if (dhtFd >= 0) ::close(dhtFd);
    if (sr501Fd >= 0) ::close(sr501Fd);
    if (rd03GpioFd >= 0) ::close(rd03GpioFd);
    if (fanFd >= 0) ::close(fanFd);
    if (servoFd >= 0) ::close(servoFd);
    closeRd03Serial();
}

void MainWindow::appendLog(const QString &message)
{
    logEdit->append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message);
}

void MainWindow::refreshSensors()
{
    double humidity = 0.0;
    double temperature = 0.0;
    if (readDht11(&humidity, &temperature)) {
        dhtValueLabel->setText(QString("T %1 C, H %2 %")
            .arg(temperature, 0, 'f', 1)
            .arg(humidity, 0, 'f', 1));
    } else {
        dhtValueLabel->setText("读取失败");
    }

    int sr501 = readBinaryDevice(sr501Fd);
    sr501ValueLabel->setText(sr501 < 0 ? "读取失败" : (sr501 ? "有人移动" : "无人"));

    int rd03Gpio = readBinaryDevice(rd03GpioFd);
    rd03GpioValueLabel->setText(rd03Gpio < 0 ? "读取失败" : (rd03Gpio ? "高电平 / 有人" : "低电平 / 无人"));

    Rd03Report report;
    report.valid = false;
    if (readRd03Serial(&report) && report.valid) {
        rd03UartValueLabel->setText(QString("%1, %2 cm")
            .arg(report.present ? "有人" : "无人")
            .arg(report.distanceCm));
        if (distanceChart) {
            distanceChart->addSample(static_cast<int>(report.distanceCm), report.present);
        }
    }
}

bool MainWindow::readDht11(double *humidity, double *temperature)
{
    unsigned char data[4];
    if (dhtFd < 0) {
        return false;
    }

    ssize_t ret = ::read(dhtFd, data, sizeof(data));
    if (ret != static_cast<ssize_t>(sizeof(data))) {
        return false;
    }

    *humidity = static_cast<double>(data[0]) + static_cast<double>(data[1]) / 10.0;
    *temperature = static_cast<double>(data[2]) + static_cast<double>(data[3]) / 10.0;
    return true;
}

int MainWindow::readBinaryDevice(int fd)
{
    unsigned char value;
    if (fd < 0) {
        return -1;
    }

    ssize_t ret = ::read(fd, &value, 1);
    if (ret != 1) {
        return -1;
    }
    return value ? 1 : 0;
}

bool MainWindow::writeFan(unsigned char direction, unsigned char speed)
{
    unsigned char command[2] = { direction, speed };
    if (fanFd < 0) {
        appendLog("风扇设备未打开。");
        return false;
    }

    if (::write(fanFd, command, sizeof(command)) != static_cast<ssize_t>(sizeof(command))) {
        appendLog(QString("风扇控制失败: %1").arg(strerror(errno)));
        return false;
    }

    fanSpeed = speed;
    updateFanButtons();
    QString speedText;
    switch (speed) {
    case 0:
        speedText = "已停止";
        break;
    case 1:
        speedText = "低速运行";
        break;
    case 2:
        speedText = "中速运行";
        break;
    case 3:
        speedText = "高速运行";
        break;
    default:
        speedText = QString("档位 %1").arg(speed);
        break;
    }
    fanValueLabel->setText(speedText);
    return true;
}

bool MainWindow::writeServo(unsigned char command)
{
    if (servoFd < 0) {
        appendLog("窗帘设备未打开。");
        return false;
    }

    if (::write(servoFd, &command, 1) != 1) {
        appendLog(QString("窗帘控制失败: %1").arg(strerror(errno)));
        return false;
    }

    curtainOpen = command ? true : false;
    updateCurtainButtons();
    servoValueLabel->setText(command ? "已打开" : "已关闭");
    return true;
}

void MainWindow::setFanStopped()
{
    if (writeFan(1, 0)) appendLog("风扇已停止。");
}

void MainWindow::setFanLow()
{
    if (writeFan(1, 1)) appendLog("风扇切换到低速。");
}

void MainWindow::setFanMedium()
{
    if (writeFan(1, 2)) appendLog("风扇切换到中速。");
}

void MainWindow::setFanHigh()
{
    if (writeFan(1, 3)) appendLog("风扇切换到高速。");
}

void MainWindow::triggerServo()
{
    if (writeServo(1)) appendLog("窗帘打开命令已发送。");
}

void MainWindow::resetServo()
{
    if (writeServo(0)) appendLog("窗帘关闭命令已发送。");
}

bool MainWindow::openRd03Serial()
{
    struct termios tio;

    rd03SerialFd = ::open(RD03_SERIAL_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (rd03SerialFd < 0) {
        return false;
    }

    if (tcgetattr(rd03SerialFd, &tio) < 0) {
        closeRd03Serial();
        return false;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    if (tcsetattr(rd03SerialFd, TCSANOW, &tio) < 0) {
        closeRd03Serial();
        return false;
    }

    tcflush(rd03SerialFd, TCIOFLUSH);
    return true;
}

void MainWindow::closeRd03Serial()
{
    if (rd03SerialFd >= 0) {
        ::close(rd03SerialFd);
        rd03SerialFd = -1;
    }
}

void MainWindow::configureRd03ReportMode()
{
    if (rd03SerialFd < 0) {
        return;
    }

    writeAll(rd03SerialFd, RD03_CMD_OPEN_MODE, sizeof(RD03_CMD_OPEN_MODE));
    usleep(100000);
    tcflush(rd03SerialFd, TCIFLUSH);
    writeAll(rd03SerialFd, RD03_CMD_SET_REPORT_MODE, sizeof(RD03_CMD_SET_REPORT_MODE));
    usleep(100000);
    tcflush(rd03SerialFd, TCIFLUSH);
    writeAll(rd03SerialFd, RD03_CMD_CLOSE_MODE, sizeof(RD03_CMD_CLOSE_MODE));
    usleep(100000);
    tcflush(rd03SerialFd, TCIFLUSH);
    appendLog("RD03 上报模式命令已发送。");
}

bool MainWindow::readRd03Serial(Rd03Report *report)
{
    uint8_t buffer[128];
    bool gotReport = false;

    report->valid = false;
    if (rd03SerialFd < 0) {
        return false;
    }

    for (;;) {
        ssize_t len = ::read(rd03SerialFd, buffer, sizeof(buffer));
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                break;
            }
            appendLog(QString("RD03 串口读取失败: %1").arg(strerror(errno)));
            break;
        }
        if (len == 0) {
            break;
        }

        for (ssize_t i = 0; i < len; ++i) {
            if (feedRd03Byte(buffer[i], report)) {
                gotReport = true;
            }
        }
    }

    return gotReport;
}

bool MainWindow::feedRd03Byte(uint8_t byte, Rd03Report *report)
{
    uint16_t payloadLen;
    int total;

    if (rd03FramePos == 0 && byte != 0xF4) {
        return false;
    }
    if (rd03FramePos == 1 && byte != 0xF3) {
        rd03FramePos = (byte == 0xF4) ? 1 : 0;
        return false;
    }
    if (rd03FramePos == 2 && byte != 0xF2) {
        rd03FramePos = 0;
        return false;
    }
    if (rd03FramePos == 3 && byte != 0xF1) {
        rd03FramePos = 0;
        return false;
    }

    if (rd03FramePos >= static_cast<int>(sizeof(rd03Frame))) {
        rd03FramePos = 0;
        return false;
    }

    rd03Frame[rd03FramePos++] = byte;
    if (rd03FramePos < 6) {
        return false;
    }

    payloadLen = getLe16(&rd03Frame[4]);
    total = 4 + 2 + payloadLen + 4;
    if (total > static_cast<int>(sizeof(rd03Frame))) {
        rd03FramePos = 0;
        return false;
    }
    if (rd03FramePos < total) {
        return false;
    }

    if (rd03Frame[total - 4] != 0xF8 || rd03Frame[total - 3] != 0xF7 ||
        rd03Frame[total - 2] != 0xF6 || rd03Frame[total - 1] != 0xF5) {
        rd03FramePos = 0;
        return false;
    }

    if (payloadLen == 35) {
        const uint8_t *payload = &rd03Frame[6];
        report->valid = true;
        report->present = payload[0] ? true : false;
        report->distanceCm = getLe16(&payload[1]);
        for (int i = 0; i < 16; ++i) {
            report->energy[i] = getLe16(&payload[3 + i * 2]);
        }
    }

    rd03FramePos = 0;
    return report->valid;
}

uint16_t MainWindow::getLe16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}
