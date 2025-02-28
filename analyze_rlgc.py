#!/usr/bin/env python3
import re
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict
import os
import argparse

def parse_args():
    parser = argparse.ArgumentParser(description='Analyze RL-GC debug logs')
    parser.add_argument('--rlgc_log', type=str, default='output/rl_gc_debug.log',
                        help='Path to RL-GC debug log')
    parser.add_argument('--ftl_log', type=str, default='output/ftl_debug.log',
                        help='Path to FTL debug log')
    parser.add_argument('--general_log', type=str, default='output/log.txt',
                        help='Path to general log')
    parser.add_argument('--output_dir', type=str, default='analysis_results',
                        help='Directory to save analysis results')
    return parser.parse_args()

def extract_response_times(log_file):
    """Extract response times from the RL-GC debug log"""
    response_times = []
    with open(log_file, 'r') as f:
        for line in f:
            if "[RL-GC RESPONSE] Recorded response time:" in line:
                match = re.search(r'Recorded response time: (\d+)ns', line)
                if match:
                    response_times.append(int(match.group(1)))
    return response_times

def analyze_tail_latency(response_times):
    """Calculate tail latency metrics"""
    if not response_times:
        return None
    
    percentiles = [50, 70, 90, 95, 99, 99.9, 99.99, 99.999, 99.9999]
    results = {}
    
    for p in percentiles:
        results[p] = np.percentile(response_times, p)
    
    return results

def extract_gc_triggers(log_file):
    """Extract GC trigger events and free block counts"""
    gc_triggers = []
    try:
        with open(log_file, 'r') as f:
            for line in f:
                if "[RL-GC DECISION] Triggering GC with state:" in line:
                    # Extract state information including free blocks from the same line
                    state_match = re.search(r'prevInterval=(\d+), currInterval=(\d+), prevAction=(\d+), freeBlocks=(\d+)', line)
                    if state_match:
                        gc_triggers.append({
                            'prev_interval': int(state_match.group(1)),
                            'curr_interval': int(state_match.group(2)),
                            'prev_action': int(state_match.group(3)),
                            'free_blocks': int(state_match.group(4))
                        })
    except Exception as e:
        print(f"Error extracting GC triggers: {e}")
    
    return gc_triggers

def extract_q_updates(log_file):
    """Extract Q-value updates to track learning progress"""
    q_updates = []
    with open(log_file, 'r') as f:
        content = f.readlines()
        
    i = 0
    while i < len(content):
        line = content[i]
        if "[RL-DEBUG] Q-UPDATE:" in line:
            # Extract state, action, reward, and new Q-value
            match = re.search(r'State\((\d+),(\d+),(\d+)\), Action=(\d+), Reward=([\d.-]+) \| Q-value: ([\d.-]+) -> ([\d.-]+)', line)
            if match:
                q_updates.append({
                    'state': (int(match.group(1)), int(match.group(2)), int(match.group(3))),
                    'action': int(match.group(4)),
                    'reward': float(match.group(5)),
                    'old_q_value': float(match.group(6)),
                    'new_q_value': float(match.group(7))
                })
        i += 1
    return q_updates

def extract_epsilon_decay(log_file):
    """Extract epsilon values to track exploration-exploitation balance"""
    epsilon_values = []
    initial_epsilon = 0.8  # Default from config
    
    # First, get the initial epsilon from the header
    with open(log_file, 'r') as f:
        for line in f:
            if "Q-table summary: epsilon=" in line:
                match = re.search(r'epsilon=([\d.]+)', line)
                if match:
                    initial_epsilon = float(match.group(1))
                    epsilon_values.append(initial_epsilon)
                    break
    
    # Then get the decayed epsilon values
    with open(log_file, 'r') as f:
        for line in f:
            if "[RL-DEBUG] Epsilon decayed to" in line:
                match = re.search(r'Epsilon decayed to ([\d.]+)', line)
                if match:
                    epsilon_values.append(float(match.group(1)))
    
    return epsilon_values

