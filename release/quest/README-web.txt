QuestView web mode (experimental): the headset itself serves a web page with its picture
https://github.com/dow2-launcher/scrcpy-macos-aarch64-v3.3.4 (branch quest-web)

What it is: a modified scrcpy-server (v3.3.4) started once over adb, which keeps running on the headset after the cable is unplugged. It
serves http://<headset IP>:8090/ : a page with a player. Open it in the browser of a TV, a phone or a computer on the same Wi-Fi. The
display is captured and encoded only while a page is open.

Check, step by step (macOS or Linux, adb in PATH or next to the script):

 1. Connect the headset with the cable (USB debugging allowed), then:      ./questweb.sh start
    It prints the address, for example http://192.168.1.50:8090/
 2. First open this address on the computer (Chrome/Safari): the picture must appear in 1-2 seconds. Enter or click opens the settings
    (eye, view, angle, lens k1/k2...), "I" shows the statistics (fps, Mbit/s, buffer).
 3. Unplug the cable, reload the page: it must still work.
 4. Open the same address in the TV browser. If the picture is black or does not start: menu -> "Размер потока" 1280x720, and tell
    which TV (brand, year) it is. On a 4K TV, try 3840x2160 for more details.
 5. Take the headset off for a minute, put it on again: the page reconnects by itself (if not: ./questweb.sh log).

    ./questweb.sh status | stop | log | wifi        (wifi: adb over Wi-Fi, to start without cable until the next reboot)

Settings at start (environment variables): EYE=left|right|both PRESET=A|B|C|D BOX=1920x1080 PROCESS=device|page BITRATE=12000000
FPS=60 PORT=8090. Example:   EYE=both PRESET=A BITRATE=16000000 ./questweb.sh start

PROCESS=device (default): the headset GPU straightens the picture (lens correction, rotation, crop), the stream is a flat picture of at
most BOX pixels: any browser able to play H.264 shows it. PROCESS=page: the raw two-eye buffer is sent (MAX_SIZE=2560 wide by default)
and the page straightens it with WebGL: lighter for the headset, but many TV browsers cannot use a video as a WebGL texture.

Known limits: no sound; the two eyes are merged along a straight seam (no automatic seam like in the Mac client); the address changes
if the router gives another IP to the headset (reserve the IP in the router); anyone on the Wi-Fi can open the page; it must be started
again after a reboot of the headset (a process started from adb cannot survive a reboot).

---

Веб-режим QuestView (эксперимент): очки сами раздают страничку со своей картинкой

Это изменённый scrcpy-server (3.3.4): запускается один раз через adb и продолжает работать на очках после отключения кабеля. Он раздаёт
http://<IP очков>:8090/ — страничку с плеером. Открывается в браузере телевизора, телефона или компьютера в той же сети Wi-Fi. Экран
снимается и кодируется, только пока страничка открыта.

Проверка по шагам (macOS или Linux, adb в PATH или рядом со скриптом):

 1. Подключить очки кабелем (отладка по USB разрешена), затем:      ./questweb.sh start
    Скрипт напечатает адрес, например http://192.168.1.50:8090/
 2. Сначала открыть адрес на компьютере (Chrome/Safari): картинка должна появиться за 1–2 секунды. Enter или щелчок — настройки (глаз,
    вид, угол, линза k1/k2…), клавиша I — статистика (кадры/с, Мбит/с, буфер).
 3. Отключить кабель и обновить страничку: всё должно работать дальше.
 4. Открыть тот же адрес в браузере телевизора. Если чёрный экран или видео не стартует: меню → «Размер потока» 1280x720 — и написать,
    какой телевизор (марка, год). На 4K-телевизоре попробовать 3840x2160: будет больше деталей.
 5. Снять очки на минуту и надеть снова: страничка переподключается сама (если нет — ./questweb.sh log).

    ./questweb.sh status | stop | log | wifi        (wifi — adb по Wi-Fi, чтобы до перезагрузки очков запускать без кабеля)

Настройки при запуске (переменные окружения): EYE=left|right|both PRESET=A|B|C|D BOX=1920x1080 PROCESS=device|page BITRATE=12000000
FPS=60 PORT=8090. Пример:   EYE=both PRESET=A BITRATE=16000000 ./questweb.sh start

PROCESS=device (по умолчанию): картинку распрямляет графический процессор очков (линза, поворот, кадрирование), в поток идёт уже
плоская картинка не больше BOX — её покажет любой браузер с H.264. PROCESS=page: идёт сырой буфер двух глаз (по умолчанию 2560 в ширину),
распрямляет страничка через WebGL — очкам легче, но многие браузеры телевизоров не умеют отдавать видео в WebGL.

Ограничения: звука нет; два глаза склеиваются по прямому шву (автоматического шва, как в клиенте для Mac, нет); адрес меняется, если
роутер выдаст очкам другой IP (закрепите IP в роутере); страничку может открыть любой в вашей сети Wi-Fi; после перезагрузки очков
нужно запустить заново (процесс, запущенный через adb, перезагрузку не переживает).
