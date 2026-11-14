# scrcpy-quest: Meta Quest 3 viewing mode (fork of scrcpy v3.3.4)

This fork adds a `--quest` mode to the scrcpy **client**: the raw Quest 3
display buffer (two fisheye, tilted eyes side by side) is turned into **one
flat picture** before being displayed: per-eye lens correction, mirrored
rotation, and merge of the two eyes along a clean seam (no ghosting, no
blending). The processing is done on the fly in the client (a few milliseconds
per frame), so the latency stays low. `scrcpy-server` is the unmodified
official v3.3.4 (scrcpy 4.x+ cannot capture the Quest 3).

[Русская версия ниже](#scrcpy-quest-режим-просмотра-meta-quest-3).

## Download and run (macOS)

1. Download `scrcpy-quest-macos-aarch64-*.zip` (Apple Silicon) or
   `scrcpy-quest-macos-x86_64-*.zip` (Intel) from the
   [releases](../../releases) and unzip it.
2. The binaries are not notarized by Apple (ad-hoc signature only), so macOS
   blocks them on first run. Remove the quarantine flag once:

   ```bash
   xattr -dr com.apple.quarantine ~/Downloads/scrcpy-quest-macos-aarch64-*
   ```

   (or right click → _Open_ on `scrcpy-quest.command`, then confirm _Open_; on
   recent macOS: _System Settings → Privacy & Security → Open Anyway_).
3. Enable developer mode on the Quest, plug it via USB and accept the USB
   debugging prompt in the headset (`./adb devices` must list it).
4. Double-click `scrcpy-quest.command`, or run from a terminal:

   ```bash
   cd ~/Downloads/scrcpy-quest-macos-aarch64-*
   ./scrcpy --quest --no-audio
   ```

Any other scrcpy option still works (`-s SERIAL`, `-f` for fullscreen,
`--max-fps=…`, `-b 16M`, `-m 2064` to reduce the resolution if it is too slow,
…). Audio forwarding does not work on Quest 3, hence `--no-audio`.

## Live controls

In `--quest` mode, these keys are handled by the window (they are _not_ sent
to the headset), without restarting. The keys are physical positions (US
layout), so they work with any keyboard layout. Hold <kbd>Shift</kbd> for
bigger steps.

| Keys                         | Action                                                  |
|------------------------------|---------------------------------------------------------|
| <kbd>1</kbd> <kbd>2</kbd> <kbd>3</kbd> <kbd>4</kbd> | View preset: A wide (~1.69:1), B (~1.13:1, default), C tall (~0.82:1), D full picture (black corners) |
| <kbd>[</kbd> <kbd>]</kbd>    | Rotation angle ∓0.1° (tilted horizon → adjust this)     |
| <kbd>-</kbd> <kbd>=</kbd>    | Lens correction `k1` ∓0.01 (curved straight lines)      |
| <kbd>;</kbd> <kbd>'</kbd>    | Lens correction `k2` ∓0.01                              |
| <kbd>,</kbd> <kbd>.</kbd>    | Eyes overlap `ovfrac` ∓0.002 (doubled or torn seam)     |
| <kbd>\</kbd>                | Toggle processing (show the raw buffer)                 |
| <kbd>/</kbd>                 | Toggle the text overlay (current values)                |
| <kbd>0</kbd>                 | Reset to the initial values                             |

The usual scrcpy shortcuts (<kbd>MOD</kbd>+<kbd>f</kbd> fullscreen,
<kbd>MOD</kbd>+<kbd>w</kbd> resize to fit…; <kbd>MOD</kbd> is <kbd>Alt</kbd> or
<kbd>Cmd</kbd>) are unchanged. Mouse clicks and touches are not forwarded in
this mode (the picture does not match the headset screen coordinates).

Once you have found good values, pass them on the command line:

```bash
./scrcpy --quest --quest-angle=21.0 --quest-k1=-0.26 --quest-k2=-0.05 \
         --quest-ovfrac=0.844 --quest-preset=B
```

Defaults: angle 20.9°, k1 −0.26, k2 −0.05, ovfrac 0.844, preset B.

## Build

The macOS binaries are built by [GitHub Actions](.github/workflows/macos.yml)
(static dependencies, official `scrcpy-server` and `adb` bundled). To build
locally, follow the [scrcpy build instructions](doc/build.md); the mode is
implemented in [`app/src/quest.c`](app/src/quest.c).

---

# scrcpy-quest: режим просмотра Meta Quest 3

Форк scrcpy v3.3.4 с режимом `--quest` в **клиенте**: сырой буфер экрана
Quest 3 (два «рыбьих глаза» рядом, с наклоном панелей) превращается в **один
ровный плоский кадр**: распрямление линз для каждого глаза, зеркальный поворот
и склейка двух глаз чистым швом (без двоения и без размытия). Обработка идёт
на лету в клиенте (несколько миллисекунд на кадр), задержка остаётся низкой.
`scrcpy-server` — официальный v3.3.4 без изменений (scrcpy 4.x и новее Quest 3
не захватывают).

## Скачать и запустить (macOS)

1. Скачайте `scrcpy-quest-macos-aarch64-*.zip` (Apple Silicon) или
   `scrcpy-quest-macos-x86_64-*.zip` (Intel) со страницы
   [релизов](../../releases) и распакуйте.
2. Программа не нотаризована Apple (подпись только ad-hoc), поэтому при первом
   запуске macOS её блокирует. Один раз снимите карантин:

   ```bash
   xattr -dr com.apple.quarantine ~/Downloads/scrcpy-quest-macos-aarch64-*
   ```

   (или правый клик → _Открыть_ на `scrcpy-quest.command` и подтвердить
   _Открыть_; в новых macOS: _Системные настройки → Конфиденциальность и
   безопасность → Всё равно открыть_).
3. Включите на шлеме режим разработчика, подключите его по USB и разрешите
   отладку по USB в шлеме (`./adb devices` должен показать устройство).
4. Дважды щёлкните `scrcpy-quest.command` или запустите из терминала:

   ```bash
   cd ~/Downloads/scrcpy-quest-macos-aarch64-*
   ./scrcpy --quest --no-audio
   ```

Остальные опции scrcpy работают как обычно (`-s SERIAL`, `-f` — на весь экран,
`--max-fps=…`, `-b 16M`, `-m 2064` — уменьшить разрешение, если тормозит, …).
Звук с Quest 3 через scrcpy не идёт, поэтому `--no-audio`.

## Управление в окне

В режиме `--quest` эти клавиши обрабатывает само окно (в шлем они _не_
уходят), перезапуск не нужен. Клавиши привязаны к физическому расположению
(как в английской раскладке), поэтому работают при любой раскладке. С
<kbd>Shift</kbd> шаг крупнее.

| Клавиши                      | Действие                                                |
|------------------------------|---------------------------------------------------------|
| <kbd>1</kbd> <kbd>2</kbd> <kbd>3</kbd> <kbd>4</kbd> | Пресет обзора: A широкий (~1.69:1), B (~1.13:1, по умолчанию), C вытянутый (~0.82:1), D весь кадр (с чёрными углами) |
| <kbd>[</kbd> <kbd>]</kbd>    | Угол поворота ∓0.1° (завален горизонт — крутить его)    |
| <kbd>-</kbd> <kbd>=</kbd>    | Распрямление `k1` ∓0.01 (прямые линии изогнуты)         |
| <kbd>;</kbd> <kbd>'</kbd>    | Распрямление `k2` ∓0.01                                 |
| <kbd>,</kbd> <kbd>.</kbd>    | Перекрытие глаз `ovfrac` ∓0.002 (шов двоит или расходится) |
| <kbd>\</kbd>                | Вкл/выкл обработку (показать сырой буфер)               |
| <kbd>/</kbd>                 | Показать/скрыть строку с текущими значениями            |
| <kbd>0</kbd>                 | Сбросить к начальным значениям                          |

Обычные сочетания scrcpy (<kbd>MOD</kbd>+<kbd>f</kbd> — полный экран,
<kbd>MOD</kbd>+<kbd>w</kbd> — подогнать окно…; <kbd>MOD</kbd> — это
<kbd>Alt</kbd> или <kbd>Cmd</kbd>) не изменились. Клики мышью и касания в этом
режиме в шлем не передаются (картинка не совпадает с координатами экрана
шлема).

Подобранные значения можно задать при запуске:

```bash
./scrcpy --quest --quest-angle=21.0 --quest-k1=-0.26 --quest-k2=-0.05 \
         --quest-ovfrac=0.844 --quest-preset=B
```

По умолчанию: угол 20.9°, k1 −0.26, k2 −0.05, ovfrac 0.844, пресет B.

---

_The original scrcpy README follows._

**This GitHub repo (<https://github.com/Genymobile/scrcpy>) is the only official
source for the project. Do not download releases from random websites, even if
their name contains `scrcpy`.**

# scrcpy (v3.3.4)

<img src="app/data/icon.svg" width="128" height="128" alt="scrcpy" align="right" />

_pronounced "**scr**een **c**o**py**"_

This application mirrors Android devices (video and audio) connected via USB or
[TCP/IP](doc/connection.md#tcpip-wireless) and allows control using the
computer's keyboard and mouse. It does not require _root_ access or an app
installed on the device. It works on _Linux_, _Windows_, and _macOS_.

![screenshot](assets/screenshot-debian-600.jpg)

It focuses on:

 - **lightness**: native, displays only the device screen
 - **performance**: 30~120fps, depending on the device
 - **quality**: 1920×1080 or above
 - **low latency**: [35~70ms][lowlatency]
 - **low startup time**: ~1 second to display the first image
 - **non-intrusiveness**: nothing is left installed on the Android device
 - **user benefits**: no account, no ads, no internet required
 - **freedom**: free and open source software

[lowlatency]: https://github.com/Genymobile/scrcpy/pull/646

Its features include:
 - [audio forwarding](doc/audio.md) (Android 11+)
 - [recording](doc/recording.md)
 - [virtual display](doc/virtual_display.md)
 - mirroring with [Android device screen off](doc/device.md#turn-screen-off)
 - [copy-paste](doc/control.md#copy-paste) in both directions
 - [configurable quality](doc/video.md)
 - [camera mirroring](doc/camera.md) (Android 12+)
 - [mirroring as a webcam (V4L2)](doc/v4l2.md) (Linux-only)
 - physical [keyboard][hid-keyboard] and [mouse][hid-mouse] simulation (HID)
 - [gamepad](doc/gamepad.md) support
 - [OTG mode](doc/otg.md)
 - and more…

[hid-keyboard]: doc/keyboard.md#physical-keyboard-simulation
[hid-mouse]: doc/mouse.md#physical-mouse-simulation

## Prerequisites

The Android device requires at least API 21 (Android 5.0).

[Audio forwarding](doc/audio.md) is supported for API >= 30 (Android 11+).

Make sure you [enabled USB debugging][enable-adb] on your device(s).

[enable-adb]: https://developer.android.com/studio/debug/dev-options#enable

On some devices (especially Xiaomi), you might get the following error:

```
Injecting input events requires the caller (or the source of the instrumentation, if any) to have the INJECT_EVENTS permission.
```

In that case, you need to enable [an additional option][control] `USB debugging
(Security Settings)` (this is an item different from `USB debugging`) to control
it using a keyboard and mouse. Rebooting the device is necessary once this
option is set.

[control]: https://github.com/Genymobile/scrcpy/issues/70#issuecomment-373286323

Note that USB debugging is not required to run scrcpy in [OTG mode](doc/otg.md).


## Get the app

 - [Linux](doc/linux.md)
 - [Windows](doc/windows.md) (read [how to run](doc/windows.md#run))
 - [macOS](doc/macos.md)


## Must-know tips

 - [Reducing resolution](doc/video.md#size) may greatly improve performance
   (`scrcpy -m1024`)
 - [_Right-click_](doc/mouse.md#mouse-bindings) triggers `BACK`
 - [_Middle-click_](doc/mouse.md#mouse-bindings) triggers `HOME`
 - <kbd>Alt</kbd>+<kbd>f</kbd> toggles [fullscreen](doc/window.md#fullscreen)
 - There are many other [shortcuts](doc/shortcuts.md)


## Usage examples

There are a lot of options, [documented](#user-documentation) in separate pages.
Here are just some common examples.

 - Capture the screen in H.265 (better quality), limit the size to 1920, limit
   the frame rate to 60fps, disable audio, and control the device by simulating
   a physical keyboard:

    ```bash
    scrcpy --video-codec=h265 --max-size=1920 --max-fps=60 --no-audio --keyboard=uhid
    scrcpy --video-codec=h265 -m1920 --max-fps=60 --no-audio -K  # short version
    ```

 - Start VLC in a new virtual display (separate from the device display):

    ```bash
    scrcpy --new-display=1920x1080 --start-app=org.videolan.vlc
    ```

 - Record the device camera in H.265 at 1920x1080 (and microphone) to an MP4
   file:

    ```bash
    scrcpy --video-source=camera --video-codec=h265 --camera-size=1920x1080 --record=file.mp4
    ```

 - Capture the device front camera and expose it as a webcam on the computer (on
   Linux):

    ```bash
    scrcpy --video-source=camera --camera-size=1920x1080 --camera-facing=front --v4l2-sink=/dev/video2 --no-playback
    ```

 - Control the device without mirroring by simulating a physical keyboard and
   mouse (USB debugging not required):

    ```bash
    scrcpy --otg
    ```

 - Control the device using gamepad controllers plugged into the computer:

    ```bash
    scrcpy --gamepad=uhid
    scrcpy -G  # short version
    ```

## User documentation

The application provides a lot of features and configuration options. They are
documented in the following pages:

 - [Connection](doc/connection.md)
 - [Video](doc/video.md)
 - [Audio](doc/audio.md)
 - [Control](doc/control.md)
 - [Keyboard](doc/keyboard.md)
 - [Mouse](doc/mouse.md)
 - [Gamepad](doc/gamepad.md)
 - [Device](doc/device.md)
 - [Window](doc/window.md)
 - [Recording](doc/recording.md)
 - [Virtual display](doc/virtual_display.md)
 - [Tunnels](doc/tunnels.md)
 - [OTG](doc/otg.md)
 - [Camera](doc/camera.md)
 - [Video4Linux](doc/v4l2.md)
 - [Shortcuts](doc/shortcuts.md)


## Resources

 - [FAQ](FAQ.md)
 - [Translations][wiki] (not necessarily up to date)
 - [Build instructions](doc/build.md)
 - [Developers](doc/develop.md)

[wiki]: https://github.com/Genymobile/scrcpy/wiki


## Articles

- [Introducing scrcpy][article-intro]
- [Scrcpy now works wirelessly][article-tcpip]
- [Scrcpy 2.0, with audio][article-scrcpy2]

[article-intro]: https://blog.rom1v.com/2018/03/introducing-scrcpy/
[article-tcpip]: https://www.genymotion.com/blog/open-source-project-scrcpy-now-works-wirelessly/
[article-scrcpy2]: https://blog.rom1v.com/2023/03/scrcpy-2-0-with-audio/

## Contact

You can open an [issue] for bug reports, feature requests or general questions.

For bug reports, please read the [FAQ](FAQ.md) first, you might find a solution
to your problem immediately.

[issue]: https://github.com/Genymobile/scrcpy/issues

You can also use:

 - Reddit: [`r/scrcpy`](https://www.reddit.com/r/scrcpy)
 - BlueSky: [`@scrcpy.bsky.social`](https://bsky.app/profile/scrcpy.bsky.social)
 - Twitter: [`@scrcpy_app`](https://twitter.com/scrcpy_app)


## Donate

I'm [@rom1v](https://github.com/rom1v), the author and maintainer of _scrcpy_.

If you appreciate this application, you can [support my open source
work][donate]:
 - [GitHub Sponsors](https://github.com/sponsors/rom1v)
 - [Liberapay](https://liberapay.com/rom1v/)
 - [PayPal](https://paypal.me/rom2v)

[donate]: https://blog.rom1v.com/about/#support-my-open-source-work

## License

    Copyright (C) 2018 Genymobile
    Copyright (C) 2018-2025 Romain Vimont

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
