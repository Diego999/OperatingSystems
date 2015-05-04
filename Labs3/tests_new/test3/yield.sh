#!/bin/sh

echo 1000 > /proc/sys/kernel/sched_dummy_timeslice
echo 1000 > /proc/sys/kernel/sched_dummy_age_threshold

nice -n 12 ./yield A &
nice -n 12 ./yield B &

wait
echo

