# RL-GC Visualizer

This tool visualizes the Q-learning data from the Reinforcement Learning Garbage Collection (RL-GC) module in SimpleSSD.

## Prerequisites

Make sure you have the following Python packages installed:
```
pip install pandas matplotlib seaborn numpy
```

## Data Files

The RL-GC exports two types of data files:

1. **Q-Table CSV files**: `output/q_table_<timestamp>.csv`
   - Contains state-action mappings and Q-values
   - Used for visualizing the learned policy

2. **Convergence Metrics CSV files**: `output/convergence_<timestamp>.csv`
   - Contains metrics about the learning process
   - Used for visualizing how the algorithm converges over time

## Usage

The visualizer supports several command-line arguments for flexible usage:

### Basic Usage

```bash
# Use the latest data files automatically
python visualizer.py --latest

# Combine ALL data files (recommended for sparse data)
python visualizer.py --combine-all

# Specify files manually
python visualizer.py --q-table output/q_table_12345.csv --metrics output/convergence_12345.csv

# Change output directory (default is 'plots')
python visualizer.py --combine-all --output-dir my_visualizations
```

### Specific Visualizations

```bash
# Only generate reward trend plots
python visualizer.py --combine-all --reward-only

# Only generate action distribution plots
python visualizer.py --combine-all --actions-only
```

### Help Information

```bash
python visualizer.py --help
```

## Visualization Types

The script generates the following visualizations:

1. **Q-Table Visualization**:
   - For datasets with few states: Bar chart showing Q-values for each state-action pair
   - For larger datasets: Heatmap showing best Q-values for each state
   - Helps understand the learned policy

2. **Convergence Metrics**:
   - For single data points: Bar chart of key metrics
   - For time series: Plots of Q-value deltas, rewards, and convergence metrics over time
   - Helps track learning progress

3. **Reward Trends**:
   - Shows how average rewards evolve over time with a moving average trendline
   - Helps assess policy performance

4. **Action Distribution**:
   - Shows which actions are selected as best for different states
   - Includes a consistency check comparing stored vs. calculated best actions
   - Helps understand the policy's decision distribution

## New Features for Handling Sparse Data

The visualizer now includes special handling for the data issues commonly seen in the SimpleSSD RL-GC implementation:

1. **Data Combining** (`--combine-all`):
   - Merges multiple CSV files to create more comprehensive visualizations
   - Essential for working with single-point data files
   - Automatically sorts data by iteration/timestamp

2. **Adaptive Visualization**:
   - Automatically selects the appropriate visualization type based on data characteristics
   - For sparse Q-tables with few states: Shows bar charts instead of heatmaps
   - For single-point metrics: Shows bar charts instead of trying to plot trend lines

3. **Data Validation**:
   - Checks for consistency between stored best actions and actual maximum Q-values
   - Provides visual feedback about data quality issues
   - Gracefully handles missing or malformed data

## Interpreting Results

### Combined Q-Table Visualization

- **Bar Chart** (for few states):
  - Each group represents a state
  - Each bar within a group represents an action's Q-value
  - Higher bars indicate preferred actions for that state

- **Heatmap** (for many states):
  - Brighter colors indicate higher Q-values
  - Look for patterns showing consistent policies for similar states

### Convergence Metrics

- **Single-Point Data**: Shows absolute values for comparison
- **Multiple Points**: Shows trends over time
  - **Max Q-Value Delta**: Should decrease over time as learning converges
  - **Average Reward**: Should stabilize or increase as policy improves
  - **Convergence Metric**: Another indicator of stability, should decrease

### Action Distribution

- Shows which actions are most commonly chosen as "best"
- The pie chart shows consistency between stored and calculated best actions
  - High consistency suggests reliable Q-value calculations
  - Low consistency suggests potential issues with the updating process

## Example Analysis Workflow

1. Run RL-GC module in SimpleSSD to generate CSV data files
2. Run `python visualizer.py --combine-all` to combine and visualize all data
3. Examine convergence plots to verify the algorithm is learning
4. Analyze Q-table visualization to understand the learned policy
5. Check reward trends to assess policy performance
6. If needed, adjust RL-GC parameters and rerun

## Troubleshooting

If you encounter errors or unsatisfactory visualizations:

1. Try using `--combine-all` to merge sparse data files
2. Check that CSV files have the expected format (run `head -n 5 output/q_table_*.csv` to inspect)
3. For malformed data, manually inspect CSV files and consider preprocessing
4. If visualizations still don't make sense, it may indicate issues with the RL-GC implementation itself 