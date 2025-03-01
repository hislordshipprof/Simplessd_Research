#!/usr/bin/env python3
"""
Visualize action-specific rewards from the RL-GC implementation.
This script reads the reward log CSV file and creates multiple visualizations:

1. Action-specific reward trends over time
2. Cumulative reward per action
3. Average reward per action
4. Action selection frequency
5. Reward distribution per action (showing how rewards fall into different threshold categories)
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import argparse
from matplotlib.ticker import MaxNLocator

def load_data(filepath):
    """Load the reward log CSV file into a pandas DataFrame."""
    if not os.path.exists(filepath):
        raise FileNotFoundError(f"Reward log file not found: {filepath}")
    
    # Load the data
    df = pd.read_csv(filepath)
    
    # Basic validation
    required_columns = ['Iteration', 'Action', 'Reward', 'AverageReward', 
                        'ThresholdT1', 'ThresholdT2', 'ThresholdT3']
    for col in required_columns:
        if col not in df.columns:
            raise ValueError(f"Required column '{col}' not found in the CSV file")
    
    print(f"Loaded {len(df)} data points for {df['Action'].nunique()} unique actions")
    return df

def plot_reward_trends(df, output_dir):
    """Plot reward trends over time for each action."""
    plt.figure(figsize=(12, 8))
    
    # Get unique actions
    actions = sorted(df['Action'].unique())
    colors = plt.cm.tab10(np.linspace(0, 1, len(actions)))
    
    # Plot rewards for each action
    for i, action in enumerate(actions):
        action_data = df[df['Action'] == action]
        
        # Use a rolling window to smooth the data
        window_size = min(50, len(action_data) // 10) if len(action_data) > 50 else 1
        if len(action_data) > window_size:
            rewards = action_data['Reward'].rolling(window=window_size).mean()
        else:
            rewards = action_data['Reward']
        
        plt.plot(action_data['Iteration'], rewards, 
                 label=f'Action {action}', color=colors[i], alpha=0.8)
    
    plt.title('Reward Trends by Action Over Time')
    plt.xlabel('GC Invocation')
    plt.ylabel('Reward (Moving Average)')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    # Add horizontal lines for reward thresholds
    if len(df) > 0:
        latest_thresholds = df.iloc[-1]
        plt.axhline(y=1.0, color='green', linestyle='--', alpha=0.5, label='Excellent (+1.0)')
        plt.axhline(y=0.5, color='blue', linestyle='--', alpha=0.5, label='Good (+0.5)')
        plt.axhline(y=0.0, color='gray', linestyle='-', alpha=0.5)
        plt.axhline(y=-0.5, color='orange', linestyle='--', alpha=0.5, label='Poor (-0.5)')
        plt.axhline(y=-1.0, color='red', linestyle='--', alpha=0.5, label='Very Poor (-1.0)')
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'reward_trends.png'), dpi=300)
    plt.close()

def plot_cumulative_rewards(df, output_dir):
    """Plot cumulative rewards per action."""
    plt.figure(figsize=(12, 8))
    
    # Get unique actions
    actions = sorted(df['Action'].unique())
    
    # For each action, calculate cumulative rewards over time
    for action in actions:
        action_data = df[df['Action'] == action]
        if len(action_data) > 0:
            # Sort by iteration to ensure proper cumulative calculation
            action_data = action_data.sort_values('Iteration')
            cumulative_rewards = action_data['Reward'].cumsum()
            
            plt.plot(action_data['Iteration'], cumulative_rewards, 
                    label=f'Action {action}')
    
    plt.title('Cumulative Reward by Action')
    plt.xlabel('GC Invocation')
    plt.ylabel('Cumulative Reward')
    plt.legend()
    plt.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'cumulative_rewards.png'), dpi=300)
    plt.close()

def plot_average_rewards(df, output_dir):
    """Plot average reward per action."""
    plt.figure(figsize=(12, 8))
    
    # Calculate average reward per action
    avg_rewards = df.groupby('Action')['Reward'].mean().reset_index()
    avg_rewards = avg_rewards.sort_values('Action')
    
    bars = plt.bar(avg_rewards['Action'], avg_rewards['Reward'], 
                  color=plt.cm.viridis(np.linspace(0, 1, len(avg_rewards))))
    
    # Add value labels on top of each bar
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                f'{height:.3f}',
                ha='center', va='bottom', rotation=0)
    
    plt.title('Average Reward by Action')
    plt.xlabel('Action (Number of Pages to Copy)')
    plt.ylabel('Average Reward')
    plt.grid(True, alpha=0.3, axis='y')
    
    # Set x-axis to show integer values
    plt.gca().xaxis.set_major_locator(MaxNLocator(integer=True))
    
    # Add horizontal line at y=0
    plt.axhline(y=0, color='gray', linestyle='-', alpha=0.5)
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'average_rewards.png'), dpi=300)
    plt.close()

def plot_action_frequency(df, output_dir):
    """Plot action selection frequency with enhanced color representation."""
    plt.figure(figsize=(14, 8))
    
    # Count occurrences of each action
    action_counts = df['Action'].value_counts().sort_index()
    
    # Calculate average reward per action for color intensity
    avg_rewards = df.groupby('Action')['Reward'].mean()
    
    # Normalize rewards to [0,1] range for color mapping
    if avg_rewards.max() != avg_rewards.min():  # Avoid division by zero
        norm_rewards = (avg_rewards - avg_rewards.min()) / (avg_rewards.max() - avg_rewards.min())
    else:
        norm_rewards = pd.Series([0.5] * len(avg_rewards), index=avg_rewards.index)
    
    # Create a colormap where higher rewards = greener color
    colors = plt.cm.RdYlGn(norm_rewards)
    
    # Get current axes
    ax = plt.gca()
    
    bars = ax.bar(action_counts.index, action_counts.values, color=colors)
    
    # Add count labels on top of each bar
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height,
                str(int(height)),
                ha='center', va='bottom')
    
    # Add percentage labels
    total_count = action_counts.sum()
    for i, bar in enumerate(bars):
        height = bar.get_height()
        percentage = (height / total_count) * 100
        if percentage > 1:  # Only show for bars with significant percentage
            plt.text(bar.get_x() + bar.get_width()/2., height/2,
                    f"{percentage:.1f}%",
                    ha='center', va='center', color='white', fontweight='bold')
    
    plt.title('Action Selection Frequency')
    plt.xlabel('Action (Number of Pages to Copy)')
    plt.ylabel('Frequency')
    plt.grid(True, alpha=0.3, axis='y')
    
    # Add a colorbar to show reward relationship - specify the axes explicitly
    if len(avg_rewards) > 1:  # Only add colorbar if we have multiple actions with different rewards
        sm = plt.cm.ScalarMappable(cmap=plt.cm.RdYlGn, norm=plt.Normalize(avg_rewards.min(), avg_rewards.max()))
        sm.set_array([])
        cbar = plt.colorbar(sm, ax=ax)
        cbar.set_label('Average Reward per Action')
    
    # Set x-axis to show integer values
    ax.xaxis.set_major_locator(MaxNLocator(integer=True))
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'action_frequency.png'), dpi=300)
    plt.close()

def plot_reward_distribution(df, output_dir):
    """Plot reward distribution per action."""
    plt.figure(figsize=(14, 10))
    
    # Get unique actions
    actions = sorted(df['Action'].unique())
    
    # Define reward categories
    reward_bins = [-1.5, -0.75, -0.25, 0.25, 0.75, 1.5]
    reward_labels = ['Very Poor (-1.0)', 'Poor (-0.5)', 'Neutral (0.0)', 'Good (+0.5)', 'Excellent (+1.0)']
    colors = ['red', 'orange', 'gray', 'lightgreen', 'darkgreen']
    
    # Create subplots
    fig, axs = plt.subplots(len(actions), 1, figsize=(12, 3*len(actions)), sharex=True)
    if len(actions) == 1:
        axs = [axs]  # Make it iterable for consistent handling
    
    for i, action in enumerate(actions):
        action_data = df[df['Action'] == action]
        
        # Create histogram data
        hist, _ = np.histogram(action_data['Reward'], bins=reward_bins)
        
        # Calculate percentage
        total = hist.sum()
        percentages = hist / total * 100 if total > 0 else hist
        
        # Plot bar chart
        bars = axs[i].bar(range(len(percentages)), percentages, color=colors)
        
        # Add percentage labels on top of each bar
        for j, bar in enumerate(bars):
            height = bar.get_height()
            if height > 0:
                axs[i].text(bar.get_x() + bar.get_width()/2., height,
                        f'{height:.1f}%',
                        ha='center', va='bottom')
        
        axs[i].set_title(f'Action {action} - Reward Distribution')
        axs[i].set_ylim(0, 100)
        axs[i].set_ylabel('Percentage')
        axs[i].grid(True, alpha=0.3, axis='y')
    
    # Set common x-axis labels
    plt.xticks(range(len(reward_labels)), reward_labels, rotation=15)
    plt.xlabel('Reward Category')
    
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'reward_distribution.png'), dpi=300)
    plt.close()

def main():
    parser = argparse.ArgumentParser(description='Visualize RL-GC action rewards')
    parser.add_argument('--input', type=str, default='output/rl_gc_reward_log.csv',
                        help='Path to the reward log CSV file')
    parser.add_argument('--output-dir', type=str, default='output/reward_plots',
                        help='Directory to save visualization plots')
    
    args = parser.parse_args()
    
    # Create output directory if it doesn't exist
    os.makedirs(args.output_dir, exist_ok=True)
    
    try:
        # Load data
        df = load_data(args.input)
        
        # Generate plots
        plot_reward_trends(df, args.output_dir)
        plot_cumulative_rewards(df, args.output_dir)
        plot_average_rewards(df, args.output_dir)
        plot_action_frequency(df, args.output_dir)
        plot_reward_distribution(df, args.output_dir)
        
        print(f"Visualizations saved to {args.output_dir}")
    
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    main() 