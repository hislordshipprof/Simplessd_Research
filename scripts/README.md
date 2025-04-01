# GC Policies Analysis Scripts

This directory contains Python scripts for analyzing the performance metrics of different garbage collection (GC) policies in SimpleSSD.

## Overview

The analysis scripts provide detailed comparisons between the following GC policies:
- Default Page-Level GC
- LazyRTGC
- RL-Baseline
- RL-Intensive
- RL-Aggressive

These scripts help visualize and quantify the differences in performance, efficiency, and long-tail latency between these policies, focusing on the metrics described in the paper "Reinforcement Learning-Assisted Garbage Collection to Mitigate Long-Tail Latency in SSD".

## Scripts

### analyze_gc_metrics.py

This script provides a comprehensive comparison of all GC policies based on their summary metrics. It generates:

- Normalized latency comparison charts (P99, P99.9, P99.99)
- Erase count comparisons
- GC efficiency metrics (invocations, page copies, etc.)
- RL-Aggressive specific feature analysis
- A comprehensive summary table of all metrics

**Usage:**
```bash
python analyze_gc_metrics.py
```

### analyze_time_series.py

This script focuses on time series analysis, showing how various metrics change over time (or GC invocations). It generates:

- Response time progression
- P99 latency over time
- Long-tail latency (P99.9 and P99.99) over time
- Page copies over time
- Estimated free blocks over time
- GC rate analysis
- Reward progression for RL policies

**Usage:**
```bash
python analyze_time_series.py
```

## Requirements

These scripts require the following Python packages:
- numpy
- pandas
- matplotlib
- seaborn

You can install these dependencies with:
```bash
pip install numpy pandas matplotlib seaborn
```

## Output

The scripts will create a `results` directory containing:
- PNG images of all generated plots
- CSV and TXT files with summary tables
- Detailed analysis of policy performance

## Interpreting Results

The key metrics to focus on are:
1. **Long-tail latency reduction**: Compare P99, P99.9, and P99.99 latencies across policies
2. **Erase count**: Lower is better for endurance
3. **GC efficiency**: How effectively each policy manages garbage collection
4. **Free blocks management**: How well each policy maintains free blocks over time

The RL-Aggressive policy specifically aims to reduce long-tail latency through features like early GC triggering and read-triggered GC, which should be visible in the analysis results.

## Reference

The analysis is based on the metrics described in the paper "Reinforcement Learning-Assisted Garbage Collection to Mitigate Long-Tail Latency in SSD". 