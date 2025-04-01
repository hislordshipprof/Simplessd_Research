#!/usr/bin/env python3
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
import seaborn as sns

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

# Summary files
DEFAULT_SUMMARY = os.path.join(OUTPUT_DIR, "default_page_level_summary.txt")
LAZY_RTGC_SUMMARY = os.path.join(OUTPUT_DIR, "lazy_rtgc__summary.txt")
RL_BASELINE_SUMMARY = os.path.join(OUTPUT_DIR, "rl_baseline_summary.txt")
RL_INTENSIVE_SUMMARY = os.path.join(OUTPUT_DIR, "rl_intensive_gc_summary.txt")
RL_AGGRESSIVE_SUMMARY = os.path.join(OUTPUT_DIR, "rl_aggressive_gc_summary.txt")

# Define colors for each policy
POLICY_COLORS = {
    "Default Page-Level": "#1f77b4",
    "LazyRTGC": "#ff7f0e",
    "RL-Baseline": "#2ca02c",
    "RL-Intensive": "#d62728",
    "RL-Aggressive": "#9467bd"
}

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

def parse_summary_file(filepath):
    """Parse summary file to extract key metrics"""
    print(f"Parsing summary file: {filepath}")
    summary = {}
    
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    # Extract key metrics
    for i, line in enumerate(lines):
        line = line.strip()
        
        # Look for key metrics
        if "GC Invocations:" in line:
            summary['gc_invocations'] = int(line.split(":")[1].strip())
        elif "Total Pages Copied:" in line or "Total Page Copies:" in line:
            summary['total_pages_copied'] = int(line.split(":")[1].strip())
        elif "Block Erasures:" in line or "Block Erases:" in line:
            summary['block_erases'] = int(line.split(":")[1].strip())
        elif "Average Response Time:" in line:
            # Convert scientific notation to float
            summary['avg_response_time'] = float(line.split(":")[1].strip().split()[0])
        elif "P99 Latency:" in line:
            summary['p99_latency'] = float(line.split(":")[1].strip().split()[0])
        elif "P99.9 Latency:" in line:
            summary['p99.9_latency'] = float(line.split(":")[1].strip().split()[0])
        elif "P99.99 Latency:" in line:
            summary['p99.99_latency'] = float(line.split(":")[1].strip().split()[0])
        elif "Average Reward:" in line:
            summary['avg_reward'] = float(line.split(":")[1].strip())
        elif "Average Pages Copied per GC:" in line:
            summary['avg_pages_per_gc'] = float(line.split(":")[1].strip())
        elif "Early GC Count:" in line:
            summary['early_gc_count'] = int(line.split(":")[1].strip())
        elif "Read-Triggered GC Count:" in line:
            summary['read_triggered_gc_count'] = int(line.split(":")[1].strip())
        elif "Intensive GC Count:" in line or "Intensive GC Operations:" in line:
            summary['intensive_gc_count'] = int(line.split(":")[1].strip())
    
    return summary

def load_all_summaries():
    """Load all summary files and return as a dictionary"""
    summaries = {
        "Default Page-Level": parse_summary_file(DEFAULT_SUMMARY),
        "LazyRTGC": parse_summary_file(LAZY_RTGC_SUMMARY),
        "RL-Baseline": parse_summary_file(RL_BASELINE_SUMMARY),
        "RL-Intensive": parse_summary_file(RL_INTENSIVE_SUMMARY),
        "RL-Aggressive": parse_summary_file(RL_AGGRESSIVE_SUMMARY)
    }
    return summaries

