

## Reinforcement Learning-Assisted Garbage Collection to Mitigate Long-Tail Latency in SSD

WONKYUNG KANG, Seoul National University DONGKUN SHIN, Sungkyunkwan University SUNGJOO YOO, Seoul National University

NAND flash memory is widely used in various systems; ranging from real-time embedded systems to enter server systems. Because the flash memory has erase-before-write characteristics; we need flash-memory management methods; ie., address translation and garbage collection In particular; garbage collection (GC) incurs long-tail latency; e.g- 100 times higher latency than the average latency at the 99th percentile. Thus;, real-time and quality-critical systems fail to meet the given requirements such as deadline and QoS constraints. In this study, we propose a novel method of GC based on reinforcement learning: The objective is to reduce the long-tail latency by exploiting the idle time in the storage system: To improve the efficiency of the reinforcement learning-assisted GC scheme; we present new optimization methods that exploit finegrained GC to further reduce the long-tail latency: The experimental results with real workloads show that our technique significantly reduces the long-tail latency by 29-36% at the 99.99th percentile compared to state-of-the-art schemes. prise


## INTRODUCTION

Flash memory storages are widely used in embedded systems; and consumer and enterprise-server systems. Flash memory has two principal issues: (1) erase-before-write (write once) property; and (2) endurance problem:. To address the erase-before-write property; a flash-translation (FTL) layer

This article was presented in the International Conference on Compilers; Architecture; and Synthesis for Embedded Sys tems (CASES) 2017 and appears as part of the ESWEEK-TECS special issue

is page-level mapping [11] is widely used to reduce the write latency induced by write-once and bulk-erase properties of flash memory storages. In the page-level mapping; when writing new data, FTL assigns a new free page; and subsequently; writes data to the newly assigned free page. Thereafter; it updates the address-mapping information between the logical and the physical addresses. If the free blocks are insufficient; are obtained by reclaiming the unused space in the used blocks. To do that; the valid pages of the victim block are copied to a new block. The victim block is then erased to obtain a free block. This procedure is called garbage collection (GC). The GC induces a long-latency problem because the page-copy and block-erase operations are time consuming: being they tail is observed in the distribution of the write latency because of the GC. For instance; the latency at the 99th percentile can be 10Ox higher than the average latency [22]. Such a long-tail latency causes a significant problem in real-time embedded and enterprise-server systems which need to meet the real-time and quality of service (QoS) requirements. long

GC latency increases as the capacity of flash memory increases. It is mainly due to the fact that the block size (number of pages per block) increases as the capacity of Flash memory increases. GC latency is determined by the time for valid page copy and block erase. Thus; as block size gets increased, GC latency also increases. According to our analysis; the block size has a strong impact on tail latency. Especially; the block size gets increased from 2D to 3D NAND flash memory; e.g, 256 pages/block in 2D planner NAND flash memory [9] and 768 pages/block in 3D NAND flash memory [8]. Even in 3D NAND flash memory; the block size is expected to continue to increase write latency problem incurred by GC can become more serious in 3D NAND flash memory-based storage: Note that the write latency due to GC can increase not only write latency but also read latency since GC can stall the service of subsequent read requests. long long long

In this study, we propose reinforcement learning-assisted GC technique to reduce the tail latency. The proposed technique is a new approach to exploit the idle time in the storage with reinforcement learning: long

The contributions of this study are as follows.

- To the best of the authors' knowledge; this is the first approach of reinforcement learning assisted idle time-aware GC.
- We also present an optimization scheme that aggressively performs fine-grained GC to pre pare free blocks in advance; thereby reducing the blockage due to the GC, which signifi cantly reduces the long-tail latency.
- The proposed reinforcement learning-assisted solution helps determine the number of GC operations to be executed to exploit the varying idle time while avoiding the long-tail la tency due to the GC.

The rest of this paper is organized as follows. Section 2 reviews previous GC techniques. Section 3 explains the motivation behind our study and the problem discussed herein. Section 4 describes the background of flash-storage systems and reinforcement learning: Section 5 presents the proposed method. Section 6 gives the experimental results. Section 7 concludes this paper:

## 2 RELATED WORK

Several techniques have been proposed to improve the GC performance [1, 2, 5, 13-17]: Wei et al Oidentified workload characteristics per address range and assigned page or block-level mapping based on identifying the workload [13] Similarly; Jang et al. classified the data into three types such as hot; cold, and warm and allocated blocks such that a block is assigned to the same type of data, which improves the GC performance [14].

