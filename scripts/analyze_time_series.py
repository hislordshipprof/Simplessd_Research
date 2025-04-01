#!/usr/bin/env python3
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from matplotlib.ticker import FuncFormatter

# Set plot style
plt.style.use('ggplot')
sns.set_context("paper")

# Constants
OUTPUT_DIR = "./output"
RESULTS_DIR = "results"

# Ensure results directory exists
os.makedirs(RESULTS_DIR, exist_ok=True)

# File paths for different policies
DEFAULT_METRICS = os.path.join(OUTPUT_DIR, "default_page_level_metrics.txt")
LAZY_RTGC_METRICS = os.path.join(OUTPUT_DIR, "lazy_rtgc__metrics.txt")
RL_BASELINE_METRICS = os.path.join(OUTPUT_DIR, "rl_baseline_metrics.txt")
RL_INTENSIVE_METRICS = os.path.join(OUTPUT_DIR, "rl_intensive_gc_metrics.txt")
RL_AGGRESSIVE_METRICS = os.path.join(OUTPUT_DIR, "rl_aggressive_gc_metrics.txt")

def format_ns_to_ms(x, pos):
    """Convert nanoseconds to milliseconds for axis labels"""
    return f"{x/1e6:.0f}"

def read_metrics_file(filepath, policy_name):
    """Read metrics file and convert to DataFrame with appropriate column names"""
    print(f"Reading {policy_name} metrics from {filepath}...")
    
    # Read the metrics file
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    # Extract the header line (contains column names)
    for line in lines:
        if line.startswith("# Format:"):
            header_line = line.strip("# Format: ").strip()
            break
    else:
        print(f"Warning: Could not find Format line in {filepath}")
        return None
    
    # Parse column names
    columns = [col.strip("<>") for col in header_line.split()]
    
    # Filter out comment lines and parse data
    data_lines = [line for line in lines if not line.startswith("#")]
    
    # Parse the data
    data = []
    for line in data_lines:
        values = line.strip().split()
        if len(values) == len(columns):
            data.append([float(val) if i > 0 else int(val) for i, val in enumerate(values)])
    
    # Create DataFrame
    df = pd.DataFrame(data, columns=columns)
    
    # Add policy name column
    df['policy'] = policy_name
    
    return df

def load_all_metrics():
    """Load all metrics files and return as dataframes"""
    metrics = {}
    
    # Define file paths and policy names
    files_and_policies = [
        (DEFAULT_METRICS, "Default Page-Level"),
        (LAZY_RTGC_METRICS, "LazyRTGC"),
        (RL_BASELINE_METRICS, "RL-Baseline"),
        (RL_INTENSIVE_METRICS, "RL-Intensive"),
        (RL_AGGRESSIVE_METRICS, "RL-Aggressive")
    ]
    
    # Load each file
    for filepath, policy_name in files_and_policies:
        if os.path.exists(filepath):
            df = read_metrics_file(filepath, policy_name)
            if df is not None:
                metrics[policy_name] = df
        else:
            print(f"Warning: File {filepath} does not exist")
    
    return metrics

def plot_response_time_over_gc_invocations(metrics):
    """Plot average response time over GC invocations for each policy"""
    print("Plotting average response time over GC invocations...")
    
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, plot response time over GC invocations
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        avg_response_time = df['avg_response_time'].values
        
        # Plot
        ax.plot(gc_invocations, avg_response_time, 
                label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('Average Response Time (ms)', fontsize=14)
    ax.set_title('Average Response Time vs. GC Invocations', fontsize=16)
    
    # Format y-axis to show milliseconds
    ax.yaxis.set_major_formatter(FuncFormatter(format_ns_to_ms))
    
    # Add legend
    ax.legend(loc='upper left')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'response_time_vs_gc_invocations.png'), dpi=300)
    plt.close()

def plot_p99_latency_over_gc_invocations(metrics):
    """Plot P99 latency over GC invocations for each policy"""
    print("Plotting P99 latency over GC invocations...")
    
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, plot P99 latency over GC invocations
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        p99_latency = df['p99_latency'].values
        
        # Plot
        ax.plot(gc_invocations, p99_latency, 
                label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('P99 Latency (ms)', fontsize=14)
    ax.set_title('P99 Latency vs. GC Invocations', fontsize=16)
    
    # Format y-axis to show milliseconds
    ax.yaxis.set_major_formatter(FuncFormatter(format_ns_to_ms))
    
    # Add legend
    ax.legend(loc='upper left')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'p99_latency_vs_gc_invocations.png'), dpi=300)
    plt.close()

