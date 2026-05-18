# Smart Home Panel

This is a minimal Qt Widgets application for the IMX6ULL smart-home demo.

It directly uses the existing device nodes:

- `/dev/querydht11` for DHT11 temperature and humidity
- `/dev/mysr501` for SR501 PIR status
- `/dev/myrd03` for RD03 OT2 GPIO status
- `/dev/ttymxc5` for RD03 UART report data
- `/dev/fanmotor` for fan speed control
- `/dev/sg90` for servo control

Build on a machine that has the target Qt toolchain:

```sh
cd yuan_rebuilt/UI
qmake SmartHomePanel.pro
make
```

Run on the board after the drivers are loaded:

```sh
./smart_home_panel
```

Desktop launcher:

```sh
chmod +x start_smart_home_panel.sh
cp smart_home_panel.desktop /usr/share/applications/
```

If your application is not placed in `/root/yuan_rebuilt/UI`, edit the
`Exec=` line in `smart_home_panel.desktop`.

The app is intentionally simple: it is useful for board-side LCD testing and
for proving that the Qt layer can talk to the drivers directly.