Studies have been conducted on approaches that utilize the idle time and workload prediction. Han et al. predicted the future workload and controlled the number of victim blocks [15] The victim blocks are selected based on the age; utilization; and erase counts. The number of reclaimed blocks is then determined by predicting the history ofthe request count and rate Lin et al:. predicted the future workload and obtained the number of victim blocks based on the predicted workload, erase count; and invalidation period [16]

Choudhuri et al: proposed GFTL, which helps perform GC to ensure fixed upper bounds in the latency of storage access by eliminating the source of non-determinism [5]. Qin et al. prodistributed partial GC policy in the RFTL, which tries to hide the -tail latency due to the GC. Periodically; the method helps perform GC and exploit buffer blocks to store the write data obtained during the GC operation; thereby reducing the GC-induced blockage [2]. partial posed long-t partial

Only a few studies have been conducted on real-time GC for flash-storage systems:. Chang et al. proposed free-page replenishment mechanism wherein the real-time tasks were prevented from being blocked due to insufficient number of free pages. Assuming the write behavior of a realtime task is known; the number of GC operations and the maximum quantum for GC operation are determined to meet the real-time constraints [17].

Zhang et al. proposed a GC method termed LazyRTGC. In this method, a page-level map ping is employed to fully utilize the flash memory space and postpone the GC as much as possible. To employ the idle time of the system; LazyRTGC schedules a partial GC after serving write requests [1]. However; fixed policy is employed to utilize the idle time. Thus; as demonstrated in our experiments, it does not consider the duration of the idle time determined by the dynamic behavior of the storage access thereby losing an opportunity to further exploit the idle time: lazy

Reinforcement learning has been widely used in a broad range of problems including robot con controller design based on reinforcement learning: This memory controller sees the state and predicts the long-term performance impact of each action it can perform: In this way; this controller learns to optimize its scheduling policy to offer maximum performance. In [26], Wang et al. proposed deriving near-optimal power management policy reinforcement learning and Bayesian classification. In [27], Peled et al: proposed context-based prefetcher using reinforce ment learning: system using

## 3 PROBLEM AND MOTIVATION

## 3.1 Long-Tail Problem in Flash Storage Access Latency

Figure 1 shows the latency comparison for a storage trace called home2 (used in our experiments) between an ideal storage without a GC overhead and real one with page-level mapping: The figure shows that the response time is short for the majority of the storage accesses. It is less than 1 ms for approximately 85% of the accesses. However; the latency difference between the median and the percentile is a factor of 100. As mentioned before; such a long-tail latency is a serious problem in real-time and quality-critical systems. For instance; the server storage typically needs to provide minimum 7.5 ms of write latency for 99.999 of the storage accesses [21]. Considering that the GC latency continues to increase due to the increasing block size; it is important to reduce the long-tail latency for such real-time and quality-critical systems. 99th

## 3.2 Idle Time in Flash Storage

Figure 2 shows the distribution of the request interval time for 6OK requests the real-world workloads used in our experiments. The x-axis represents the inter-request interval time; and the y-axis represents the frequency of the request in each bin. As the figure shows, the storage has system

Fig. 1. tail latency problem: trace home2. Long

![Figure 2](/home/epiclab/docling/extracted/RL-figure-10.png)

Fig: 2 Inter-request interval distribution.

![Figure 3](/home/epiclab/docling/extracted/RL-figure-2.png)

frequent and long idle periods. Such an idle time can be exploited to perform GC operations. In idle time-aware GC methods [15, 16] it is important to determine how many GC operations need to be performed for a given idle time: The difficulty of this problem is that the length of the current idle period is unknown To address this problem; several techniques exist [15, 16]. These techniques use fixed policies determined at the design time. Thus; are limited in adapting to the dynam ically changing storage access behavior because of the different program runs or phases. In this study, we propose an RL-assisted adaptive GC method, which learns the storage access behavior online and adjusts the GC to it to reduce the long-tail latency. they

## BACKGROUND

## 4.1 SSD Architecture and Garbage Collection

