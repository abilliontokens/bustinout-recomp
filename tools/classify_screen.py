#!/usr/bin/env python3
"""Classify a full-screen screenshot of the running port into a walk state.

Usage: classify_screen.py <png>  ->  one word on stdout:
  intro | blue | saveselect | name | cas | question | house | dialogue | unknown

"blue" covers the title and the main menu (both are blue-gradient screens the
walk treats alike: START then A). The game picture is centred in the display;
probes are taken relative to the picture rectangle actually found, so this
works at any window size and in the pillarboxed and widened shapes alike. The
probes are coarse colour facts measured on the real screens (docs/playtest.md):
the name keyboard is a bright-lettered grid low in a dark picture, the
household is a dark lot whose clock pill is bright blue at the picture's top
right, its greeting dialogue is a bright translucent panel in the centre, and
Create-a-Sim is a warm brown room whose save question covers the centre in
blue.
"""
import sys

import numpy as np
from PIL import Image


def picture(img):
    """The centred non-black game picture, ignoring the desktop taskbar."""
    h, w, _ = img.shape
    body = img[: int(h * 0.92)]
    colmean = (body.sum(axis=2) > 60).mean(axis=0)
    bright = np.where(colmean > 0.5)[0]
    if len(bright) < 50:
        return None
    rowmean = (img.sum(axis=2) > 60).mean(axis=1)
    rows = np.where(rowmean > 0.2)[0]
    return img[rows[0]:rows[-1], bright[0]:bright[-1]]


def main() -> int:
    img = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
    pic = picture(img)
    if pic is None or pic.shape[0] < 50:
        print("intro")  # a black frame: movies / loading
        return 0
    ph, pw, _ = pic.shape

    def reg(fx0, fy0, fx1, fy1):
        return pic[int(ph * fy0):int(ph * fy1), int(pw * fx0):int(pw * fx1)]

    def mean(r):
        return r.reshape(-1, 3).mean(axis=0)

    def bluefrac(r):
        return float(((r[:, :, 2] > 140) & (r[:, :, 2] > r[:, :, 0] + 40)).mean())

    def whitefrac(r):
        return float((r.min(axis=2) > 180).mean())

    centre = reg(0.35, 0.35, 0.65, 0.65)
    lowmid = reg(0.30, 0.60, 0.70, 0.95)
    clock = reg(0.84, 0.0, 0.99, 0.09)
    cm, pm = mean(centre), float(pic.reshape(-1, 3).mean())

    # the intro movie's sunset frame: warm orange centre, no UI anywhere
    if cm[0] > cm[2] + 80 and cm[0] > 150:
        print("intro")
        return 0
    left = mean(reg(0.02, 0.30, 0.20, 0.80))
    # household: dark lot, bright-blue clock pill top-right; dialogue covers centre
    if pm < 100 and bluefrac(clock) > 0.015:
        print("dialogue" if bluefrac(centre) > 0.5 else "house")
        return 0
    # the warm CAS room: name entry and the save question both overlay it in blue
    if left[0] > left[2] + 25:
        if bluefrac(centre) > 0.6:
            print("name" if whitefrac(lowmid) > 0.04 else "question")
        else:
            print("cas")
        return 0
    if bluefrac(centre) > 0.6:
        print("dialogue" if pm < 100 else "blue")
        return 0
    if cm[0] > cm[2] + 25:
        print("cas")
        return 0
    if bluefrac(lowmid) > 0.5:
        print("blue")
        return 0
    print("intro")  # no UI facts matched: a movie frame (only waited on at launch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
