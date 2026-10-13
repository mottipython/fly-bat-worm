#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

scene.jpeg is the square painting of the trap in the cave. Three more pictures are
made from it, so that they line up with it exactly: its outlines as white lines on
black, which is what the Bat's cries show up; a grey relief of its surfaces, which
is what the Worm feels; and a dark blurred copy to go behind everything.
creatures.jpeg is the Fly, the Bat and the Worm in a row on flat magenta. Needs
ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def runs(on, gap):
    """Stretches where `on` is true, joining ones separated by less than `gap`."""
    out, start, last = [], None, None
    for i, v in enumerate(on):
        if not v: continue
        if start is None: start = i
        elif i - last > gap: out.append((start, last + 1)); start = i
        last = i
    if start is not None: out.append((start, last + 1))
    return [r for r in out if r[1] - r[0] > 30]

def pieces(a, rows):
    """The separate drawings on a sheet, in reading order: split into rows by the
    empty bands between them, then each row into drawings by the empty gaps."""
    solid = a[..., 3] > 128
    bands = runs(solid.sum(axis=1) > 2, 40)
    if len(bands) != rows:
        raise SystemExit(f"found {len(bands)} rows of drawings, expected {rows}")
    out = []
    for y0, y1 in bands:
        for x0, x1 in runs(solid[y0:y1].sum(axis=0) > 2, 40):
            out.append(a[y0:y1, x0:x1])
    return out

def trim(a):
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 2)[0]; cols = np.where(solid.sum(axis=0) > 2)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

def main():
    import os
    subprocess.run(["mkdir", "-p", OUT], check=True)
    src = f"{SRC}/scene.jpeg"
    subprocess.run(["magick", src, "-resize", "768x768!", "-quality", "90", f"{OUT}/scene.jpg"], check=True)
    subprocess.run(["magick", src, "-resize", "768x768!", "-colorspace", "Gray", "-blur", "0x1.4", "-canny", "0x1+8%+24%",
                    "-morphology", "Dilate", "Disk:1.6", "-blur", "0x0.7", "-quality", "92", f"{OUT}/edges.jpg"], check=True)
    subprocess.run(["magick", src, "-resize", "768x768!", "-colorspace", "Gray", "-blur", "0x1", "-shade", "135x30", "-auto-level",
                    "-fill", "#c8a27a", "-tint", "100", "-quality", "90", f"{OUT}/relief.jpg"], check=True)
    subprocess.run(["magick", src, "-resize", "960x540^", "-gravity", "center", "-extent", "960x540", "-blur", "0x14",
                    "-brightness-contrast", "-50x-30", "-attenuate", "0.25", "+noise", "Gaussian", "-quality", "92", f"{OUT}/bg.jpg"], check=True)
    print("scene, edges, relief, bg")
    if os.path.exists(f"{SRC}/creatures.jpeg"):
        got = [trim(p) for p in pieces(key(load(f"{SRC}/creatures.jpeg")), 1)]
        if len(got) != 3:
            raise SystemExit(f"creatures: found {len(got)} drawings, expected 3")
        for name, p in zip(["fly", "bat", "worm"], got):
            save(p, 300, max(1, round(p.shape[0] * 300 / p.shape[1])), f"{OUT}/{name}.png")
        print("creatures")

if __name__ == "__main__":
    main()