Solid-state drives (SSDs) are one of the flash-storage systems widely used in consumer and enterprise systems. Figure 3 shows the internal architecture of the SSD. It comprises flash-memory packages for data storage; a controller; and DRAM for the buffer. The controller is connected to the host interface SATA) and flash-memory packages:. To exploit the parallelism of mul tiple flash memories to maximize the performance; multiple flash-memory interface channels are (e.g,

Fig. 3. SSD internal architecture diagram.

![Figure 4](/home/epiclab/docling/extracted/RL-figure-3.png)

4 Garbage collection. Fig:

![Figure 5](/home/epiclab/docling/extracted/RL-figure-4.png)

employed in the flash controller: The DRAM stores the address mapping table and read/write data for caching and buffering [12].

Figure 4 illustrates the GC operation. GC is typically triggered if the number of free blocks is less than a certain threshold, e.g. 5% of the total number of blocks. To reduce the cost of page copy; block having the lowest number of valid pages is typically selected as a victim block. As shown in the figure; the valid pages (e.g- pages 1, 0, 4, 3, and 2) are read from the victim block and written to a free block; which is called the valid page copy. After copying all the valid pages, the victim block is erased to obtain a free block. The GC latency depends on the number of valid pages in the victim blocks; which is proportional to the block size; ie. the number of pages in a block. As the 3D NAND flash memory becomes more popular; the block size increases rapidly; thereby increasing the GC latency; which can make the GC-induced long-tail write-latency problem more severe in the 3D NAND flash memory. Note that the GC can increase the latency of the read access and that of the write access because the flash memory is blocked during the GC operation. Specifically; the plane under the valid page copy or block erase is blocked to subsequent accesses; which increases the read or write latency of the blocked plane. Note that a flash memory die contains two or four planes. A plane consists of a number of blocks. Each plane can be accessed independently. large

## 4.2 Reinforcement Learning

Figure 5 shows a simplified view of the reinforcement learning (RL). The agent (e.g- the GC scheduler in the SSD controller) has a set of actions. In our work; the actions are defined to be partial GC operations; e.g5 page copies or erase operation:  Thus, policy selects one of possible acstates. Based on the current state, the policy of the agent tries to maximize the immediate reward,

Fig. 5. Environment agent interaction

![Figure 6](/home/epiclab/docling/extracted/RL-figure-5.png)

e.g-, reduction in the tail latency; in selecting an action. After executing an action; the environment can enter a new state\_ long-t

Note that the reinforcement learning is an online method.  Thus; it applies exploitation taking an action suggested by the current policy) and exploration (e.g, trying an action different from the one the current policy suggests and updating the policy with the reward) during the runtime [10] (e.g,

The basic model of the reinforcement learning is given as follows.

State (S): a set of environment and agent states

Reward r): reward associated with last action

Action (A): a set of actions of agent

Policy (r): agent's way of action selection at a given time

As shown in Figure 5, the agent interacts with the environment in discrete time At time t, the agent receives observation 0, which includes the state St and reward rt. The agent's policy selects an action At from a set of actions and sends it to the environment. The environment changes its state to a next state and gives the agent reward rt+ 1 via the state transition (St,At, St+1) The of the agent's policy is to maximize the reward. steps. St + goal

For policy learning; we employ Q-learning [10], which manages the value functions of the stateaction and updates them based on the recent state-action and rewards. The value function of the state-action is defined as follows. pair pairs pair

To apply the reinforcement learning to our GC problem; we need to define four components: states; actions; reward, and policy; with respect to the storage In terms of the actions, we exploit a fine-€ 'grained partial GC method wherein an action involves performing a number of valid page copy operations or a single erase operation [17]. Thus; the only objective of the policy is to determine many valid copies to perform or whether to perform an erase operation: system how

<!-- formula-not-decoded -->

where time t. Q(s,a) is the expectation of the reward when action a is taken at state and time t

The policy is defined as follows.

As expressed in Equation (2) the policy chooses an action to maximize the Q value at state s.2 As mentioned earlier; the reinforcement learning is an online method.  Thus; the policy is modified for the dynamic storage access behavior. Hence, we use €-greedy technique [10]. In this method,

(St, At, St+1) represents a state transition from St to St+ initiated by action At.

are two types of policy; deterministic and stochastic ones. The policy in Equation (2) is deterministic since the action of the maximum Q value is chosen. A stochastic policy chooses an action in a probabilistic manner that the probability of choosing action a is proportional to Qs,a) In our experiments; we applied the stochastic policy: 2There

the agent, ie , the GC scheduler; performs exploitation and exploration. In most cases; the GC scheduler selects an action; e.g. two valid page copies; by exploiting the learned policy. At a low probability; e.g. 1% (corresponding to e), the agent explores a new possibility by taking a random action; which is different from the one selected by the policy. The policy is updated with the reward of this random choice in exploration or the one chosen by the policy in exploitation as follows.

