import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
import argparse
import os
import glob
import re

# Load and combine multiple Q-table data files
def combine_q_tables(file_patterns):
    """Combine multiple Q-table files into a single DataFrame."""
    all_files = []
    for pattern in file_patterns:
        all_files.extend(glob.glob(pattern))
    
    if not all_files:
        print("No Q-table files found matching the patterns.")
        return None
    
    # Sort files by timestamp (assuming timestamp is in filename)
    all_files.sort(key=lambda x: int(re.search(r'q_table_(\d+)\.csv', x).group(1)) if re.search(r'q_table_(\d+)\.csv', x) else 0)
    
    print(f"Found {len(all_files)} Q-table files. Combining...")
    
    combined_df = pd.DataFrame()
    for file in all_files:
        try:
            df = pd.read_csv(file)
            # Extract timestamp from filename
            timestamp = re.search(r'q_table_(\d+)\.csv', file)
            if timestamp:
                df['Timestamp'] = int(timestamp.group(1))
            else:
                df['Timestamp'] = 0
                
            # Append to combined dataframe
            combined_df = pd.concat([combined_df, df])
        except Exception as e:
            print(f"Error reading file {file}: {e}")
    
    return combined_df

# Load and combine multiple convergence data files
def combine_convergence_files(file_patterns):
    """Combine multiple convergence files into a single DataFrame."""
    all_files = []
    for pattern in file_patterns:
        all_files.extend(glob.glob(pattern))
    
    if not all_files:
        print("No convergence files found matching the patterns.")
        return None
    
    # Sort files by timestamp (assuming timestamp is in filename)
    all_files.sort(key=lambda x: int(re.search(r'convergence_(\d+)\.csv', x).group(1)) if re.search(r'convergence_(\d+)\.csv', x) else 0)
    
    print(f"Found {len(all_files)} convergence files. Combining...")
    
    combined_df = pd.DataFrame()
    for file in all_files:
        try:
            df = pd.read_csv(file)
            
            # For single-row files, extract the timestamp from the filename
            timestamp = re.search(r'convergence_(\d+)\.csv', file)
            if timestamp:
                df['FileTimestamp'] = int(timestamp.group(1))
            else:
                df['FileTimestamp'] = 0
                
            # Append to combined dataframe
            combined_df = pd.concat([combined_df, df])
        except Exception as e:
            print(f"Error reading file {file}: {e}")
    
    # Sort by iteration
    if not combined_df.empty:
        combined_df = combined_df.sort_values('Iteration').reset_index(drop=True)
    
    return combined_df

# Enhanced Q-table heatmap plot
def plot_q_table_heatmap(csv_data, output_file=None):
    """Plot a heatmap of the Q-table values."""
    # If input is a string (filename), read it
    if isinstance(csv_data, str):
        if os.path.exists(csv_data):
            df = pd.read_csv(csv_data)
        else:
            print(f"Error: File not found: {csv_data}")
            return
    else:
        # Assume it's already a DataFrame
        df = csv_data
    
    if df.empty:
        print("Error: No data to visualize in Q-table.")
        return
    
    # Check if we have necessary columns
    action_columns = [col for col in df.columns if col.startswith('Action')]
    if not action_columns or 'PrevInterval' not in df.columns or 'CurrInterval' not in df.columns:
        print("Error: Q-table data does not have expected columns.")
        return
    
    # Calculate best action and value
    df['CalculatedBestAction'] = df[action_columns].idxmax(axis=1).str.replace('Action', '').astype(int)
    df['BestQValue'] = df.apply(lambda row: row[f'Action{int(row["CalculatedBestAction"])}'], axis=1)
    
    # Create a more meaningful state representation
    df['State'] = df.apply(lambda row: f"({row['PrevInterval']},{row['CurrInterval']},{row['PrevAction']})", axis=1)
    
    # If there are too few states for a meaningful heatmap, create a bar chart instead
    if len(df['State'].unique()) <= 5:
        plt.figure(figsize=(12, 8))
        
        # Plot all action values for each state
        states = df['State'].unique()
        x = np.arange(len(states))
        bar_width = 0.8 / len(action_columns)
        
        for i, action_col in enumerate(action_columns):
            values = [df[df['State'] == state][action_col].values[0] for state in states]
            plt.bar(x + i * bar_width, values, width=bar_width, label=action_col)
        
        plt.xlabel('State (PrevInterval, CurrInterval, PrevAction)')
        plt.ylabel('Q-Value')
        plt.title('Q-Values by State and Action')
        plt.xticks(x + bar_width * len(action_columns) / 2, states, rotation=45)
        plt.legend()
        plt.grid(axis='y')
        plt.tight_layout()
    else:
        # Create pivot table for heatmap
        try:
            pivot = df.pivot_table(
                values='BestQValue', 
                index=['PrevInterval', 'PrevAction'],
                columns='CurrInterval',
                aggfunc='mean'
            )
            
            # Plot heatmap
            plt.figure(figsize=(12, 10))
            sns.heatmap(pivot, annot=True, cmap='viridis', fmt='.2f')
            plt.title('Q-Table Heatmap: Best Action Values')
        except Exception as e:
            print(f"Error creating heatmap: {e}")
            # Fallback to a simpler visualization
            plt.figure(figsize=(10, 6))
            plt.scatter(df['CurrInterval'], df['BestQValue'], c=df['PrevInterval'], 
                       cmap='viridis', s=100, alpha=0.7)
            plt.colorbar(label='Previous Interval')
            plt.xlabel('Current Interval')
            plt.ylabel('Best Q-Value')
            plt.title('Q-Table Scatter Plot: Best Action Values by State')
            plt.grid(True)
    
    if output_file:
        plt.savefig(output_file)
        print(f"Saved Q-table visualization to {output_file}")
    else:
        plt.show()

