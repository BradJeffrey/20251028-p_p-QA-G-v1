from dataclasses import dataclass
from typing import Dict, List, Tuple

# Define severity type alias
Severity = str  # 'normal'|'mild'|'moderate'|'severe'

@dataclass
class Thresholds:
    severe: float
    moderate: float
    mild: float

def classify_severity(z: float, thr: Thresholds) -> Severity:
    """Classify the absolute z-score into severity levels."""
    a = abs(z)
    if a >= thr.severe:
        return 'severe'
    if a >= thr.moderate:
        return 'moderate'
    if a >= thr.mild:
        return 'mild'
    return 'normal'

# Weight mapping for severity levels
WEIGHT = {'severe': 3, 'moderate': 2, 'mild': 1, 'normal': 0}

def cluster_scores(run_row: dict,
                   cluster_map: Dict[str, Dict],
                   thr_map: Dict[str, Thresholds]) -> Tuple[Dict[str, int], Dict[str, str], List[dict]]:
    """
    Compute scores and labels for each cluster based on metric and indicator severities.

    Parameters:
        run_row: dict containing metric z-scores and indicator values for a single run.
        cluster_map: configuration mapping clusters to their metrics and indicators.
        thr_map: mapping of metrics/indicators to threshold objects; must include 'global'.

    Returns:
        scores: sum of weights for symptoms in each cluster.
        labels: severity label per cluster based on score thresholds.
        records: list of symptom records with metric, z value, severity and cluster.
    """
    scores: Dict[str, int] = {}
    labels: Dict[str, str] = {}
    records: List[dict] = []

    # Initialize scores per cluster
    for cname in cluster_map['clusters']:
        scores[cname] = 0

    # Iterate clusters
    for cname, c in cluster_map['clusters'].items():
        # Process metrics
        for m in c.get('metrics', []):
            # try 'z_<metric>' first, then '<metric>_z_local'
            z = run_row.get(f'z_{m}', run_row.get(f'{m}_z_local'))
            if z is None:
                continue
            thr = thr_map.get(m, thr_map['global'])
            sev = classify_severity(z, thr)
            scores[cname] += WEIGHT[sev]
            records.append({'metric': m, 'z': z, 'severity': sev, 'cluster': cname})
        # Process indicators
        for ind in c.get('indicators', []):
            z = run_row.get(ind)
            if z is None:
                continue
            thr = thr_map.get(ind, thr_map['global'])
            sev = classify_severity(z, thr)
            scores[cname] += WEIGHT[sev]
            records.append({'metric': ind, 'z': z, 'severity': sev, 'cluster': cname})

    # Determine labels based on scores
    for cname, s in scores.items():
        if s >= 6:
            labels[cname] = 'strong'
        elif s >= 3:
            labels[cname] = 'moderate'
        elif s >= 1:
            labels[cname] = 'weak'
        else:
            labels[cname] = 'none'

    return scores, labels, records