def compare_latency_metrics(summaries):
    """Create plots for tail latencies and average response time"""
    print("Generating latency comparison plots...")
    
    # Extract policies and colors
    policies = list(summaries.keys())
    colors = [POLICY_COLORS[policy] for policy in policies]
    
    # 1. Plot tail latencies (P99, P99.9, P99.99)
    fig, ax = plt.subplots(figsize=(15, 8))
    
    # Set width of bars and positions
    bar_width = 0.15
    group_spacing = 0.8  # Space between percentile groups
    r = np.array([0, 1 + group_spacing, 2 + 2 * group_spacing])  # Positions for each percentile group
    
    # Get tail latency values for each policy
    p99_values = [summaries[p]['p99_latency'] for p in policies]
    p99_9_values = [summaries[p]['p99.9_latency'] for p in policies]
    p99_99_values = [summaries[p]['p99.99_latency'] for p in policies]
    
    # Create bars for each policy
    for i, (policy, color) in enumerate(zip(policies, colors)):
        positions = r + i * bar_width
        values = [
            summaries[policy]['p99_latency'],
            summaries[policy]['p99.9_latency'],
            summaries[policy]['p99.99_latency']
        ]
        bars = ax.bar(positions, values, bar_width, label=policy, color=color)
        
        # Add value labels
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar_width/2., height,
                   f"{height:,.0f}", ha='center', va='bottom', 
                   fontsize=8, rotation=45)
    
    # Customize plot
    ax.set_ylabel('Latency (ns)', fontsize=14)
    ax.set_title('Tail Latency Comparison Across GC Policies', fontsize=16, pad=20)
    
    # Set x-ticks at the center of each group
    group_centers = r + (len(policies) - 1) * bar_width / 2
    ax.set_xticks(group_centers)
    ax.set_xticklabels(['P99', 'P99.9', 'P99.99'], fontsize=12)
    
    # Format y-axis with commas
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, p: f"{x:,.0f}"))
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7, axis='y')
    
    # Add legend with better positioning
    ax.legend(bbox_to_anchor=(1.02, 1), loc='upper left', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'tail_latency_comparison.png'), 
                dpi=300, bbox_inches='tight')
    plt.close()
    
    # 2. Plot normalized tail latencies
    fig, ax = plt.subplots(figsize=(15, 8))
    
    # Calculate normalized values
    lazy_rtgc_values = {
        'p99': summaries['LazyRTGC']['p99_latency'],
        'p99.9': summaries['LazyRTGC']['p99.9_latency'],
        'p99.99': summaries['LazyRTGC']['p99.99_latency']
    }
    
    norm_data = {}
    
    # Create bars for each policy
    for i, (policy, color) in enumerate(zip(policies, colors)):
        positions = r + i * bar_width
        values = [
            summaries[policy]['p99_latency'] / lazy_rtgc_values['p99'],
            summaries[policy]['p99.9_latency'] / lazy_rtgc_values['p99.9'],
            summaries[policy]['p99.99_latency'] / lazy_rtgc_values['p99.99']
        ]
        
        # Store normalized values for summary table
        if policy == 'LazyRTGC':
            norm_data['norm_p99_latency'] = [v / lazy_rtgc_values['p99'] for v in p99_values]
            norm_data['norm_p99_9_latency'] = [v / lazy_rtgc_values['p99.9'] for v in p99_9_values]
            norm_data['norm_p99_99_latency'] = [v / lazy_rtgc_values['p99.99'] for v in p99_99_values]
        
        bars = ax.bar(positions, values, bar_width, label=policy, color=color)
        
        # Add value labels
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar_width/2., height,
                   f"{height:.2f}", ha='center', va='bottom', 
                   fontsize=8, rotation=45)
    
    # Add reference line at y=1 (LazyRTGC)
    ax.axhline(y=1.0, color='black', linestyle='--', alpha=0.7, 
               label='LazyRTGC Reference')
    
    # Customize plot
    ax.set_ylabel('Normalized Latency (relative to LazyRTGC)', fontsize=14)
    ax.set_title('Normalized Tail Latency Comparison', fontsize=16, pad=20)
    
    # Set x-ticks at the center of each group
    group_centers = r + (len(policies) - 1) * bar_width / 2
    ax.set_xticks(group_centers)
    ax.set_xticklabels(['P99', 'P99.9', 'P99.99'], fontsize=12)
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7, axis='y')
    
    # Add legend with better positioning
    ax.legend(bbox_to_anchor=(1.02, 1), loc='upper left', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'normalized_tail_latency_comparison.png'), 
                dpi=300, bbox_inches='tight')
    plt.close()
    
    # 3. Plot average response time comparison
    fig, ax = plt.subplots(figsize=(10, 6))
    
    # Get average response time values
    avg_rt_values = [summaries[p]['avg_response_time'] for p in policies]
    
    # Create bars
    bars = ax.bar(policies, avg_rt_values, color=colors)
    
    # Add value labels
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
               f"{height:,.0f}", ha='center', va='bottom', 
               fontsize=10)
    
    # Customize plot
    ax.set_ylabel('Average Response Time (ns)', fontsize=14)
    ax.set_title('Average Response Time Comparison', fontsize=16)
    
    # Format y-axis with commas
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, p: f"{x:,.0f}"))
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7, axis='y')
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'average_response_time_comparison.png'), 
                dpi=300)
    plt.close()
    
    # Calculate normalized average response time for summary table
    lazy_rtgc_avg = summaries['LazyRTGC']['avg_response_time']
    norm_data['norm_avg_response_time'] = [v / lazy_rtgc_avg for v in avg_rt_values]
    
    return norm_data

