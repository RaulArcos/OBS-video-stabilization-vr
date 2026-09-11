"""Measure the residual tremor of a plugin recording of synth.mp4.

The recording is compared frame by frame against ideal.mp4 (intentional motion
only). Because the scene is static, phase correlation between the two gives
the residual displacement directly, with sub-pixel accuracy.

usage: python tools/eval_synth.py <recording.mkv> --ideal tools/out/ideal.mp4 --synth tools/out/synth.mp4 --zoom 1.05
"""
import argparse
import numpy as np
import cv2


def zoomed(f, zoom):
    H, W = f.shape[:2]
    M = cv2.getRotationMatrix2D((W / 2, H / 2), 0, zoom)
    return cv2.warpAffine(f, M, (W, H))


def gray(f):
    return cv2.cvtColor(f, cv2.COLOR_BGR2GRAY).astype(np.float32)


def find_offset(rec, ideal, zoom, probe=150, search=12, span=240):
    """Pick the frame offset that minimizes the residual displacement over a
    span of frames (frame similarity alone is ambiguous on a slowly panning
    static scene)."""
    def frames(path, start, n):
        c = cv2.VideoCapture(path)
        c.set(cv2.CAP_PROP_POS_FRAMES, start)
        out = []
        for _ in range(n):
            ok, f = c.read()
            if not ok:
                break
            out.append(cv2.resize(f, (480, 270), interpolation=cv2.INTER_AREA))
        return out
    r = [gray(f)[35:235, 65:415] for f in frames(rec, probe, span)]
    ideal_frames = frames(ideal, probe - search, span + 2 * search)
    win = cv2.createHanningWindow((r[0].shape[1], r[0].shape[0]), cv2.CV_32F)
    best, best_v = 0, 1e9
    for off in range(-search, search + 1):
        acc = 0.0
        for i in range(0, len(r), 4):
            j = i + off + search
            if j >= len(ideal_frames):
                break
            b = gray(zoomed(ideal_frames[j], zoom))[35:235, 65:415]
            (dx, dy), _ = cv2.phaseCorrelate(b, r[i], win)
            acc += dx * dx + dy * dy
        if acc < best_v:
            best, best_v = off, acc
    return best


def residuals(video, ideal, zoom, offset, start, n, apply_zoom):
    cv_ = cv2.VideoCapture(video)
    ci = cv2.VideoCapture(ideal)
    cv_.set(cv2.CAP_PROP_POS_FRAMES, start)
    ci.set(cv2.CAP_PROP_POS_FRAMES, start + offset)
    H, W = 1080, 1920
    win = None
    out = []
    for i in range(n):
        ok1, a = cv_.read()
        ok2, b = ci.read()
        if not (ok1 and ok2):
            break
        if apply_zoom:
            a = zoomed(a, zoom)
        b = zoomed(b, zoom)
        # central region only, away from any border effects
        ga = gray(a)[140:940, 260:1660]
        gb = gray(b)[140:940, 260:1660]
        if win is None:
            win = cv2.createHanningWindow((ga.shape[1], ga.shape[0]), cv2.CV_32F)
        (dx, dy), resp = cv2.phaseCorrelate(gb, ga, win)
        out.append((dx, dy, resp))
    return np.array(out)


def highpass(p, fps=60.0, fc=4.0):
    p = p - p.mean()
    n = len(p)
    w = np.hanning(n)
    sp = np.fft.rfft(p * w)
    fr = np.fft.rfftfreq(n, 1.0 / fps)
    sp[fr < fc] = 0
    return (np.fft.irfft(sp, n) / np.maximum(w, 0.1))[n // 10:-n // 10]


def report(name, r):
    for k, ax in ((0, "x"), (1, "y")):
        p = r[:, k]
        hp = highpass(p)
        lp = p - hp.mean()
        print(f"  {name} {ax}: residual rms {p.std():.2f} px | tremor (>4 Hz) rms {hp.std():.2f} px, p99 {np.percentile(np.abs(hp), 99):.2f} px "
              f"| max |offset| {np.abs(p).max():.1f} px")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording")
    ap.add_argument("--ideal", default="tools/out/ideal.mp4")
    ap.add_argument("--synth", default="tools/out/synth.mp4")
    ap.add_argument("--zoom", type=float, default=1.05)
    ap.add_argument("--start", type=int, default=90)
    ap.add_argument("--frames", type=int, default=1100)
    args = ap.parse_args()

    off = find_offset(args.recording, args.ideal, args.zoom)
    print(f"recording offset vs ideal: {off} frames")
    print("residual displacement vs the ideal (intentional-only) clip:")
    r_in = residuals(args.synth, args.ideal, args.zoom, 0, args.start + off, args.frames, apply_zoom=True)
    report("input (synth)   ", r_in)
    r_out = residuals(args.recording, args.ideal, args.zoom, off, args.start, args.frames, apply_zoom=False)
    report("plugin output   ", r_out)
    hi, ho = highpass(r_in[:, 0]), highpass(r_out[:, 0])
    print(f"tremor removed: x {(1 - ho.std() / hi.std()) * 100:.0f}%  y {(1 - highpass(r_out[:, 1]).std() / highpass(r_in[:, 1]).std()) * 100:.0f}%")


if __name__ == "__main__":
    main()
