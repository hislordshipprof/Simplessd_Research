# Response Time Comparison: RL-GC vs Regular GC

This guide explains how to compare the response times of the Reinforcement Learning Garbage Collector (RL-GC) versus the regular Garbage Collector in SimpleSSD.

## Overview

In SSDs, garbage collection can significantly impact I/O response times, particularly causing tail latency issues. We've implemented a system to track response times for both GC approaches:

1. **Regular GC**: The traditional approach that performs GC operations based on predefined thresholds
2. **RL-GC**: The reinforcement learning approach that adapts GC operations based on learned patterns

This comparison tool helps visualize the differences in performance between these two approaches.

## Response Time Measurement

In both cases, response time is measured as:

> The overall latency an I/O request experiences—that is, the elapsed time from when the request is issued until it is fully serviced (including any delays caused by GC operations).

## How to Use

### Step 1: Run Simulations with Different Configurations

You need to run two separate simulations with identical workloads:

1. **With RL-GC enabled**:
   ```
   ./simplessd [your usual parameters]
   ```
   This will generate `output/rl_gc_response.csv` containing RL-GC response times.

2. **With RL-GC disabled**:
   ```
   ./simplessd [your usual parameters] --disable-rlgc
   ```
   This will generate `output/regular_gc_response.csv` containing regular GC response times.

### Step 2: Compare the Results

Run the comparison script to generate visualization and statistics:

```
./scripts/compare_gc_response.py
```

This will:
- Read both CSV files
- Generate comparison plots
- Print summary statistics
- Save all results to `output/response_comparison/`

### Custom Input/Output Paths

You can specify custom input files and output directory:

```
./scripts/compare_gc_response.py --rl-gc-file=path/to/rl_data.csv --regular-gc-file=path/to/regular_data.csv --output-dir=path/to/output
```

## Interpretation of Results

The comparison tool generates several visualization types:

1. **Average Response Time**: Simple bar chart comparing mean response times
2. **Response Time Distribution**: Shows the density distribution of response times
3. **Percentile Comparison**: Shows response times at various percentiles (50%, 90%, 95%, 99%, 99.9%, 99.99%)
4. **Cumulative Distribution Function (CDF)**: Shows what percentage of requests complete within a given response time

### Key Metrics to Evaluate

When comparing the two approaches, consider these metrics:

- **Mean Response Time**: Lower is better for overall performance
- **Median (50th percentile)**: Represents typical performance
- **Tail Latencies (99th+)**: Critical for consistent performance; lower values mean fewer outlier slow operations
- **Distribution Shape**: Narrower distributions with fewer outliers mean more predictable performance
- **Maximum Response Time**: Lower values indicate better worst-case scenario performance

## Requirements

- Python 3.x
- Required Python packages:
  ```
  pip install pandas numpy matplotlib
  ```

## Troubleshooting

- **Missing Data Files**: Ensure both simulations completed successfully and generated their respective CSV files
- **Column Name Issues**: If the script can't identify column names, check the CSV files and adjust the script accordingly
- **Empty Output**: Check that both runs had similar I/O patterns for meaningful comparison

For more details on the implementation, see the source code:
- `simplessd/ftl/page_mapping.cc`: Response time logging implementation
- `scripts/compare_gc_response.py`: Visualization and analysis script 