def compare_erase_counts(summaries):
    """Create bar chart for erase counts and other operational metrics"""
    print("Generating operational metrics comparison plots...")
    
    # Extract policies and colors
    policies = list(summaries.keys())
    colors = [POLICY_COLORS[policy] for policy in policies]
    
    # Create separate plots for each operational metric
    metrics = [
        ('block_erases', 'Block Erases', 'block_erases_comparison.png'),
        ('gc_invocations', 'GC Invocations', 'gc_invocations_comparison.png'),
        ('total_pages_copied', 'Total Pages Copied', 'pages_copied_comparison.png'),
        ('avg_pages_per_gc', 'Average Pages Copied per GC', 'avg_pages_per_gc_comparison.png')
    ]
    
    # Normalized erase counts (for the summary table)
    lazy_rtgc_erases = summaries['LazyRTGC']['block_erases']
    norm_erase_counts = [summaries[p]['block_erases'] / lazy_rtgc_erases for p in policies]
    
    for metric_key, metric_label, filename in metrics:
        # Instead of skipping, use 0 as default value for missing metrics
        metric_values = []
        for p in policies:
            if metric_key in summaries[p]:
                metric_values.append(summaries[p][metric_key])
            else:
                print(f"Note: {metric_key} not found for {p}, using 0")
                metric_values.append(0)
            
        # Create figure
        fig, ax = plt.subplots(figsize=(12, 8))
        
        # Create bar chart
        bars = ax.bar(policies, metric_values, color=colors)
        
        # Add value labels on top of bars
        for bar in bars:
            height = bar.get_height()
            # Format large numbers with commas
            label = f"{height:,.0f}" if height >= 1000 else f"{height:.2f}"
            
            ax.text(bar.get_x() + bar.get_width()/2., height + 0.05 * max(max(metric_values, default=1), 1),
                   label, ha='center', va='bottom', fontsize=10)
        
        # Customize plot
        ax.set_xlabel('Policy', fontsize=14)
        ax.set_ylabel(metric_label, fontsize=14)
        ax.set_title(f'{metric_label} Comparison Across GC Policies', fontsize=16)
        
        # Format y-axis with commas for large numbers
        ax.yaxis.set_major_formatter(FuncFormatter(lambda x, p: f"{x:,.0f}"))
        
        # Add grid
        ax.grid(True, linestyle='--', alpha=0.7, axis='y')
        
        plt.tight_layout()
        plt.savefig(os.path.join(RESULTS_DIR, filename), dpi=300)
        plt.close()
        
        # Also create a normalized version for block erases
        if metric_key == 'block_erases':
            fig, ax = plt.subplots(figsize=(12, 8))
            
            # Create bar chart for normalized values
            bars = ax.bar(policies, norm_erase_counts, color=colors)
            
            # Add reference line at y=1 (LazyRTGC)
            ax.axhline(y=1.0, color='black', linestyle='--', alpha=0.7)
            
            # Add value labels on top of bars
            for bar in bars:
                height = bar.get_height()
                ax.text(bar.get_x() + bar.get_width()/2., height + 0.05,
                       f"{height:.2f}", ha='center', va='bottom', fontsize=10)
            
            # Customize plot
            ax.set_xlabel('Policy', fontsize=14)
            ax.set_ylabel(f'Normalized {metric_label} (relative to LazyRTGC)', fontsize=14)
            ax.set_title(f'Normalized {metric_label} Comparison', fontsize=16)
            
            plt.tight_layout()
            plt.savefig(os.path.join(RESULTS_DIR, f"normalized_{filename}"), dpi=300)
            plt.close()
    
    return norm_erase_counts

