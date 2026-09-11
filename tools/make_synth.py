"""Generate a synthetic test clip with a known head trajectory.

Takes one frame of a real recording as a static scene and moves a virtual
camera over it: slow intentional pans/tilts, two quick head turns, and a
tremor (5-13 Hz sinusoids plus noise) on top. Writes:

  <out>/synth.mp4       the clip to feed through the plugin
  <out>/ideal.mp4       the same clip with the intentional motion only
  <out>/synth_traj.npy  per-frame [X, Y, rot, ix, iy] (camera position)

Because the scene is static, comparing the plugin output against ideal.mp4
with phase correlation measures the residual tremor almost exactly.

usage: python tools/make_synth.py test.mp4 --frame 5 --out tools/out
"""
import argparse, math, os, subprocess
import numpy as np
import cv2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--frame", type=int, default=5)
    ap.add_argument("--out", default="tools/out")
    ap.add_argument("--seconds", type=float, default=22.0)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--margin-scale", type=float, default=1.6)
    args = ap.parse_args()

    fps, n = args.fps, int(args.fps * args.seconds)
    cap = cv2.VideoCapture(args.video)
    cap.set(cv2.CAP_PROP_POS_FRAMES, args.frame)
    ok, base = cap.read()
    if not ok:
        raise SystemExit("cannot read base frame")
    H, W = base.shape[:2]
    big = cv2.resize(base, None, fx=args.margin_scale, fy=args.margin_scale, interpolation=cv2.INTER_CUBIC)
    BH, BW = big.shape[:2]
    cx, cy = BW / 2, BH / 2

    rng = np.random.default_rng(1)
    t = np.arange(n) / fps
    ix = 120 * np.sin(2 * np.pi * 0.12 * t) + 60 * np.sin(2 * np.pi * 0.05 * t + 1)
    iy = 40 * np.sin(2 * np.pi * 0.09 * t + 0.5)
    # two quick head turns: look left over 1.5 s, hold 2 s, come back over 1.5 s
    turn = np.zeros(n)
    for t0 in (6.0, 14.0):
        m = (t > t0) & (t <= t0 + 1.5)
        turn[m] += -220 * (1 - np.cos(np.pi * (t[m] - t0) / 1.5)) / 2
        m = (t > t0 + 1.5) & (t <= t0 + 3.5)
        turn[m] += -220
        m = (t > t0 + 3.5) & (t <= t0 + 5.0)
        turn[m] += -220 * (1 + np.cos(np.pi * (t[m] - t0 - 3.5) / 1.5)) / 2
    ix = ix + turn
    slow_rot = np.radians(0.6) * np.sin(2 * np.pi * 0.07 * t)

    env = np.clip(t - 1, 0, 1)
    tx = env * (3.0 * np.sin(2 * np.pi * 5 * t) + 2.0 * np.sin(2 * np.pi * 8 * t + 0.7) + 1.0 * np.sin(2 * np.pi * 12 * t + 1.3))
    ty = env * (2.5 * np.sin(2 * np.pi * 6 * t + 0.2) + 1.5 * np.sin(2 * np.pi * 9 * t + 2.0) + 0.8 * np.sin(2 * np.pi * 13 * t))
    noise = rng.normal(0, 0.8, (n, 2))
    noise = (noise + np.roll(noise, 1, axis=0)) / 2
    tx = tx + env * noise[:, 0]
    ty = ty + env * noise[:, 1]
    rot = slow_rot + env * np.radians(0.15) * np.sin(2 * np.pi * 6.5 * t)
    X, Y = ix + tx, iy + ty
    np.save(os.path.join(args.out, "synth_traj.npy"), np.stack([X, Y, rot, ix, iy], axis=1))

    def render(path, px, py, prot):
        raw = path + ".raw.mp4"
        vw = cv2.VideoWriter(raw, cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
        for i in range(n):
            M = cv2.getRotationMatrix2D((cx, cy), -math.degrees(prot[i]), 1.0)
            M[0, 2] += -px[i] - (cx - W / 2)
            M[1, 2] += -py[i] - (cy - H / 2)
            vw.write(cv2.warpAffine(big, M, (W, H), flags=cv2.INTER_LINEAR))
        vw.release()
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", raw, "-c:v", "libx264", "-preset", "fast", "-crf", "16",
                        "-pix_fmt", "yuv420p", path], check=True)
        os.remove(raw)
        print("wrote", path)

    render(os.path.join(args.out, "synth.mp4"), X, Y, rot)
    render(os.path.join(args.out, "ideal.mp4"), ix, iy, slow_rot)


if __name__ == "__main__":
    main()
