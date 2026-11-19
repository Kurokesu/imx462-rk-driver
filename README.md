# IMX462 kernel driver for Rockchip

[![Kernel 6.1.99-rockchip-rk3588](https://img.shields.io/badge/kernel-6.1.99--rockchip--rk3588-brightgreen?logo=linux&logoColor=white)](https://github.com/orangepi-xunlong/linux-orangepi/tree/orange-pi-6.1-rk35xx)

Rockchip vendor kernel driver for Sony IMX462, a 2 MP 1/2.8" STARVIS back-side illuminated CMOS sensor optimized for low-light and night-vision applications.

Current driver capabilities:

- 1920×1080 and 1280×720 at up to 60 fps through ISP
- 10-bit and 12-bit RAW output
- 2-lane MIPI CSI-2, up to 4 cameras
- HCG (High Conversion Gain) mode for improved low-light SNR
- Color and mono with rkaiq ISP tuning

*IMX462 camera modules are available at [kurokesu.com](https://www.kurokesu.com/item/462C-CSI)*

## Install

> [!NOTE]
> Tested on Orange Pi CM5 base board (RK3588S) with vendor kernel `6.1.99-rockchip-rk3588`.

Connect camera to any of cam1 to cam4 with board powered off.

Install required tools:

```bash
sudo apt install -y --no-install-recommends dkms
```

Clone this repository:

```bash
cd ~
git clone https://github.com/Kurokesu/imx462-rk-driver.git
cd imx462-rk-driver/
```

Run install script:

```bash
sudo ./install.sh
```

Install script:

- Builds and installs kernel module via [DKMS](https://github.com/dell/dkms)
- Builds and copies device tree overlays (`.dtbo`) to `/boot/overlay-user`
- Installs [ISP tuning](#isp-tuning) to `/etc/iqfiles`
- Loads module at boot through `/etc/modules-load.d/imx462.conf`

Edit boot configuration:

```bash
sudo nano /boot/orangepiEnv.txt
```

Add overlay for connected port (see [overlays](#overlays)) and append `initcall_blacklist` to `extraargs`:

```ini
user_overlays=imx462-cam1
extraargs=cma=128M initcall_blacklist=rkcif_clr_unready_dev,rkisp_clr_unready_dev
```

*Without `initcall_blacklist`, vendor rkcif and rkisp drop sensors that load as modules.*

Save and exit. Reboot for changes to take effect.

After reboot, verify sensor is detected:

```bash
grep imx462 /sys/class/video4linux/v4l-subdev*/name
```

Expected output (subdev number varies):

```
/sys/class/video4linux/v4l-subdev2/name:m00_b_imx462 4-001a
```

*Name ends in I2C bus and address, `4-001a` is cam1.*

## Overlays

Each port has color and mono overlays. For multiple cameras, separate entries with spaces:

```ini
user_overlays=imx462-cam1 imx462-mono-cam3
```

| Port | I2C name | ISP           | Color overlay | Mono overlay       |
| ---- | -------- | ------------- | ------------- | ------------------ |
| cam1 | `4-001a` | `rkisp0-vir0` | `imx462-cam1` | `imx462-mono-cam1` |
| cam2 | `3-001a` | `rkisp0-vir1` | `imx462-cam2` | `imx462-mono-cam2` |
| cam3 | `5-001a` | `rkisp1-vir1` | `imx462-cam3` | `imx462-mono-cam3` |
| cam4 | `6-001a` | `rkisp1-vir0` | `imx462-cam4` | `imx462-mono-cam4` |

## Image output

| Resolution | Bit depth | Color format | Mono format | Data rate / lane | Max fps |
| ---------- | --------- | ------------ | ----------- | ---------------- | ------- |
| 1920×1080  | 10        | `RG10`       | `Y10`       | 891 Mbps         | 60 fps  |
| 1920×1080  | 12        | `RG12`       | `Y12`       | 891 Mbps         | 60 fps  |
| 1280×720   | 10        | `RG10`       | `Y10`       | 594 Mbps         | 60 fps  |
| 1280×720   | 12        | `RG12`       | `Y12`       | 594 Mbps         | 60 fps  |

*See [frame rate](#frame-rate).*

### GStreamer

Find ISP output node of port by ISP name from [overlays](#overlays) (cam1 here):

```bash
ISP=$(for m in /dev/media*; do media-ctl -d $m -p | grep -q 'driver *rkisp0-vir0$' && media-ctl -d $m -e rkisp_mainpath; done)
```

*Video node numbers change with overlay set, so nodes are looked up by name.*

Start live preview on desktop:

```bash
gst-launch-1.0 v4l2src device=$ISP ! \
   video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
   xvimagesink sync=false
```

*Caps `framerate` only labels stream. Sensor [frame rate](#frame-rate) sets real rate.*

Capture still image on headless system:

```bash
gst-launch-1.0 v4l2src device=$ISP num-buffers=90 ! \
   video/x-raw,format=NV12,width=1920,height=1080 ! \
   jpegenc ! multifilesink location=test.jpg
```

Record 10 s of H.264 1080p30 through hardware encoder:

```bash
gst-launch-1.0 -e v4l2src device=$ISP num-buffers=300 min-buffers=4 ! \
   video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! \
   mpph264enc bps=8000000 rc-mode=cbr ! h264parse ! mp4mux ! filesink location=test.mp4
```

*Without `min-buffers=4`, `v4l2src` queues only 2 buffers and rkisp drops frames.*

> [!IMPORTANT]
> Stock `gstreamer1.0-rockchip1` encodes at about 6 fps. Plugin from Rockchip SDK release `linux-6.1-stan-rkr4` reaches 30 fps.

<details>
<summary>Install gstreamer-rockchip</summary>

Build:

```bash
sudo apt install -y --no-install-recommends meson ninja-build
cd ~
curl -LO https://gitlab.com/rockchip_linux_sdk_6.1/linux/gstreamer-rockchip/-/archive/linux-6.1-stan-rkr4/gstreamer-rockchip-linux-6.1-stan-rkr4.tar.gz
tar xf gstreamer-rockchip-linux-6.1-stan-rkr4.tar.gz
cd gstreamer-rockchip-linux-6.1-stan-rkr4
meson setup build --prefix=/usr -Drkximage=disabled -Dkmssrc=disabled -Dvpxalphadec=disabled
ninja -C build
```

Replace stock MPP plugin:

```bash
sudo dpkg-divert --local --rename --add /usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstrockchipmpp.so
sudo ninja -C build install
```

*Divert moves stock plugin aside to `libgstrockchipmpp.so.distrib` and keeps apt upgrades from overwriting new one.*

</details>

### Raw V4L2

Find raw node and sensor subdev of port by I2C name (cam1 here):

```bash
RAW=$(for m in /dev/media*; do media-ctl -d $m -p | grep -q 'imx462 4-001a' && media-ctl -d $m -e stream_cif_mipi_id0; done)
SUBDEV=/dev/$(grep -l 'imx462 4-001a' /sys/class/video4linux/v4l-subdev*/name | cut -d/ -f5)
```

Stream raw 10-bit frames to file with rkaiq stopped, so exposure and gain stay as set:

```bash
sudo systemctl stop rkaiq_3A
v4l2-ctl -d $SUBDEV --set-ctrl exposure=1000,analogue_gain=0
v4l2-ctl -d $RAW --set-fmt-video=width=1920,height=1080,pixelformat=RG10 \
   --stream-mmap --stream-skip=10 --stream-count=10 --stream-to=frames.raw
sudo systemctl start rkaiq_3A
```

*Exposure is in lines (14.8 µs at 1080p), gain in 0.3 dB steps.*

For 12-bit, set sensor format first:

```bash
v4l2-ctl -d $SUBDEV --set-subdev-fmt pad=0,width=1920,height=1080,code=0x3012
```

*Frames are saved as packed little-endian bitstream.*

### Frame rate

Sensor starts at 30 fps after every boot. Set new rate on sensor subdev, with `$SUBDEV` from [raw V4L2](#raw-v4l2), then restart rkaiq:

```bash
v4l2-ctl -d $SUBDEV --set-subdev-fps pad=0,fps=60
sudo systemctl restart rkaiq_3A
```

*rkaiq reads sensor rate only when it starts. Raw capture follows new rate without restart.*

ISP tuning uses 50 Hz anti-flicker, so auto exposure moves in 10 ms steps. Frame rate caps longest exposure:

| Frame rate      | Max exposure |
| --------------- | ------------ |
| 50 to 60 fps    | 10 ms        |
| 34 to 49 fps    | 20 ms        |
| 33 fps or lower | 30 ms        |

*In low light, 49 fps gives double exposure of 60 fps. ISP tuning tops out at 30 ms, so rates below 33 fps add no exposure.*

## Test mode

IMX462 has built-in test pattern generator for verifying data path.

Enable test pattern on sensor subdev, with `$SUBDEV` from [raw V4L2](#raw-v4l2):

```bash
# Horizontal color-bar chart example (test_pattern = 2)
v4l2-ctl -d $SUBDEV --set-ctrl test_pattern=2
```

Turn test pattern off:

```bash
v4l2-ctl -d $SUBDEV --set-ctrl test_pattern=0
```

| Test pattern code | Description                |
| ----------------- | -------------------------- |
| 0                 | Disabled                   |
| 1                 | Sequence Pattern 1         |
| 2                 | Horizontal Color-bar Chart |
| 3                 | Vertical Color-bar Chart   |
| 4                 | Sequence Pattern 2         |
| 5                 | Gradation Pattern 1        |
| 6                 | Gradation Pattern 2        |
| 7                 | 000/555h Toggle Pattern    |

## ISP tuning

Install script copies color and mono rkaiq tuning files to `/etc/iqfiles`. Overlay selects matching one.

rkaiq switches between LCG (Low Conversion Gain) and HCG on its own as gain rises.

## Limitations

- libcamera is not supported. It has no pipeline handler for vendor rkisp and rkaiq stack.
- Mono tuning has lens shading off, since with `Y10` it makes ISP output all zero.
- Sometimes `ksoftirqd/0` runs at 100% and lens shading stops updating, seen at boot and after rkaiq restarts. This is a vendor rkisp bug and only a reboot clears it.
- Above 58 fps, rkcif logs `vblank need >= 1000us if isp work in online` at each stream start. ISP still runs at full rate without frame loss.

## Development builds

For manual builds without DKMS:

```bash
make            # build everything (dtbo + kernel module)
make dtbo       # build only device tree overlays
make module     # build only kernel module
make clean      # remove build artifacts
```

Build artifacts are placed in `./build`.

> [!NOTE]
> Module loaded with `insmod` after boot gives raw capture only, since rkcif links ISP only to sensors bound within 5 s of its probe. Use `sudo ./install.sh` and reboot for ISP path.

## Uninstall

Run uninstall script:

```bash
sudo ./uninstall.sh
```

Remove `user_overlays` and `initcall_blacklist` entries from `/boot/orangepiEnv.txt`, then reboot.

## Special thanks

- FRAMOS GmbH, Manivannan Sadhasivam (Linaro) and all later contributors to mainline `imx290.c`, which this driver is based on.
