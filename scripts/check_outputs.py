import sys, glob, csv, math
import pandas as pd

REQUIRED_COLS = ["run","segment","file","value","error","weight"]

def main():
    csvs = sorted(glob.glob("20250928/out/metrics_*.csv"))
    if not csvs:
        print("No metrics CSVs found.")
        sys.exit(2)
    errors = 0
    for p in csvs[:8]:  # sample first 8 files
        df = pd.read_csv(p)
        missing = [c for c in REQUIRED_COLS if c not in df.columns]
        if missing:
            print(f"[FAIL] {p} missing columns: {missing}")
            errors += 1
        if df.empty:
            print(f"[FAIL] {p} has zero rows")
            errors += 1
    print("DONE" if errors == 0 else f"FAILED with {errors} issues")
    sys.exit(0 if errors == 0 else 1)

if __name__ == "__main__":
    main()
