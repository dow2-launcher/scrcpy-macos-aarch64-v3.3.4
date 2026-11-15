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
| <kbd>5</kbd>                 | Eyes: both (merged) → left eye only → right eye only (a single eye is lens-corrected and rotated, without merge, with its own presets) |
| <kbd>6</kbd>                 | Seam mode: `auto` → `lock` → `fixed` → `dynamic` (see below) |
| <kbd>7</kbd>                 | Recompute the seam now (useful in `lock` mode)          |
| <kbd>8</kbd> <kbd>9</kbd>    | Straight vertical seam (`fixed`): move it left / right by ~2 px (~20 px with <kbd>Shift</kbd>) |
| <kbd>[</kbd> <kbd>]</kbd>    | Rotation angle ∓0.1° (tilted horizon → adjust this)     |
| <kbd>-</kbd> <kbd>=</kbd>    | Lens correction `k1` ∓0.01 (curved straight lines)      |
| <kbd>;</kbd> <kbd>'</kbd>    | Lens correction `k2` ∓0.01                              |
| <kbd>,</kbd> <kbd>.</kbd>    | Eyes overlap `ovfrac` ∓0.002 (doubled or torn seam)     |
| <kbd>Q</kbd> / <kbd>Shift</kbd>+<kbd>Q</kbd> | Video quality: next / previous bit rate (16, 30, 50, 80, 120, 200 Mbps) |
| <kbd>H</kbd>                 | Video codec: H.264 ↔ H.265 (H.265 is decoded in software: slower, adds latency) |
| <kbd>T</kbd>                 | Decoding threads: auto → 1 → 2 → 3 → 4 → 6             |
| <kbd>V</kbd>                 | Hardware decoding (VideoToolbox, experimental) on / off |
| <kbd>F</kbd>                 | Toggle fullscreen                                       |
| <kbd>M</kbd>                 | Resize the window to fill the screen                    |
| <kbd>P</kbd>                 | Toggle the performance line (also printed to the terminal every second) |
| <kbd>\</kbd>                | Toggle processing (show the raw buffer)                 |
| <kbd>/</kbd>                 | Toggle the text overlay (current values)                |
| <kbd>0</kbd>                 | Reset everything to the default values                  |

Seam modes (the cut between the two eyes; `--quest-seam=…`):

 - `auto` (default): optimal curved seam. It is kept as long as it is good and
   replaced only when it has been clearly worse than a new one for a while, so
   the picture no longer jumps on every frame.
 - `lock`: the current seam is frozen (press <kbd>7</kbd> to recompute it).
 - `fixed`: straight vertical cut, at the center of the overlap by default;
   move it with <kbd>8</kbd> / <kbd>9</kbd> (or `--quest-seam-x=…`). Pressing
   <kbd>8</kbd> or <kbd>9</kbd> switches to this mode.
 - `dynamic`: optimal seam recomputed on every frame (the behavior of the
   first version; the picture may jump).

The overlay shows the eye mode, the seam mode and the seam position (`X`, in
pixels of the merged picture).

**Video quality.** Blocky artifacts come from the encoder bit rate: the scrcpy
default (8 Mbps) is far too low for the Quest display size, so in `--quest`
mode the default is **30 Mbps** (unless `-b`/`--video-bit-rate` is given). The
encoder and decoder parameters cannot be changed on a running stream, so
<kbd>Q</kbd>/<kbd>H</kbd>/<kbd>T</kbd>/<kbd>V</kbd> restart the mirroring with the new values about one
second after the last key press: the window closes and reopens at the same
place within a couple of seconds, and all the other settings are kept. The
overlay shows the requested bit rate, the codec and the bit rate actually
received (`ACTUAL`; the encoder produces less on static pictures). The new
quality is remembered only once a picture has been received with it: if the
headset encoder rejects a setting, scrcpy exits and the next run uses the
previous working values. The codec is never remembered (H.264 unless
`--video-codec` is given). The effective values are printed on start
(`Quest stream: …`).