def compare_gc_efficiency(summaries):
    """Create GC efficiency comparison"""
    print("Generating GC efficiency comparison plot...")
    
    # Extract GC metrics
    policies = list(summaries.keys())
    colors = [POLICY_COLORS[policy] for policy in policies]
    gc_invocations = [summaries[p]['gc_invocations'] for p in policies]
    pages_copied = [summaries[p].get('total_pages_copied', 0) for p in policies]
    avg_pages_per_gc = [summaries[p].get('avg_pages_per_gc', 0) for p in policies]
    
    # Setup plot
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 15))
    
    # Plot GC Invocations
    bars1 = ax1.bar(policies, gc_invocations, color=colors)
    ax1.set_title('Total GC Invocations', fontsize=16)
    ax1.set_ylabel('Count', fontsize=14)
    
    # Add value labels
    for bar in bars1:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height + 0.05 * max(gc_invocations),
               f"{height:,}", ha='center', va='bottom', fontsize=10)
    
    # Format large numbers with commas
    ax1.get_yaxis().set_major_formatter(
        FuncFormatter(lambda x, p: format(int(x), ',')))
    
    # Plot Total Pages Copied
    bars2 = ax2.bar(policies, pages_copied, color=colors)
    ax2.set_title('Total Pages Copied', fontsize=16)
    ax2.set_ylabel('Count', fontsize=14)
    
    # Add value labels
    for bar in bars2:
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height + 0.05 * max(pages_copied),
               f"{height:,}", ha='center', va='bottom', fontsize=10)
    
    # Format large numbers with commas
    ax2.get_yaxis().set_major_formatter(
        FuncFormatter(lambda x, p: format(int(x), ',')))
    
    # Plot Average Pages per GC
    bars3 = ax3.bar(policies, avg_pages_per_gc, color=colors)
    ax3.set_title('Average Pages Copied per GC', fontsize=16)
    ax3.set_ylabel('Pages', fontsize=14)
    
    # Add value labels
    for bar in bars3:
        height = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width()/2., height + 0.05 * max(avg_pages_per_gc),
               f"{height:.2f}", ha='center', va='bottom', fontsize=10)
    
    # Add grid to all axes
    ax1.grid(True, linestyle='--', alpha=0.7, axis='y')
    ax2.grid(True, linestyle='--', alpha=0.7, axis='y')
    ax3.grid(True, linestyle='--', alpha=0.7, axis='y')
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'gc_efficiency_comparison.png'), dpi=300)
    plt.close()

