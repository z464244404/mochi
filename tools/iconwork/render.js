// 把 meteocons 的 SVG 渲染成 PNG（Node 14 + @resvg/resvg-js）
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

const SRC = path.join(__dirname, 'package', 'fill');
const OUT = path.join(__dirname, 'png');
fs.mkdirSync(OUT, { recursive: true });

// 名字: 输出尺寸
const icons = [
  ['clear-day', 128],
  ['clear-night', 128],
  ['partly-cloudy-day', 128],
  ['partly-cloudy-night', 128],
  ['cloudy', 128],
  ['overcast', 128],
  ['fog', 128],
  ['mist', 128],
  ['drizzle', 128],
  ['rain', 128],
  ['overcast-rain', 128],
  ['partly-cloudy-day-rain', 128],
  ['snow', 128],
  ['sleet', 128],
  ['thunderstorms-rain', 128],
];

for (const [name, size] of icons) {
  const file = path.join(SRC, name + '.svg');
  if (!fs.existsSync(file)) {
    console.log('missing', name);
    continue;
  }
  const svg = fs.readFileSync(file, 'utf8');
  const resvg = new Resvg(svg, { fitTo: { mode: 'width', value: size } });
  const png = resvg.render().asPng();
  fs.writeFileSync(path.join(OUT, name + '.png'), png);
  console.log(name, size, png.length + ' bytes');
}
