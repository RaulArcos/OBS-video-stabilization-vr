"""Offline global-motion analysis of an OBS recording.

Estimates a per-frame rigid transform (dx, dy, rotation) between consecutive
frames using sparse LK optical flow + RANSAC, writes a CSV and prints
statistics about the high-frequency (tremor) content. Used to tune the
real-time filter in the OBS plugin.

usage: python tools/analyze_motion.py test.mp4 [--scale 0.5] [--out tools/out/motion.csv]
"""
import argparse, csv, sys, time
import numpy as np
import cv2


def estimate(prev_gray, cur_gray, prev_pts):
    if prev_pts is None or len(prev_pts) < 30:
        prev_pts = cv2.goodFeaturesToTrack(prev_gray, maxCorners=600, qualityLevel=0.01,
                                           minDistance=12, blockSize=7)
        if prev_pts is None:
            return None, None
    cur_pts, st, err = cv2.calcOpticalFlowPyrLK(prev_gray, cur_gray, prev_pts, None,
                                                winSize=(21, 21), maxLevel=4,
                                                criteria=(cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 30, 0.01))
    if cur_pts is None:
        return None, None
    good = st.reshape(-1) == 1
    p0 = prev_pts[good].reshape(-1, 2)
    p1 = cur_pts[good].reshape(-1, 2)
    if len(p0) < 12:
        return None, None
    M, inl = cv2.estimateAffinePartial2D(p0, p1, method=cv2.RANSAC, ransacReprojThreshold=1.0,
                                         maxIters=2000, confidence=0.995, refineIters=10)
    if M is None:
        return None, None
    # M = [[s cos, -s sin, tx],[s sin, s cos, ty]]
    a, b = M[0, 0], M[1, 0]
    scale = float(np.hypot(a, b))
    rot = float(np.arctan2(b, a))
    tx, ty = float(M[0, 2]), float(M[1, 2])
    n_in = int(inl.sum()) if inl is not None else 0
    # re-detect features every frame in the current image for the next step
    next_pts = cv2.goodFeaturesToTrack(cur_gray, maxCorners=600, qualityLevel=0.01,
                                       minDistance=12, blockSize=7)
    return (tx, ty, rot, scale, n_in, len(p0)), next_pts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--out", default="tools/out/motion.csv")
    ap.add_argument("--max-frames", type=int, default=0)
    args = ap.parse_args()

    cap = cv2.VideoCapture(args.video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 60.0
    rows = []
    prev = None
    prev_pts = None
    t0 = time.time()
    i = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        small = cv2.resize(frame, None, fx=args.scale, fy=args.scale, interpolation=cv2.INTER_AREA)
        gray = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY)
        if prev is not None:
            res, prev_pts = estimate(prev, gray, prev_pts)
            if res is None:
                rows.append((i, 0.0, 0.0, 0.0, 1.0, 0, 0))
            else:
                tx, ty, rot, sc, n_in, n = res
                rows.append((i, tx / args.scale, ty / args.scale, rot, sc, n_in, n))
        prev = gray
        i += 1
        if args.max_frames and i >= args.max_frames:
            break
        if i % 300 == 0:
            print(f"{i} frames, {time.time()-t0:.1f}s", file=sys.stderr)
    cap.release()

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "dx", "dy", "rot_rad", "scale", "inliers", "tracked"])
        w.writerows(rows)

    a = np.array([r[1:5] for r in rows], dtype=np.float64)
    dx, dy, rot, sc = a[:, 0], a[:, 1], a[:, 2], a[:, 3]
    px = np.cumsum(dx); py = np.cumsum(dy); pr = np.cumsum(rot)
    print(f"frames={len(rows)} fps={fps:.2f}")
    print(f"per-frame |dx| mean={np.abs(dx).mean():.3f} p95={np.percentile(np.abs(dx),95):.3f} max={np.abs(dx).max():.2f} px")
    print(f"per-frame |dy| mean={np.abs(dy).mean():.3f} p95={np.percentile(np.abs(dy),95):.3f} max={np.abs(dy).max():.2f} px")
    print(f"per-frame |rot| mean={np.degrees(np.abs(rot)).mean():.4f} p95={np.degrees(np.percentile(np.abs(rot),95)):.4f} deg")
    print(f"scale mean={sc.mean():.5f} std={sc.std():.5f}")

    # split trajectory into low-pass (intentional) and residual (tremor) with a
    # zero-phase butterworth, purely for diagnostics
    from numpy.fft import rfft, rfftfreq
    def spectrum(sig):
        sig = sig - sig.mean()
        sp = np.abs(rfft(sig * np.hanning(len(sig)))) / len(sig)
        fr = rfftfreq(len(sig), 1.0 / fps)
        return fr, sp
    for name, sig in (("dx", dx), ("dy", dy), ("rot_deg", np.degrees(rot))):
        fr, sp = spectrum(sig)
        bands = [(0, 1), (1, 3), (3, 6), (6, 10), (10, 15), (15, 30)]
        parts = []
        for lo, hi in bands:
            m = (fr >= lo) & (fr < hi)
            parts.append(f"{lo}-{hi}Hz:{np.sqrt((sp[m]**2).sum()):.3f}")
        print(f"velocity spectrum {name}: " + " ".join(parts))

    # tremor estimate: residual after a 0.25 s moving-average of the trajectory
    k = int(round(fps * 0.25)) | 1
    ker = np.ones(k) / k
    for name, p in (("x", px), ("y", py)):
        lp = np.convolve(p, ker, mode="same")
        res = p - lp
        res = res[k:-k]
        print(f"trajectory {name}: tremor residual rms={res.std():.2f}px p99={np.percentile(np.abs(res),99):.2f}px max={np.abs(res).max():.2f}px")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
