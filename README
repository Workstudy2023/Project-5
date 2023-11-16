# Project-5
# Resource Management Inside An Operating System

This project is focuses on resource management. Where we simulate process resource requests and allocation, including deadlock detection and resolution.

## Features

- Process Scheduling Simulation
- Memory Allocation and Management
- Deadlock Detection Algorithm Implementation

## Overview

The child processes try to request or release a resource. Additionally, potentially terminate after a quarter of a second. The parent process manages the created children. It checks if there is a deadlock when granting
resources, if so it resolves the deadlock by progressively removing children.

## Deadlock Policy
When we detect a deadlock in our system, we first determine if we can
satisify any ongoing resource requests. If the deadlock persists, we try to remove the deadlock
by removing the most recent child. In other words, the child that's done the least amount of work.

## Run the oss program:

./oss [-h] [-n proc] [-s simul] [-t timeToLaunchNewChild] [-f logfile]

### Parameters

-h: Displays help information.
-n proc: The total number of child processes oss will ever launch.
-s simul: Maximum number of user processes in the system at any time.
-t timeToLaunchNewChild: Time interval (in nanoseconds) to launch a new child process.
-f logfile: Specifies the name of the log file.

## Output

The program writes detailed logs of its operation to a specified log file. This includes resource/allocation table, process table, and deadlock information.

## Author

Christine Mckelvey

## Date

11-04-23