def analyze_rl_aggressive_features(summaries):
    """Analyze RL-Aggressive specific features"""
    print("Analyzing RL-Aggressive specific features...")
    
    # Extract RL-Aggressive specific metrics
    rl_agg = summaries['RL-Aggressive']
    
    # Check if we have the required metrics
    required_metrics = ['early_gc_count', 'read_triggered_gc_count', 'intensive_gc_count']
    if not all(metric in rl_agg for metric in required_metrics):
        print("Warning: Missing RL-Aggressive specific metrics, skipping feature analysis")
        return [0, 0, 0]  # Return dummy values
    
    # Create figure
    fig, ax = plt.subplots(figsize=(10, 6))
    
    # Prepare data
    features = ['Early GC', 'Read-Triggered GC', 'Intensive GC']
    counts = [
        rl_agg.get('early_gc_count', 0),
        rl_agg.get('read_triggered_gc_count', 0),
        rl_agg.get('intensive_gc_count', 0)
    ]
    
    # Create pie chart
    wedges, texts, autotexts = ax.pie(
        counts, 
        labels=features,
        autopct='%1.1f%%',
        startangle=90,
        colors=['#ff9999','#66b3ff','#99ff99']
    )
    
    # Add labels with counts
    for i, autotext in enumerate(autotexts):
        autotext.set_text(f"{autotext.get_text()}\n({counts[i]:,})")
    
    # Equal aspect ratio ensures that pie is drawn as a circle
    ax.axis('equal')
    ax.set_title('RL-Aggressive GC Feature Distribution', fontsize=16)
    
    # Increase font size for labels and percentages
    plt.setp(autotexts, size=10, weight='bold')
    plt.setp(texts, size=12)
    
    plt.savefig(os.path.join(RESULTS_DIR, 'rl_aggressive_features.png'), dpi=300)
    plt.close()
    
    # Also create a bar chart version
    fig, ax = plt.subplots(figsize=(10, 6))
    
    # Create bar chart
    bars = ax.bar(features, counts, color=['#ff9999','#66b3ff','#99ff99'])
    
    # Add value labels
    for bar in bars:
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height + 0.05 * max(counts),
               f"{height:,}", ha='center', va='bottom', fontsize=10)
    
    # Customize plot
    ax.set_xlabel('Feature', fontsize=14)
    ax.set_ylabel('Count', fontsize=14)
    ax.set_title('RL-Aggressive GC Feature Counts', fontsize=16)
    
    # Add grid
    ax.grid(True, linestyle='--', alpha=0.7, axis='y')
    
    # Format y-axis with commas
    ax.yaxis.set_major_formatter(FuncFormatter(lambda x, p: f"{x:,.0f}"))
    
    plt.tight_layout()
    plt.savefig(os.path.join(RESULTS_DIR, 'rl_aggressive_features_bar.png'), dpi=300)
    plt.close()
    
    return counts

