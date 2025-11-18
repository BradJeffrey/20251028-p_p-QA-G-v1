import sys, glob
import pandas as pd

REQUIRED_COLS = ["run","segment","file","value","error","weight"]

csvs = sorted(glob.glob("out/metrics_*.csv"))
if not csvs:
    print("No metrics CSVs found.")
    sys.exit(2)

bad = 0
for p in csvs[:12]:
    try:
        df = pd.read_csv(p)
    except Exception as e:
        print(f"[FAIL] {p}: cannot read CSV ({e})")
        bad += 1
        continue
    missing = [c for c in REQUIRED_COLS if c not in df.columns]
    if missing:
        print(f"[FAIL] {p}: missing columns {missing}")
        bad += 1
    elif df.empty:
        print(f"[WARN] {p}: empty CSV")
print("DONE" if bad == 0 else f"FAILED with {bad} issues")
sys.exit(0 if bad == 0 else 1)
