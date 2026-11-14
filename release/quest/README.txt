scrcpy-quest: scrcpy v3.3.4 with a Meta Quest 3 viewing mode (--quest)
https://github.com/dow2-launcher/scrcpy-macos-aarch64-v3.3.4

First run (the binaries are not notarized by Apple), in a terminal:

    xattr -dr com.apple.quarantine /path/to/this/folder

or right click -> Open on scrcpy-quest.command.

Run: double-click scrcpy-quest.command, or:

    ./scrcpy --quest --no-audio

Keys in the window (US layout positions, hold Shift for bigger steps):

    1 2 3 4   view preset A (wide) / B / C (tall) / D (full picture)
    [ ]       rotation angle -/+ 0.1 degree
    - =       lens correction k1 -/+ 0.01
    ; '       lens correction k2 -/+ 0.01
    , .       eyes overlap -/+ 0.002
    \         toggle processing (raw buffer)
    /         toggle the text overlay
    0         reset to the initial values

Starting values: --quest-angle=20.9 --quest-k1=-0.26 --quest-k2=-0.05
                 --quest-ovfrac=0.844 --quest-preset=B

---

Первый запуск (программа не нотаризована Apple), в терминале:

    xattr -dr com.apple.quarantine /путь/к/этой/папке

или правый клик -> Открыть на scrcpy-quest.command.

Запуск: двойной щелчок по scrcpy-quest.command или:

    ./scrcpy --quest --no-audio

Клавиши в окне (по расположению английской раскладки, с Shift шаг крупнее):

    1 2 3 4   пресет обзора A (широкий) / B / C (вытянутый) / D (весь кадр)
    [ ]       угол поворота -/+ 0.1 градуса
    - =       распрямление k1 -/+ 0.01
    ; '       распрямление k2 -/+ 0.01
    , .       перекрытие глаз -/+ 0.002
    \         вкл/выкл обработку (сырой буфер)
    /         показать/скрыть строку значений
    0         сброс к начальным значениям