def generate_summary_table(summaries, norm_latencies, norm_erases):
    """Generate summary table of all metrics"""
    print("Generating summary table...")
    
    # Create DataFrame for summary table
    policies = list(summaries.keys())
    
    # Prepare data for table
    data = {
        'Policy': policies,
        'GC Invocations': [summaries[p]['gc_invocations'] for p in policies],
        'Pages Copied': [summaries[p].get('total_pages_copied', 0) for p in policies],
        'Avg Pages/GC': [summaries[p].get('avg_pages_per_gc', 0) for p in policies],
        'Block Erases': [summaries[p]['block_erases'] for p in policies],
        'Norm. Erase Count': norm_erases,
        'Avg Response Time (ns)': [summaries[p]['avg_response_time'] for p in policies],
        'P99 Latency (ns)': [summaries[p]['p99_latency'] for p in policies],
        'P99.9 Latency (ns)': [summaries[p]['p99.9_latency'] for p in policies],
        'P99.99 Latency (ns)': [summaries[p]['p99.99_latency'] for p in policies],
        'Norm. Avg RT': norm_latencies['norm_avg_response_time'],
        'Norm. P99': norm_latencies['norm_p99_latency'],
        'Norm. P99.9': norm_latencies['norm_p99_9_latency'],
        'Norm. P99.99': norm_latencies['norm_p99_99_latency']
    }
    
    # Add RL-specific metrics if available
    for p in policies:
        if 'avg_reward' in summaries[p]:
            if 'Avg Reward' not in data:
                data['Avg Reward'] = [None] * len(policies)
            idx = policies.index(p)
            data['Avg Reward'][idx] = summaries[p]['avg_reward']
    
    # Calculate percentage improvements
    # 1. Compared to LazyRTGC
    lazy_rtgc_idx = policies.index('LazyRTGC')
    
    # P99 latency improvements vs LazyRTGC
    p99_lazy = [((summaries['LazyRTGC']['p99_latency'] - summaries[p]['p99_latency']) / 
                summaries['LazyRTGC']['p99_latency']) * 100 for p in policies]
    data['P99 Improve vs LazyRTGC (%)'] = p99_lazy
    
    # P99.9 latency improvements vs LazyRTGC
    p999_lazy = [((summaries['LazyRTGC']['p99.9_latency'] - summaries[p]['p99.9_latency']) / 
                 summaries['LazyRTGC']['p99.9_latency']) * 100 for p in policies]
    data['P99.9 Improve vs LazyRTGC (%)'] = p999_lazy
    
    # P99.99 latency improvements vs LazyRTGC
    p9999_lazy = [((summaries['LazyRTGC']['p99.99_latency'] - summaries[p]['p99.99_latency']) / 
                  summaries['LazyRTGC']['p99.99_latency']) * 100 for p in policies]
    data['P99.99 Improve vs LazyRTGC (%)'] = p9999_lazy
    
    # Block erase improvements vs LazyRTGC
    erase_lazy = [((summaries['LazyRTGC']['block_erases'] - summaries[p]['block_erases']) / 
                  summaries['LazyRTGC']['block_erases']) * 100 for p in policies]
    data['Erase Reduction vs LazyRTGC (%)'] = erase_lazy
    
    # Avg response time improvements vs LazyRTGC
    avg_rt_lazy = [((summaries['LazyRTGC']['avg_response_time'] - summaries[p]['avg_response_time']) / 
                  summaries['LazyRTGC']['avg_response_time']) * 100 for p in policies]
    data['Avg RT Improve vs LazyRTGC (%)'] = avg_rt_lazy
    
    # 2. Compared to Default Page-Level
    default_idx = policies.index('Default Page-Level')
    
    # P99 latency improvements vs Default
    p99_default = [((summaries['Default Page-Level']['p99_latency'] - summaries[p]['p99_latency']) / 
                    summaries['Default Page-Level']['p99_latency']) * 100 for p in policies]
    data['P99 Improve vs Default (%)'] = p99_default
    
    # P99.9 latency improvements vs Default
    p999_default = [((summaries['Default Page-Level']['p99.9_latency'] - summaries[p]['p99.9_latency']) / 
                    summaries['Default Page-Level']['p99.9_latency']) * 100 for p in policies]
    data['P99.9 Improve vs Default (%)'] = p999_default
    
    # P99.99 latency improvements vs Default
    p9999_default = [((summaries['Default Page-Level']['p99.99_latency'] - summaries[p]['p99.99_latency']) / 
                     summaries['Default Page-Level']['p99.99_latency']) * 100 for p in policies]
    data['P99.99 Improve vs Default (%)'] = p9999_default
    
    # Block erase improvements vs Default
    erase_default = [((summaries['Default Page-Level']['block_erases'] - summaries[p]['block_erases']) / 
                     summaries['Default Page-Level']['block_erases']) * 100 for p in policies]
    data['Erase Reduction vs Default (%)'] = erase_default
    
    # Avg response time improvements vs Default
    avg_rt_default = [((summaries['Default Page-Level']['avg_response_time'] - summaries[p]['avg_response_time']) / 
                     summaries['Default Page-Level']['avg_response_time']) * 100 for p in policies]
    data['Avg RT Improve vs Default (%)'] = avg_rt_default
    
    # Create DataFrame
    df = pd.DataFrame(data)
    
    # Format columns
    for col in ['Avg Response Time (ns)', 'P99 Latency (ns)', 'P99.9 Latency (ns)', 'P99.99 Latency (ns)']:
        df[col] = df[col].apply(lambda x: f"{x:,.2f}")
    
    for col in ['Norm. Avg RT', 'Norm. P99', 'Norm. P99.9', 'Norm. P99.99', 'Norm. Erase Count']:
        df[col] = df[col].apply(lambda x: f"{x:.2f}")
    
    for col in ['P99 Improve vs LazyRTGC (%)', 'P99.9 Improve vs LazyRTGC (%)', 'P99.99 Improve vs LazyRTGC (%)', 
                'Erase Reduction vs LazyRTGC (%)', 'Avg RT Improve vs LazyRTGC (%)',
                'P99 Improve vs Default (%)', 'P99.9 Improve vs Default (%)', 'P99.99 Improve vs Default (%)', 
                'Erase Reduction vs Default (%)', 'Avg RT Improve vs Default (%)']:
        df[col] = df[col].apply(lambda x: f"{x:.2f}")
    
    if 'Avg Reward' in df:
        df['Avg Reward'] = df['Avg Reward'].apply(lambda x: f"{x:.4f}" if x is not None else "nan")
    
    # Create a shorter summary table for the text file
    # Use subset of columns for better readability
    text_df = df[['Policy', 'GC Invocations', 'Pages Copied', 'Avg Pages/GC', 
                 'Block Erases', 'Norm. Erase Count', 'Avg Response Time (ns)',
                 'P99 Latency (ns)', 'P99.9 Latency (ns)', 'P99.99 Latency (ns)',
                 'Norm. Avg RT', 'Norm. P99', 'Norm. P99.9', 'Norm. P99.99']].copy()
    
    # Fix the warning by using .loc to add the Avg Reward column
    if 'Avg Reward' in df.columns:
        text_df.loc[:, 'Avg Reward'] = df['Avg Reward'].values
    
    # Save to CSV (full details)
    df.to_csv(os.path.join(RESULTS_DIR, 'gc_policies_summary.csv'), index=False)
    
    # Save formatted version for report (shorter version)
    with open(os.path.join(RESULTS_DIR, 'gc_policies_summary.txt'), 'w') as f:
        f.write("# GC Policies Comparison Summary\n\n")
        f.write(text_df.to_string(index=False))
        f.write("\n\n")
        f.write("Notes:\n")
        f.write("- Normalized metrics are relative to LazyRTGC\n")
        f.write("- Lower values for latency metrics are better\n")
        f.write("- The RL-Aggressive policy aims to reduce long-tail latency through early GC and read-triggered GC\n")
    
    # Create a separate file with organized improvement metrics by category
    with open(os.path.join(RESULTS_DIR, 'gc_policies_improvements.txt'), 'w') as f:
        f.write("# GC Policies Percentage Improvements\n\n")
        f.write("## Percentage Improvements (positive values = improvement)\n\n")
        
        # Tail Latency Improvements vs LazyRTGC
        f.write("### Tail Latency Improvements vs LazyRTGC (%)\n")
        f.write("Higher values = better performance\n\n")
        
        latency_lazy_df = df[['Policy', 'P99 Improve vs LazyRTGC (%)', 
                              'P99.9 Improve vs LazyRTGC (%)', 
                              'P99.99 Improve vs LazyRTGC (%)']].copy()
        f.write(latency_lazy_df.to_string(index=False))
        f.write("\n\n")
        
        # Tail Latency Improvements vs Default
        f.write("### Tail Latency Improvements vs Default Page-Level (%)\n")
        f.write("Higher values = better performance\n\n")
        
        latency_default_df = df[['Policy', 'P99 Improve vs Default (%)', 
                                 'P99.9 Improve vs Default (%)', 
                                 'P99.99 Improve vs Default (%)']].copy()
        f.write(latency_default_df.to_string(index=False))
        f.write("\n\n")
        
        # Average Response Time Improvements
        f.write("### Average Response Time Improvements (%)\n")
        f.write("Higher values = better performance\n\n")
        
        avg_rt_df = df[['Policy', 'Avg RT Improve vs LazyRTGC (%)', 
                        'Avg RT Improve vs Default (%)']].copy()
        f.write(avg_rt_df.to_string(index=False))
        f.write("\n\n")
        
        # Erase Reduction Improvements
        f.write("### Block Erase Reduction (%)\n")
        f.write("Higher values = better endurance\n\n")
        
        erase_df = df[['Policy', 'Erase Reduction vs LazyRTGC (%)', 
                       'Erase Reduction vs Default (%)']].copy()
        f.write(erase_df.to_string(index=False))
        f.write("\n\n")
        
        # Summary of Best Policy per Metric
        f.write("### Summary of Best Performing Policies per Metric\n\n")
        
        best_p99 = df.iloc[df['P99 Improve vs LazyRTGC (%)'].astype(float).idxmax()]
        best_p999 = df.iloc[df['P99.9 Improve vs LazyRTGC (%)'].astype(float).idxmax()]
        best_p9999 = df.iloc[df['P99.99 Improve vs LazyRTGC (%)'].astype(float).idxmax()]
        best_erase = df.iloc[df['Erase Reduction vs LazyRTGC (%)'].astype(float).idxmax()]
        best_avg_rt = df.iloc[df['Avg RT Improve vs LazyRTGC (%)'].astype(float).idxmax()]
        
        f.write(f"- Best P99 Latency: {best_p99['Policy']} with {best_p99['P99 Improve vs LazyRTGC (%)']}% improvement vs LazyRTGC\n")
        f.write(f"- Best P99.9 Latency: {best_p999['Policy']} with {best_p999['P99.9 Improve vs LazyRTGC (%)']}% improvement vs LazyRTGC\n")
        f.write(f"- Best P99.99 Latency: {best_p9999['Policy']} with {best_p9999['P99.99 Improve vs LazyRTGC (%)']}% improvement vs LazyRTGC\n")
        f.write(f"- Best Erase Reduction: {best_erase['Policy']} with {best_erase['Erase Reduction vs LazyRTGC (%)']}% reduction vs LazyRTGC\n")
        f.write(f"- Best Average Response Time: {best_avg_rt['Policy']} with {best_avg_rt['Avg RT Improve vs LazyRTGC (%)']}% improvement vs LazyRTGC\n")
        f.write("\n")
        
        f.write("Notes:\n")
        f.write("- Positive percentages indicate improvement (reduction) in latency or erase counts\n")
        f.write("- Negative percentages indicate worse performance compared to the baseline\n")
        f.write("- For latency metrics, higher percentage = better performance\n")
        f.write("- For erase counts, higher percentage = better endurance\n")
    
    return df

def main():
    """Main function to run the analysis"""
    print("Starting GC metrics analysis...")
    
    # Load summary data
    summaries = load_all_summaries()
    
    # Generate comparisons
    norm_latencies = compare_latency_metrics(summaries)
    norm_erases = compare_erase_counts(summaries)
    compare_gc_efficiency(summaries)
    
    # Analyze RL-Aggressive specific features
    if 'RL-Aggressive' in summaries:
        analyze_rl_aggressive_features(summaries)
    
    # Generate summary table
    summary_df = generate_summary_table(summaries, norm_latencies, norm_erases)
    
    print(f"Analysis complete! Results saved to {RESULTS_DIR} directory.")
    print(f"Summary table saved as {os.path.join(RESULTS_DIR, 'gc_policies_summary.txt')}")

if __name__ == "__main__":
    main() 