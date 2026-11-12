// �?Edge headless �?meteocons �?SMIL 动画逐帧渲染成透明 PNG
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer-core');

const EDGE = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const SRC = path.join(__dirname, 'anime', 'package', 'fill');
const OUT = path.join(__dirname, 'anim');
const SIZE = 120;
const FRAMES = 8;

const icons = [
  'clear-day', 'clear-night', 'partly-cloudy-day', 'partly-cloudy-night',
  'cloudy', 'overcast', 'fog', 'drizzle', 'rain', 'overcast-rain',
  'partly-cloudy-day-rain', 'snow', 'sleet', 'thunderstorms-rain', 'mist',
];

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await puppeteer.launch({
    executablePath: EDGE,
    headless: true,
    args: ['--no-sandbox', '--disable-gpu', '--hide-scrollbars'],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: SIZE, height: SIZE, deviceScaleFactor: 1 });

  const meta = {};

  for (const name of icons) {
    const file = path.join(SRC, name + '.svg');
    if (!fs.existsSync(file)) {
      console.log('missing', name);
      continue;
    }
    const svg = fs.readFileSync(file, 'utf8');
    await page.setContent(
      '<style>html,body{margin:0;padding:0;background:transparent}svg{width:' +
        SIZE + 'px;height:' + SIZE + 'px;display:block}</style>' + svg,
      { waitUntil: 'load' }
    );

    const dur = await page.evaluate(() => {
      const svg = document.querySelector('svg');
      svg.pauseAnimations();
      let d = 0;
      document.querySelectorAll('animate, animateTransform, animateMotion').forEach((a) => {
        const raw = a.getAttribute('dur') || '0s';
        const v = parseFloat(raw) * (raw.indexOf('ms') >= 0 ? 0.001 : 1);
        if (v > d) d = v;
      });
      return d > 0 ? d : 2;
    });

    const dir = path.join(OUT, name);
    fs.mkdirSync(dir, { recursive: true });
    for (let i = 0; i < FRAMES; i++) {
      const t = (dur * i) / FRAMES;
      await page.evaluate((tt) => document.querySelector('svg').setCurrentTime(tt), t);
      await page.screenshot({
        path: path.join(dir, String(i).padStart(2, '0') + '.png'),
        omitBackground: true,
      });
    }
    meta[name] = { dur: dur, frames: FRAMES };
    console.log(name, 'dur', dur + 's');
  }

  fs.writeFileSync(path.join(OUT, 'meta.json'), JSON.stringify(meta, null, 1));
  await browser.close();
})();