def plot_long_tail_latency(metrics):
    """Plot P99.9 and P99.99 latency over GC invocations"""
    print("Plotting long-tail latency (P99.9 and P99.99) over GC invocations...")
    
    # Create subplots for P99.9 and P99.99
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 14))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, plot P99.9 latency
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        p99_9_latency = df['p99.9_latency'].values
        
        # Plot P99.9
        ax1.plot(gc_invocations, p99_9_latency, 
                 label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize P99.9 plot
    ax1.set_xlabel('GC Invocations', fontsize=14)
    ax1.set_ylabel('P99.9 Latency (ms)', fontsize=14)
    ax1.set_title('P99.9 Latency vs. GC Invocations', fontsize=16)
    ax1.yaxis.set_major_formatter(FuncFormatter(format_ns_to_ms))
    ax1.legend(loc='upper left')
    ax1.grid(True, linestyle='--', alpha=0.7)
    
    # For each policy, plot P99.99 latency
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        p99_99_latency = df['p99.99_latency'].values
        
        # Plot P99.99
        ax2.plot(gc_invocations, p99_99_latency, 
                 label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize P99.99 plot
    ax2.set_xlabel('GC Invocations', fontsize=14)
    ax2.set_ylabel('P99.99 Latency (ms)', fontsize=14)
    ax2.set_title('P99.99 Latency vs. GC Invocations', fontsize=16)
    ax2.yaxis.set_major_formatter(FuncFormatter(format_ns_to_ms))
    ax2.legend(loc='upper left')
    ax2.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'long_tail_latency_vs_gc_invocations.png'), dpi=300)
    plt.close()

def plot_page_copies_over_time(metrics):
    """Plot total page copies over GC invocations"""
    print("Plotting page copies over GC invocations...")
    
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, plot page copies
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        
        # Check if 'page_copies' exists, otherwise use other column names
        if 'page_copies' in df.columns:
            page_copies = df['page_copies'].values
        elif 'pages_copied' in df.columns:
            page_copies = df['pages_copied'].values
        else:
            # Try to find any column that might contain page copies info
            for col in df.columns:
                if 'page' in col.lower() and 'cop' in col.lower():
                    page_copies = df[col].values
                    break
            else:
                print(f"Warning: Could not find page copies column for {policy}")
                continue
        
        # Plot
        ax.plot(gc_invocations, page_copies, 
                label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('Total Pages Copied', fontsize=14)
    ax.set_title('Page Copies vs. GC Invocations', fontsize=16)
    
    # Format y-axis with commas
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, p: format(int(x), ',')))
    
    # Add legend
    ax.legend(loc='upper left')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'page_copies_vs_gc_invocations.png'), dpi=300)
    plt.close()

def plot_reward_progression(metrics):
    """Plot reward value progression for RL policies"""
    print("Plotting reward progression for RL policies...")
    
    # Check if we have any RL policies with reward data
    rl_policies = {policy: df for policy, df in metrics.items() 
                  if policy.startswith('RL-') and 'avg_reward' in df.columns}
    
    if not rl_policies:
        print("No RL policies with reward data found, skipping reward progression plot")
        return
    
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#2ca02c', '#d62728', '#9467bd']
    
    # For each RL policy, plot average reward
    for i, (policy, df) in enumerate(rl_policies.items()):
        # Extract data
        gc_invocations = df['gc_invocations'].values
        avg_reward = df['avg_reward'].values
        
        # Plot
        ax.plot(gc_invocations, avg_reward, 
                label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('Average Reward', fontsize=14)
    ax.set_title('Average Reward Progression for RL Policies', fontsize=16)
    
    # Add legend
    ax.legend(loc='lower right')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'reward_progression.png'), dpi=300)
    plt.close()

