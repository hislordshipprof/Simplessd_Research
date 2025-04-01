# RL-Aggressive Garbage Collection

This implementation is based on the paper "Reinforcement Learning-Assisted Garbage Collection for Solid-State Drives" and focuses on reducing long-tail latency in SSDs through aggressive garbage collection techniques.

## Key Features

- **Early GC Triggering**: Uses TAGC (Triggering Aggressive GC) threshold to proactively trigger GC before free blocks reach the critical threshold.
- **Max-Limited GC Operations**: Limits the maximum number of GC operations to 2 when between TAGC and TGC thresholds.
- **Read-Triggered GC**: Can trigger garbage collection on read operations to reduce tail latency for subsequent operations.
- **Intensive Mode**: Uses max page copies when in intensive mode for more aggressive space reclamation.
- **State-Based Decision Making**: Uses inter-request intervals and previous actions as state to determine optimal GC action.

## Implementation Details

### Thresholds
- **TGC**: Regular garbage collection threshold
- **TIGC**: Intensive garbage collection threshold
- **TAGC**: Early aggressive garbage collection threshold (default: 100)

### Parameters
- **maxGCOps**: Maximum number of GC operations when between TAGC and TGC thresholds (default: 2)
- **readTriggeredGC**: Whether to trigger GC on read operations
- **maxPageCopies**: Maximum number of pages to copy in a single GC operation

### Statistics Tracked
- GC invocations
- Page copies
- Intensive GC count
- Read-triggered GC count
- Early GC count
- Block erases
- Average reward

## Configuration in sample.cfg

Key configuration parameters for RL-Aggressive GC:

```
# RL-Aggressive GC specific parameters
RLAggressiveTAGCThreshold = 100    # Threshold for triggering aggressive early GC
RLAggressiveMaxGCOps = 2           # Maximum GC operations in early GC mode
RLAggressiveReadTriggeredGC = 1    # Enable read-triggered GC
RLAggressiveDebugEnable = 1        # Enable debug output
RLAggressiveMetricsEnable = 1      # Enable metrics collection
```

## References

The implementation is based on the paper, which describes the following key mechanisms:

1. Early GC triggering with threshold TAGC (100 free blocks)
2. Limited maximum GC operations (2) when in early GC mode
3. Read-triggered GC to prevent read requests from experiencing long tail latency
4. Reinforcement learning to optimize garbage collection decisions based on system state

This implementation fully follows the paper's recommendations for aggressive garbage collection to minimize tail latency. 