# IMX6ULL 智能家居控制系统

本项目基于 100ASK IMX6ULL 开发板，实现了一个嵌入式智能家居演示系统。项目包含 Linux 字符设备驱动、板端测试程序、Qt LCD 控制界面，以及华为云 IoTDA MQTT 上云客户端。

系统可以采集温湿度、人体红外、毫米波雷达数据，并控制风扇和舵机窗帘，同时支持将设备属性实时上传到华为云。

## 功能介绍

- DHT11 温湿度采集
- SR501 人体红外检测
- RD-03 毫米波雷达 GPIO 检测
- RD-03 UART 串口距离数据解析
- TB6612 风扇档位控制
- SG90 舵机模拟窗帘开关
- Qt 全屏 LCD 智能家居控制面板
- 华为云 IoTDA MQTT 属性上报
- 华为云下发风扇和窗帘控制命令

## 项目目录

```text
.
├── driver/                         Linux 内核字符设备驱动
├── test_app/                       板端硬件测试程序
├── UI/                             Qt LCD 智能家居控制界面
├── Net/                            华为云 MQTT 客户端和配置示例
├── 100ask_sg90_fan_imx6ull-14x14.dts
├── 100ask_motor_sg90_imx6ull-14x14.dts
└── Makefile
```

## 硬件设备节点

| 设备节点 | 功能 |
| --- | --- |
| `/dev/querydht11` | DHT11 温湿度数据 |
| `/dev/mysr501` | SR501 人体红外状态 |
| `/dev/myrd03` | RD-03 OT2 GPIO 有无人状态 |
| `/dev/ttymxc5` | RD-03 UART 串口数据 |
| `/dev/fanmotor` | TB6612 风扇控制 |
| `/dev/sg90` | SG90 舵机控制 |

## 驱动说明

`driver/` 目录下包含各个硬件模块的 Linux 内核驱动：

| 驱动文件 | 对应硬件 | 设备节点 |
| --- | --- | --- |
| `dht11driver.c` | DHT11 温湿度传感器 | `/dev/querydht11` |
| `sr501driver.c` | SR501 人体红外 | `/dev/mysr501` |
| `rd03driver.c` | RD-03 雷达 OT2 引脚 | `/dev/myrd03` |
| `fandriver.c` | TB6612 风扇驱动 | `/dev/fanmotor` |
| `sg90driver.c` | SG90 舵机 | `/dev/sg90` |
| `motordriver.c` | 普通步进电机 | `/dev/motor` |
| `motordriver_TB6612.c` | TB6612 步进电机 | `/dev/motor` |

## 编译方法

### 1. 编译驱动和测试程序

如果 SDK 路径不同，需要先修改根目录 `Makefile` 中的 `KERN_DIR` 和 `CROSS_COMPILE`。

```sh
make
```

### 2. 编译华为云 MQTT 客户端

```sh
cd Net
make
```

生成程序：

```text
Net/huawei_mqtt_client
```

### 3. 编译 Qt LCD 控制界面

需要使用目标板对应的 Qt 交叉编译环境。

```sh
cd UI
qmake SmartHomePanel.pro
make
```

生成程序：

```text
UI/smart_home_panel
```

## 运行方法

先在开发板上加载需要的内核模块，确认 `/dev` 下已经生成对应设备节点。

运行 LCD 控制界面：

```sh
cd UI
./smart_home_panel
```

运行华为云 MQTT 客户端：

```sh
cd Net
./huawei_mqtt_client ./huawei_cloud.conf
```

如果不指定配置文件，程序会尝试读取：

```text
/etc/huawei_cloud.conf
./huawei_cloud.conf
```

## 华为云产品模型

华为云 IoTDA 产品模型中的服务 ID 需要和程序保持一致：

```text
smart_home
```

建议创建以下属性：

| 属性名称 | 数据类型 | 访问权限 | 说明 |
| --- | --- | --- | --- |
| `temperature` | decimal 小数 | 可读 | 温度 |
| `humidity` | decimal 小数 | 可读 | 湿度 |
| `sr501_present` | int 整型 | 可读 | `0` 无人，`1` 有人 |
| `rd03_gpio_present` | int 整型 | 可读 | RD-03 OT2 GPIO 检测状态 |
| `rd03_present` | int 整型 | 可读 | RD-03 串口解析到的有无人状态 |
| `rd03_distance_cm` | int 整型 | 可读 | RD-03 测距，单位 cm |
| `fan_speed` | int 整型 | 可读/可写 | `0` 停止，`1` 低速，`2` 中速，`3` 高速 |
| `curtain_open` | int 整型 | 可读/可写 | `0` 关闭，`1` 打开 |

云端可下发控制的属性：

```text
fan_speed
curtain_open
```

## 配置文件

复制示例配置：

```sh
cp Net/huawei_cloud.conf.example Net/huawei_cloud.conf
```

然后填写自己的华为云设备接入地址、设备 ID 和设备密钥。

真实配置文件不要提交到 GitHub：

```text
Net/huawei_cloud.conf
Net/*KEY*.txt
Net/DEVICES-KEY-*.txt
Net/IMX6ULL_MQTT_ACCESS-KEY.txt
```

仓库中只保留 `huawei_cloud.conf.example` 作为模板。

## 测试程序

`test_app/` 目录下提供了独立测试程序，可以用于单独验证硬件：

```sh
./dht11_test
./sr501_test
./rd03_test
./rd03_monitor
./fan_test 1 2
./sg90_test 1
```

示例：

```sh
./fan_test 1 0
```

表示停止风扇。

## 注意事项

1. 当前 Qt UI 和 MQTT 客户端都会直接访问 `/dev` 设备节点。如果两个进程同时运行，可能会出现 RD-03 串口数据被抢读、风扇状态不同步等问题。

2. 更推荐的架构是：由一个后台服务统一访问硬件和华为云，Qt UI 只负责显示数据和发送控制命令。

3. 风扇和 SG90 舵机建议使用独立的大电流电源供电，并与开发板共地。不要直接从开发板或 LCD 供电口给电机供电，否则可能导致 LCD 触摸屏 I2C 异常、界面闪屏或按钮失灵。

4. 编译产物不要提交到仓库，例如：

```text
*.o
*.ko
*.cmd
*.mod.c
Module.symvers
modules.order
UI/moc_*
```

5. 如果上传到 GitHub，提交前请确认没有包含真实设备密钥、华为云 Access Key 或个人私密配置。

## 项目状态

本项目主要用于嵌入式 Linux、设备驱动、Qt 界面和华为云 IoTDA 上云联调学习。当前版本可以完成本地 LCD 控制和 MQTT 属性上报，但后续仍可继续优化为统一后台服务架构，以提升多进程协同稳定性。
