import pandas as pd

def build_summary(symptoms_file='out/symptoms_perrun.csv',
                  causes_file='out/causes_per_run.csv',
                  summary_csv='out/cohort_summary_symptoms.csv',
                  summary_md='out/DIAGNOSIS_SUMMARY.md'):
    """Build cohort-level summary tables and markdown document."""
    sym = pd.read_csv(symptoms_file)
    ca = pd.read_csv(causes_file)

    # Frequency table per metric
    freq_metric = (sym
                   .assign(bucket=sym['severity'].fillna('normal'))
                   .pivot_table(index='metric', columns='bucket', values='run',
                                aggfunc='nunique', fill_value=0)
                   .reset_index())

    # Frequency per cluster label
    label_cols = [c for c in ca.columns if c.startswith('label_')]
    melt = ca.melt(id_vars='run', value_vars=label_cols,
                   var_name='label_cluster', value_name='label')
    melt['cluster'] = melt['label_cluster'].str.replace('label_', '', regex=False)
    freq_cluster = (melt
                    .pivot_table(index='cluster', columns='label', values='run',
                                 aggfunc='nunique', fill_value=0)
                    .reset_index())

    # Save summary csv
    freq_metric.to_csv(summary_csv, index=False)

    # Write markdown summary
    with open(summary_md, 'w') as f:
        f.write("# Cohort Diagnosis Summary\n\n")
        f.write("## Per-metric symptom frequencies\n")
        for _, r in freq_metric.iterrows():
            f.write(f"- {r['metric']}: " +
                    f"severe={r.get('severe',0)}, moderate={r.get('moderate',0)}, " +
                    f"mild={r.get('mild',0)}, normal={r.get('normal',0)}\n")
        f.write("\n## Per-cluster cause labels (run counts)\n")
        for _, r in freq_cluster.iterrows():
            f.write(f"- {r['cluster']}: " +
                    f"strong={r.get('strong',0)}, moderate={r.get('moderate',0)}, " +
                    f"weak={r.get('weak',0)}, none={r.get('none',0)}\n")

if __name__ == '__main__':
    build_summary()