# Enhanced convergence metrics plot
def plot_convergence_metrics(metrics_data, output_file=None):
    """Plot convergence metrics over iterations."""
    # If input is a string (filename), read it
    if isinstance(metrics_data, str):
        if os.path.exists(metrics_data):
            df = pd.read_csv(metrics_data)
        else:
            print(f"Error: File not found: {metrics_data}")
            return
    else:
        # Assume it's already a DataFrame
        df = metrics_data
    
    if df.empty:
        print("Error: No data to visualize in convergence metrics.")
        return
    
    # Check if we have single-point data or time series
    if len(df) <= 1:
        print("Warning: Only one data point in convergence file. Cannot show trends.")
        # Create a bar chart for the single data point
        plt.figure(figsize=(10, 6))
        metrics = ['MaxQDelta', 'AvgReward', 'ConvMetric']
        values = [df[metric].values[0] for metric in metrics if metric in df.columns]
        plt.bar(metrics, values)
        plt.ylabel('Value')
        plt.title('Convergence Metrics (Single Point)')
        
        # Add NumStates as a text annotation if available
        if 'NumStates' in df.columns and df['NumStates'].values[0] > 0:
            plt.figtext(0.7, 0.8, f"Unique States: {int(df['NumStates'].values[0])}", 
                        fontsize=12, ha='center', 
                        bbox={"facecolor":"orange", "alpha":0.2, "pad":5})
            
            # If MaxQDelta is 0 but we know it shouldn't be, add a note
            if 'MaxQDelta' in df.columns and df['MaxQDelta'].values[0] == 0:
                plt.figtext(0.3, 0.8, "Note: MaxQDelta may be incorrect due to timing of export", 
                        fontsize=10, ha='center', 
                        bbox={"facecolor":"yellow", "alpha":0.2, "pad":5})
        
        plt.grid(axis='y')
    else:
        # Create subplots for the metrics
        fig, axes = plt.subplots(4, 1, figsize=(12, 16), sharex=True)
        
        # Plot MaxQDelta if available
        if 'MaxQDelta' in df.columns:
            axes[0].plot(df['Iteration'], df['MaxQDelta'], 'b-')
            # If all values are 0, add an annotation
            if (df['MaxQDelta'] == 0).all():
                axes[0].annotate("Note: MaxQDelta values may be incorrect due to timing of export", 
                               xy=(0.5, 0.5), xycoords='axes fraction',
                               ha='center', va='center',
                               bbox={"facecolor":"yellow", "alpha":0.2, "pad":5})
            axes[0].set_ylabel('Max Q-Value Delta')
            axes[0].set_title('Q-Value Stability (Lower = More Converged)')
            axes[0].grid(True)
        
        # Plot AvgReward if available
        if 'AvgReward' in df.columns:
            axes[1].plot(df['Iteration'], df['AvgReward'], 'g-')
            axes[1].set_ylabel('Average Reward')
            axes[1].set_title('Reward Trends')
            axes[1].grid(True)
        
        # Plot NumStates if available
        if 'NumStates' in df.columns:
            axes[2].plot(df['Iteration'], df['NumStates'], 'm-')
            axes[2].set_ylabel('Number of States')
            axes[2].set_title('State Space Growth')
            axes[2].grid(True)
        
        # Plot ConvMetric if available
        if 'ConvMetric' in df.columns:
            axes[3].plot(df['Iteration'], df['ConvMetric'], 'r-')
            axes[3].set_ylabel('Convergence Metric')
            axes[3].set_title('Convergence Metric (Lower = More Converged)')
            axes[3].grid(True)
        
        axes[3].set_xlabel('Iteration')
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file)
        print(f"Saved convergence metrics plot to {output_file}")
    else:
        plt.show()