def extract_general_stats(log_file):
    """Extract general statistics from log.txt"""
    stats = {}
    try:
        with open(log_file, 'r') as f:
            lines = f.readlines()
            
        # Process the last section of the log file to get the final statistics
        # We'll look at the last 250 lines which should contain the final statistics
        last_lines = lines[-250:] if len(lines) > 250 else lines
        
        for line in last_lines:
            # Extract read/write request counts
            if "read.request_count" in line and not "icl" in line and not "dram" in line:
                match = re.search(r'read\.request_count\s+(\d+\.\d+)', line)
                if match:
                    stats['read_requests'] = float(match.group(1))
            
            elif "write.request_count" in line and not "icl" in line and not "dram" in line:
                match = re.search(r'write\.request_count\s+(\d+\.\d+)', line)
                if match:
                    stats['write_requests'] = float(match.group(1))
            
            elif "request_count" in line and not "read" in line and not "write" in line and not "icl" in line and not "dram" in line:
                match = re.search(r'request_count\s+(\d+\.\d+)', line)
                if match:
                    stats['total_requests'] = float(match.group(1))
            
            # Extract busy times
            elif "read.busy" in line and not "icl" in line and not "dram" in line:
                match = re.search(r'read\.busy\s+(\d+\.\d+)', line)
                if match:
                    stats['read_busy'] = float(match.group(1))
            
            elif "write.busy" in line and not "icl" in line and not "dram" in line:
                match = re.search(r'write\.busy\s+(\d+\.\d+)', line)
                if match:
                    stats['write_busy'] = float(match.group(1))
            
            # Extract GC statistics
            elif "ftl.page_mapping.gc.count" in line:
                match = re.search(r'ftl\.page_mapping\.gc\.count\s+(\d+\.\d+)', line)
                if match:
                    stats['gc_count'] = float(match.group(1))
            
            elif "ftl.page_mapping.gc.page_copies" in line:
                match = re.search(r'ftl\.page_mapping\.gc\.page_copies\s+(\d+\.\d+)', line)
                if match:
                    stats['gc_page_copies'] = float(match.group(1))
            
            elif "ftl.page_mapping.gc.reclaimed_blocks" in line:
                match = re.search(r'ftl\.page_mapping\.gc\.reclaimed_blocks\s+(\d+\.\d+)', line)
                if match:
                    stats['gc_reclaimed_blocks'] = float(match.group(1))
            
            # Extract RL-GC statistics
            elif "ftl.ftl.rlgc.gc_invocations" in line:
                match = re.search(r'ftl\.ftl\.rlgc\.gc_invocations\s+(\d+\.\d+)', line)
                if match:
                    stats['rlgc_invocations'] = float(match.group(1))
            
            elif "ftl.ftl.rlgc.page_copies" in line:
                match = re.search(r'ftl\.ftl\.rlgc\.page_copies\s+(\d+\.\d+)', line)
                if match:
                    stats['rlgc_page_copies'] = float(match.group(1))
            
            elif "ftl.ftl.rlgc.intensive_gc" in line:
                match = re.search(r'ftl\.ftl\.rlgc\.intensive_gc\s+(\d+\.\d+)', line)
                if match:
                    stats['rlgc_intensive_gc'] = float(match.group(1))
            
            elif "ftl.ftl.rlgc.avg_reward" in line:
                match = re.search(r'ftl\.ftl\.rlgc\.avg_reward\s+(\d+\.\d+)', line)
                if match:
                    stats['rlgc_avg_reward'] = float(match.group(1))
            
            # Extract erase count
            elif "pal.erase.count" in line:
                match = re.search(r'pal\.erase\.count\s+(\d+\.\d+)', line)
                if match:
                    stats['erase_count'] = float(match.group(1))
    
    except Exception as e:
        print(f"Error extracting general stats: {e}")
    
    return stats

