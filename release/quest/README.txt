scrcpy-quest: scrcpy v3.3.4 with a Meta Quest 3 viewing mode (--quest)
https://github.com/dow2-launcher/scrcpy-macos-aarch64-v3.3.4

First run (the binaries are not notarized by Apple), in a terminal:

    xattr -dr com.apple.quarantine /path/to/this/folder

or right click -> Open on scrcpy-quest.command.

Run: double-click scrcpy-quest.command, or:

    ./scrcpy --quest --no-audio

Keys in the window (US layout positions, hold Shift for bigger steps):

    1 2 3 4   view preset A (wide) / B / C (tall) / D (full picture)
    5         eyes: both (merged) / left only / right only
    6         seam mode: auto / lock / fixed (straight vertical) / dynamic
    7         recompute the seam (lock mode)
    8 9       move the straight vertical seam left / right (switches to fixed)
    [ ]       rotation angle -/+ 0.1 degree
    - =       lens correction k1 -/+ 0.01
    ; '       lens correction k2 -/+ 0.01
    , .       eyes overlap -/+ 0.002
    Q         video quality: next bit rate 16/30/50/80/120/200 Mbps (Shift+Q:
              previous); default 30 Mbps; the mirroring restarts to apply it
    T         decoding threads auto/1/2/3/4/6 (more threads if the decoder
              is too slow: "LOAD ~100% (TOO SLOW)" in the performance line)
    V         hardware decoding (VideoToolbox, experimental) on/off
    H         video codec H.264 / H.265 (restarts too; H.265 is slower on a
              Mac and adds latency: keep H.264)
    F         fullscreen
    M         resize the window to fill the screen
    P         performance line (decode/processing/draw ms, fps, delay)
    \         toggle processing (raw buffer)
    /         toggle the text overlay
    0         reset to the default values

The last values are saved to ~/.config/scrcpy-quest.conf and restored on the
next run. The equivalent command line is printed on every change, e.g.:

    ./scrcpy --quest --quest-angle=20.9 --quest-k1=-0.26 --quest-k2=-0.05
             --quest-ovfrac=0.844 --quest-preset=B --quest-eye=both
             --quest-seam=auto --quest-seam-x=0

---

Первый запуск (программа не нотаризована Apple), в терминале:

    xattr -dr com.apple.quarantine /путь/к/этой/папке

или правый клик -> Открыть на scrcpy-quest.command.

Запуск: двойной щелчок по scrcpy-quest.command или:

    ./scrcpy --quest --no-audio

Клавиши в окне (по расположению английской раскладки, с Shift шаг крупнее):

    1 2 3 4   пресет обзора A (широкий) / B / C (вытянутый) / D (весь кадр)
    5         глаза: оба (склейка) / только левый / только правый
    6         режим шва: auto / lock / fixed (прямой вертикальный) / dynamic
    7         пересчитать шов (режим lock)
    8 9       сдвинуть прямой вертикальный шов влево / вправо (включает fixed)
    [ ]       угол поворота -/+ 0.1 градуса
    - =       распрямление k1 -/+ 0.01
    ; '       распрямление k2 -/+ 0.01
    , .       перекрытие глаз -/+ 0.002
    Q         качество: следующий битрейт 16/30/50/80/120/200 Мбит/с (Shift+Q —
              предыдущий); по умолчанию 30; трансляция перезапускается
    T         потоки декодера авто/1/2/3/4/6 (больше потоков, если декодер не
              успевает: "LOAD ~100% (TOO SLOW)" в строке производительности)
    V         аппаратное декодирование (VideoToolbox, экспериментально)
    H         кодек H.264 / H.265 (тоже с перезапуском; H.265 на Mac медленнее
              и даёт задержку: оставляйте H.264)
    F         полный экран
    M         растянуть окно на весь экран
    P         строка производительности (мс декодера/обработки, fps, задержка)
    \         вкл/выкл обработку (сырой буфер)
    /         показать/скрыть строку значений
    0         сброс к значениям по умолчанию

Последние значения сохраняются в ~/.config/scrcpy-quest.conf и подхватываются
при следующем запуске; готовая командная строка печатается в терминал.