# Enhanced reward trends plot
def plot_reward_trends(metrics_data, output_file=None):
    """Plot reward trends over iterations."""
    # If input is a string (filename), read it
    if isinstance(metrics_data, str):
        if os.path.exists(metrics_data):
            df = pd.read_csv(metrics_data)
        else:
            print(f"Error: File not found: {metrics_data}")
            return
    else:
        # Assume it's already a DataFrame
        df = metrics_data
    
    if df.empty or 'AvgReward' not in df.columns:
        print("Error: No reward data to visualize.")
        return
    
    # Check if we have single-point data or time series
    if len(df) <= 1:
        print("Warning: Only one data point for rewards. Cannot show trends.")
        plt.figure(figsize=(8, 6))
        plt.bar(['Average Reward'], [df['AvgReward'].values[0]])
        plt.ylabel('Value')
        plt.title('Average Reward (Single Point)')
        plt.grid(axis='y')
    else:
        plt.figure(figsize=(12, 6))
        plt.plot(df['Iteration'], df['AvgReward'], 'b-', label='Average Reward')
        
        # Add a trend line (moving average)
        window_size = min(10, len(df))
        if window_size > 0:
            df['RewardMA'] = df['AvgReward'].rolling(window=window_size, min_periods=1).mean()
            plt.plot(df['Iteration'], df['RewardMA'], 'r--', label=f'{window_size}-point Moving Average')
        
        plt.xlabel('GC Invocations')
        plt.ylabel('Reward')
        plt.title('RL-GC Reward Trend Analysis')
        plt.grid(True)
        plt.legend()
    
    if output_file:
        plt.savefig(output_file)
        print(f"Saved reward trends plot to {output_file}")
    else:
        plt.show()

# Enhanced action distribution plot
def plot_action_distribution(csv_data, output_file=None):
    """Plot distribution of best actions from Q-table."""
    # If input is a string (filename), read it
    if isinstance(csv_data, str):
        if os.path.exists(csv_data):
            df = pd.read_csv(csv_data)
        else:
            print(f"Error: File not found: {csv_data}")
            return
    else:
        # Assume it's already a DataFrame
        df = csv_data
    
    if df.empty:
        print("Error: No data to visualize for action distribution.")
        return
    
    # Check if BestAction column exists
    if 'BestAction' not in df.columns:
        print("Error: No BestAction column in Q-table.")
        return
    
    # Count occurrences of each best action
    action_counts = df['BestAction'].value_counts().sort_index()
    
    plt.figure(figsize=(10, 6))
    bars = plt.bar(action_counts.index, action_counts.values)
    
    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 0.1,
                 f'{height}', ha='center', va='bottom')
    
    plt.xlabel('Action (Pages to Copy)')
    plt.ylabel('Count of States')
    plt.title('Q-Table Action Distribution')
    plt.xticks(action_counts.index)
    plt.grid(axis='y')
    
    # Add a second plot to compare calculated best action vs. stored best action
    action_columns = [col for col in df.columns if col.startswith('Action')]
    if action_columns:
        # Calculate best action (max Q-value)
        df['CalculatedBestAction'] = df[action_columns].idxmax(axis=1).str.replace('Action', '').astype(int)
        
        # Count matches and mismatches
        match_count = (df['BestAction'] == df['CalculatedBestAction']).sum()
        mismatch_count = len(df) - match_count
        
        # Add a small sub-plot to show match/mismatch
        if mismatch_count > 0:
            plt.axes([0.7, 0.7, 0.2, 0.2])
            plt.pie([match_count, mismatch_count], 
                   labels=['Matching', 'Mismatched'], 
                   autopct='%1.1f%%',
                   colors=['green', 'red'])
            plt.title('Best Action Consistency')
    
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file)
        print(f"Saved action distribution plot to {output_file}")
    else:
        plt.show()

