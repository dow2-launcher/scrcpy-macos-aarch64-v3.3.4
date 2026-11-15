// Automated check of the web mode page in a real browser (headless Chrome), against WebFeed (see WebFeed.java).
//
//   npm install puppeteer-core
//   CHROME=/path/to/chrome node release/quest/e2e/web_test.js http://localhost:8090/ [screenshot-prefix]
//
// For each player (MSE, WebCodecs) and each processing place (device = flat stream, page = WebGL shader), the video must be playing,
// and the picture must not be black.
const puppeteer = require('puppeteer-core');

const base = process.argv[2] || 'http://localhost:8090/';
const shots = process.argv[3];

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function check(browser, player, where) {
    const page = await browser.newPage();
    await page.setViewport({width: 1920, height: 1080});
    const logs = [];
    page.on('console', (m) => logs.push(m.text()));
    page.on('pageerror', (e) => logs.push('PAGE ERROR: ' + e.message));
    await page.goto(base + '?stats=1&noblackcheck=1&player=' + player);
    await sleep(1500);
    await page.evaluate((w) => window.questview.send('set process=' + w), where);
    await sleep(6000);
    const state = await page.evaluate(() => window.questview.state());
    // Mean luma of the picture actually displayed
    const shot = await page.screenshot({type: 'png', path: shots ? `${shots}_${player}_${where}.png` : undefined});
    const luma = await page.evaluate(async (b64) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + b64;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = 192;
        c.height = 108;
        const ctx = c.getContext('2d');
        ctx.drawImage(img, 0, 0, c.width, c.height);
        const d = ctx.getImageData(0, 0, c.width, c.height).data;
        let sum = 0;
        for (let i = 0; i < d.length; i += 4) {
            sum += d[i] + d[i + 1] + d[i + 2];
        }
        return sum / (d.length / 4 * 3);
    }, Buffer.from(shot).toString('base64'));
    await page.close();

    const expected = player === 'mse' ? 'MSE' : 'WebCodecs';
    const errors = [];
    if (state.player !== expected) {
        errors.push(`player is ${state.player}, expected ${expected}`);
    }
    if (!state.hello || state.hello.quest.process !== where) {
        errors.push('processing place not applied');
    }
    // Without GPU (software WebGL and video decoding), the canvas modes are slow
    if (state.fps < (state.useCanvas ? 8 : 20)) {
        errors.push(`only ${state.fps.toFixed(1)} fps displayed`);
    }
    if (state.message) {
        errors.push('message displayed: ' + state.message);
    }
    if (luma < 8) {
        errors.push(`black picture (mean luma ${luma.toFixed(1)})`);
    }
    if (player === 'mse' && !(state.lag < 0.6)) {
        errors.push(`MSE buffer too long: ${state.lag}`);
    }
    if (where === 'page' && !state.gl) {
        errors.push('WebGL not used');
    }
    const lag = state.lag !== undefined && state.lag !== null ? ` buffer=${Math.round(state.lag * 1000)}ms` : '';
    console.log(`${errors.length ? 'FAIL' : 'ok  '} ${player}/${where}: ${state.hello ? state.hello.width + 'x' + state.hello.height : '?'}`
        + ` canvas=${state.useCanvas ? state.canvas.join('x') : 'no'} fps=${state.fps.toFixed(1)}${lag} luma=${luma.toFixed(1)}`);
    for (const e of errors) {
        console.log('     ' + e);
    }
    if (errors.length) {
        console.log(logs.join('\n'));
    }
    return errors.length === 0;
}

(async () => {
    const browser = await puppeteer.launch({
        executablePath: process.env.CHROME,
        headless: 'new',
        args: ['--no-sandbox', '--autoplay-policy=no-user-gesture-required', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
    });
    let ok = true;
    for (const player of ['mse', 'webcodecs']) {
        for (const where of ['device', 'page']) {
            ok = await check(browser, player, where) && ok;
        }
    }
    await browser.close();
    process.exit(ok ? 0 : 1);
})();
