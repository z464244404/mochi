[中文](screenshots.md) · **English**

# Screenshots

## Real photos

The three files under `docs/photos/` are phone photos, cropped to the device and resized to 720 px wide:

| File | Screen |
|---|---|
| `pet.jpg` | Pet (typing animation) + balance 10.21 / today 1.17 / off-peak |
| `weather.jpg` | Weather (Guiyang 12 °C, overcast + 3-day forecast) |
| `clock.jpg` | Clock, minimal style |

How to shoot: turn off the room lights and keep a little ambient light, hold the phone square
to the screen, no flash. Afterwards a quick `PIL` crop driven by the dark-region projection
removes the desk background.

## Rendered previews

`docs/preview-*.png` come from `tools/preview.py`, which redraws the UI on a PC with the same
primitives and fonts. They are for layout work: colors and positions match the device closely,
but there is no backlight, no round glass, and no panel-level antialiasing difference.

```bash
python tools/preview.py            # regenerate docs/preview-*.png
python tools/preview.py --pet      # pet frame only
```

The pet preview reads real frames out of `firmware/assets/*.mjp`, so it is essentially the
device pixels.

## Camera-free screenshots

A `GET /api/shot.jpg` endpoint (not implemented yet) would encode the current framebuffer with
`esp_jpeg_enc` and return it as JPEG, so you could just open it in a browser and save the image.
It works anywhere on the local network (the hotspot at `192.168.4.1` or the IP shown on screen).
