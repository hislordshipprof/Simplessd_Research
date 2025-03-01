#!/usr/bin/env python3

import os
import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter

def ensure_dir(directory):
    """Create directory if it doesn't exist."""
    if not os.path.exists(directory):
        os.makedirs(directory)

def load_data(rl_gc_file, regular_gc_file):
    """Load the data from both CSV files."""
    try:
        # Load RL-GC data
        df_rl = pd.read_csv(rl_gc_file)
        
        # Load regular GC data
        df_regular = pd.read_csv(regular_gc_file)
        
        # Verify that both files have the required columns
        if 'IO_Count' not in df_rl.columns or 'Response_Time_ns' not in df_rl.columns:
            print(f"Error: RL-GC file '{rl_gc_file}' missing required columns. Expected 'IO_Count' and 'Response_Time_ns'")
            return None, None
            
        if 'IO_Count' not in df_regular.columns or 'Response_Time_ns' not in df_regular.columns:
            print(f"Error: Regular GC file '{regular_gc_file}' missing required columns. Expected 'IO_Count' and 'Response_Time_ns'")
            return None, None
        
        print(f"Successfully loaded data:")
        print(f"  RL-GC file: {len(df_rl)} records")
        print(f"  Regular GC file: {len(df_regular)} records")
        
        return df_rl, df_regular
    except Exception as e:
        print(f"Error loading data: {e}")
        return None, None

def plot_response_time_comparison(df_rl, df_regular, output_dir):
    """Plot comparison of response times between RL-GC and regular GC."""
    ensure_dir(output_dir)
    
    # Extract response times from both dataframes - now we have consistent column names
    regular_response = df_regular['Response_Time_ns']
    rl_response = df_rl['Response_Time_ns']
    
    # Export raw data for external analysis
    export_raw_data(regular_response, rl_response, output_dir)
    
    # 1. Plot average response time comparison
    plt.figure(figsize=(10, 6))
    avg_times = [regular_response.mean(), rl_response.mean()]
    labels = ['Regular GC', 'RL-GC']
    plt.bar(labels, avg_times, color=['#1f77b4', '#ff7f0e'])
    plt.ylabel('Average Response Time (ns)')
    plt.title('Average Response Time: Regular GC vs RL-GC')
    for i, v in enumerate(avg_times):
        plt.text(i, v + 0.01*max(avg_times), f"{v:.2f}", ha='center')
    plt.savefig(os.path.join(output_dir, 'avg_response_comparison.png'), dpi=300, bbox_inches='tight')
    plt.close()
    
    # 2. Plot response time distributions
    plt.figure(figsize=(12, 6))
    
    # Use kernel density estimation for smoother distributions
    regular_response.plot.kde(label='Regular GC', color='#1f77b4')
    rl_response.plot.kde(label='RL-GC', color='#ff7f0e')
    
    plt.title('Response Time Distribution: Regular GC vs RL-GC')
    plt.xlabel('Response Time (ns)')
    plt.ylabel('Density')
    plt.legend()
    plt.savefig(os.path.join(output_dir, 'response_time_distribution.png'), dpi=300, bbox_inches='tight')
    plt.close()
    
    # 3. Plot percentile comparison (important for tail latency analysis)
    percentiles = [50, 90, 95, 99, 99.9, 99.99]
    regular_percentiles = [np.percentile(regular_response, p) for p in percentiles]
    rl_percentiles = [np.percentile(rl_response, p) for p in percentiles]
    
    plt.figure(figsize=(12, 6))
    x = np.arange(len(percentiles))
    width = 0.35
    
    plt.bar(x - width/2, regular_percentiles, width, label='Regular GC', color='#1f77b4')
    plt.bar(x + width/2, rl_percentiles, width, label='RL-GC', color='#ff7f0e')
    
    plt.xlabel('Percentile')
    plt.ylabel('Response Time (ns)')
    plt.title('Response Time Percentiles: Regular GC vs RL-GC')
    plt.xticks(x, [f"{p}%" for p in percentiles])
    plt.legend()
    plt.grid(axis='y', linestyle='--', alpha=0.7)
    plt.savefig(os.path.join(output_dir, 'percentile_comparison.png'), dpi=300, bbox_inches='tight')
    plt.close()
    
    # 4. Plot CDF (Cumulative Distribution Function)
    plt.figure(figsize=(12, 6))
    
    # Sort values and calculate cumulative probability
    sorted_regular = np.sort(regular_response)
    sorted_rl = np.sort(rl_response)
    
    # Create a CDF from the sorted data
    yvals_regular = np.arange(1, len(sorted_regular) + 1) / len(sorted_regular)
    yvals_rl = np.arange(1, len(sorted_rl) + 1) / len(sorted_rl)
    
    plt.plot(sorted_regular, yvals_regular, label='Regular GC', color='#1f77b4')
    plt.plot(sorted_rl, yvals_rl, label='RL-GC', color='#ff7f0e')
    
    plt.xlabel('Response Time (ns)')
    plt.ylabel('Cumulative Probability')
    plt.title('Response Time CDF: Regular GC vs RL-GC')
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.gca().yaxis.set_major_formatter(PercentFormatter(1.0))
    plt.savefig(os.path.join(output_dir, 'cdf_comparison.png'), dpi=300, bbox_inches='tight')
    plt.close()
    
    # 5. Print summary statistics
    print("\nSummary Statistics:")
    print("-" * 50)
    print("                    Regular GC        RL-GC")
    print(f"Mean Response Time: {regular_response.mean():.2f} ns      {rl_response.mean():.2f} ns")
    print(f"Median (50th %ile): {np.percentile(regular_response, 50):.2f} ns      {np.percentile(rl_response, 50):.2f} ns")
    print(f"90th Percentile:    {np.percentile(regular_response, 90):.2f} ns      {np.percentile(rl_response, 90):.2f} ns")
    print(f"99th Percentile:    {np.percentile(regular_response, 99):.2f} ns      {np.percentile(rl_response, 99):.2f} ns")
    print(f"99.9th Percentile:  {np.percentile(regular_response, 99.9):.2f} ns      {np.percentile(rl_response, 99.9):.2f} ns")
    print(f"Max Response Time:  {regular_response.max():.2f} ns      {rl_response.max():.2f} ns")
    print(f"Min Response Time:  {regular_response.min():.2f} ns      {rl_response.min():.2f} ns")
    print(f"Standard Deviation: {regular_response.std():.2f} ns      {rl_response.std():.2f} ns")
    
    # Save summary to a text file in the output directory
    with open(os.path.join(output_dir, 'summary_statistics.txt'), 'w') as f:
        f.write("Summary Statistics:\n")
        f.write("-" * 50 + "\n")
        f.write("                    Regular GC        RL-GC\n")
        f.write(f"Mean Response Time: {regular_response.mean():.2f} ns      {rl_response.mean():.2f} ns\n")
        f.write(f"Median (50th %ile): {np.percentile(regular_response, 50):.2f} ns      {np.percentile(rl_response, 50):.2f} ns\n")
        f.write(f"90th Percentile:    {np.percentile(regular_response, 90):.2f} ns      {np.percentile(rl_response, 90):.2f} ns\n")
        f.write(f"99th Percentile:    {np.percentile(regular_response, 99):.2f} ns      {np.percentile(rl_response, 99):.2f} ns\n")
        f.write(f"99.9th Percentile:  {np.percentile(regular_response, 99.9):.2f} ns      {np.percentile(rl_response, 99.9):.2f} ns\n")
        f.write(f"Max Response Time:  {regular_response.max():.2f} ns      {rl_response.max():.2f} ns\n")
        f.write(f"Min Response Time:  {regular_response.min():.2f} ns      {rl_response.min():.2f} ns\n")
        f.write(f"Standard Deviation: {regular_response.std():.2f} ns      {rl_response.std():.2f} ns\n")

    print(f"\nPlots and statistics saved to {output_dir}/")

