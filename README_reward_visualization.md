# RL-GC Reward Visualization

This README explains how to use the reward visualization tool for the Reinforcement Learning Garbage Collector (RL-GC) module in SimpleSSD.

## Overview

The reward visualization system logs rewards for each action taken by the RL-GC agent and allows you to visualize them to better understand the agent's learning patterns. This helps answer questions like:

- Which actions tend to get better rewards over time?
- Is Action 0 (copying 0 pages) always preferred, or does it depend on the state?
- How do the rewards for each action change as the agent learns?
- How does the reward distribution across different actions evolve over time?

## How It Works

1. **Data Collection**: The RL-GC now has built-in reward logging enabled by default. It logs every reward given to each action, along with other metrics (iteration number, cumulative reward, etc.).

2. **CSV Output**: This data is saved to a CSV file (default: `output/rl_gc_reward_log.csv`) that is updated periodically during execution.

3. **Visualization**: A Python script (`scripts/visualize_rewards.py`) reads this CSV file and generates various plots to help you understand the agent's behavior.

## Generated Visualizations

The visualization script produces five different plots:

1. **Reward Trends by Action**: Shows how rewards for each action change over GC invocations.
2. **Cumulative Reward by Action**: Shows the total accumulated reward for each action.
3. **Average Reward by Action**: Displays the average reward for each action.
4. **Action Selection Frequency**: Shows how often each action is selected.
5. **Reward Distribution**: Shows what percentage of rewards for each action fall into the excellent/good/neutral/poor/very poor categories.

## How to Use

### 1. Run the SimpleSSD Simulation

The reward logging is enabled by default in the RL-GC module. Run your simulation as usual:

```bash
./simplessd-standalone [your configuration]
```

This will generate a reward log file at `output/rl_gc_reward_log.csv`.

### 2. Visualize the Results

After the simulation is complete, run the visualization script:

```bash
# Basic usage with default parameters
./scripts/visualize_rewards.py

# Or specify custom input and output paths
./scripts/visualize_rewards.py --input=output/rl_gc_reward_log.csv --output-dir=output/reward_plots
```

### 3. Examine the Plots

The visualizations will be saved in the specified output directory (default: `output/reward_plots/`). Open these files to analyze the RL agent's behavior:

- `reward_trends.png`: Shows how rewards change over time for each action
- `cumulative_rewards.png`: Shows accumulated rewards for each action
- `average_rewards.png`: Shows average reward per action
- `action_frequency.png`: Shows how often each action is selected
- `reward_distribution.png`: Shows distribution of rewards for each action

## Requirements

The visualization script requires Python 3 with the following packages:
- pandas
- matplotlib
- numpy

Install them using:

```bash
pip install pandas matplotlib numpy
```

## Customizing Reward Logging

If you want to disable reward logging or change the output file, you can modify the RL-GC initialization in your simulation:

```cpp
// To disable reward logging:
rlgc->disableActionRewardLogging();

// To change the output file:
rlgc->enableActionRewardLogging("path/to/custom_file.csv");
```

## Understanding the Results

When analyzing the visualizations, look for:

1. **Convergence Patterns**: Do rewards for particular actions stabilize over time?
2. **Action Preference**: Does the agent consistently choose specific actions in similar states?
3. **Exploration vs. Exploitation**: During the early phase, the agent explores different actions. Later, it should exploit the knowledge it has gained.
4. **Threshold Effects**: How do the different reward thresholds (based on response time percentiles) affect action selection?

If you see that Action 0 (copying 0 pages) consistently gets higher rewards, this indicates that minimal interference with normal I/O operations often leads to better response times in your workload. 