**Latency.** The pictures are big (4128×2208 at up to 90 fps), and the higher
the bit rate, the longer each picture takes to decode. When the decoder is
slower than the stream, the pictures queue up before it and the delay grows
to hundreds of milliseconds: measured on an Apple Silicon Mac, one thread
keeps up with H.264 at 30 Mbps, but not at 40 Mbps (~13 ms per picture). So:

 - up to 32 Mbps, a single decoding thread is used (lowest latency);
 - above, several threads are used automatically (2 up to 56 Mbps, 3 up to
   90, 4 up to 130, then 6): with N threads the decoder is up to N times
   faster, but its output is delayed by N−1 pictures (~13 ms each at 78 fps).
   <kbd>T</kbd> or `--quest-decode-threads=N` forces a value;
 - <kbd>V</kbd> or `--quest-hwdec=on` enables hardware decoding by
   VideoToolbox (macOS only, **experimental**: not tested on a real Mac with
   the headset). If it works, it is the fastest option at any bit rate; the
   performance line then shows `(HW)`;
 - if the performance line shows `LOAD` close to 100 % `(TOO SLOW)`, or a
   growing `DELAY`, the decoder does not keep up: add threads
   (<kbd>T</kbd>), lower the quality (<kbd>Shift</kbd>+<kbd>Q</kbd>) or limit
   the frame rate (`--max-fps=60`).

Keep **H.264** (the default): on a Mac, H.265 is decoded in software unless
hardware decoding is enabled, which is too slow for this picture size and
adds a clearly visible delay; the overlay marks it `(SLOWER)`. Moreover, the Quest H.265 encoder rejects the full 4128-pixel
width, so with H.265 the size is limited to 4096 (`--max-size` to change);
when an encoder fails, the headset silently retries with a much smaller
picture: the actual size is logged (`Quest input: WxH`) and shown in the
performance line.

**Performance line** (<kbd>P</kbd> or `--quest-perf`):
`DECODE` (ms per frame in the video decoder, with the number of threads or
`HW`, and `LOAD`: the share of time its thread spends decoding), `QUEST`
(processing), `DRAW`
(processing + texture upload + rendering), `FPS` decoded / shown / skipped
(decoded but replaced by a newer frame before being shown), `DELAY` and
`INPUT` (decoded picture size). `DELAY` is only an **estimate** of the
additional stream delay: the time between the capture timestamp set by the
headset and the end of decoding, minus the smallest value seen since the
start. It shows how much later than the best frame the pictures arrive
(network or decoding backlog), not the absolute latency.

**Bigger picture.** The window can be resized freely, <kbd>M</kbd> makes it
fill the screen and <kbd>F</kbd> switches to fullscreen; the picture is
scaled with linear filtering. The source has no more pixels than the preset
size shown in the overlay, so a bigger window is smoother, not more detailed.
If the picture looks rough when the window is much smaller than the video,
try `--render-driver=opengl` (enables trilinear filtering).

**The last values are remembered**: they are saved to
`~/.config/scrcpy-quest.conf` on every change and restored on the next run
(command line options take precedence; `--quest-no-config` disables this).
On every change, the equivalent command line is also printed to the terminal.

The usual scrcpy shortcuts (<kbd>MOD</kbd>+<kbd>f</kbd> fullscreen,
<kbd>MOD</kbd>+<kbd>w</kbd> resize to fit…; <kbd>MOD</kbd> is <kbd>Alt</kbd> or
<kbd>Cmd</kbd>) are unchanged. Mouse clicks and touches are not forwarded in
this mode (the picture does not match the headset screen coordinates).

The values can also be passed on the command line:

```bash
./scrcpy --quest --quest-angle=21.0 --quest-k1=-0.26 --quest-k2=-0.05 \
         --quest-ovfrac=0.844 --quest-preset=B --quest-eye=both \
         --quest-seam=fixed --quest-seam-x=0 -b 120M --video-codec=h265
```