def export_raw_data(regular_response, rl_response, output_dir):
    """Export the raw response times to CSV files for external analysis."""
    # Create a DataFrame with both series
    combined_df = pd.DataFrame({
        'Regular_GC': regular_response.reset_index(drop=True),
        'RL_GC': rl_response.reset_index(drop=True)
    })
    
    # Export to CSV
    combined_df.to_csv(os.path.join(output_dir, 'raw_response_times.csv'), index=False)
    
    # Also export summary statistics
    stats_df = pd.DataFrame({
        'Metric': [
            'Mean', 'Median', '90th_Percentile', '95th_Percentile', 
            '99th_Percentile', '99.9th_Percentile', 'Max', 'Min', 'Std_Dev',
            'Sample_Count'
        ],
        'Regular_GC': [
            regular_response.mean(),
            regular_response.median(),
            np.percentile(regular_response, 90),
            np.percentile(regular_response, 95),
            np.percentile(regular_response, 99),
            np.percentile(regular_response, 99.9),
            regular_response.max(),
            regular_response.min(),
            regular_response.std(),
            len(regular_response)
        ],
        'RL_GC': [
            rl_response.mean(),
            rl_response.median(),
            np.percentile(rl_response, 90),
            np.percentile(rl_response, 95),
            np.percentile(rl_response, 99),
            np.percentile(rl_response, 99.9),
            rl_response.max(),
            rl_response.min(),
            rl_response.std(),
            len(rl_response)
        ]
    })
    
    # Export statistics to CSV
    stats_df.to_csv(os.path.join(output_dir, 'response_time_statistics.csv'), index=False)
    
    print(f"Raw data and statistics exported to {output_dir}/")

def main():
    parser = argparse.ArgumentParser(description='Compare response times between RL-GC and regular GC')
    parser.add_argument('--rl-gc-file', type=str, default='output/rl_gc_response.csv',
                       help='Path to the RL-GC response time CSV file')
    parser.add_argument('--regular-gc-file', type=str, default='output/regular_gc_response.csv',
                       help='Path to the regular GC response time CSV file')
    parser.add_argument('--output-dir', type=str, default='output/response_comparison',
                       help='Directory to save output plots and statistics')
    
    args = parser.parse_args()
    
    print(f"Loading RL-GC data from: {args.rl_gc_file}")
    print(f"Loading regular GC data from: {args.regular_gc_file}")
    
    df_rl, df_regular = load_data(args.rl_gc_file, args.regular_gc_file)
    
    if df_rl is not None and df_regular is not None:
        plot_response_time_comparison(df_rl, df_regular, args.output_dir)
    else:
        print("Failed to compare response times. Check input files and try again.")

if __name__ == "__main__":
    main() 