<!-- formula-not-decoded -->

where r is the reward (i.e., given from the response time of the storage access) and a are the subsequent state and action (for the next storage access), respectively, œ is the step size; and y is the discount factor [10]3 . The basic concept of Equation (3) is that the Q value update is proportional to the difference between the target reward, ier + YQ(s',a'), and the old estimate of the reward, Q(s,a)

In Equation (3), Q values are reused from the existing ones (in the q-table) which is called bootstrapping; enabling a fast calculation of Q value updates. Thus; only reward r, which is measured by the GC scheduler; is newly needed to update the Q value in the q-table; which finally updates the policy because the policy is determined by the Q values. The exploration finally improves the policy by adapting to the characteristics of the given storage accesses.

When implementing the reinforcement learning; the data structure is q-table; which stores Q(s,a). The size (# of entries) of q-table is # states x # actions. The size needs to be small to reduce the memory overhead of the reinforcement learning-assisted solution. key

Given that a fine grained GC method and Q-learning with €-greedy technique are applied, our contribution is to define the states and rewards for the reinforcement learning-assisted GC. In Section 5, we describe how the states and rewards are defined and when to trigger the RL-assisted GC scheduler: key

## 5 PROPOSED METHODS

## 5.1 Solution Overview

We aim to reduce the long-tail latency by (1) hiding the GC latency by exploiting the idle time; and (2) minimizing the GC-induced blocking: In this section; we present an RL-assisted GC scheduler to hide the GC latency (Section 5.2) and an aggressive fine-grained partial GC scheme to reduce the blocking time (Section 5.3)

After serving the request, the GC scheduler calculates the response time. Because our is to reduce the long-tail latency, we need to reflect the response time in our reward We explain the details of how the reward is calculated the response time in Section 5.2. Note that the response time of the kth request gives the reward for the request. Thus, in the aforementioned Q-learning (Equation (3)), we update the Q value for the current state and action a only after the next request is served and the corresponding reward is calculated. goal using

Our proposed RL-assisted GC scheduler is triggered in a manner. Thus, only when an access request arrives at the storage and the number of free pages goes below threshold [1], it is triggered .  When triggered, it chooses an action. Because our GC method is based on the GC; the action is to perform a number of partial GC operations; e.g., five page copies from a victim block to free block. Thus; the GC scheduler chooses an action; determines how many partial GC operations will be performed after serving the current request. An erase operation is performed when an action is chosen by the scheduler and block is ready to be erased. In such case; instead of executing the action; the block is erased. lazy partial ie,

size &amp; and the discount factor Y are set to typical values; 0.3 and 0.8, respectively [10] 3Step

| ALGORITHM 1: RL-Assisted GC Scheduling                                                                | ALGORITHM 1: RL-Assisted GC Scheduling                                                                |
|-------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| Inputs: request, state t - 1 ( S t - 1 ) , state t ( S t ) , action t - 1 ( A t-1 Output: action (A ) | Inputs: request, state t - 1 ( S t - 1 ) , state t ( S t ) , action t - 1 ( A t-1 Output: action (A ) |
| 1: if T GC > = N free then                                                                            | 1: if T GC > = N free then                                                                            |
| 2:                                                                                                    | A t = e_greedy(interval t - 1 , interval t , action t - 1 )                                           |
| 3:                                                                                                    | if interval t == 0 then                                                                               |
| 4:                                                                                                    | go to line 1                                                                                          |
| 5:                                                                                                    | end if                                                                                                |
| 6:                                                                                                    | serve the request and obtain response time                                                            |
| 7:                                                                                                    | run partial_gc(A t )                                                                                  |
| 8:                                                                                                    | r = reward(response_time)                                                                             |
| 9:                                                                                                    | Q ( S t-1 , A t-1 ) = ( 1 - α ) Q ( S t-1 , A t-1 ) + α [r + γ Q(S t ,A t )]                          |
| 10: end if                                                                                            | 10: end if                                                                                            |

In Section 5.2, we explain the baseline RL-assisted GC scheduling: In Section 5.3, we present a more aggressive method of GC to further reduce the long-tail latency.

## 5.2 RL-Assisted Garbage Collection Scheduling

States: In the reinforcement learning, the states need to represent the history; which helps in maximizing the reward. We propose using the following information as the states

- Previous inter-request interval
- Current inter-request interval
- Previous action

The inter-request interval is an important information of history because it reflects the intensity (ie , the idleness) of storage traffics. Thus, if the interval is the RL-assisted GC scheduler tends to take a more aggressive action; more number of partial GC operations. The previous action plays a role of a summary of both recent history and the decision of the GC scheduler: large; ie

We divide of the three components into multiple bins; 2 bins for previous inter-request interval, 17 bins for current inter-request interval, and 2 bins for previous action; which gives total 68 (=2 X 17 X 2) states. The details of binning are given in Section 6.1. each

From the viewpoint of the agent, both the host and the SSD subsystem constitute the environment. The inter-request intervals represent the state of the host. Note that the previous action can represent that of the SSD subsystem as well as that of the host. It is because the previous action does not only plays a role of a summary of both recent history and the decision of GC scheduler; but also affects the state of the SSD subsystem; ie busy in page copy or idle. For instance; if the previous action is to copy a number of pages; then the current state of SSD subsystem tends to be busy. being large

Reward: Regarding the reward, we need to assign a reward for a smaller response time. We also need to penalize an action giving a long response time. Figure 6 shows our reward function. The reward ranges between -0.5 and 1. For instance; if the response time is large (larger than the threshold t3), a negative reward is assigned to penalize the action. larger

The thresholds in the reward function in Figure 6 need to be adjusted to the characteristics of the storage accesses. A fixed set of thresholds will not cover diverse scenarios in the storage accesses. Thus, we set the thresholds based on the characteristics of the storage accesses In particular; we set three thresholds; t1, t2, and t3 to the 7Oth , 9oth , and percentiles of the response time, 99th

6. Reward function. Fig

![Figure 7](/home/epiclab/docling/extracted/RL-figure-6.png)

respectively. Hence, even if the storage-access behavior changes, the thresholds can be adjusted based on the new distribution of the response time.

GC Scheduling: Algorithm 1 shows the pseudo code of the proposed RL-assisted GC scheduler: For each request to the storage; the GC scheduler compares the number of free blocks with threshold Tcc (=10 blocks in our experiments) If Tcc &gt; we call function e\_greedy() (line 2), which performs either exploration or exploitation based on the probability of €; ie random action is selected at a probability of € or an action is selected using the policy at a probability of 1 - € [10]. Note that we do not trigger the GC scheduler in case of consecutive requests wherein the inter-request interval is zero (line 3-5). After serving the request and obtaining the response time for the current request (line 6), we perform the selected action; ie., partial GC operation (line 7). We then call the reward function with the response time of the current request (line 8). Finally; we update the q-table entry of the previous request (line 9) Note that; as mentioned previously; we update the entry of the q-table associated with the previous request. Nfree =Nfree,

Exploitation and Exploration Balance: The exploration aims at in all the entries of the 9-table; and subsequently; improving them toward the optimal policy: To do that; we employ the €-greedy technique [10]. In the initial period of RL execution (the first 1000 GC operations in our experiments), we utilize € value (807) to perform aggressive explorations. Then; we utilize € value (19) for a balance between exploitation and exploration during the rest of filling large small period.

Intensive Garbage Collection: The baseline method in Algorithm 1 is not free from a blocked situation wherein the flash storage is out of the free block. To avoid such a situation; we employ an intensive garbage collection (GC) method from LazyRTGC [1] and modify it for further improvement. The objective of the intensive GC is to perform more (5 or 7 valid page copies in our experiments) partial GC operations than that in the normal partial GC operations (typically, 1 Or 2 page copies), thus enabling faster reclamation of free blocks. The number (5 or 7) of partial GC operations is determined by considering the number of pages in a block and other parameters of the flash memory; e.g- erase time [1]

In [1], the intensive GC is triggered when there is only one free block left. Under the inten sive GC, the action chosen by the RL policy is ignored and fixed number of partial GC operations is performed after serving a write request. In [1], after the number of free blocks becomes greater than one; the intensive GC is no threshold (termed the threshold of the stopping intensive GC, TIcc) than the one required to stop

applying the intensive GC. We use larger one (3), which is obtained via a sensitivity analysis in our experiments.

## 5.3 Aggressive RL-Assisted Garbage Collection Scheduling

In this subsection; we propose two methods of aggressively triggering the GC to further reduce the long-tail latency. To reduce the long-tail latency; it is effective to limit the maximum number of partial GC operations per action. In our experiments; we found that when the number of partial GC operations is limited to two; the best result is obtained. Thus; when the policy chooses an action; and if the action has more than two GC operations; we set the number of GC operations to two. When applying this method, we need to consider the blocking situation where the flash storage is out of the free block) because we limit the maximum number of partial GC operations. To avoid the blocking situation; we trigger the GC collection more aggressively by introducing a new threshold for number of free blocks TAGc: TAGc is set higher than TGc (10). We call this method early GC triggering with the maximum limit of partial GC operation; in short; max-limited early GC triggering: Note the maximum number of partial GC operations is limited only when the number of free blocks is between TAGC and TGc. When &lt;= TGc, the maximum limit is not applied to the action chosen by the RL-assisted GC scheduler. partial that, Nfree Nfree

In conventional GC methods; a write request triggers GC when the number of free blocks is less than a certain threshold. In case of the read request; the GC is not triggered to avoid the increase in the read latency: We propose triggering a partial GC operation even for a read request when the triggering condition is met. Note that the latency of the read request does not increase because the GC operation is performed after serving the read request. We call this method read-initiated GC triggering:

The aggressive GC operation can increase the erase count. To avoid we carefully select the victim blocks. When is within the two thresholds TAGC TGc, we select a victim block only when it has number of invalid pages than the threshold (607 of the block size in our experiments) . this, and Nfree larger

Note that, in our aggressive method, the RL-assisted GC scheduler is triggered using the two methods: max-limited early GC triggering and read-initiated GC triggering: Based on our experi ments, useful in obtaining free blocks during the idle time; thereby reducing the tail latency. they longprove

## 6 EXPERIMENTS

## 6.1 Experimental Setup

We compare our proposed RL-assisted GC methods (baseline in Section 5.2 and aggressive in Section 5.3) with typical GC method based on page-level mapping (page-level) [11] and LazyRTGC [1]. We implemented our proposed methods; page-level and LazyRTGC on a FlashSim simulator [3]. We use the mtrics of long-tail latency at the 99th , 99. and 99. percentiles and erase count. We use eight real-world workloads (six workloads from FIU [19] and two workloads from Microsoft [19]) and a synthetic one (from filebench [20]) as listed in Table 1. The of our work is to reduce long tail latency. In read-intensive workloads; the problem of tail latency is not severe since GC is rarely invoked. Thus, we used write-intensive workloads in our experiments. .99th, 9999th goal long

We started simulations with empty contents in the flash-memory model and measured the latency of all the requests for each workload. We use two types of 3D flash-memory systems as listed in Table 2

Table Workload Characteristics

|                  | Write ratio   |   Avg. interval [ µ s] |   Avg. request size [KB] |
|------------------|---------------|------------------------|--------------------------|
| home1            | 99%           |                  85565 |                     8.08 |
| home2            | 91%           |                 320548 |                     9.4  |
| home3            | 99%           |                1882329 |                     8.26 |
| home4            | 94%           |                 693651 |                     7.56 |
| webmail          | 74%           |                 303762 |                     8    |
| webmail + online | 78%           |                 127184 |                     8    |
| RBESQL           | 82%           |                  11664 |                    57.85 |
| MSNSFS           | 67%           |                    739 |                    21.67 |
| oltp             | 99%           |                     84 |                     4.46 |

Table 2 NAND Flash Memory

|                        | 3D 128 Gb [18]   | 3D 512 Gb [8]   |
|------------------------|------------------|-----------------|
| Page size              | 8KB              | 16KB            |
| Number of pages/block  | 384              | 768             |
| Number of blocks/plane | 2731             | 2874            |
| Number of planes       | 2                | 2               |
| Page read time         | 49 µ s           | 60 µ s          |
| Page program time      | 600 µ s          | 700 µ s         |
| Block erase time       | 4000 µ s         | 3500 µ s        |
| Data transfer rate     | 533 Mbps         | 1 Gbps          |

Table 3. States

| Previous inter-request interval [ µ s]   | Previous action                | Current inter-request interval [ µ s]   |
|------------------------------------------|--------------------------------|-----------------------------------------|
| < 100                                    | < max action/2                 | < 100 < 500 /220e/220e/220e > 100000    |
| > 100                                    | > max action/2 /220e/220e/220e | /220e/220e/220e /220e/220e/220e         |

Table 3 shows the binning for the components of the state: The binning was obtained by sensitivity analysis on binning choices by varying the numbers ofbins; 1~3 and 15~20 for previous and current inter-request intervals; and 1~3 for previous actions; respectively, with an aim to reduce the q-table size; ie., the number of states while improving the tail latency. long

Considering that the accesses to NAND flash memory take head of the agent is negligibly small. It is because the agent accesses the q-table (in a small SRAM) at maximum twice and executes a few instructions on the controller chip. Thus, the runtime of the agent is much smaller than the read latency of NAND flash memory.

Table 4. Threshold

| Threshold   |   Value | Remark                   |
|-------------|---------|--------------------------|
| T GC        |      10 | Triggering GC            |
| T IGC       |       3 | Stopping intensive GC    |
| T A GC      |     100 | Triggering aggressive GC |

7 . Comparison of write long-tail latency (512Gb 3D NAND Flash memory). Fig

![Figure 8](/home/epiclab/docling/extracted/RL-figure-7.png)

Table 4 summarizes the thresholds used in our method.  We obtained them by conducting sensitivity analysis with all the storage traces. To improve the generality of our proposed methods, in our future work; we will investigate the feasibility of reducing the number of thresholds by enhancing the RL model; e.g- by introducing the number of free blocks into the states of the agent.

## 6.2 Results and Discussion

Figure 7 compares the long-tail latency (in CDF) for writes. The figure shows that our proposed methods exhibit better long-tail latency than that page-level and LazyRTGC. Page-level is not shown in the figure due to too large latency since it does not adopt any optimization to reduce long tail latency. LazyRTGC lies partial GC operations in a manner and shows better latency than page-level. using lazy

Latency: Table 5 compares the latency normalized to LazyRTGC on a 512 Gb 3D NAND flash memory; Our baseline method (Base in the table) gives better (smaller) average latency: 0.86x at 99. 0.94x at 99. and 0.92x at percentile. The is result of the reinforcement learning-assisted action selection: LazyRTGC utilizes a fixed number of partial GC operations. In contrast, our proposed RL-assisted method can adapt to the characteristics of storage behavior; 9999th , 99th 99th gain

Table 5. Latency Comparison on 512Gb 3D NAND

thereby providing variable number of partial GC operations to better exploit the idle time; which contributes to reducing the long-tail latency: Our aggressive method in the table) gives much smaller latency: 0.76x at 99.9999th , 0.71x at 99. and 0.92x at 99th percentile. This proves that the two aggressive solutions; max-limited early GC triggering and read-initiated GC triggering; are effective in further reducing the long-tail latency: (Aggr .99th ,

Such a write behavior in homel increases the ratio of invalid pages across large number of blocks; which makes the GC cheaper; i.e., a free block can be obtained for fewer valid page copies. Thus; our aggressive method is effective in homel. However; as shown in Figure 8(b) home3 has weaker overwrite behavior than homel, which makes it difficult for the aggressive method to reclaim the free blocks using fine-grained partial GC.

In particular; the aggressive method gives much better latency in the four workloads: homel, home2, webmail, and webmail + online. These workloads have overwrite traffics distributed across a wide range of addresses. Figure 8 exemplifies the distribution of the write traffics for homel and home3. As the figure shows, in the case of homel, the overwrites are much stronger than that in home3 (see y axis). In addition; such strong overwrites are more distributed across wider address range than that in home3 . heavy

In Table 5, both the LazyRTGC and our methods give similar latencies in home3 and In case of home3, the inter-request interval is as listed in Table 1. In such a case, the GC (and its optimization) does not help in reducing the latency: On the other hand, has very short idle time; small inter-request interval as listed in Table 1. oltp. large oltp ie,

Thus; there is little 0 pportunity to improve the GC. Table 6 compares the latencies in the case of 128 Gb 3D NAND flash memory. Compared to the results in Table 5, our proposed methods give further reductions; e.g., 0.66x (in Table 6) vs 0.76x (Table 5), compared to the aggressive method at the 99. The low capacity triggers GC more frequently, which increases the overhead of the GC in the conventional GC method (page-level). In Table 6, our proposed methods are more effective than the LazyRTGC in reducing the GC overhead in such a difficult condition: 9999th

Fig: 8. Distribution of write traffics.

![Figure 9](/home/epiclab/docling/extracted/RL-figure-8.png)

Free block: Figure 9 shows the variation in the number of free blocks over time in the workload homel under LazyRTGC, and under our baseline and aggressive methods. As shown in the figure; after an initial period; LazyRTGC continues to retain 3 or 4 free blocks; which can lead to frequent GC operations because the number of free blocks is less. Our baseline method manages slightly more number (3-6) of free blocks. Our aggressive method manages significantly more number of free blocks; which helps in reducing the GC operations; thereby contributing to reducing the long-tail latency

Note that; as mentioned in Section 5.3, our aggressive method increases the number of free blocks only when there are victim blocks having large ratio of invalid pages. Thus; although the

Table 6. Latency Comparison on 128Gb 3D NAND

Table 7. Erase Count Comparison on 512Gb 3D NAND

aggressive method manages a significantly more number of free blocks than LazyRTGC, it does not have a negative impact on the erase count; as demonstrated later in this section.

RL related Analysis: In order to evaluate the robustness of our method, we measured the latency of ten executions of each trace. Tables 9 and 10 show that the results of proposed method are consistent having a very small standard deviation of latency 3.87 of the average normalized latency.

Erase Count: Tables 7 and 8 compare the erase counts (normalized to LazyRTGC) on 512 Gb and 128 Gb 3D NAND flash-memory systems; respectively. From Tables and 8, it is clear that our proposed aggressive method and LazyRTGC give similar erase counts while the page-level gives higher erase count because of the block-level GC.

We evaluated the utilization of q-table entries for each workload. In the analysis; we found that the average utilization is 79% and there is possibility of further improvement by adjusting the q-table size to each workload; which is left for our future work.

Average   application performance:  It is important to evaluate  the impact of our promethod on average application performance: Since we did trace-based experiments; the posed

9. Comparison of number of free blocks Fig

![Figure 10](/home/epiclab/docling/extracted/RL-figure-9.png)

Table 8 Erase Count Comparison on 128Gb 3D NAND

Table 9. Standard Deviation of Normalized Latency on 512Gb 3D NAND

Table 10. Standard Deviation of Normalized Latency on 128Gb 3D NAND

average latency of request is considered to be correlated with average application performance Our experiments  (the corresponding  results of which are omitted due to page limit) show that, the average latency of our proposed baseline and aggressive methods is slightly better than that of the existing method, RTGC. Thus, we can state that our proposed methods improve the long tail latency without degrading the average application performance: Lazy

Simple prediction method: Our problem of reducing tail latency could be addressed by existing; possibly simpler; alternatives as those based on time series prediction: In our experiments; we did quantitative comparison with GC methods based on two typical methods of predicting the inter-request interval with moving average and exponential smoothing; respectively. Tables 11 and 12 show that our proposed method constantly outperforms them. It is because our method manages history and learns appropriate actions in a more fine-grained manner using the q-table: long such

Long trace experiment: We also evaluated our proposed method with longer traces by stitch the original traces. Our experiments show that; in the trace cases, our proposed method outperforms the existing one; as in the case of short ones. long ing lazy

In summary; the experimental results show that the LazyRTGC does not fully utilize the idle time available in the storage workload. In contrast; our baseline method can better exploit the idle time because of the reinforcement learning-based GC. In addition; our aggressive method helps in

Table 11. Latency Comparison of Simple Prediction Method on 512Gb 3D NAND

Table 12 Latency Comparison of Simple Prediction Method on 128Gb 3D NAND

further reducing the long-tail latency by (1) preparing free blocks with frequent small fine-grained partial GCs, which helps in reducing the frequency of triggering the GC operations and stalling the subsequent requests, and (2) hiding the GC operation by exploiting the idle time based on the reinforcement learning: Consequently; as presented in Tables 5 and 6, our proposed aggressive method helps in reducing the long-tail latency by 29-367 at the 99. percentile for the two flash-storage devices. 99th

## CONCLUSION

In this paper; we addressed the problem of long-tail latency in NAND flash memory-based stor age systems and proposed a reinforcement learning-assisted garbage collection technique; which learns the storage access behavior online and determines the number of GC operations to exploit the idle time We also presented aggressive methods; which helps in further reducing the tail latency by aggressively performing fine-grained GC operations. We evaluated our proposed methods with eight real-world workloads on two 3D NAND flash memory storages. We managed to reduce the long-tail latency by 29-36%. We expect that such a reduction is beneficial for realtime embedded systems and quality-critical server systems. long-

## REFERENCES

- [1] garbage collection mechanism with jointly optimizing average and worst performance for NAND flash memory storage systems. ACM 10.1145/2746236 lazy Trans:
- [2] Zhiwei Qin, Yi Wang; Duo Liu; and Zili Shao. 2012. Real-time flash translation layer for NAND flash memory storage systems. In Symp. IEEE 18th Real Time and Embedded Technology and Application 35-44. DOI:lO.11O9/RTAS.2012.27