Defaults: angle 20.9°, k1 −0.26, k2 −0.05, ovfrac 0.844, preset B, both eyes,
seam `auto`, 30 Mbps H.264.

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
| <kbd>5</kbd>                 | Глаза: оба (склейка) → только левый → только правый (один глаз распрямлён и повёрнут, без склейки, со своими пресетами) |
| <kbd>6</kbd>                 | Режим шва: `auto` → `lock` → `fixed` → `dynamic` (см. ниже) |
| <kbd>7</kbd>                 | Пересчитать шов сейчас (полезно в режиме `lock`)        |
| <kbd>8</kbd> <kbd>9</kbd>    | Прямой вертикальный шов (`fixed`): сдвинуть влево / вправо на ~2 пикселя (с <kbd>Shift</kbd> — на ~20) |
| <kbd>[</kbd> <kbd>]</kbd>    | Угол поворота ∓0.1° (завален горизонт — крутить его)    |
| <kbd>-</kbd> <kbd>=</kbd>    | Распрямление `k1` ∓0.01 (прямые линии изогнуты)         |
| <kbd>;</kbd> <kbd>'</kbd>    | Распрямление `k2` ∓0.01                                 |
| <kbd>,</kbd> <kbd>.</kbd>    | Перекрытие глаз `ovfrac` ∓0.002 (шов двоит или расходится) |
| <kbd>Q</kbd> / <kbd>Shift</kbd>+<kbd>Q</kbd> | Качество видео: следующий / предыдущий битрейт (16, 30, 50, 80, 120, 200 Мбит/с) |
| <kbd>H</kbd>                 | Кодек: H.264 ↔ H.265 (H.265 декодируется программно: медленнее, добавляет задержку) |
| <kbd>T</kbd>                 | Потоки декодера: авто → 1 → 2 → 3 → 4 → 6              |
| <kbd>V</kbd>                 | Аппаратное декодирование (VideoToolbox, экспериментально) вкл/выкл |
| <kbd>F</kbd>                 | Полный экран                                            |
| <kbd>M</kbd>                 | Растянуть окно на весь экран                            |
| <kbd>P</kbd>                 | Строка производительности (также раз в секунду печатается в терминал) |
| <kbd>\</kbd>                | Вкл/выкл обработку (показать сырой буфер)               |
| <kbd>/</kbd>                 | Показать/скрыть строку с текущими значениями            |
| <kbd>0</kbd>                 | Сбросить всё к значениям по умолчанию                   |

Режимы шва (линия стыка двух глаз; `--quest-seam=…`):

 - `auto` (по умолчанию): оптимальный кривой шов. Держится, пока он хорош, и
   заменяется, только если какое-то время был заметно хуже нового, поэтому
   картинка больше не скачет на каждом кадре.
 - `lock`: текущий шов заморожен (<kbd>7</kbd> — пересчитать).
 - `fixed`: прямой вертикальный разрез, по умолчанию по центру перекрытия;
   двигается клавишами <kbd>8</kbd> / <kbd>9</kbd> (или `--quest-seam-x=…`).
   Нажатие <kbd>8</kbd> или <kbd>9</kbd> само включает этот режим.
 - `dynamic`: оптимальный шов заново на каждом кадре (как в первой версии;
   картинка может скакать).

В строке значений видно режим глаз, режим шва и положение шва (`X`, в
пикселях склеенного кадра).

**Качество видео.** Картинка рассыпается квадратами из-за битрейта кодировщика:
стандартные для scrcpy 8 Мбит/с слишком малы для экрана Quest, поэтому в
режиме `--quest` по умолчанию **30 Мбит/с** (если не задан
`-b`/`--video-bit-rate`). Параметры кодировщика и декодера нельзя менять на
идущем потоке, поэтому <kbd>Q</kbd>/<kbd>H</kbd>/<kbd>T</kbd>/<kbd>V</kbd> перезапускают трансляцию с новыми
значениями примерно через секунду после последнего нажатия: окно закрывается и
за пару секунд открывается на том же месте, остальные настройки сохраняются. В
строке значений видны запрошенный битрейт, кодек и реально приходящий битрейт
(`ACTUAL`; на неподвижной картинке кодировщик выдаёт меньше). Новое качество
запоминается, только когда с ним пришла картинка: если кодировщик шлема не
принял настройку, scrcpy завершится, а следующий запуск возьмёт прежние
рабочие значения. Кодек не запоминается никогда (H.264, если не задан
`--video-codec`). Действующие значения печатаются при старте
(`Quest stream: …`).