def extract_rlgc_config(log_file):
    """Extract RL-GC configuration from the debug log"""
    config = {}
    with open(log_file, 'r') as f:
        for line in f:
            if "Free blocks thresholds:" in line:
                match = re.search(r'tgc=(\d+), tigc=(\d+)', line)
                if match:
                    config['tgc_threshold'] = int(match.group(1))
                    config['tigc_threshold'] = int(match.group(2))
            elif "Q-table summary: epsilon=" in line:
                match = re.search(r'epsilon=([\d.]+)', line)
                if match:
                    config['initial_epsilon'] = float(match.group(1))
    return config

def plot_response_time_distribution(response_times, output_dir):
    """Plot the distribution of response times"""
    if not response_times:
        return
    
    plt.figure(figsize=(10, 6))
    plt.hist(response_times, bins=50, alpha=0.7)
    plt.title('Response Time Distribution')
    plt.xlabel('Response Time (ns)')
    plt.ylabel('Frequency')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'response_time_distribution.png'))
    
    # Plot log scale for better visualization of tail
    plt.figure(figsize=(10, 6))
    plt.hist(response_times, bins=50, alpha=0.7, log=True)
    plt.title('Response Time Distribution (Log Scale)')
    plt.xlabel('Response Time (ns)')
    plt.ylabel('Frequency (Log Scale)')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'response_time_distribution_log.png'))

def plot_free_blocks_at_gc(gc_triggers, output_dir):
    """Plot the free blocks at GC trigger points"""
    if not gc_triggers:
        return
    
    free_blocks = [trigger['free_blocks'] for trigger in gc_triggers]
    
    plt.figure(figsize=(10, 6))
    plt.plot(range(len(free_blocks)), free_blocks, marker='o', alpha=0.7)
    plt.title('Free Blocks at GC Trigger Points')
    plt.xlabel('GC Trigger Event')
    plt.ylabel('Number of Free Blocks')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'free_blocks_at_gc.png'))
    
    # Plot histogram of free blocks
    plt.figure(figsize=(10, 6))
    plt.hist(free_blocks, bins=range(min(free_blocks), max(free_blocks) + 2), alpha=0.7)
    plt.title('Distribution of Free Blocks at GC Trigger')
    plt.xlabel('Number of Free Blocks')
    plt.ylabel('Frequency')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'free_blocks_histogram.png'))

def plot_q_value_evolution(q_updates, output_dir):
    """Plot the evolution of Q-values over time"""
    if not q_updates:
        return
    
    # Group Q-values by state-action pairs
    q_values_by_sa = defaultdict(list)
    for update in q_updates:
        state_action = (update['state'], update['action'])
        q_values_by_sa[state_action].append(update['new_q_value'])
    
    # Plot for the top 5 most updated state-action pairs
    top_sa_pairs = sorted(q_values_by_sa.items(), key=lambda x: len(x[1]), reverse=True)[:5]
    
    plt.figure(figsize=(12, 8))
    for (state, action), q_values in top_sa_pairs:
        label = f"State {state}, Action {action}"
        plt.plot(range(len(q_values)), q_values, label=label, alpha=0.7)
    
    plt.title('Q-Value Evolution for Top State-Action Pairs')
    plt.xlabel('Update Count')
    plt.ylabel('Q-Value')
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'q_value_evolution.png'))

def plot_epsilon_decay(epsilon_values, output_dir):
    """Plot the epsilon decay over time"""
    if not epsilon_values:
        return
    
    plt.figure(figsize=(10, 6))
    plt.plot(range(len(epsilon_values)), epsilon_values, marker='o', alpha=0.7)
    plt.title('Epsilon Decay Over Time')
    plt.xlabel('Update Count')
    plt.ylabel('Epsilon Value')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'epsilon_decay.png'))

