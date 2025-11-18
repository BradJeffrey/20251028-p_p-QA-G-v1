# Metric Explanations  

This document summarizes formulas, typical patterns, physics context, and rationale for each metric used in the real-data QA pipeline.  

## intt_adc_peak  
- **Formula:** argmax_x ADC(x)  
- **Typical Pattern:** Slow drift\u2192aging/temperature; step change\u2192calibration or hardware swap.  
- **Physics Context:** Peak ADC location tracks gain; shifts indicate gain drift or calibration shifts.  
- **Rationale:** Stability across run number implies consistent amplification; jumps/drifts are red flags.  
  
## intt_bco_peak  
- **Formula:** argmax_b BCO(b)  
- **Typical Pattern:** Alternating offsets\u2192phase toggling; monotone drift\u2192clock drift.  
- **Physics Context:** Aligns timing to bunch crossing; shifts suggest clock/phase issues.  
- **Rationale:** Run-dependent offsets reveal timing misalignment.  
  