def find_latest_file(pattern):
    """Find the most recently created file matching the pattern."""
    files = glob.glob(pattern)
    if not files:
        return None
    return max(files, key=os.path.getctime)

def main():
    # Set up argument parser
    parser = argparse.ArgumentParser(description='Visualize RL-GC Q-table and convergence metrics')
    parser.add_argument('--q-table', type=str, help='Path to Q-table CSV file')
    parser.add_argument('--metrics', type=str, help='Path to convergence metrics CSV file')
    parser.add_argument('--output-dir', type=str, default='plots', help='Directory to save output plots')
    parser.add_argument('--latest', action='store_true', help='Use the latest files in the output directory')
    parser.add_argument('--reward-only', action='store_true', help='Only plot reward trends')
    parser.add_argument('--actions-only', action='store_true', help='Only plot action distribution')
    parser.add_argument('--combine-all', action='store_true', help='Combine all data files found in output directory')
    
    args = parser.parse_args()
    
    # Create output directory if it doesn't exist
    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)
        print(f"Created output directory: {args.output_dir}")
    
    # Determine which files to use
    q_table_file = args.q_table
    metrics_file = args.metrics
    combined_q_table = None
    combined_metrics = None
    
    if args.combine_all:
        print("Combining all data files...")
        combined_q_table = combine_q_tables(["output/q_table_*.csv"])
        combined_metrics = combine_convergence_files(["output/convergence_*.csv"])
        
        if combined_q_table is not None:
            print(f"Combined {len(combined_q_table)} Q-table rows.")
        if combined_metrics is not None:
            print(f"Combined {len(combined_metrics)} convergence data points.")
    elif args.latest:
        q_table_file = find_latest_file("output/q_table_*.csv")
        metrics_file = find_latest_file("output/convergence_*.csv")
        
        if q_table_file:
            print(f"Using latest Q-table file: {q_table_file}")
        if metrics_file:
            print(f"Using latest metrics file: {metrics_file}")
    
    # Generate plots
    if args.actions_only:
        if combined_q_table is not None:
            output_file = os.path.join(args.output_dir, 'action_distribution_combined.png')
            plot_action_distribution(combined_q_table, output_file)
        elif q_table_file and os.path.exists(q_table_file):
            output_file = os.path.join(args.output_dir, 'action_distribution.png')
            plot_action_distribution(q_table_file, output_file)
        else:
            print("No Q-table data available for action distribution plot.")
    elif args.reward_only:
        if combined_metrics is not None:
            output_file = os.path.join(args.output_dir, 'reward_trends_combined.png')
            plot_reward_trends(combined_metrics, output_file)
        elif metrics_file and os.path.exists(metrics_file):
            output_file = os.path.join(args.output_dir, 'reward_trends.png')
            plot_reward_trends(metrics_file, output_file)
        else:
            print("No metrics data available for reward trends plot.")
    else:
        # Generate all plots
        if combined_q_table is not None:
            output_file = os.path.join(args.output_dir, 'q_table_heatmap_combined.png')
            plot_q_table_heatmap(combined_q_table, output_file)
            
            output_file = os.path.join(args.output_dir, 'action_distribution_combined.png')
            plot_action_distribution(combined_q_table, output_file)
        elif q_table_file and os.path.exists(q_table_file):
            output_file = os.path.join(args.output_dir, 'q_table_heatmap.png')
            plot_q_table_heatmap(q_table_file, output_file)
            
            output_file = os.path.join(args.output_dir, 'action_distribution.png')
            plot_action_distribution(q_table_file, output_file)
        
        if combined_metrics is not None:
            output_file = os.path.join(args.output_dir, 'convergence_plot_combined.png')
            plot_convergence_metrics(combined_metrics, output_file)
            
            output_file = os.path.join(args.output_dir, 'reward_trends_combined.png')
            plot_reward_trends(combined_metrics, output_file)
        elif metrics_file and os.path.exists(metrics_file):
            output_file = os.path.join(args.output_dir, 'convergence_plot.png')
            plot_convergence_metrics(metrics_file, output_file)
            
            output_file = os.path.join(args.output_dir, 'reward_trends.png')
            plot_reward_trends(metrics_file, output_file)
    
    # If no valid files were provided or found
    if not args.combine_all and not (q_table_file and os.path.exists(q_table_file)) and not (metrics_file and os.path.exists(metrics_file)):
        if args.latest:
            print("No data files found in the output directory. Try using --combine-all to combine multiple files.")
        else:
            print("No valid files specified. Run with --help for usage information.")

if __name__ == "__main__":
    main()