def plot_reward_distribution(q_updates, output_dir):
    """Plot the distribution of rewards"""
    if not q_updates:
        return
    
    rewards = [update['reward'] for update in q_updates]
    
    plt.figure(figsize=(10, 6))
    plt.hist(rewards, bins=20, alpha=0.7)
    plt.title('Reward Distribution')
    plt.xlabel('Reward Value')
    plt.ylabel('Frequency')
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, 'reward_distribution.png'))

def main():
    args = parse_args()
    
    # Create output directory if it doesn't exist
    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)
    
    # Extract data from logs
    print("Extracting response times...")
    response_times = extract_response_times(args.rlgc_log)
    
    print("Extracting GC triggers...")
    gc_triggers = extract_gc_triggers(args.rlgc_log)
    
    print("Extracting Q-updates...")
    q_updates = extract_q_updates(args.rlgc_log)
    
    print("Extracting epsilon decay...")
    epsilon_values = extract_epsilon_decay(args.rlgc_log)
    
    print("Extracting general statistics...")
    general_stats = extract_general_stats(args.general_log)
    
    print("Extracting RL-GC configuration...")
    rlgc_config = extract_rlgc_config(args.rlgc_log)
    
    # Analyze data
    print("Analyzing tail latency...")
    tail_latency = analyze_tail_latency(response_times)
    
    # Generate plots
    print("Generating plots...")
    plot_response_time_distribution(response_times, args.output_dir)
    plot_free_blocks_at_gc(gc_triggers, args.output_dir)
    plot_q_value_evolution(q_updates, args.output_dir)
    plot_epsilon_decay(epsilon_values, args.output_dir)
    plot_reward_distribution(q_updates, args.output_dir)
    
    # Save analysis results to text file
    with open(os.path.join(args.output_dir, 'analysis_results.txt'), 'w') as f:
        f.write("=== RL-GC Analysis Results ===\n\n")
        
        # RL-GC Configuration
        f.write("=== RL-GC Configuration ===\n")
        if rlgc_config:
            for key, value in sorted(rlgc_config.items()):
                f.write(f"{key}: {value}\n")
            f.write("\n")
        else:
            f.write("No RL-GC configuration found.\n\n")
        
        # Response time statistics
        f.write("=== Response Time Statistics ===\n")
        if response_times:
            f.write(f"Total response times recorded: {len(response_times)}\n")
            f.write(f"Min response time: {min(response_times)} ns\n")
            f.write(f"Max response time: {max(response_times)} ns\n")
            f.write(f"Mean response time: {np.mean(response_times):.2f} ns\n")
            f.write(f"Median response time: {np.median(response_times):.2f} ns\n\n")
            
            f.write("=== Tail Latency ===\n")
            for percentile, value in sorted(tail_latency.items()):
                f.write(f"{percentile}th percentile: {value:.2f} ns\n")
            f.write("\n")
        else:
            f.write("No response time data found.\n\n")
        
        # GC trigger statistics
        f.write("=== GC Trigger Statistics ===\n")
        if gc_triggers:
            free_blocks = [trigger['free_blocks'] for trigger in gc_triggers]
            f.write(f"Total GC triggers: {len(gc_triggers)}\n")
            f.write(f"Min free blocks at trigger: {min(free_blocks)}\n")
            f.write(f"Max free blocks at trigger: {max(free_blocks)}\n")
            f.write(f"Mean free blocks at trigger: {np.mean(free_blocks):.2f}\n")
            
            # Count triggers by free block count
            free_block_counts = {}
            for fb in free_blocks:
                free_block_counts[fb] = free_block_counts.get(fb, 0) + 1
            
            f.write("\nGC triggers by free block count:\n")
            for fb, count in sorted(free_block_counts.items()):
                f.write(f"  {fb} free blocks: {count} triggers ({count/len(gc_triggers)*100:.2f}%)\n")
            f.write("\n")
        else:
            f.write("No GC trigger data found.\n\n")
        
        # General statistics
        f.write("=== General Statistics ===\n")
        if general_stats:
            for key, value in sorted(general_stats.items()):
                f.write(f"{key}: {value}\n")
            
            # Calculate derived metrics
            if 'read_requests' in general_stats and 'read_busy' in general_stats and general_stats['read_requests'] > 0:
                avg_read_latency = general_stats['read_busy'] / general_stats['read_requests']
                f.write(f"Average read latency: {avg_read_latency:.2f} ns\n")
            
            if 'write_requests' in general_stats and 'write_busy' in general_stats and general_stats['write_requests'] > 0:
                avg_write_latency = general_stats['write_busy'] / general_stats['write_requests']
                f.write(f"Average write latency: {avg_write_latency:.2f} ns\n")
            
            if 'gc_page_copies' in general_stats and 'gc_count' in general_stats and general_stats['gc_count'] > 0:
                avg_pages_per_gc = general_stats['gc_page_copies'] / general_stats['gc_count']
                f.write(f"Average pages copied per GC: {avg_pages_per_gc:.2f}\n")
            
            if 'rlgc_page_copies' in general_stats and 'rlgc_invocations' in general_stats and general_stats['rlgc_invocations'] > 0:
                avg_rlgc_pages_per_gc = general_stats['rlgc_page_copies'] / general_stats['rlgc_invocations']
                f.write(f"Average pages copied per RL-GC: {avg_rlgc_pages_per_gc:.2f}\n")
            
            if 'gc_reclaimed_blocks' in general_stats and 'gc_count' in general_stats and general_stats['gc_count'] > 0:
                avg_blocks_per_gc = general_stats['gc_reclaimed_blocks'] / general_stats['gc_count']
                f.write(f"Average blocks reclaimed per GC: {avg_blocks_per_gc:.2f}\n")
                
            if 'gc_page_copies' in general_stats and 'rlgc_page_copies' in general_stats:
                page_copy_diff = general_stats['rlgc_page_copies'] - general_stats['gc_page_copies']
                f.write(f"Additional page copies by RL-GC: {page_copy_diff:.2f}\n")
                
            if 'gc_count' in general_stats and 'rlgc_invocations' in general_stats:
                gc_invocation_diff = general_stats['rlgc_invocations'] - general_stats['gc_count']
                f.write(f"Additional GC invocations by RL-GC: {gc_invocation_diff:.2f}\n")
            f.write("\n")
        else:
            f.write("No general statistics found.\n\n")
        
        # Q-learning statistics
        f.write("=== Q-Learning Statistics ===\n")
        if q_updates:
            rewards = [update['reward'] for update in q_updates]
            f.write(f"Total Q-updates: {len(q_updates)}\n")
            f.write(f"Min reward: {min(rewards)}\n")
            f.write(f"Max reward: {max(rewards)}\n")
            f.write(f"Mean reward: {np.mean(rewards):.6f}\n")
            
            # Count rewards by value
            reward_counts = {}
            for r in rewards:
                reward_counts[r] = reward_counts.get(r, 0) + 1
            
            f.write("\nRewards distribution:\n")
            for r, count in sorted(reward_counts.items()):
                f.write(f"  Reward {r}: {count} occurrences ({count/len(rewards)*100:.2f}%)\n")
            f.write("\n")
        else:
            f.write("No Q-update data found.\n\n")
        
        # Epsilon decay statistics
        f.write("=== Epsilon Decay Statistics ===\n")
        if epsilon_values:
            f.write(f"Initial epsilon: {epsilon_values[0]}\n")
            f.write(f"Final epsilon: {epsilon_values[-1]}\n")
            if len(epsilon_values) > 1:
                f.write(f"Epsilon decay rate: {(epsilon_values[0] - epsilon_values[-1]) / (len(epsilon_values) - 1):.6f} per update\n")
            f.write("\n")
        else:
            f.write("No epsilon decay data found.\n\n")
    
    print(f"Analysis complete! Results saved to {args.output_dir}/")

if __name__ == "__main__":
    main() 