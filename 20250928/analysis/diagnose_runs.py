import pandas as pd
import yaml
import glob
from symptom_engine import cluster_scores, Thresholds

def diagnose_runs(metrics_pattern='out/metrics_*_perrun.csv',
                  physq_file='out/physics_quality_perrun.csv',
                  cluster_map_file='configs/cluster_map.yaml',
                  thr_file='configs/severity_thresholds.yaml',
                  output_symptoms='out/symptoms_perrun.csv',
                  output_causes='out/causes_per_run.csv'):
    """Aggregate per-run metrics and physics indicators to produce symptom and cause reports."""
    # Discover metrics files
    perrun_files = glob.glob(metrics_pattern)
    if not perrun_files:
        raise FileNotFoundError(f'No per-run metric files matching {metrics_pattern}')
    # Build runs DataFrame with all z columns
    runs_df = None
    zwide = None
    for f in perrun_files:
        df = pd.read_csv(f)
        metric_name = f.split('metrics_')[1].split('_perrun.csv')[0]
        # Determine z column heuristically
        z_col_candidates = [f'z_{metric_name}', 'z_local', f'{metric_name}_z_local']
        z_col = next((c for c in z_col_candidates if c in df.columns), None)
        if z_col is None:
            continue
        df_metric = df[['run', z_col]].rename(columns={z_col: f'z_{metric_name}'})
        if zwide is None:
            zwide = df_metric
        else:
            zwide = zwide.merge(df_metric, on='run', how='outer')
    if zwide is None:
        raise RuntimeError('No z-score columns found in metrics files.')
    # Merge physics indicators
    physq = pd.read_csv(physq_file)
    wide = zwide.merge(physq, on='run', how='left')
    # Load configs
    cluster_map = yaml.safe_load(open(cluster_map_file))
    thr_cfg = yaml.safe_load(open(thr_file))
    thr_map = {'global': Thresholds(**thr_cfg['global'])}
    for k, v in thr_cfg.items():
        if k == 'global':
            continue
        thr_map[k] = Thresholds(**v)
    # Diagnose each run
    symptom_rows = []
    cause_rows = []
    for _, row in wide.iterrows():
        scores, labels, recs = cluster_scores(row.to_dict(), cluster_map, thr_map)
        for r in recs:
            symptom_rows.append({'run': row['run'], 'metric': r['metric'], 'z': r['z'],
                                 'severity': r['severity'], 'clusters': r['cluster']})
        cause_row = {'run': row['run']}
        for cname, s in scores.items():
            cause_row[cname] = s
            cause_row[f'label_{cname}'] = labels[cname]
        cause_rows.append(cause_row)
    pd.DataFrame(symptom_rows).to_csv(output_symptoms, index=False)
    pd.DataFrame(cause_rows).to_csv(output_causes, index=False)

if __name__ == '__main__':
    diagnose_runs()