def calculate_free_blocks_over_time(metrics):
    """Calculate the number of free blocks over time based on erase counts and GC invocations"""
    print("Calculating and plotting free blocks over time...")
    
    # For each policy, estimate free blocks over time
    for policy, df in metrics.items():
        # For simplicity, we'll set initial free blocks to 10 (typically TGC threshold)
        # and update based on erases and GC activity
        
        if 'erases' not in df.columns:
            print(f"Warning: No 'erases' column found for {policy}, skipping free blocks calculation")
            continue
        
        # Start with 10 free blocks
        initial_free_blocks = 10
        
        # Calculate free blocks based on erase operations and GC operations
        # This is a simplified model - in reality it depends on many factors
        free_blocks = [initial_free_blocks]
        
        for i in range(1, len(df)):
            prev_erases = df['erases'].iloc[i-1]
            curr_erases = df['erases'].iloc[i]
            
            # Calculate blocks freed by erases
            blocks_freed = curr_erases - prev_erases
            
            # Assume each GC invocation consumes a free block on average
            # This is a simplification - in reality it depends on the specific GC policy
            prev_gc = df['gc_invocations'].iloc[i-1]
            curr_gc = df['gc_invocations'].iloc[i]
            gc_ops = curr_gc - prev_gc
            
            # Update free blocks (capped at a reasonable maximum)
            new_free_blocks = min(free_blocks[-1] + blocks_freed - 0.5 * gc_ops, 50)
            
            # Ensure we don't go below 0
            new_free_blocks = max(0, new_free_blocks)
            
            free_blocks.append(new_free_blocks)
        
        # Add to dataframe
        df['estimated_free_blocks'] = free_blocks
    
    # Plot free blocks over time
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, plot free blocks
    for i, (policy, df) in enumerate(metrics.items()):
        if 'estimated_free_blocks' in df.columns:
            # Extract data
            gc_invocations = df['gc_invocations'].values
            free_blocks = df['estimated_free_blocks'].values
            
            # Plot
            ax.plot(gc_invocations, free_blocks, 
                    label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('Estimated Free Blocks', fontsize=14)
    ax.set_title('Estimated Free Blocks vs. GC Invocations', fontsize=16)
    
    # Add horizontal line at TGC threshold (typically 10)
    ax.axhline(y=10, color='black', linestyle='--', alpha=0.7)
    ax.text(0, 10.5, 'TGC Threshold (10)', fontsize=10)
    
    # Add horizontal line at TIGC threshold (typically 3)
    ax.axhline(y=3, color='red', linestyle='--', alpha=0.7)
    ax.text(0, 3.5, 'TIGC Threshold (3)', fontsize=10)
    
    # Add legend
    ax.legend(loc='upper right')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'free_blocks_vs_gc_invocations.png'), dpi=300)
    plt.close()

def plot_gc_rate(metrics):
    """Plot the rate of GC invocations over time"""
    print("Plotting GC rate over time...")
    
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # Set colors for each policy
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']
    
    # For each policy, calculate and plot GC rate
    for i, (policy, df) in enumerate(metrics.items()):
        # Extract data
        timestamp = df['timestamp'].values
        gc_invocations = df['gc_invocations'].values
        
        # Calculate rate of GC invocations
        # We'll use a rolling window of 10 points
        window_size = min(10, len(gc_invocations))
        gc_rate = []
        
        for j in range(len(gc_invocations)):
            if j < window_size:
                # For the first window_size points, use all available data
                if j == 0:
                    gc_rate.append(gc_invocations[j])
                else:
                    time_diff = timestamp[j] - timestamp[0]
                    if time_diff > 0:  # Avoid division by zero
                        rate = (gc_invocations[j] - gc_invocations[0]) / (time_diff / 1e9)  # Convert ns to seconds
                    else:
                        rate = 0
                    gc_rate.append(rate)
            else:
                # For remaining points, use a sliding window
                time_diff = timestamp[j] - timestamp[j - window_size]
                if time_diff > 0:  # Avoid division by zero
                    rate = (gc_invocations[j] - gc_invocations[j - window_size]) / (time_diff / 1e9)  # Convert ns to seconds
                else:
                    rate = 0
                gc_rate.append(rate)
        
        # Plot
        ax.plot(gc_invocations, gc_rate, 
                label=policy, color=colors[i % len(colors)], linewidth=2)
    
    # Customize plot
    ax.set_xlabel('GC Invocations', fontsize=14)
    ax.set_ylabel('GC Rate (invocations/second)', fontsize=14)
    ax.set_title('GC Rate vs. GC Invocations', fontsize=16)
    
    # Add legend
    ax.legend(loc='upper right')
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'gc_rate_vs_gc_invocations.png'), dpi=300)
    plt.close()

def main():
    """Main function to run the time series analysis"""
    print("Starting GC metrics time series analysis...")
    
    # Load metrics data
    metrics = load_all_metrics()
    
    if not metrics:
        print("No metrics data loaded, exiting.")
        return
    
    # Generate plots
    plot_response_time_over_gc_invocations(metrics)
    plot_p99_latency_over_gc_invocations(metrics)
    plot_long_tail_latency(metrics)
    plot_page_copies_over_time(metrics)
    
    # Calculate and plot free blocks over time
    calculate_free_blocks_over_time(metrics)
    
    # Plot GC rate
    plot_gc_rate(metrics)
    
    # Plot reward progression for RL policies
    plot_reward_progression(metrics)
    
    print(f"Time series analysis complete! Results saved to {RESULTS_DIR} directory.")

if __name__ == "__main__":
    main() 