**Задержка.** Кадры большие (4128×2208 при частоте до 90 кадров/с), и чем
выше битрейт, тем дольше декодируется каждый кадр. Когда декодер медленнее
потока, кадры копятся перед ним в очереди и задержка растёт до сотен
миллисекунд: по замеру на Mac с Apple Silicon один поток успевает за H.264 на
30 Мбит/с, но не на 40 (~13 мс на кадр). Поэтому:

 - до 32 Мбит/с работает один поток декодера (наименьшая задержка);
 - выше потоки добавляются автоматически (2 до 56 Мбит/с, 3 до 90, 4 до 130,
   дальше 6): с N потоками декодер быстрее до N раз, но выдаёт кадры с
   опозданием на N−1 кадр (~13 мс каждый при 78 кадрах/с). <kbd>T</kbd> или
   `--quest-decode-threads=N` задаёт число вручную;
 - <kbd>V</kbd> или `--quest-hwdec=on` включает аппаратное декодирование
   через VideoToolbox (только macOS, **экспериментально**: на настоящем Mac
   со шлемом не проверено). Если работает — это самый быстрый вариант на любом
   битрейте; в строке производительности тогда видно `(HW)`;
 - если в строке производительности `LOAD` около 100 % `(TOO SLOW)` или растёт
   `DELAY`, декодер не успевает: добавьте потоки (<kbd>T</kbd>), снизьте
   качество (<kbd>Shift</kbd>+<kbd>Q</kbd>) или ограничьте частоту кадров
   (`--max-fps=60`).

Оставляйте **H.264** (по умолчанию): на Mac H.265 без аппаратного
декодирования декодируется программно, для такого размера кадра это слишком
медленно и даёт заметную задержку; в строке значений он помечен `(SLOWER)`.
Кроме того, кодировщик H.265 в Quest не принимает
полную ширину 4128 пикселей, поэтому с H.265 размер ограничивается 4096
(меняется через `--max-size`); если кодировщик не справился, шлем молча
повторяет с намного меньшей картинкой: реальный размер пишется в лог
(`Quest input: WxH`) и виден в строке производительности.

**Строка производительности** (<kbd>P</kbd> или `--quest-perf`):
`DECODE` (мс на кадр в декодере, число потоков или `HW`, и `LOAD` — доля
времени, которую его поток занят декодированием), `QUEST` (обработка), `DRAW` (обработка +
загрузка текстуры + отрисовка), `FPS` декодировано / показано / пропущено
(декодирован, но заменён более новым кадром до показа), `DELAY` и `INPUT`
(размер декодированного кадра). `DELAY` — только **оценка** добавочной
задержки потока: время от метки захвата, поставленной шлемом, до конца
декодирования, минус наименьшее значение с начала сеанса. Она показывает,
насколько кадры приходят позже лучшего кадра (сеть или очередь декодера), а
не абсолютную задержку.

**Картинка крупнее.** Окно можно свободно растягивать, <kbd>M</kbd>
разворачивает его на весь экран, <kbd>F</kbd> включает полноэкранный режим;
картинка масштабируется с линейной фильтрацией. В источнике не больше
пикселей, чем размер пресета в строке значений, поэтому в большом окне
картинка глаже, но не детальнее. Если картинка грубая, когда окно намного
меньше видео, попробуйте `--render-driver=opengl` (включает трилинейную
фильтрацию).

**Последние значения запоминаются**: при каждом изменении они сохраняются в
`~/.config/scrcpy-quest.conf` и подхватываются при следующем запуске (опции
командной строки главнее; `--quest-no-config` отключает это). При каждом
изменении в терминал также печатается готовая командная строка.

Обычные сочетания scrcpy (<kbd>MOD</kbd>+<kbd>f</kbd> — полный экран,
<kbd>MOD</kbd>+<kbd>w</kbd> — подогнать окно…; <kbd>MOD</kbd> — это
<kbd>Alt</kbd> или <kbd>Cmd</kbd>) не изменились. Клики мышью и касания в этом
режиме в шлем не передаются (картинка не совпадает с координатами экрана
шлема).

Значения можно задать и при запуске:

```bash
./scrcpy --quest --quest-angle=21.0 --quest-k1=-0.26 --quest-k2=-0.05 \
         --quest-ovfrac=0.844 --quest-preset=B --quest-eye=both \
         --quest-seam=fixed --quest-seam-x=0 -b 120M --video-codec=h265
```

По умолчанию: угол 20.9°, k1 −0.26, k2 −0.05, ovfrac 0.844, пресет B, оба
глаза, шов `auto`, 30 Мбит/с H.264.

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
