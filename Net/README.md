# 华为云 IoTDA MQTT 客户端

这个程序作为 IMX6ULL 智能家居项目的独立上云进程运行。它会读取现有驱动设备节点，通过 MQTT 连接华为云 IoTDA，实时上报传感器属性，并监听云端下发的风扇和窗帘控制。

## 编译

```sh
cd /home/book/xuexi/yuan_rebuilt/Net
make
```

编译产物是：

```text
huawei_mqtt_client
```

## 当前实例接入地址

从你截图里的华为云实例看，设备 MQTTS 接入地址是：

```text
46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com
```

端口：

```text
8883
```

协议是 `MQTTS`，注意这是设备接入地址，不要使用应用接入地址。

## 华为云控制台配置

在 IoTDA 控制台中继续完成：

1. 创建产品。
2. 在产品模型中新增服务，服务 ID 建议填 `smart_home`。
3. 在该服务下新增属性。
4. 在产品下创建设备，复制设备 ID 和设备密钥。

建议创建这些属性：

- `temperature`：温度，数值型
- `humidity`：湿度，数值型
- `sr501_present`：人体红外是否有人，整数或布尔
- `rd03_gpio_present`：雷达开关量是否有人，整数或布尔
- `rd03_present`：雷达串口是否检测到人，整数或布尔
- `rd03_distance_cm`：雷达距离，整数，单位 cm
- `fan_speed`：风扇档位，整数，`0` 停止，`1` 低速，`2` 中速，`3` 高速
- `curtain_open`：窗帘是否打开，整数或布尔，`0` 关闭，`1` 打开

云端控制可下发：

- `fan_speed`
- `curtain_open`

## 配置文件

```sh
cp huawei_cloud.conf.example /etc/huawei_cloud.conf
vi /etc/huawei_cloud.conf
```

程序已经内置了当前这台设备的默认连接参数：

```ini
server_address=46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com
device_id=6a082ae5cbb0cf6bb95b6975_imx6ull
device_secret=wobujidemima1
service_id=smart_home
port=8883
use_tls=1
connect_timeout_ms=8000
```

如果你想改成别的设备，再填写配置文件覆盖：

```ini
server_address=46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com
device_id=你的设备ID
device_secret=你的设备密钥
service_id=smart_home
port=8883
use_tls=1
connect_timeout_ms=8000
```

运行：

```sh
./huawei_mqtt_client /etc/huawei_cloud.conf
```

如果不传配置文件路径，程序会先尝试读取 `/etc/huawei_cloud.conf`，然后尝试读取当前目录下的 `./huawei_cloud.conf`；两处都没有时，会直接使用源码里内置的华为云连接参数。

## MQTT Topic

默认属性上报 Topic：

```text
$oc/devices/{device_id}/sys/properties/report
```

默认属性下发订阅 Topic：

```text
$oc/devices/{device_id}/sys/properties/set/#
```

上报的数据格式：

```json
{
  "services": [
    {
      "service_id": "smart_home",
      "properties": {
        "temperature": 26.5,
        "humidity": 60.0,
        "sr501_present": 0,
        "rd03_gpio_present": 1,
        "rd03_present": 1,
        "rd03_distance_cm": 115,
        "fan_speed": 2,
        "curtain_open": 1
      },
      "event_time": null
    }
  ],
  "seq": 1
}
```

程序收到云端下发 JSON 后，会查找 `fan_speed` 和 `curtain_open` 字段并执行控制。

## 连接超时排查

`ping` 通只说明域名能解析且 ICMP 可达，不代表 MQTT 的 `8883/tcp` 端口可连。开发板上先测试端口：

```sh
nc -vz 46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com 8883
```

如果系统没有 `nc`，可用：

```sh
telnet 46a22f6f08.st1.iotda-device.cn-north-4.myhuaweicloud.com 8883
```

端口不通时，重点检查开发板所在网络是否拦截公网 `8883/tcp`、华为云实例“设备接入”页的 MQTTS 地址和端口是否填写正确、是否误用了应用接入地址。端口已通但客户端提示 MQTT 鉴权失败时，再检查 `device_id`、`device_secret` 和系统 UTC 时间。
