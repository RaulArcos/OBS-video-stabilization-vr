"""Run the reference filter over motion.csv, report how much shake is removed,
and optionally render a side-by-side preview video.

usage:
  python tools/simulate.py tools/out/motion.csv --sweep
  python tools/simulate.py tools/out/motion.csv --render test.mp4 --start 20 --dur 15 --out tools/out/preview.mp4
"""
import argparse, csv, math, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stabfilter import Stabilizer


def load(path):
    rows = list(csv.DictReader(open(path)))
    dx = np.array([float(r["dx"]) for r in rows])
    dy = np.array([float(r["dy"]) for r in rows])
    rot = np.array([float(r["rot_rad"]) for r in rows])
    return dx, dy, rot


def run(dx, dy, rot, fps, **kw):
    st = Stabilizer(**kw)
    dt = 1.0 / fps
    out = np.zeros((len(dx), 3))
    for i in range(len(dx)):
        out[i] = st.step(dx[i], dy[i], rot[i], dt)
    return out


def shake_metric(v, fps, fc=4.0):
    """rms / p99 of the position component above fc Hz (tremor + judder)."""
    p = np.cumsum(v)
    p = p - p.mean()
    n = len(p)
    win = np.hanning(n)
    sp = np.fft.rfft(p * win)
    fr = np.fft.rfftfreq(n, 1.0 / fps)
    sp[fr < fc] = 0
    hp = np.fft.irfft(sp, n) / np.maximum(win, 0.1)
    hp = hp[n // 10: -n // 10]
    return hp.std(), np.percentile(np.abs(hp), 99)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--zoom", type=float, default=1.04)
    ap.add_argument("--tau-v", type=float, default=0.25)
    ap.add_argument("--v0", type=float, default=2.0)
    ap.add_argument("--tau-c", type=float, default=0.6)
    ap.add_argument("--render", default=None)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--dur", type=float, default=15.0)
    ap.add_argument("--out", default="tools/out/preview.mp4")
    ap.add_argument("--sweep", action="store_true")
    args = ap.parse_args()

    dx, dy, rot = load(args.csv)
    fps = args.fps

    if args.sweep:
        print("tau_v  v0   tau_c  zoom | shake_x rms/p99 | shake_y rms/p99 | sat%")
        for tau_v in (0.12, 0.2, 0.3, 0.4):
            for v0 in (1.0, 2.0, 4.0):
                for tau_c in (0.4, 0.8):
                    c = run(dx, dy, rot, fps, width=args.width, height=args.height, zoom=args.zoom,
                            tau_v=tau_v, v0=v0, tau_c=tau_c)
                    sx, px99 = shake_metric(dx + np.diff(c[:, 0], prepend=0), fps)
                    sy, py99 = shake_metric(dy + np.diff(c[:, 1], prepend=0), fps)
                    st = Stabilizer(width=args.width, height=args.height, zoom=args.zoom)
                    sat = (np.abs(c[:, 0]) > 0.8 * st.fx.margin).mean() * 100
                    print(f"{tau_v:4.2f} {v0:4.1f} {tau_c:4.1f} {args.zoom:5.2f} | {sx:5.2f} {px99:5.2f} | {sy:5.2f} {py99:5.2f} | {sat:4.1f}")
        return

    c = run(dx, dy, rot, fps, width=args.width, height=args.height, zoom=args.zoom,
            tau_v=args.tau_v, v0=args.v0, tau_c=args.tau_c)
    sx0, px0 = shake_metric(dx, fps)
    sy0, py0 = shake_metric(dy, fps)
    sx1, px1 = shake_metric(dx + np.diff(c[:, 0], prepend=0), fps)
    sy1, py1 = shake_metric(dy + np.diff(c[:, 1], prepend=0), fps)
    print(f"shake x: rms {sx0:.2f} -> {sx1:.2f} px, p99 {px0:.2f} -> {px1:.2f} px")
    print(f"shake y: rms {sy0:.2f} -> {sy1:.2f} px, p99 {py0:.2f} -> {py1:.2f} px")
    print(f"correction |cx| max {np.abs(c[:,0]).max():.1f}  |cy| max {np.abs(c[:,1]).max():.1f}  |rot| max {math.degrees(np.abs(c[:,2]).max()):.2f} deg")

    if args.render:
        import cv2
        cap = cv2.VideoCapture(args.render)
        W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        f0 = int(args.start * fps); n = int(args.dur * fps)
        cap.set(cv2.CAP_PROP_POS_FRAMES, f0)
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        ow, oh = W, H // 2
        vw = cv2.VideoWriter(args.out, fourcc, fps, (ow, oh))
        z = args.zoom
        for i in range(f0, min(f0 + n, len(c))):
            ok, frame = cap.read()
            if not ok:
                break
            cx, cy, cr = c[i]
            M = cv2.getRotationMatrix2D((W / 2, H / 2), -math.degrees(cr), z)
            M[0, 2] += cx * z
            M[1, 2] += cy * z
            stab = cv2.warpAffine(frame, M, (W, H), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
            Mz = cv2.getRotationMatrix2D((W / 2, H / 2), 0, z)
            orig = cv2.warpAffine(frame, Mz, (W, H), flags=cv2.INTER_LINEAR)
            a = cv2.resize(orig, (ow // 2, oh)); b = cv2.resize(stab, (ow // 2, oh))
            cv2.putText(a, "original", (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)
            cv2.putText(b, "stabilized", (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)
            vw.write(np.hstack([a, b]))
        vw.release()
        print("wrote", args.out)


if __name__ == "__main__":
    main()
