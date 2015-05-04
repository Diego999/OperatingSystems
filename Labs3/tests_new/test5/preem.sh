#!/bin/sh

echo 1000 > /proc/sys/kernel/sched_dummy_timeslice
echo 1000 > /proc/sys/kernel/sched_dummy_age_threshold

./preem

wait
echo 'done'
